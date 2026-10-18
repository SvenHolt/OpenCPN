"""Assemble S-57 cells from the offline store."""

import os
import zlib

from shapely.geometry import (GeometryCollection, LineString,
                              MultiLineString, MultiPoint, MultiPolygon,
                              Point, Polygon, box)
from shapely.ops import unary_union
from shapely.prepared import prep

from . import coast, depth
from .s57map import Mapper
from .s57writer import S57Cell
from .sources import is_inland_water, log

DISCLAIMER = ("Generated from OpenSeaMap/OpenStreetMap (ODbL) and public "
              "bathymetry. NOT FOR NAVIGATION.")

M_PER_DEG = 111320.0


def intended_usage(scale):
    for limit, intu in ((1500000, 1), (350000, 2), (90000, 3), (22000, 4),
                        (4000, 5)):
        if scale > limit:
            return intu
    return 6


def cell_name(intu, *key):
    code = zlib.crc32(repr(key).encode()) % (36 ** 5)
    digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    s = ""
    for _ in range(5):
        code, d = divmod(code, 36)
        s = digits[d] + s
    return "SC%d%s" % (intu, s)


def _parts(geom, kind):
    """Yield the Points/LineStrings/Polygons of a (collection) geometry."""
    if geom is None or geom.is_empty:
        return
    if isinstance(geom, kind):
        yield geom
    elif isinstance(geom, (MultiPoint, MultiLineString, MultiPolygon,
                           GeometryCollection)):
        for g in geom.geoms:
            yield from _parts(g, kind)


def _polys(geom, min_area=0.0):
    for p in _parts(geom, Polygon):
        if p.area > min_area:
            yield p


def _add(cell, objl, attrs, geom):
    if isinstance(geom, Point):
        cell.add_point(objl, attrs, geom.x, geom.y)
        return 1
    n = 0
    for p in _polys(geom):
        cell.add_area(objl, attrs, [list(p.exterior.coords)] +
                      [list(r.coords) for r in p.interiors])
        n += 1
    for ln in _parts(geom, LineString):
        cell.add_line(objl, attrs, list(ln.coords))
        n += 1
    return n


