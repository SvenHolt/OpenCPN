"""Offline data store (a single SQLite file).

* ``features``  OSM / OpenSeaMap objects (tags as JSON, geometry as WKB)
* ``grids``     bathymetry grid chunks (float32 depth, positive down)
* ``downloads`` which source covered which bounding box, and when
"""

import datetime
import json
import sqlite3

import numpy as np
from shapely import wkb
from shapely.geometry import box

SCHEMA = """
CREATE TABLE IF NOT EXISTS features (
    id INTEGER PRIMARY KEY, osm_id TEXT UNIQUE, tags TEXT, geom BLOB);
CREATE VIRTUAL TABLE IF NOT EXISTS features_idx USING rtree(
    id, minx, maxx, miny, maxy);
CREATE TABLE IF NOT EXISTS grids (
    id INTEGER PRIMARY KEY, source TEXT, lon0 REAL, lat0 REAL, dlon REAL,
    dlat REAL, nx INTEGER, ny INTEGER, data BLOB);
CREATE VIRTUAL TABLE IF NOT EXISTS grids_idx USING rtree(
    id, minx, maxx, miny, maxy);
CREATE TABLE IF NOT EXISTS downloads (
    source TEXT, minx REAL, miny REAL, maxx REAL, maxy REAL, date TEXT);
"""


