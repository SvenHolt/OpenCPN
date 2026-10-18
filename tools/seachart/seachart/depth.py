"""Depth contours (DEPCNT), depth areas (DEPARE) and soundings (SOUNDG)
from a regular depth grid (metres, positive down, NaN = no data)."""

import numpy as np
from shapely.geometry import LineString, box
from shapely.ops import polygonize, unary_union

DEFAULT_LEVELS = [2, 5, 10, 15, 20, 30, 50, 100, 200, 500, 1000, 2000, 5000]


def contours(lon0, lat0, dlon, dlat, grid, level):
    """Marching squares; returns a list of LineStrings for one level."""
    g = np.where(np.isnan(grid), np.nan, grid - level)
    # values exactly on the level would give degenerate crossings at nodes
    g[g == 0] = -1e-4
    a, b = g[:-1, :-1], g[:-1, 1:]      # SW, SE
    c, d = g[1:, 1:], g[1:, :-1]        # NE, NW
    valid = ~(np.isnan(a) | np.isnan(b) | np.isnan(c) | np.isnan(d))
    idx = ((a > 0) * 1 + (b > 0) * 2 + (c > 0) * 4 + (d > 0) * 8)
    rows, cols = np.nonzero(valid & (idx > 0) & (idx < 15))

    def interp(v1, v2, p1, p2):
        t = v1 / (v1 - v2)
        return (p1[0] + t * (p2[0] - p1[0]), p1[1] + t * (p2[1] - p1[1]))

    segs = []
    err = np.seterr(divide="ignore", invalid="ignore")  # unused edges
    for r, k in zip(rows.tolist(), cols.tolist()):
        va, vb, vc, vd = a[r, k], b[r, k], c[r, k], d[r, k]
        pa, pb, pc, pd = (k, r), (k + 1, r), (k + 1, r + 1), (k, r + 1)
        s = interp(va, vb, pa, pb)   # south edge
        e = interp(vb, vc, pb, pc)   # east edge
        n = interp(vc, vd, pc, pd)   # north edge
        w = interp(vd, va, pd, pa)   # west edge
        case = idx[r, k]
        if case in (5, 10):          # saddle: decide with the centre value
            centre = (va + vb + vc + vd) / 4 > 0
            # 5 = SW+NE deeper, 10 = SE+NW deeper: cut off the two corners
            # that are not connected through the centre.
            if case == 5:
                pairs = [(s, e), (n, w)] if centre else [(s, w), (n, e)]
            else:
                pairs = [(s, w), (n, e)] if centre else [(s, e), (n, w)]
        else:
            edges = {1: (s, w), 2: (s, e), 3: (w, e), 4: (e, n), 6: (s, n),
                     7: (w, n), 8: (w, n), 9: (s, n), 11: (e, n), 12: (w, e),
                     13: (s, e), 14: (s, w)}
            pairs = [edges[case]]
        segs += pairs
    np.seterr(**err)

    # join segments into polylines
    def key(p):
        return (round(p[0], 6), round(p[1], 6))
    ends = {}
    for i, (p, q) in enumerate(segs):
        ends.setdefault(key(p), []).append(i)
        ends.setdefault(key(q), []).append(i)
    used = [False] * len(segs)
    lines = []
    for i in range(len(segs)):
        if used[i]:
            continue
        used[i] = True
        line = [segs[i][0], segs[i][1]]
        for forward in (True, False):
            while True:
                tip = line[-1] if forward else line[0]
                nxt = None
                for j in ends.get(key(tip), []):
                    if not used[j]:
                        nxt = j
                        break
                if nxt is None:
                    break
                used[nxt] = True
                p, q = segs[nxt]
                other = q if key(p) == key(tip) else p
                if forward:
                    line.append(other)
                else:
                    line.insert(0, other)
        coords = [(lon0 + x * dlon, lat0 + y * dlat) for x, y in line]
        if len(coords) >= 2:
            lines.append(LineString(coords))
    return lines


