"""Data sources: OpenSeaMap/OSM (Overpass API, .osm/.pbf files) and
bathymetry (ERDDAP griddap, ESRI ASCII grids, XYZ files)."""

import gzip
import json
import math
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET

import numpy as np
from shapely.geometry import LineString, Point, Polygon
from shapely.ops import polygonize, unary_union

USER_AGENT = "seachart-offline/1.0 (OpenCPN offline chart builder)"

OVERPASS_URLS = [
    "https://overpass-api.de/api/interpreter",
    "https://maps.mail.ru/osm/tools/overpass/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
]

# Everything a sea chart needs from OSM.
OVERPASS_QUERY = """[out:json][timeout:{timeout}];
(
  nwr["seamark:type"]({s},{w},{n},{e});
  way["natural"="coastline"]({s},{w},{n},{e});
  nwr["natural"="water"]["water"~"^(lake|river|canal|lagoon|reservoir)$"]({s},{w},{n},{e});
  nwr["waterway"="riverbank"]({s},{w},{n},{e});
  nwr["leisure"="marina"]({s},{w},{n},{e});
  way["man_made"~"^(pier|breakwater|groyne)$"]({s},{w},{n},{e});
  node["place"~"^(city|town|village)$"]({s},{w},{n},{e});
);
out geom;"""

ERDDAP_URL = "https://coastwatch.pfeg.noaa.gov/erddap"
ERDDAP_DATASET = "srtm15plus"
ERDDAP_VARIABLE = "z"


def log(msg):
    print(msg, file=sys.stderr, flush=True)


def tiles(bounds, size):
    """Split (minx, miny, maxx, maxy) into tiles aligned to ``size``."""
    minx, miny, maxx, maxy = bounds
    x0 = math.floor(minx / size) * size
    y0 = math.floor(miny / size) * size
    y = y0
    while y < maxy - 1e-12:
        x = x0
        while x < maxx - 1e-12:
            yield (round(x, 9), round(y, 9), round(x + size, 9),
                   round(y + size, 9))
            x += size
        y += size


def http_get(url, data=None, timeout=300, retries=4):
    body = urllib.parse.urlencode(data).encode() if data else None
    delay = 5
    for attempt in range(retries + 1):
        req = urllib.request.Request(url, data=body, headers={
            "User-Agent": USER_AGENT, "Accept-Encoding": "gzip"})
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                raw = resp.read()
                if resp.headers.get("Content-Encoding") == "gzip":
                    raw = gzip.decompress(raw)
                return raw
        except (urllib.error.URLError, OSError) as exc:
            code = getattr(exc, "code", None)
            if attempt == retries or code in (400, 404):
                raise
            log("    %s - retry in %ds" % (exc, delay))
            time.sleep(delay)
            delay *= 2


# -- OSM geometry helpers --------------------------------------------------

def assemble_polygon(outers, inners):
    """Build a (multi)polygon from lists of member way coordinates."""
    def faces(lines):
        if not lines:
            return None
        polys = list(polygonize(unary_union([LineString(c) for c in lines
                                             if len(c) >= 2])))
        return unary_union(polys) if polys else None
    outer = faces(outers)
    if outer is None:
        return None
    inner = faces(inners)
    geom = outer.difference(inner) if inner is not None else outer
    return geom if not geom.is_empty else None


def overpass_elements_to_features(elements):
    for el in elements:
        tags = el.get("tags") or {}
        if not tags:
            continue
        kind = el["type"]
        osm_id = "%s/%d" % (kind, el["id"])
        if kind == "node":
            yield osm_id, tags, Point(el["lon"], el["lat"])
        elif kind == "way":
            coords = [(p["lon"], p["lat"]) for p in el.get("geometry") or []
                      if p]
            if len(coords) >= 2:
                yield osm_id, tags, LineString(coords)
            elif len(coords) == 1:
                yield osm_id, tags, Point(coords[0])
        elif kind == "relation":
            if tags.get("type") not in ("multipolygon", "boundary"):
                continue
            outers, inners = [], []
            for m in el.get("members") or []:
                if m.get("type") != "way" or not m.get("geometry"):
                    continue
                coords = [(p["lon"], p["lat"]) for p in m["geometry"] if p]
                (inners if m.get("role") == "inner" else outers).append(coords)
            geom = assemble_polygon(outers, inners)
            if geom is not None:
                yield osm_id, tags, geom


