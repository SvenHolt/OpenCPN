"""Command line interface.

    seachart download --bbox 9.8,54.2,11.2,55.0
    seachart route    törn.gpx --width 8 --out charts/
"""

import argparse
import os
import re
import sys

from shapely.geometry import box
from shapely.ops import unary_union

from . import route as rt
from . import sources
from .builder import Builder, cell_name, intended_usage
from .catalog import Catalog
from .depth import DEFAULT_LEVELS
from .sources import log
from .store import Store


def _bbox(text):
    try:
        w, s, e, n = (float(v) for v in text.split(","))
    except ValueError:
        raise argparse.ArgumentTypeError("expected W,S,E,N in degrees")
    if not (w < e and s < n):
        raise argparse.ArgumentTypeError("expected W,S,E,N with W<E, S<N")
    return (w, s, e, n)


def _levels(text):
    return sorted(float(v) for v in text.split(","))


def _slug(text):
    s = re.sub(r"[^A-Za-z0-9._-]+", "_", text).strip("_")
    return s or "route"


def _routes(args):
    if args.waypoints:
        return [(args.name or "route", rt.parse_waypoints(args.waypoints))]
    if not args.gpx:
        raise SystemExit("give a GPX file or --waypoints")
    routes = rt.read_gpx(args.gpx, args.name)
    if not routes:
        raise SystemExit("no route/track with at least 2 points in %s"
                         % args.gpx)
    return routes


def download(store, area, args):
    bounds = area.bounds if hasattr(area, "bounds") else area
    if not args.no_osm:
        log("OpenSeaMap/OSM data for %.3f,%.3f,%.3f,%.3f" % tuple(bounds))
        urls = [args.overpass_url] if args.overpass_url else None
        sources.download_overpass(store, bounds, tile_size=args.osm_tile,
                                  urls=urls, force=args.force)
    if not args.no_depth:
        log("Bathymetry (%s) for %.3f,%.3f,%.3f,%.3f"
            % ((args.dataset,) + tuple(bounds)))
        sources.download_erddap(store, bounds, base=args.erddap_url,
                                dataset=args.dataset,
                                variable=args.variable, force=args.force)


def _download_area(args):
    if args.bbox:
        return box(*args.bbox)
    shapes = [rt.corridor(pts, args.width) for _, pts in _routes(args)]
    return unary_union(shapes)


def cmd_download(args):
    store = Store(args.db)
    try:
        area = _download_area(args)
        if not args.bbox:
            # only fetch the tiles the corridor actually touches
            for tile in sources.tiles(area.bounds, args.osm_tile):
                if area.intersects(box(*tile)):
                    download(store, tile, args)
        else:
            download(store, area, args)
    finally:
        store.close()
    log("done. offline store: %s" % os.path.abspath(args.db))


def cmd_import(args):
    store = Store(args.db)
    try:
        for path in args.files:
            low = path.lower()
            if low.endswith(".pbf"):
                n = sources.import_pbf(store, path)
            elif low.endswith(".osm"):
                n = sources.import_osm_xml(store, path)
            elif low.endswith(".json"):
                n = sources.import_overpass_json(store, path)
            elif low.endswith(".asc"):
                n = sources.import_esri_ascii(store, path,
                                              args.positive_down,
                                              os.path.basename(path))
            elif low.endswith((".xyz", ".csv")):
                if not args.resolution:
                    raise SystemExit("--resolution (degrees) is needed for"
                                     " xyz/csv soundings")
                n = sources.import_xyz(store, path, args.resolution,
                                       positive_down=not args.elevation,
                                       name=os.path.basename(path))
            else:
                raise SystemExit("unknown file type: %s" % path)
            log("%s: %d objects/values imported" % (path, n))
            if store.put_bounds and not low.endswith((".asc", ".xyz",
                                                      ".csv")):
                # the extent of an extract counts as "available offline"
                store.mark_downloaded("osm", store.put_bounds)
                store.put_bounds = None
    finally:
        store.close()