class Builder:
    def __init__(self, store, catalog, levels=None, verbose=True):
        self.store = store
        self.cat = catalog
        self.mapper = Mapper(catalog)
        self.levels = levels or depth.DEFAULT_LEVELS
        self.verbose = verbose

    def build_cell(self, path, name, bounds, coverage, scale, comment=""):
        intu = intended_usage(scale)
        cell = S57Cell(self.cat, name + ".000", scale, intu,
                       comment=(comment + " " + DISCLAIMER).strip())
        frame = box(*bounds)
        coverage = coverage.intersection(frame)
        tol = scale * 0.0002 / M_PER_DEG          # 0.2 mm on the chart
        min_area = (scale * 0.0005 / M_PER_DEG) ** 2
        stats = {}

        def count(objl, n=1):
            stats[objl] = stats.get(objl, 0) + n

        margin = 0.01
        qbounds = (bounds[0] - margin, bounds[1] - margin,
                   bounds[2] + margin, bounds[3] + margin)
        feats = list(self.store.features(qbounds))

        # -- depth grid ---------------------------------------------------
        min_res = scale * 0.002 / M_PER_DEG        # 2 mm on the chart
        grid = self.store.depth_grid(qbounds)
        if grid is not None and grid[2] < min_res:
            grid = self.store.depth_grid(qbounds, resolution=min_res)

        def dem_says_land():
            if grid is None:
                return False
            import numpy as np
            g = grid[4]
            return bool(np.nanmedian(g) < 0) if not np.all(np.isnan(g)) \
                else False

        # -- land / water -------------------------------------------------
        coastlines = [g for _, t, g in feats
                      if t.get("natural") == "coastline"]
        # Lakes and rivers (OpenSeaMap covers inland waters, too).
        inland = [g for _, t, g in feats if is_inland_water(t)
                  and isinstance(g, (Polygon, MultiPolygon))]
        inland = unary_union(inland).intersection(frame) if inland else None
        if inland is not None and inland.is_empty:
            inland = None
        if coastlines:
            # Work on the covered part only: coastline data outside the
            # corridor may be incomplete.
            cb = coverage.bounds
            land = coast.land_polygons(coastlines, (
                max(bounds[0], cb[0] - 1e-3), max(bounds[1], cb[1] - 1e-3),
                min(bounds[2], cb[2] + 1e-3), min(bounds[3], cb[3] + 1e-3)))
        elif inland is not None:
            land = frame
        else:
            land = coast.land_polygons([], bounds, default_land=dem_says_land)
        if land is not None and inland is not None:
            land = land.difference(inland)
        land = land.intersection(coverage) if land is not None else None
        if land is not None:
            land = land.simplify(tol)
        water = coverage.difference(land) if land is not None else coverage
        for p in _polys(land, min_area):
            count("LNDARE", _add(cell, "LNDARE", {}, p))

        # -- depth areas, contours, soundings -----------------------------
        covered = None
        if grid is not None:
            lon0, lat0, dlon, dlat, arr = grid
            contours, areas = depth.depth_products(
                lon0, lat0, dlon, dlat, arr, self.levels, simplify=tol)
            first = next((lv for lv in self.levels if lv > 0), 2)
            pieces = []
            for d1, d2, poly in areas:
                clipped = poly.intersection(water)
                if d2 <= 0:
                    # DEM land inside OSM water: coarse grid near the coast,
                    # treat as shallow.  Lakes lie above sea level in a DEM,
                    # there the grid says nothing about the depth.
                    d1, d2 = 0.0, first
                    if inland is not None:
                        clipped = clipped.difference(inland)
                pieces.append(clipped)
                for p in _polys(clipped, min_area):
                    count("DEPARE", _add(cell, "DEPARE",
                                         {"DRVAL1": float(d1),
                                          "DRVAL2": float(d2)}, p))
            covered = unary_union(pieces) if pieces else None
            water_p = prep(water.buffer(0))
            for level, line in contours:
                if not water_p.intersects(line):
                    continue
                clipped = line.intersection(water)
                count("DEPCNT", _add(cell, "DEPCNT",
                                     {"VALDCO": float(level)}, clipped))
            spacing = scale * 0.012 / M_PER_DEG    # ~1.2 cm on the chart
            snd = depth.soundings(lon0, lat0, dlon, dlat, arr, spacing,
                                  region=water)
            if snd:
                cell.add_soundings(snd)
                count("SOUNDG", len(snd))
        rest = water.difference(covered) if covered is not None else water
        for p in _polys(rest, min_area):
            # no depth information: shown as shallow water on the chart
            count("DEPARE(no data)", _add(cell, "DEPARE", {"DRVAL1": 0.0}, p))

        # -- OpenSeaMap objects ------------------------------------------
        cov_p = prep(coverage)
        for osm_id, tags, geom in feats:
            if not cov_p.intersects(geom):
                continue
            for objl, attrs, g in self.mapper.map(tags, geom):
                if isinstance(g, Point):
                    if not cov_p.contains(g):
                        continue
                else:
                    g = g.intersection(coverage).simplify(tol / 2)
                count(objl, _add(cell, objl, attrs, g))

        # -- meta ---------------------------------------------------------
        for p in _polys(coverage):
            cell.add_area("M_COVR", {"CATCOV": 1}, [list(p.exterior.coords)]
                          + [list(r.coords) for r in p.interiors])
        size = cell.write(path)
        if self.verbose:
            summary = ", ".join("%s %d" % kv for kv in sorted(stats.items()))
            log("  %s  1:%d  %.1f kB  %s" % (
                os.path.basename(path), scale, size / 1024.0, summary))
            if grid is None:
                log("    warning: no bathymetry offline for this cell")
            if not coastlines and inland is None and land is None:
                log("    note: no coastline - cell treated as open water")
        return stats