def download_overpass(store, bounds, tile_size=0.5, urls=None, force=False,
                      timeout=300):
    urls = urls or OVERPASS_URLS
    todo = list(tiles(bounds, tile_size))
    for k, t in enumerate(todo, 1):
        if not force and store.is_downloaded("osm", t):
            log("  [%d/%d] OSM %s already offline" % (k, len(todo), t))
            continue
        query = OVERPASS_QUERY.format(timeout=timeout - 20, w=t[0], s=t[1],
                                      e=t[2], n=t[3])
        last = None
        for url in urls:
            try:
                log("  [%d/%d] OSM %s <- %s" % (k, len(todo), t, url))
                raw = http_get(url, {"data": query}, timeout=timeout,
                               retries=2)
                data = json.loads(raw)
                break
            except Exception as exc:     # try the next mirror
                last = exc
                log("    failed: %s" % exc)
        else:
            raise RuntimeError("Overpass download failed for %s: %s"
                               % (t, last))
        n = 0
        for osm_id, tags, geom in overpass_elements_to_features(
                data.get("elements", [])):
            store.put_feature(osm_id, tags, geom)
            n += 1
        store.mark_downloaded("osm", t)
        log("    %d objects" % n)
        time.sleep(1)                    # be polite to the public servers


def import_overpass_json(store, path):
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    n = 0
    for osm_id, tags, geom in overpass_elements_to_features(
            data.get("elements", [])):
        store.put_feature(osm_id, tags, geom)
        n += 1
    return n


def is_inland_water(tags):
    return ((tags.get("natural") == "water" and tags.get("water") in
             ("lake", "river", "canal", "lagoon", "reservoir"))
            or tags.get("waterway") == "riverbank")


def _wanted(tags):
    return ("seamark:type" in tags or tags.get("natural") == "coastline"
            or is_inland_water(tags)
            or tags.get("leisure") == "marina"
            or tags.get("man_made") in ("pier", "breakwater", "groyne")
            or (tags.get("place") in ("city", "town", "village")
                and "name" in tags))


def import_osm_xml(store, path):
    """Import a .osm (XML) file, e.g. a JOSM or Overpass export."""
    nodes, ways, n = {}, {}, 0
    root = ET.parse(path).getroot()
    for el in root:
        tags = {t.get("k"): t.get("v") for t in el.findall("tag")}
        if el.tag == "node":
            pt = (float(el.get("lon")), float(el.get("lat")))
            nodes[el.get("id")] = pt
            if tags and _wanted(tags):
                store.put_feature("node/" + el.get("id"), tags, Point(pt))
                n += 1
        elif el.tag == "way":
            coords = [nodes[nd.get("ref")] for nd in el.findall("nd")
                      if nd.get("ref") in nodes]
            ways[el.get("id")] = coords
            if tags and _wanted(tags) and len(coords) >= 2:
                store.put_feature("way/" + el.get("id"), tags,
                                  LineString(coords))
                n += 1
        elif el.tag == "relation" and _wanted(tags) and \
                tags.get("type") == "multipolygon":
            outers, inners = [], []
            for m in el.findall("member"):
                if m.get("type") == "way" and m.get("ref") in ways:
                    (inners if m.get("role") == "inner" else outers).append(
                        ways[m.get("ref")])
            geom = assemble_polygon(outers, inners)
            if geom is not None:
                store.put_feature("relation/" + el.get("id"), tags, geom)
                n += 1
    return n


def import_pbf(store, path):
    """Import an .osm.pbf extract (e.g. from download.geofabrik.de).
    Requires the ``osmium`` Python package."""
    try:
        import osmium
    except ImportError:
        raise SystemExit("PBF import needs pyosmium: pip install osmium")

    class Handler(osmium.SimpleHandler):
        def __init__(self):
            super().__init__()
            self.n = 0

        def node(self, n):
            tags = dict(n.tags)
            if tags and _wanted(tags):
                store.put_feature("node/%d" % n.id, tags,
                                  Point(n.location.lon, n.location.lat))
                self.n += 1

        def way(self, w):
            tags = dict(w.tags)
            if not tags or not _wanted(tags):
                return
            try:
                coords = [(nd.lon, nd.lat) for nd in w.nodes]
            except osmium.InvalidLocationError:
                return
            if len(coords) >= 2:
                store.put_feature("way/%d" % w.id, tags, LineString(coords))
                self.n += 1

        def area(self, a):
            if a.from_way():
                return                     # closed ways are handled above
            tags = dict(a.tags)
            if not _wanted(tags):
                return
            polys = []
            for outer in a.outer_rings():
                holes = [[(nd.lon, nd.lat) for nd in inner]
                         for inner in a.inner_rings(outer)]
                polys.append(Polygon([(nd.lon, nd.lat) for nd in outer],
                                     holes))
            geom = unary_union(polys) if polys else None
            if geom is not None and not geom.is_empty:
                store.put_feature("relation/%d" % a.orig_id(), tags, geom)
                self.n += 1

    h = Handler()
    h.apply_file(path, locations=True)
    return h.n


