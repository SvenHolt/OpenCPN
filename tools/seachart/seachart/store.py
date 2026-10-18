"""Offline data store (a single SQLite file).

* ``features``  OSM / OpenSeaMap objects (tags as JSON, geometry as WKB)
* ``grids``     bathymetry grid chunks (float32 depth, positive down)
* ``downloads`` which source covered which bounding box, and when
* ``harbours``, ``photos``  marinas/anchorages maintained by the user
* ``pois``      restaurants, shops, ... near harbours (OpenStreetMap)
* ``weather``   hourly forecast on a grid (Open-Meteo)
* ``navgrid``   water/depth raster used by OpenCPN's passage planner
"""

import datetime
import json
import os
import sqlite3
import sys

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
""" + """
-- The tables below are shared with OpenCPN (gui/src/seachart_db.cpp).
-- Keep both sides in sync when changing them.
CREATE TABLE IF NOT EXISTS harbours (
    id INTEGER PRIMARY KEY, kind TEXT NOT NULL, name TEXT NOT NULL,
    lon REAL NOT NULL, lat REAL NOT NULL, osm_id TEXT UNIQUE,
    source TEXT NOT NULL DEFAULT 'user', edited INTEGER NOT NULL DEFAULT 0,
    notes TEXT NOT NULL DEFAULT '', depth REAL, berths INTEGER,
    vhf TEXT NOT NULL DEFAULT '', phone TEXT NOT NULL DEFAULT '',
    website TEXT NOT NULL DEFAULT '', facilities TEXT NOT NULL DEFAULT '',
    rating INTEGER, updated TEXT);
CREATE TABLE IF NOT EXISTS photos (
    id INTEGER PRIMARY KEY, harbour_id INTEGER NOT NULL, path TEXT NOT NULL,
    caption TEXT NOT NULL DEFAULT '', added TEXT);
CREATE TABLE IF NOT EXISTS pois (
    id INTEGER PRIMARY KEY, osm_id TEXT UNIQUE, category TEXT NOT NULL,
    name TEXT NOT NULL DEFAULT '', lon REAL NOT NULL, lat REAL NOT NULL,
    tags TEXT NOT NULL DEFAULT '{}');
CREATE INDEX IF NOT EXISTS pois_pos ON pois (lat, lon);
CREATE TABLE IF NOT EXISTS weather (
    lon REAL NOT NULL, lat REAL NOT NULL, time INTEGER NOT NULL,
    wind_kn REAL, wind_dir REAL, gust_kn REAL, wave_m REAL, wave_dir REAL,
    PRIMARY KEY (lon, lat, time));
CREATE TABLE IF NOT EXISTS weather_meta (
    lon REAL NOT NULL, lat REAL NOT NULL, fetched INTEGER NOT NULL,
    PRIMARY KEY (lon, lat));
CREATE TABLE IF NOT EXISTS navgrid (
    id INTEGER PRIMARY KEY, lon0 REAL, lat0 REAL, dlon REAL, dlat REAL,
    nx INTEGER, ny INTEGER, data BLOB, created TEXT);
"""


def default_path():
    """Shared location, also used by OpenCPN's passage planner:
    <Documents>/seachart/seachart.db (override with SEACHART_DB)."""
    if os.environ.get("SEACHART_DB"):
        return os.environ["SEACHART_DB"]
    return os.path.join(documents_dir(), "seachart", "seachart.db")


def documents_dir():
    """The folder wxStandardPaths::GetDocumentsDir() returns in OpenCPN."""
    if sys.platform == "win32":
        import ctypes
        buf = ctypes.create_unicode_buffer(260)
        # CSIDL_PERSONAL: "Documents", also when redirected to OneDrive
        if ctypes.windll.shell32.SHGetFolderPathW(None, 5, None, 0, buf) == 0:
            return buf.value
    home = os.path.expanduser("~")
    try:   # XDG user dirs (Linux), as used by wxWidgets
        with open(os.path.join(home, ".config", "user-dirs.dirs")) as f:
            for line in f:
                if line.startswith("XDG_DOCUMENTS_DIR="):
                    path = line.split("=", 1)[1].strip().strip('"')
                    return path.replace("$HOME", home)
    except OSError:
        pass
    if sys.platform == "darwin":
        return os.path.join(home, "Documents")
    return home     # wxGTK without XDG configuration


class Store:
    def __init__(self, path):
        self.path = path
        folder = os.path.dirname(os.path.abspath(path))
        os.makedirs(folder, exist_ok=True)
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
                        (datetime.datetime.now(datetime.timezone.utc)
                         .isoformat(timespec="seconds"),))
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
