"""Hourly wind and wave forecast from Open-Meteo, stored offline on a grid.

OpenCPN's passage planner interpolates this grid in space and time; the
chart guides show it for every harbour.  The data is only as fresh as the
last download - run ``seachart weather`` before leaving the harbour.
"""

import calendar
import json
import math
import time

from .sources import http_get, log

FORECAST_URL = "https://api.open-meteo.com/v1/forecast"
MARINE_URL = "https://marine-api.open-meteo.com/v1/marine"


def grid_points(bounds, spacing):
    minx, miny, maxx, maxy = bounds
    pts = []
    y = math.floor(miny / spacing) * spacing
    while y <= maxy + spacing / 2:
        x = math.floor(minx / spacing) * spacing
        while x <= maxx + spacing / 2:
            pts.append((round(x, 4), round(y, 4)))
            x += spacing
        y += spacing
    return pts


def _fetch(url, pts, params):
    query = "latitude=%s&longitude=%s&%s" % (
        ",".join("%.4f" % p[1] for p in pts),
        ",".join("%.4f" % p[0] for p in pts), params)
    data = json.loads(http_get(url + "?" + query))
    return data if isinstance(data, list) else [data]


def _epoch(iso):
    return calendar.timegm(time.strptime(iso, "%Y-%m-%dT%H:%M"))


def download(store, points, days=7, batch=50):
    """Fetch forecasts for [(lon, lat)] and replace the stored values."""
    days = max(1, min(int(days), 16))
    fetched = int(time.time())
    n = 0
    for k in range(0, len(points), batch):
        chunk = points[k:k + batch]
        log("  weather %d-%d of %d points"
            % (k + 1, k + len(chunk), len(points)))
        wind = _fetch(FORECAST_URL, chunk,
                      "hourly=wind_speed_10m,wind_direction_10m,"
                      "wind_gusts_10m&wind_speed_unit=kn&timezone=GMT"
                      "&forecast_days=%d" % days)
        try:
            waves = _fetch(MARINE_URL, chunk,
                           "hourly=wave_height,wave_direction&timezone=GMT"
                           "&forecast_days=%d" % min(days, 8))
        except Exception as exc:          # marine model not everywhere
            log("    no wave data: %s" % exc)
            waves = [None] * len(chunk)
        for (lon, lat), w, m in zip(chunk, wind, waves):
            h = w["hourly"]
            wave = {}
            if m and "hourly" in m:
                mh = m["hourly"]
                wave = {t: (hgt, d) for t, hgt, d in zip(
                    mh["time"], mh["wave_height"], mh["wave_direction"])}
            store.db.execute("DELETE FROM weather WHERE lon=? AND lat=?",
                             (lon, lat))
            rows = []
            for i, t in enumerate(h["time"]):
                hgt, wdir = wave.get(t, (None, None))
                rows.append((lon, lat, _epoch(t), h["wind_speed_10m"][i],
                             h["wind_direction_10m"][i],
                             h["wind_gusts_10m"][i], hgt, wdir))
            store.db.executemany(
                "INSERT OR REPLACE INTO weather VALUES (?,?,?,?,?,?,?,?)",
                rows)
            store.db.execute("INSERT OR REPLACE INTO weather_meta VALUES"
                             " (?,?,?)", (lon, lat, fetched))
            n += 1
        store.db.commit()
    return n


def download_area(store, bounds, spacing=0.25, days=7):
    pts = grid_points(bounds, spacing)
    return download(store, pts, days)


def at(store, lon, lat, t):
    """Forecast at the nearest stored grid point, nearest hour.
    Returns dict or None."""
    row = store.db.execute(
        "SELECT lon, lat FROM weather_meta ORDER BY (lon-?)*(lon-?)+"
        "(lat-?)*(lat-?) LIMIT 1", (lon, lon, lat, lat)).fetchone()
    if row is None:
        return None
    rec = store.db.execute(
        "SELECT time, wind_kn, wind_dir, gust_kn, wave_m, wave_dir FROM"
        " weather WHERE lon=? AND lat=? ORDER BY abs(time-?) LIMIT 1",
        (row[0], row[1], int(t))).fetchone()
    if rec is None:
        return None
    keys = ("time", "wind_kn", "wind_dir", "gust_kn", "wave_m", "wave_dir")
    return dict(zip(keys, rec))


def series(store, lon, lat, start=None, hours=72, step=3):
    """Forecast table for a position (nearest grid point)."""
    start = int(start or time.time()) // 3600 * 3600
    out = []
    for h in range(0, hours, step):
        rec = at(store, lon, lat, start + h * 3600)
        if rec and abs(rec["time"] - (start + h * 3600)) <= 3600:
            out.append(rec)
    return out


def fetched_at(store):
    row = store.db.execute("SELECT max(fetched) FROM weather_meta"
                           ).fetchone()
    return row[0] if row else None