def sample(lon0, lat0, dlon, dlat, grid, x, y):
    """Bilinear interpolation (ignores NaN neighbours)."""
    fx, fy = (x - lon0) / dlon, (y - lat0) / dlat
    i, j = int(np.floor(fy)), int(np.floor(fx))
    vals, wts = [], []
    for di, dj, w in ((0, 0, (1 - (fy - i)) * (1 - (fx - j))),
                      (0, 1, (1 - (fy - i)) * (fx - j)),
                      (1, 0, (fy - i) * (1 - (fx - j))),
                      (1, 1, (fy - i) * (fx - j))):
        ii, jj = i + di, j + dj
        if 0 <= ii < grid.shape[0] and 0 <= jj < grid.shape[1] and \
                not np.isnan(grid[ii, jj]):
            vals.append(grid[ii, jj])
            wts.append(w)
    if not vals:
        return np.nan
    return float(np.dot(vals, wts) / sum(wts)) if sum(wts) > 0 else \
        float(np.mean(vals))


def depth_products(lon0, lat0, dlon, dlat, grid, levels=None,
                   simplify=0.0):
    """Return (contours, areas) where contours = [(level, LineString)] and
    areas = [(drval1, drval2, Polygon)] covering the grid extent.

    Depth areas are built by polygonizing all contour lines together with
    the grid frame, so they share their borders with the contour lines."""
    levels = levels or DEFAULT_LEVELS
    finite = grid[~np.isnan(grid)]
    if finite.size == 0:
        return [], []
    lo, hi = float(finite.min()), float(finite.max())
    used = [lv for lv in levels if lo < lv < hi]
    cont, coast = [], []
    # The 0 m line of the grid only separates the band "drying / land in
    # the DEM" from the shallowest band; it is not emitted as a contour.
    for lv in ([0.0] if lo < 0 < hi else []) + used:
        for line in contours(lon0, lat0, dlon, dlat, grid, lv):
            if simplify:
                line = line.simplify(simplify)
            if line.length > 0:
                (cont if lv else coast).append((lv, line))
    ny, nx = grid.shape
    frame = box(lon0, lat0, lon0 + dlon * (nx - 1), lat0 + dlat * (ny - 1))
    noded = unary_union([frame.boundary] + [ln for _, ln in cont + coast])
    bounds = [-1e9, 0.0] + used + [1e9]
    bands = {}
    for face in polygonize(noded):
        pt = face.representative_point()
        z = sample(lon0, lat0, dlon, dlat, grid, pt.x, pt.y)
        if np.isnan(z):
            continue
        k = int(np.searchsorted(bounds, z, side="right")) - 1
        k = max(0, min(k, len(bounds) - 2))
        bands.setdefault(k, []).append(face)
    areas = []
    for k, faces in sorted(bands.items()):
        d1 = bounds[k] if k > 0 else -10.0       # drying / land in the DEM
        d2 = bounds[k + 1] if bounds[k + 1] < 1e8 else max(hi, bounds[k])
        areas.append((d1, d2, unary_union(faces)))
    return cont, areas


def soundings(lon0, lat0, dlon, dlat, grid, spacing_deg, region=None):
    """Thin the grid to roughly ``spacing_deg``; keep the shallowest value of
    each block (safe side).  Returns [(lon, lat, depth)] for depth > 0."""
    from shapely.prepared import prep
    ny, nx = grid.shape
    step_x = max(1, int(round(spacing_deg / dlon)))
    step_y = max(1, int(round(spacing_deg / dlat)))
    pr = prep(region) if region is not None else None
    from shapely.geometry import Point
    out = []
    for r0 in range(0, ny, step_y):
        for c0 in range(0, nx, step_x):
            blk = grid[r0:r0 + step_y, c0:c0 + step_x]
            if np.all(np.isnan(blk)):
                continue
            r, c = np.unravel_index(np.nanargmin(blk), blk.shape)
            z = float(blk[r, c])
            if z <= 0:
                continue
            x, y = lon0 + (c0 + c) * dlon, lat0 + (r0 + r) * dlat
            if pr is not None and not pr.contains(Point(x, y)):
                continue
            out.append((x, y, round(z, 1)))
    return out
