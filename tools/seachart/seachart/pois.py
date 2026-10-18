"""Points of interest near harbours from OpenStreetMap (restaurants,
supermarkets, fuel, chandlers, services, sights)."""

import json
import time

from .sources import OVERPASS_URLS, http_get, log

# category -> Overpass filters
CATEGORIES = {
    "restaurant": ['amenity~"^(restaurant|cafe|fast_food|bar|pub|'
                   'biergarten|ice_cream)$"'],
    "supermarket": ['shop~"^(supermarket|convenience|bakery|butcher|'
                    'greengrocer|deli|beverages|kiosk)$"'],
    "fuel": ['amenity="fuel"', 'waterway="fuel"'],
    "chandlery": ['shop~"^(boat|ship_chandler|outdoor|fishing)$"'],
    "service": ['amenity~"^(pharmacy|doctors|hospital|atm|bank|post_office|'
                'toilets|shower|drinking_water|recycling|bicycle_rental|'
                'car_rental|taxi)$"', 'shop~"^(laundry|dry_cleaning)$"'],
    "transport": ['amenity~"^(bus_station|ferry_terminal)$"',
                  'railway="station"'],
    "sight": ['tourism~"^(attraction|museum|viewpoint|information)$"',
              'historic~"^(castle|monument|lighthouse|ship)$"'],
}

KEEP_TAGS = ("opening_hours", "phone", "contact:phone", "website",
             "contact:website", "cuisine", "addr:street",
             "addr:housenumber", "addr:city", "amenity", "shop", "tourism",
             "historic", "brand", "wheelchair")


def category_of(tags):
    for cat, filters in CATEGORIES.items():
        for flt in filters:
            if "~" in flt:
                key, _, rx = flt.partition("~")
                vals = rx.strip('"^$()').split("|")
                if tags.get(key) in vals:
                    return cat
            else:
                key, _, val = flt.partition("=")
                if tags.get(key) == val.strip('"'):
                    return cat
    return None


def build_query(points, radius):
    parts = []
    for lon, lat in points:
        for filters in CATEGORIES.values():
            for flt in filters:
                parts.append('nwr[%s](around:%d,%.6f,%.6f);'
                             % (flt, radius, lat, lon))
    return "[out:json][timeout:180];(%s);out center tags;" % "".join(parts)


def download(store, harbours, radius=1500, urls=None, batch=15):
    """Fetch POIs within ``radius`` m around the given harbours."""
    urls = urls or OVERPASS_URLS
    pts = [(h["lon"], h["lat"]) for h in harbours]
    total = 0
    for k in range(0, len(pts), batch):
        chunk = pts[k:k + batch]
        log("  POIs around harbours %d-%d of %d" % (
            k + 1, k + len(chunk), len(pts)))
        data, last = None, None
        for url in urls:
            try:
                data = json.loads(http_get(
                    url, {"data": build_query(chunk, radius)}, retries=2))
                break
            except Exception as exc:
                last = exc
                log("    %s failed: %s" % (url, exc))
        if data is None:
            raise RuntimeError("POI download failed: %s" % last)
        for el in data.get("elements", []):
            tags = el.get("tags") or {}
            cat = category_of(tags)
            if cat is None:
                continue
            lat = el.get("lat", el.get("center", {}).get("lat"))
            lon = el.get("lon", el.get("center", {}).get("lon"))
            if lat is None:
                continue
            store.db.execute(
                "INSERT OR REPLACE INTO pois (osm_id, category, name, lon,"
                " lat, tags) VALUES (?,?,?,?,?,?)",
                ("%s/%d" % (el["type"], el["id"]), cat,
                 tags.get("name", tags.get("brand", "")), lon, lat,
                 json.dumps({t: tags[t] for t in KEEP_TAGS if t in tags},
                            ensure_ascii=False)))
            total += 1
        store.db.commit()
        time.sleep(1)
    return total


def near(store, lon, lat, radius=1500):
    """POIs around a position, nearest first: [(dist_m, row dict)]."""
    from .harbours import distance_m
    d = radius / 111320.0
    cur = store.db.execute(
        "SELECT id, category, name, lon, lat, tags FROM pois WHERE lat"
        " BETWEEN ? AND ? AND lon BETWEEN ? AND ?",
        (lat - d, lat + d, lon - d * 2.5, lon + d * 2.5))
    out = []
    for pid, cat, name, plon, plat, tags in cur:
        dist = distance_m(lon, lat, plon, plat)
        if dist <= radius:
            out.append((dist, {"id": pid, "category": cat, "name": name,
                               "lon": plon, "lat": plat,
                               "tags": json.loads(tags)}))
    out.sort(key=lambda t: t[0])
    return out