def _check_offline(store, area):
    for src, label in (("osm", "OpenSeaMap data"), (None, "bathymetry")):
        if src:
            cov = store.coverage(src)
        else:
            covs = [store.coverage(s) for (s,) in store.db.execute(
                "SELECT DISTINCT source FROM downloads WHERE source LIKE"
                " 'depth:%'")]
            covs = [c for c in covs if c is not None]
            cov = unary_union(covs) if covs else None
        missing = area if cov is None else area.difference(cov)
        if missing.area > area.area * 0.001:
            log("warning: %s missing offline for %.0f%% of the chart area -"
                " run 'download' or use --download"
                % (label, 100.0 * missing.area / area.area))


def _build_set(builder, out, label, area, scale, cell_size, comment):
    files = []
    for r, c, bounds, cov in rt.cells_for(area, cell_size):
        name = cell_name(intended_usage(scale), label, scale, r, c)
        path = os.path.join(out, name + ".000")
        builder.build_cell(path, name, bounds, cov, scale, comment)
        files.append(path)
    return files


def cmd_route(args):
    catalog = Catalog(args.s57data)
    store = Store(args.db)
    try:
        for label, pts in _routes(args):
            area = rt.corridor(pts, args.width)
            log("Route '%s': %d points, %.1f NM, corridor +/- %.1f NM"
                % (label, len(pts), rt.route_length_nm(pts), args.width))
            if args.download:
                for tile in sources.tiles(area.bounds, args.osm_tile):
                    if area.intersects(box(*tile)):
                        download(store, tile, args)
            _check_offline(store, area)
            out = os.path.join(args.out, _slug(label))
            os.makedirs(out, exist_ok=True)
            builder = Builder(store, catalog, args.levels)
            files = []
            comment = "Route %s" % label
            if args.overview_scale:
                log("Overview charts 1:%d" % args.overview_scale)
                files += _build_set(builder, out, label, area,
                                    args.overview_scale,
                                    args.overview_cell_size, comment)
            log("Route charts 1:%d" % args.scale)
            files += _build_set(builder, out, label, area, args.scale,
                                args.cell_size, comment)
            if args.harbour_radius > 0 and args.harbour_scale:
                log("Harbour charts 1:%d" % args.harbour_scale)
                for k, p in enumerate((pts[0], pts[-1])):
                    ha = rt.circle(p, args.harbour_radius)
                    name = cell_name(intended_usage(args.harbour_scale),
                                     label, "harbour", k)
                    path = os.path.join(out, name + ".000")
                    builder.build_cell(path, name, ha.bounds, ha,
                                       args.harbour_scale, comment)
                    files.append(path)
            _write_readme(out, label, files)
            log("%d chart cells in %s" % (len(files), os.path.abspath(out)))
            if builder.mapper.unknown_types:
                log("seamark types without S-57 mapping: %s" % ", ".join(
                    "%s (%d)" % kv for kv in
                    sorted(builder.mapper.unknown_types.items())))
    finally:
        store.close()


def cmd_region(args):
    catalog = Catalog(args.s57data)
    store = Store(args.db)
    try:
        area = box(*args.bbox)
        if args.download:
            download(store, args.bbox, args)
        _check_offline(store, area)
        os.makedirs(args.out, exist_ok=True)
        builder = Builder(store, catalog, args.levels)
        label = args.name or "region_%g_%g_%g_%g" % args.bbox
        files = _build_set(builder, args.out, label, area, args.scale,
                           args.cell_size, label)
        _write_readme(args.out, label, files)
        log("%d chart cells in %s" % (len(files), os.path.abspath(args.out)))
    finally:
        store.close()


def cmd_info(args):
    store = Store(args.db)
    try:
        for k, v in store.summary().items():
            print("%-24s %s" % (k, v))
    finally:
        store.close()


def _write_readme(out, label, files):
    with open(os.path.join(out, "README.txt"), "w", encoding="utf-8") as f:
        f.write("S-57 charts for '%s'\n\n" % label)
        f.write("Add this directory in OpenCPN: Options > Charts > Chart Files"
                " > Add Directory.\n\n")
        f.write("Data: OpenSeaMap / OpenStreetMap contributors (ODbL), "
                "bathymetry as downloaded.\n")
        f.write("NOT FOR NAVIGATION - for planning and as a supplement to "
                "official charts only.\n\n")
        for p in files:
            f.write(os.path.basename(p) + "\n")