# -- bathymetry -------------------------------------------------------------

def download_erddap(store, bounds, tile_size=1.0, base=ERDDAP_URL,
                    dataset=ERDDAP_DATASET, variable=ERDDAP_VARIABLE,
                    force=False):
    """Download an elevation grid (negative = below sea level) from an ERDDAP
    griddap server.  Default: SRTM15+ (15 arc seconds, global)."""
    src = "depth:%s" % dataset
    todo = list(tiles(bounds, tile_size))
    for k, t in enumerate(todo, 1):
        if not force and store.is_downloaded(src, t):
            log("  [%d/%d] depth %s already offline" % (k, len(todo), t))
            continue
        url = "%s/griddap/%s.json?%s[(%s):1:(%s)][(%s):1:(%s)]" % (
            base, dataset, variable, t[1], t[3], t[0], t[2])
        log("  [%d/%d] depth %s <- %s" % (k, len(todo), t, dataset))
        rows = json.loads(http_get(url))["table"]["rows"]
        lats = sorted({round(r[0], 9) for r in rows})
        lons = sorted({round(r[1], 9) for r in rows})
        li = {v: i for i, v in enumerate(lats)}
        oi = {v: i for i, v in enumerate(lons)}
        arr = np.full((len(lats), len(lons)), np.nan, dtype="f4")
        for lat, lon, z in rows:
            if z is not None:
                arr[li[round(lat, 9)], oi[round(lon, 9)]] = -z
        dlat = (lats[-1] - lats[0]) / max(len(lats) - 1, 1)
        dlon = (lons[-1] - lons[0]) / max(len(lons) - 1, 1)
        store.put_grid(src, lons[0], lats[0], dlon, dlat, arr)
        store.mark_downloaded(src, t)
        log("    %dx%d grid points" % (len(lons), len(lats)))


def import_esri_ascii(store, path, positive_down=False, name=None):
    """Import an ESRI ASCII grid (.asc), e.g. an EMODnet Bathymetry DTM tile.
    By default values are elevations (negative below sea level)."""
    header = {}
    with open(path) as f:
        while True:
            pos = f.tell()
            line = f.readline()
            parts = line.split()
            if len(parts) == 2 and parts[0][0].isalpha():
                header[parts[0].lower()] = float(parts[1])
            else:
                f.seek(pos)
                break
        data = np.loadtxt(f, dtype="f4")
    nx, ny = int(header["ncols"]), int(header["nrows"])
    cell = header["cellsize"]
    data = data.reshape(ny, nx)[::-1]           # south row first
    nodata = header.get("nodata_value")
    if nodata is not None:
        data[data == nodata] = np.nan
    if not positive_down:
        data = -data
    x0 = header.get("xllcenter", header.get("xllcorner", 0) + cell / 2)
    y0 = header.get("yllcenter", header.get("yllcorner", 0) + cell / 2)
    src = "depth:%s" % (name or "asc")
    store.put_grid(src, x0, y0, cell, cell, data)
    store.mark_downloaded(src, (x0, y0, x0 + cell * (nx - 1),
                                y0 + cell * (ny - 1)))
    return nx * ny


def import_xyz(store, path, resolution, positive_down=True, name=None):
    """Import scattered soundings ``lon lat depth`` (e.g. own echo sounder
    logs) by gridding them; the shallowest value per cell wins."""
    pts = np.loadtxt(path, delimiter=None if path.endswith(".xyz") else ",",
                     usecols=(0, 1, 2), dtype="f8")
    if not positive_down:
        pts[:, 2] = -pts[:, 2]
    x0, y0 = pts[:, 0].min(), pts[:, 1].min()
    ix = np.rint((pts[:, 0] - x0) / resolution).astype(int)
    iy = np.rint((pts[:, 1] - y0) / resolution).astype(int)
    arr = np.full((iy.max() + 1, ix.max() + 1), np.nan, dtype="f4")
    order = np.argsort(-pts[:, 2])              # deepest first, shallow wins
    arr[iy[order], ix[order]] = pts[order, 2]
    src = "depth:%s" % (name or "xyz")
    store.put_grid(src, x0, y0, resolution, resolution, arr)
    store.mark_downloaded(src, (x0, y0, x0 + resolution * ix.max(),
                                y0 + resolution * iy.max()))
    return len(pts)
