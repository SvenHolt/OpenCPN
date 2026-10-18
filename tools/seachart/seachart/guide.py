"""Offline harbour guide written next to the charts: an HTML page with the
user's harbour data, photos, nearby POIs and the stored weather forecast,
plus a GPX file with all harbours and POIs for OpenCPN."""

import datetime
import html
import os
import shutil
from xml.sax.saxutils import escape

from . import harbours as hb
from . import pois as poi
from . import weather

KIND_DE = {"marina": "Marina", "harbour": "Hafen", "anchorage": "Ankerplatz",
           "buoy": "Muringtonne"}
CAT_DE = {"restaurant": "Essen & Trinken", "supermarket": "Einkaufen",
          "fuel": "Tankstelle", "chandlery": "Bootszubehör",
          "service": "Service", "transport": "Verkehr",
          "sight": "Sehenswert"}
# OpenCPN waypoint icon names (data/svg/markicons without ".svg")
GPX_SYM = {"marina": "Service-Marina", "harbour": "Service-Marina",
           "anchorage": "anchorage", "buoy": "mooring",
           "restaurant": "Service-Wine&Dine",
           "supermarket": "Service-Grocery1",
           "fuel": "Service-Fuel-Pump-Diesel-Petrol",
           "chandlery": "Service-Chandlery", "service": "Info-Info",
           "transport": "Info-Info", "sight": "Info-Photo"}

ARROWS = "↓↙←↖↑↗→↘"     # direction the wind blows *to*, from N clockwise


