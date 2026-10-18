"""Marinas and anchorages, maintained by the user.

The list is seeded from OpenSeaMap/OSM (positions and names only) and can
then be edited freely: notes, depth, berths, VHF channel, facilities, rating
and own photos.  Entries edited by the user are never overwritten by a new
seed.  Photos are copied next to the database into ``<db>_media/``.
"""

import csv
import datetime
import math
import os
import shutil
import xml.etree.ElementTree as ET

from shapely.geometry import LineString, Point, Polygon

KINDS = ("marina", "harbour", "anchorage", "buoy")
FIELDS = ("kind", "name", "lon", "lat", "notes", "depth", "berths", "vhf",
          "phone", "website", "facilities", "rating")

# OSM tags that describe what a marina offers.
FACILITY_TAGS = {
    "fuel": ("fuel", "seamark:small_craft_facility:category=fuel_station"),
    "water": ("drinking_water", "water_point"),
    "electricity": ("power_supply",),
    "showers": ("shower", "showers"),
    "toilets": ("toilets",),
    "pump-out": ("sanitary_dump_station",),
    "slipway": ("slipway",),
    "crane": ("crane", "boat_hoist"),
}


def now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat(
        timespec="seconds")


def distance_m(lon1, lat1, lon2, lat2):
    k = math.cos(math.radians((lat1 + lat2) / 2))
    return math.hypot((lon2 - lon1) * k, lat2 - lat1) * 111320.0


def media_dir(store):
    base = os.path.splitext(os.path.abspath(store.path))[0]
    return base + "_media"


def _kind_of(tags):
    stype = tags.get("seamark:type")
    if stype in ("anchorage", "anchor_berth"):
        return "anchorage"
    if stype == "mooring" and "buoy" in tags.get(
            "seamark:mooring:category", ""):
        return None                       # single mooring buoys: too many
    if tags.get("leisure") == "marina":
        return "marina"
    if stype == "harbour":
        cat = tags.get("seamark:harbour:category", "")
        return "marina" if ("marina" in cat or "yacht" in cat) else "harbour"
    return None


def _facilities(tags):
    found = []
    for name, keys in FACILITY_TAGS.items():
        for key in keys:
            k, _, v = key.partition("=")
            yes = tags.get(k) in ("yes", "true", "1")
            if (v and v in tags.get(k, "")) or (not v and yes):
                found.append(name)
                break
    return ",".join(found)


def _name(tags, kind):
    for key in ("seamark:name", "name", "name:de", "name:en"):
        if tags.get(key):
            return tags[key]
    return {"anchorage": "Ankerplatz", "marina": "Marina"}.get(kind, "Hafen")


def seed_from_osm(store, bounds):
    """Create harbour entries from the offline OSM data.  Returns the
    number of new entries."""
    db = store.db
    new = 0
    for osm_id, tags, geom in store.features(bounds):
        kind = _kind_of(tags)
        if kind is None:
            continue
        if isinstance(geom, LineString) and geom.is_closed and \
                len(geom.coords) >= 4:
            geom = Polygon(geom.coords)       # closed way = harbour area
        pt = geom if isinstance(geom, Point) else geom.representative_point()
        row = db.execute("SELECT id, edited FROM harbours WHERE osm_id=?",
                         (osm_id,)).fetchone()
        name = _name(tags, kind)
        if row:
            if not row[1]:
                db.execute("UPDATE harbours SET kind=?, name=?, lon=?, lat=?"
                           " WHERE id=?", (kind, name, pt.x, pt.y, row[0]))
            continue
        # the same marina is often tagged twice (leisure=marina + seamark)
        dup = [r for r in db.execute(
            "SELECT id, lon, lat FROM harbours WHERE kind=? AND lat BETWEEN ?"
            " AND ?", (kind, pt.y - 0.005, pt.y + 0.005))
            if distance_m(r[1], r[2], pt.x, pt.y) < 300]
        if dup:
            continue
        db.execute(
            "INSERT INTO harbours (kind, name, lon, lat, osm_id, source,"
            " phone, website, facilities, berths, updated) VALUES"
            " (?,?,?,?,?,'osm',?,?,?,?,?)",
            (kind, name, pt.x, pt.y, osm_id,
             tags.get("phone", tags.get("contact:phone", "")),
             tags.get("website", tags.get("contact:website", "")),
             _facilities(tags), _int(tags.get("capacity")), now()))
        new += 1
    db.commit()
    return new


def _int(value):
    try:
        return int(str(value).split(";")[0])
    except (TypeError, ValueError):
        return None


def _float(value):
    try:
        return float(str(value).replace(",", "."))
    except (TypeError, ValueError):
        return None


def _clean(values):
    out = {}
    for key, value in values.items():
        if value is None or key not in FIELDS:
            continue
        if key in ("lon", "lat", "depth"):
            value = _float(value)
        elif key in ("berths", "rating"):
            value = _int(value)
        elif key == "kind" and value not in KINDS:
            raise ValueError("kind must be one of %s" % ", ".join(KINDS))
        else:
            value = str(value)
        out[key] = value
    return out


