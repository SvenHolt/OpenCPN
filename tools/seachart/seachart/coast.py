"""Build land polygons for a rectangular area from OSM coastline ways.

OSM coastlines are directed with land on the left.  Closed rings become
islands; open chains are clipped to the area and closed by walking along the
area boundary counter-clockwise.
"""

from shapely.geometry import LineString, MultiLineString, Polygon, box
from shapely import line_merge
from shapely.ops import unary_union

EPS = 1e-7


def _perimeter_pos(b, pt):
    """Position of a boundary point on the CCW perimeter, in [0, 4)."""
    minx, miny, maxx, maxy = b
    x, y = pt
    w, h = maxx - minx, maxy - miny
    if abs(y - miny) < EPS and x < maxx - EPS:
        return (x - minx) / w
    if abs(x - maxx) < EPS and y < maxy - EPS:
        return 1 + (y - miny) / h
    if abs(y - maxy) < EPS and x > minx + EPS:
        return 2 + (maxx - x) / w
    if abs(x - minx) < EPS:
        return 3 + (maxy - y) / h
    return None


def _nearest_side(b, pt):
    minx, miny, maxx, maxy = b
    x, y = pt
    d = [(y - miny, (x, miny)), (maxx - x, (maxx, y)), (maxy - y, (x, maxy)),
         (x - minx, (minx, y))]
    return min(d)[1]


def _corner(b, k):
    minx, miny, maxx, maxy = b
    return [(maxx, miny), (maxx, maxy), (minx, maxy), (minx, miny)][k % 4]


def _walk(b, t0, t1):
    """Corner points passed when walking CCW from t0 to t1."""
    pts = []
    if t1 < t0:
        t1 += 4
    k = int(t0) + 1
    while k <= t1:
        pts.append(_corner(b, k - 1))
        k += 1
    return pts


def _segments(geom):
    if geom.is_empty:
        return []
    if isinstance(geom, LineString):
        return [geom]
    if isinstance(geom, MultiLineString):
        return list(geom.geoms)
    return [g for g in getattr(geom, "geoms", []) if isinstance(g, LineString)]


def land_polygons(coastlines, bounds, default_land=None):
    """coastlines: iterable of LineStrings.  Returns a (Multi)Polygon or None.

    ``default_land`` decides the case where no coastline crosses the area:
    a callable returning True when the area is on land."""
    b = tuple(bounds)
    frame = box(*b)
    parts = []
    for c in coastlines:
        parts += _segments(c)
    lines = _segments(line_merge(MultiLineString(parts), directed=True)) \
        if parts else []

    islands, holes, pieces = [], [], []
    for line in lines:
        if line.is_ring:
            poly = Polygon(line.coords)
            if not poly.is_valid:
                poly = poly.buffer(0)
            ccw = Polygon(line.coords).exterior.is_ccw
            if poly.intersects(frame):
                (islands if ccw else holes).append(poly)
            continue
        for seg in _segments(line.intersection(frame)):
            coords = list(seg.coords)
            if seg.is_ring or len(coords) < 2:
                continue
            # A coastline ending inside the area means the data around it
            # is incomplete: extend it to the nearest side of the area.
            t_in = _perimeter_pos(b, coords[0])
            if t_in is None:
                coords.insert(0, _nearest_side(b, coords[0]))
                t_in = _perimeter_pos(b, coords[0])
            t_out = _perimeter_pos(b, coords[-1])
            if t_out is None:
                coords.append(_nearest_side(b, coords[-1]))
                t_out = _perimeter_pos(b, coords[-1])
            pieces.append([coords, t_in, t_out])

    polys = []
    unused = list(range(len(pieces)))
    while unused:
        first = unused.pop(0)
        ring = list(pieces[first][0])
        cur = first
        for _ in range(len(pieces) + 1):
            t = pieces[cur][2]
            # next entry point CCW from the current exit
            cands = [(((pieces[i][1] - t) % 4), i) for i in unused + [first]]
            dist, nxt = min(cands)
            ring += _walk(b, t, t + dist)
            if nxt == first:
                break
            ring += pieces[nxt][0]
            unused.remove(nxt)
            cur = nxt
        if len(ring) >= 3:
            p = Polygon(ring)
            polys.append(p if p.is_valid else p.buffer(0))

    if pieces:
        land = unary_union(polys)
    elif default_land is not None and default_land():
        land = frame
    else:
        land = None
    if islands:
        isl = unary_union(islands).intersection(frame)
        land = isl if land is None else land.union(isl)
    if holes and land is not None:
        land = land.difference(unary_union(holes))
    if land is None or land.is_empty:
        return None
    return land.intersection(frame)
