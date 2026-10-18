"""Route handling: read routes from GPX (as exported by OpenCPN), build a
corridor of a given width around them and split it into chart cells."""

import math
import xml.etree.ElementTree as ET

import shapely
from shapely.geometry import LineString, Polygon, box

NM_DEG = 1.0 / 60.0     # one nautical mile in degrees of latitude


def read_gpx(path, name=None):
    """Return [(route name, [(lon, lat), ...])] for all routes and tracks."""
    root = ET.parse(path).getroot()
    ns = root.tag[1:].split("}")[0] if root.tag.startswith("{") else ""
    q = (lambda t: "{%s}%s" % (ns, t)) if ns else (lambda t: t)
    out = []

    def label(el, default):
        n = el.find(q("name"))
        return n.text.strip() if n is not None and n.text else default

    for k, rte in enumerate(root.iter(q("rte")), 1):
        pts = [(float(p.get("lon")), float(p.get("lat")))
               for p in rte.iter(q("rtept"))]
        out.append((label(rte, "route%d" % k), pts))
    for k, trk in enumerate(root.iter(q("trk")), 1):
        pts = [(float(p.get("lon")), float(p.get("lat")))
               for p in trk.iter(q("trkpt"))]
        out.append((label(trk, "track%d" % k), pts))
    out = [(n, p) for n, p in out if len(p) >= 2]
    if name:
        out = [(n, p) for n, p in out if n == name]
    return out


def parse_waypoints(text):
    """'54.32,10.15 54.50,11.00' or with ';' -> [(lon, lat)] (input lat,lon)"""
    pts = []
    for item in text.replace(";", " ").split():
        lat, lon = (float(v) for v in item.split(","))
        pts.append((lon, lat))
    return pts


def corridor(points, width_nm):
    """Polygon (lon/lat) within ``width_nm`` nautical miles of the route."""
    lat0 = sum(p[1] for p in points) / len(points)
    k = math.cos(math.radians(lat0))
    # buffer in a local equirectangular projection (x scaled by cos(lat))
    line = shapely.transform(LineString(points), lambda c: c * [k, 1.0])
    poly = line.buffer(width_nm * NM_DEG, quad_segs=16)
    return shapely.transform(poly, lambda c: c / [k, 1.0])


def circle(center, radius_nm):
    """Polygon (lon/lat) within ``radius_nm`` of a point."""
    k = math.cos(math.radians(center[1]))
    pts = []
    for i in range(64):
        a = 2 * math.pi * i / 64
        pts.append((center[0] + math.cos(a) * radius_nm * NM_DEG / k,
                    center[1] + math.sin(a) * radius_nm * NM_DEG))
    return Polygon(pts)


def route_length_nm(points):
    total = 0.0
    for (x1, y1), (x2, y2) in zip(points, points[1:]):
        k = math.cos(math.radians((y1 + y2) / 2))
        total += math.hypot((x2 - x1) * k, y2 - y1) * 60
    return total


def cells_for(area, cell_size):
    """Grid-aligned boxes of ``cell_size`` degrees (lat; longitude size is
    widened with latitude so cells stay roughly square) covering ``area``.
    Returns [(row, col, bounds, coverage polygon)]."""
    minx, miny, maxx, maxy = area.bounds
    out = []
    r = math.floor(miny / cell_size)
    while r * cell_size < maxy:
        lat_mid = (r + 0.5) * cell_size
        dlon = cell_size / max(0.2, math.cos(math.radians(lat_mid)))
        dlon = round(dlon / cell_size) * cell_size or cell_size
        c = math.floor(minx / dlon)
        while c * dlon < maxx:
            b = (c * dlon, r * cell_size, (c + 1) * dlon, (r + 1) * cell_size)
            cov = area.intersection(box(*b))
            if not cov.is_empty and cov.area > 0:
                out.append((r, c, b, cov))
            c += 1
        r += 1
    return out
