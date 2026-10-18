"""Navigation grid for OpenCPN's passage planner.

A regular raster over the area: -1 = land, 0 = water of unknown depth,
> 0 = water depth in metres.  Stored as little-endian float32, row 0 is the
southern row.  OpenCPN searches routes on this raster (see
gui/src/passage_router.cpp), so its resolution decides how narrow a passage
can still be found.
"""

import datetime

import numpy as np
import shapely
from shapely.geometry import MultiPolygon, Polygon, box
from shapely.ops import unary_union

from . import coast
from .sources import is_inland_water

LAND, UNKNOWN = -1.0, 0.0


def land_mask(store, bounds):
    feats = list(store.features(bounds))
    coastlines = [g for _, t, g in feats if t.get("natural") == "coastline"]
    inland = [g for _, t, g in feats if is_inland_water(t)
              and isinstance(g, (Polygon, MultiPolygon))]
    frame = box(*bounds)
    inland = unary_union(inland).intersection(frame) if inland else None
    if coastlines:
        land = coast.land_polygons(coastlines, bounds)
    elif inland is not None and not inland.is_empty:
        land = frame
    else:
        land = None
    if land is not None and inland is not None and not inland.is_empty:
        land = land.difference(inland)
    return land


def build(store, bounds, resolution=0.005):
    minx, miny, maxx, maxy = bounds
    nx = int(round((maxx - minx) / resolution)) + 1
    ny = int(round((maxy - miny) / resolution)) + 1
    xs = minx + np.arange(nx) * resolution
    ys = miny + np.arange(ny) * resolution
    gx, gy = np.meshgrid(xs, ys)
    out = np.full((ny, nx), UNKNOWN, dtype="<f4")
    depth = store.depth_grid(bounds, resolution=resolution)
    if depth is not None:
        _, _, _, _, arr = depth
        arr = arr[:ny, :nx]
        ok = ~np.isnan(arr) & (arr > 0)
        out[:arr.shape[0], :arr.shape[1]][ok] = arr[ok]
    land = land_mask(store, bounds)
    if land is not None and not land.is_empty:
        shapely.prepare(land)
        out[shapely.contains_xy(land, gx, gy)] = LAND
    elif depth is not None:
        # no coastline data at all: fall back to the elevation model
        _, _, _, _, arr = depth
        arr = arr[:ny, :nx]
        out[:arr.shape[0], :arr.shape[1]][arr < -1] = LAND
    store.db.execute(
        "DELETE FROM navgrid WHERE lon0>=? AND lat0>=? AND"
        " lon0+dlon*(nx-1)<=? AND lat0+dlat*(ny-1)<=?",
        (minx - 1e-9, miny - 1e-9, maxx + 1e-9, maxy + 1e-9))
    store.db.execute(
        "INSERT INTO navgrid (lon0, lat0, dlon, dlat, nx, ny, data, created)"
        " VALUES (?,?,?,?,?,?,?,?)",
        (minx, miny, resolution, resolution, nx, ny, out.tobytes(),
         datetime.datetime.now(datetime.timezone.utc).isoformat(
             timespec="seconds")))
    store.db.commit()
    return out