def _arrow(deg):
    if deg is None:
        return ""
    return ARROWS[int(((deg + 22.5) % 360) // 45)]


def _fmt_time(t):
    return datetime.datetime.fromtimestamp(t).strftime("%a %d.%m. %H:%M")


CSS = """
body{font-family:system-ui,sans-serif;margin:0;background:#f4f6f8;color:#1b2733}
header{background:#0b3d5c;color:#fff;padding:16px}
main{max-width:1000px;margin:auto;padding:12px}
section{background:#fff;border-radius:8px;padding:12px 16px;margin:12px 0;
box-shadow:0 1px 3px #0002}
h2{margin:.2em 0}.kind{font-size:.8em;color:#456;font-weight:normal}
table{border-collapse:collapse;font-size:.9em}td,th{padding:2px 8px;
text-align:left;border-bottom:1px solid #e3e8ee}
.photos img{max-height:180px;margin:4px;border-radius:4px}
.wx td{text-align:center}.warn{color:#b00}
nav a{margin-right:10px}small{color:#678}
"""


def write(store, out_dir, bounds, title, radius=1500):
    """Write ``guide.html``, photos and ``harbours.gpx`` into out_dir."""
    rows = hb.query(store, bounds)
    os.makedirs(out_dir, exist_ok=True)
    img_dir = os.path.join(out_dir, "guide_photos")
    fetched = weather.fetched_at(store)
    parts = ["<!doctype html><html lang=de><meta charset=utf-8>"
             "<meta name=viewport content='width=device-width'>"
             "<title>%s</title><style>%s</style>" % (html.escape(title), CSS),
             "<header><h1>Hafenführer: %s</h1><small>%d Häfen und "
             "Ankerplätze. Wetter: %s. Nicht zur Navigation.</small>"
             "</header><main><nav>" % (
                 html.escape(title), len(rows),
                 "Stand " + _fmt_time(fetched) if fetched else
                 "keine Vorhersage offline")]
    parts += ["<a href='#h%d'>%s</a>" % (r["id"], html.escape(r["name"]))
              for r in rows]
    parts.append("</nav>")
    gpx, seen = [], set()
    for r in rows:
        parts.append(_section(store, r, img_dir, radius))
        gpx.append(_wpt(r["lon"], r["lat"], r["name"],
                        GPX_SYM.get(r["kind"], "Info-Info"),
                        KIND_DE.get(r["kind"], r["kind"]) +
                        ("\n" + r["notes"] if r["notes"] else "")))
        for dist, p in poi.near(store, r["lon"], r["lat"], radius):
            if p["id"] in seen:
                continue
            seen.add(p["id"])
            gpx.append(_wpt(p["lon"], p["lat"], p["name"] or
                            CAT_DE.get(p["category"], p["category"]),
                            GPX_SYM.get(p["category"], "Info-Info"),
                            "%s, %d m von %s" % (
                                CAT_DE.get(p["category"], ""), dist,
                                r["name"])))
    parts.append("</main></html>")
    path = os.path.join(out_dir, "guide.html")
    with open(path, "w", encoding="utf-8") as f:
        f.write("".join(parts))
    with open(os.path.join(out_dir, "harbours.gpx"), "w",
              encoding="utf-8") as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n<gpx version="1.1"'
                ' creator="seachart" xmlns="http://www.topografix.com/GPX/1/'
                '1">\n%s</gpx>\n' % "".join(gpx))
    return path, len(rows)


def _wpt(lon, lat, name, sym, desc):
    return ('<wpt lat="%.6f" lon="%.6f"><name>%s</name><desc>%s</desc>'
            '<sym>%s</sym></wpt>\n' % (lat, lon, escape(name or ""),
                                       escape(desc or ""), escape(sym)))


def _section(store, r, img_dir, radius):
    e = html.escape
    out = ["<section id='h%d'><h2>%s <span class=kind>%s</span></h2>" % (
        r["id"], e(r["name"]), KIND_DE.get(r["kind"], r["kind"]))]
    info = [("Position", "%.4f° N, %.4f° E" % (r["lat"], r["lon"]))]
    for key, label, fmt in (("depth", "Wassertiefe", "%s m"),
                            ("berths", "Liegeplätze", "%s"),
                            ("vhf", "UKW-Kanal", "%s"),
                            ("phone", "Telefon", "%s"),
                            ("facilities", "Ausstattung", "%s"),
                            ("rating", "Bewertung", "%s/5")):
        if r.get(key) not in (None, ""):
            info.append((label, fmt % r[key]))
    if r.get("website"):
        info.append(("Website", "<a href='%s'>%s</a>" % (
            e(r["website"]), e(r["website"]))))
    out.append("<table>%s</table>" % "".join(
        "<tr><th>%s</th><td>%s</td></tr>" % (k, v if k == "Website" else
                                             e(str(v))) for k, v in info))
    if r["notes"]:
        out.append("<p>%s</p>" % e(r["notes"]).replace("\n", "<br>"))
    pics = hb.photos(store, r["id"])
    if pics:
        os.makedirs(img_dir, exist_ok=True)
        out.append("<div class=photos>")
        for pid, path, caption in pics:
            if not os.path.isfile(path):
                continue
            name = "%d_%s" % (pid, os.path.basename(path))
            shutil.copy2(path, os.path.join(img_dir, name))
            out.append("<a href='guide_photos/%s'><img src='guide_photos/%s'"
                       " alt='%s' title='%s'></a>" % (
                           e(name), e(name), e(caption), e(caption)))
        out.append("</div>")
    wx = weather.series(store, r["lon"], r["lat"])
    if wx:
        out.append("<h3>Wetter</h3><table class=wx><tr><th>Zeit</th>"
                   "<th>Wind kn</th><th>Richtung</th><th>Böen kn</th>"
                   "<th>Welle m</th></tr>")
        for w in wx:
            warn = " class=warn" if (w["gust_kn"] or 0) >= 25 else ""
            out.append("<tr%s><td>%s</td><td>%.0f</td><td>%s %03.0f°</td>"
                       "<td>%.0f</td><td>%s</td></tr>" % (
                           warn, _fmt_time(w["time"]), w["wind_kn"] or 0,
                           _arrow(w["wind_dir"]), w["wind_dir"] or 0,
                           w["gust_kn"] or 0,
                           "%.1f" % w["wave_m"] if w["wave_m"] is not None
                           else "–"))
        out.append("</table>")
    near = poi.near(store, r["lon"], r["lat"], radius)
    if near:
        out.append("<h3>In der Nähe</h3><table>")
        for dist, p in near[:60]:
            t = p["tags"]
            extra = " · ".join(x for x in (
                t.get("opening_hours"), t.get("phone", t.get(
                    "contact:phone")), t.get("cuisine")) if x)
            out.append("<tr><td>%s</td><td>%s</td><td>%d m</td><td><small>"
                       "%s</small></td></tr>" % (
                           CAT_DE.get(p["category"], p["category"]),
                           e(p["name"] or "–"), dist, e(extra)))
        out.append("</table>")
    out.append("</section>")
    return "".join(out)