def _add_source_opts(p):
    p.add_argument("--no-osm", action="store_true",
                   help="do not download OpenSeaMap/OSM objects")
    p.add_argument("--no-depth", action="store_true",
                   help="do not download bathymetry")
    p.add_argument("--overpass-url", help="Overpass API endpoint")
    p.add_argument("--osm-tile", type=float, default=0.5,
                   help="download tile size in degrees (default 0.5)")
    p.add_argument("--erddap-url", default=sources.ERDDAP_URL)
    p.add_argument("--dataset", default=sources.ERDDAP_DATASET,
                   help="ERDDAP griddap dataset (default srtm15plus)")
    p.add_argument("--variable", default=sources.ERDDAP_VARIABLE)
    p.add_argument("--force", action="store_true",
                   help="download again even if already offline")


def _add_route_opts(p):
    p.add_argument("gpx", nargs="?", help="GPX file with route(s)/track(s)")
    p.add_argument("--name", help="only use the route with this name")
    p.add_argument("--waypoints",
                   help="instead of GPX: 'lat,lon lat,lon ...'")
    p.add_argument("--width", type=float, default=10.0,
                   help="corridor half width in NM (default 10)")


def main(argv=None):
    ap = argparse.ArgumentParser(
        prog="seachart",
        description="Offline OpenSeaMap charts (S-57) for OpenCPN")
    ap.add_argument("--db", default="seachart.db",
                    help="offline data store (default seachart.db)")
    ap.add_argument("--s57data", help="OpenCPN data/s57data directory")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("download", help="make data for an area offline")
    p.add_argument("--bbox", type=_bbox, help="W,S,E,N")
    _add_route_opts(p)
    _add_source_opts(p)
    p.set_defaults(func=cmd_download)

    p = sub.add_parser("import", help="import local files (.osm.pbf, .osm,"
                       " Overpass .json, .asc grids, .xyz/.csv soundings)")
    p.add_argument("files", nargs="+")
    p.add_argument("--positive-down", action="store_true",
                   help=".asc values are depths (default: elevations)")
    p.add_argument("--elevation", action="store_true",
                   help=".xyz values are elevations (default: depths)")
    p.add_argument("--resolution", type=float,
                   help="grid size in degrees for .xyz/.csv soundings")
    p.set_defaults(func=cmd_import)

    for name, func, hlp in (("route", cmd_route,
                             "build charts along a route"),
                            ("region", cmd_region,
                             "build charts for a bounding box")):
        p = sub.add_parser(name, help=hlp)
        if name == "route":
            _add_route_opts(p)
            p.add_argument("--overview-scale", type=int, default=250000,
                           help="0 disables the overview charts")
            p.add_argument("--overview-cell-size", type=float, default=1.0)
            p.add_argument("--harbour-scale", type=int, default=12000)
            p.add_argument("--harbour-radius", type=float, default=2.0,
                           help="NM around start and end (0 disables)")
        else:
            p.add_argument("--bbox", type=_bbox, required=True)
            p.add_argument("--name")
        p.add_argument("--scale", type=int, default=50000,
                       help="compilation scale of the charts (default"
                       " 50000)")
        p.add_argument("--cell-size", type=float, default=0.25,
                       help="cell height in degrees (default 0.25)")
        p.add_argument("--levels", type=_levels, default=DEFAULT_LEVELS,
                       help="depth contour levels in m, e.g. 2,5,10,20,50")
        p.add_argument("--out", default="charts")
        p.add_argument("--download", action="store_true",
                       help="fetch missing data online before building")
        _add_source_opts(p)
        p.set_defaults(func=func)

    p = sub.add_parser("info", help="show what is stored offline")
    p.set_defaults(func=cmd_info)

    args = ap.parse_args(argv)
    if args.cmd == "download" and not (args.bbox or args.gpx or
                                       args.waypoints):
        ap.error("download needs --bbox, a GPX file or --waypoints")
    args.func(args)


if __name__ == "__main__":
    sys.exit(main())