def add(store, **values):
    v = _clean(values)
    for key in ("kind", "name", "lon", "lat"):
        if v.get(key) is None:
            raise ValueError("%s is required" % key)
    cols = list(v) + ["source", "edited", "updated"]
    cur = store.db.execute(
        "INSERT INTO harbours (%s) VALUES (%s)" % (
            ",".join(cols), ",".join("?" * len(cols))),
        list(v.values()) + ["user", 1, now()])
    store.db.commit()
    return cur.lastrowid


def edit(store, hid, **values):
    v = _clean(values)
    if not v:
        return
    sets = ",".join("%s=?" % k for k in v)
    cur = store.db.execute(
        "UPDATE harbours SET %s, edited=1, updated=? WHERE id=?" % sets,
        list(v.values()) + [now(), hid])
    if cur.rowcount == 0:
        raise KeyError("no harbour with id %d" % hid)
    store.db.commit()


def remove(store, hid):
    for (path,) in store.db.execute(
            "SELECT path FROM photos WHERE harbour_id=?", (hid,)):
        full = os.path.join(os.path.dirname(os.path.abspath(store.path)),
                            path)
        if os.path.isfile(full):
            os.remove(full)
    store.db.execute("DELETE FROM photos WHERE harbour_id=?", (hid,))
    store.db.execute("DELETE FROM harbours WHERE id=?", (hid,))
    store.db.commit()


def add_photo(store, hid, path, caption=""):
    if not store.db.execute("SELECT 1 FROM harbours WHERE id=?",
                            (hid,)).fetchone():
        raise KeyError("no harbour with id %d" % hid)
    folder = os.path.join(media_dir(store), str(hid))
    os.makedirs(folder, exist_ok=True)
    name = os.path.basename(path)
    target = os.path.join(folder, name)
    stem, ext = os.path.splitext(name)
    k = 1
    while os.path.exists(target):
        target = os.path.join(folder, "%s_%d%s" % (stem, k, ext))
        k += 1
    shutil.copy2(path, target)
    rel = os.path.relpath(target, os.path.dirname(os.path.abspath(
        store.path))).replace(os.sep, "/")
    cur = store.db.execute(
        "INSERT INTO photos (harbour_id, path, caption, added) VALUES"
        " (?,?,?,?)", (hid, rel, caption, now()))
    store.db.execute("UPDATE harbours SET edited=1, updated=? WHERE id=?",
                     (now(), hid))
    store.db.commit()
    return cur.lastrowid


def photos(store, hid):
    base = os.path.dirname(os.path.abspath(store.path))
    return [(pid, os.path.join(base, path), caption) for pid, path, caption
            in store.db.execute("SELECT id, path, caption FROM photos WHERE"
                                " harbour_id=? ORDER BY id", (hid,))]


def query(store, bounds=None, kind=None):
    sql = "SELECT * FROM harbours WHERE 1=1"
    args = []
    if bounds:
        sql += " AND lon BETWEEN ? AND ? AND lat BETWEEN ? AND ?"
        args += [bounds[0], bounds[2], bounds[1], bounds[3]]
    if kind:
        sql += " AND kind=?"
        args.append(kind)
    cur = store.db.execute(sql + " ORDER BY name", args)
    cols = [c[0] for c in cur.description]
    return [dict(zip(cols, row)) for row in cur]


def export_csv(store, path, bounds=None):
    rows = query(store, bounds)
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=("id",) + FIELDS)
        w.writeheader()
        for r in rows:
            w.writerow({k: r[k] for k in ("id",) + FIELDS})
    return len(rows)


def import_file(store, path):
    """Import a CSV (as written by export_csv; rows with an id update that
    entry) or GPX waypoints (``sym`` containing 'anchor' -> anchorage)."""
    n = 0
    if path.lower().endswith(".gpx"):
        root = ET.parse(path).getroot()
        ns = root.tag[1:].split("}")[0] if root.tag.startswith("{") else ""
        q = (lambda t: "{%s}%s" % (ns, t)) if ns else (lambda t: t)
        for wpt in root.iter(q("wpt")):
            def text(tag):
                el = wpt.find(q(tag))
                return el.text.strip() if el is not None and el.text else ""
            sym = text("sym").lower()
            add(store, kind="anchorage" if "anchor" in sym else "marina",
                name=text("name") or "Wegpunkt", lon=wpt.get("lon"),
                lat=wpt.get("lat"), notes=text("desc") or text("cmt"))
            n += 1
        return n
    with open(path, newline="", encoding="utf-8-sig") as f:
        for row in csv.DictReader(f):
            row = {k: (v if v != "" else None) for k, v in row.items()}
            hid = _int(row.pop("id", None))
            if hid and store.db.execute("SELECT 1 FROM harbours WHERE id=?",
                                        (hid,)).fetchone():
                edit(store, hid, **row)
            else:
                add(store, **row)
            n += 1
    return n
