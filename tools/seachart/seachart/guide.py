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


# Same look as the OpenCPN Qt6 frontend (qt/src/ui/Theme.cpp): dark glass
# cards at night, light cards by day (follows the system setting).
CSS = """
:root{--bg:#0a0f15;--panel:rgba(20,23,28,.94);--border:rgba(255,255,255,.07);
--hover:rgba(255,255,255,.06);--text:#f3f5f8;--dim:#9aa3ae;--faint:#5b636d;
--accent:#3e6ae1;--accent2:#5d8bff;--warn:#ffb020;--shadow:rgba(0,0,0,.35)}
@media (prefers-color-scheme:light){:root{--bg:#d9e4ee;--panel:#fff;
--border:rgba(0,0,0,.06);--hover:rgba(0,0,0,.04);--text:#14171c;
--dim:#5b6470;--faint:#9aa1aa;--accent2:#2f5bd3;--warn:#e08a00;
--shadow:rgba(20,30,50,.12)}}
*{box-sizing:border-box}
body{font:14px Inter,"SF Pro Display","Segoe UI Variable","Segoe UI",
"Noto Sans",Roboto,"Helvetica Neue",Arial,sans-serif;margin:0;
background:var(--bg);color:var(--text)}
header{max-width:1000px;margin:auto;padding:28px 16px 4px}
h1{font-size:30px;font-weight:300;margin:0 0 4px}
header small,.dim{color:var(--dim);font-size:13px}
main{max-width:1000px;margin:auto;padding:4px 16px 32px}
nav{display:flex;flex-wrap:wrap;gap:6px;margin:12px 0}
nav a{background:var(--hover);color:var(--text);text-decoration:none;
padding:8px 14px;border-radius:12px;font-weight:600;font-size:13px}
nav a:hover{background:var(--accent);color:#fff}
section{background:var(--panel);border:1px solid var(--border);
border-radius:22px;padding:18px 22px;margin:14px 0;
box-shadow:0 10px 28px var(--shadow)}
h2{margin:0 0 10px;font-size:22px;font-weight:600}
h3{color:var(--faint);font-size:11px;font-weight:600;letter-spacing:1px;
text-transform:uppercase;margin:18px 0 6px}
.kind{font-size:12px;font-weight:600;color:var(--accent2);margin-left:8px;
text-transform:uppercase;letter-spacing:1px}
table{border-collapse:collapse;width:100%;font-size:13px}
td,th{padding:6px 8px;text-align:left;border-bottom:1px solid var(--border)}
th{color:var(--dim);font-weight:500}
a{color:var(--accent2)}
.photos{display:flex;gap:10px;overflow-x:auto;padding:4px 0}
.photos img{height:150px;border-radius:14px;display:block}
.wx td{text-align:center}.warn,.warn td{color:var(--warn)}
p{line-height:1.5}
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
             "<header><h1>Hafenführer %s</h1><small>%d Häfen und "
             "Ankerplätze · Wetter: %s · Nicht zur Navigation</small>"
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