class Store:
    def __init__(self, path):
        self.path = path
        self.db = sqlite3.connect(path)
        self.db.executescript(SCHEMA)
        self.put_bounds = None      # extent of everything put since open

    def close(self):
        self.db.commit()
        self.db.close()

    # -- features -----------------------------------------------------
    def put_feature(self, osm_id, tags, geom):
        if geom is None or geom.is_empty:
            return
        cur = self.db.execute("SELECT id FROM features WHERE osm_id=?",
                              (osm_id,))
        row = cur.fetchone()
        blob = wkb.dumps(geom)
        minx, miny, maxx, maxy = geom.bounds
        pb = self.put_bounds
        self.put_bounds = (minx, miny, maxx, maxy) if pb is None else (
            min(pb[0], minx), min(pb[1], miny), max(pb[2], maxx),
            max(pb[3], maxy))
        if row:
            fid = row[0]
            self.db.execute("UPDATE features SET tags=?, geom=? WHERE id=?",
                            (json.dumps(tags), blob, fid))
            self.db.execute("UPDATE features_idx SET minx=?, maxx=?, miny=?,"
                            " maxy=? WHERE id=?",
                            (minx, maxx, miny, maxy, fid))
        else:
            cur = self.db.execute(
                "INSERT INTO features (osm_id, tags, geom) VALUES (?,?,?)",
                (osm_id, json.dumps(tags), blob))
            self.db.execute("INSERT INTO features_idx VALUES (?,?,?,?,?)",
                            (cur.lastrowid, minx, maxx, miny, maxy))

    def features(self, bounds):
        minx, miny, maxx, maxy = bounds
        cur = self.db.execute(
            "SELECT f.osm_id, f.tags, f.geom FROM features f JOIN features_idx"
            " i ON f.id = i.id WHERE i.maxx >= ? AND i.minx <= ? AND"
            " i.maxy >= ? AND i.miny <= ?", (minx, maxx, miny, maxy))
        for osm_id, tags, blob in cur:
            yield osm_id, json.loads(tags), wkb.loads(blob)

    def feature_count(self):
        return self.db.execute("SELECT count(*) FROM features").fetchone()[0]

    # -- bathymetry ---------------------------------------------------
    def put_grid(self, source, lon0, lat0, dlon, dlat, depth):
        """depth: 2-D array [row = lat index from lat0 upward, col = lon]."""
        depth = np.asarray(depth, dtype="<f4")
        ny, nx = depth.shape
        cur = self.db.execute(
            "INSERT INTO grids (source, lon0, lat0, dlon, dlat, nx, ny, data)"
            " VALUES (?,?,?,?,?,?,?,?)",
            (source, lon0, lat0, dlon, dlat, nx, ny, depth.tobytes()))
        self.db.execute("INSERT INTO grids_idx VALUES (?,?,?,?,?)",
                        (cur.lastrowid, lon0, lon0 + dlon * (nx - 1), lat0,
                         lat0 + dlat * (ny - 1)))

    def grids(self, bounds):
        minx, miny, maxx, maxy = bounds
        cur = self.db.execute(
            "SELECT g.source, g.lon0, g.lat0, g.dlon, g.dlat, g.nx, g.ny,"
            " g.data FROM grids g JOIN grids_idx i ON g.id = i.id WHERE"
            " i.maxx >= ? AND i.minx <= ? AND i.maxy >= ? AND i.miny <= ?",
            (minx, maxx, miny, maxy))
        for src, lon0, lat0, dlon, dlat, nx, ny, data in cur:
            arr = np.frombuffer(data, dtype="<f4").reshape(ny, nx)
            yield src, lon0, lat0, dlon, dlat, arr

    def depth_grid(self, bounds, resolution=None):
        """Mosaic all stored grids into one regular grid covering bounds.

        Finer grids win over coarser ones.  Returns (lon0, lat0, dlon, dlat,
        array) or None when no bathymetry is stored for the area."""
        chunks = list(self.grids(bounds))
        if not chunks:
            return None
        chunks.sort(key=lambda c: -c[3])          # coarse first
        res = resolution or min(c[3] for c in chunks)
        minx, miny, maxx, maxy = bounds
        nx = int(np.ceil((maxx - minx) / res)) + 1
        ny = int(np.ceil((maxy - miny) / res)) + 1
        lons = minx + np.arange(nx) * res
        lats = miny + np.arange(ny) * res
        out = np.full((ny, nx), np.nan, dtype="f4")
        for _, lon0, lat0, dlon, dlat, arr in chunks:
            ci = np.rint((lons - lon0) / dlon).astype(int)
            ri = np.rint((lats - lat0) / dlat).astype(int)
            cok = (ci >= 0) & (ci < arr.shape[1])
            rok = (ri >= 0) & (ri < arr.shape[0])
            if not cok.any() or not rok.any():
                continue
            sub = arr[np.ix_(ri[rok], ci[cok])]
            view = out[np.ix_(rok, cok)]
            mask = ~np.isnan(sub)
            view[mask] = sub[mask]
            out[np.ix_(rok, cok)] = view
        return minx, miny, res, res, out

    # -- download bookkeeping -----------------------------------------
    def mark_downloaded(self, source, bounds):
        self.db.execute("INSERT INTO downloads VALUES (?,?,?,?,?,?)",
                        (source,) + tuple(bounds) +
                        (datetime.datetime.utcnow().isoformat(
                            timespec="seconds"),))
        self.db.commit()

    def is_downloaded(self, source, bounds, eps=1e-9):
        cur = self.db.execute(
            "SELECT 1 FROM downloads WHERE source=? AND minx<=? AND miny<=?"
            " AND maxx>=? AND maxy>=?", (source, bounds[0] + eps,
                                         bounds[1] + eps, bounds[2] - eps,
                                         bounds[3] - eps))
        return cur.fetchone() is not None

    def coverage(self, source):
        from shapely.ops import unary_union
        cur = self.db.execute(
            "SELECT minx, miny, maxx, maxy FROM downloads WHERE source=?",
            (source,))
        boxes = [box(*r) for r in cur]
        return unary_union(boxes) if boxes else None

    def summary(self):
        out = {"features": self.feature_count(),
               "grids": self.db.execute(
                   "SELECT count(*) FROM grids").fetchone()[0]}
        for src, n, minx, miny, maxx, maxy, last in self.db.execute(
                "SELECT source, count(*), min(minx), min(miny), max(maxx),"
                " max(maxy), max(date) FROM downloads GROUP BY source"):
            out[src] = "%d tiles, extent %.3f,%.3f,%.3f,%.3f, last %s" % (
                n, minx, miny, maxx, maxy, last)
        return out
