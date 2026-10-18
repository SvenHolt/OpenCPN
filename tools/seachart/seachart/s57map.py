"""Translate OpenSeaMap / OSM tags into S-57 objects.

OpenSeaMap's ``seamark:*`` tagging was designed after S-57, so most types
and attributes have a direct counterpart.  Values are resolved with explicit
tables for the attributes that drive S-52 symbology (buoy shapes, light
characters, ...) and with a fuzzy match against the S-57 expected-input
catalogue for everything else.
"""

import re

from shapely.geometry import LineString, Point, Polygon, MultiPolygon

TYPE_MAP = {
    "anchorage": "ACHARE", "anchor_berth": "ACHBRT", "beacon_cardinal":
    "BCNCAR", "beacon_isolated_danger": "BCNISD", "beacon_lateral": "BCNLAT",
    "beacon_safe_water": "BCNSAW", "beacon_special_purpose": "BCNSPP",
    "berth": "BERTHS", "bridge": "BRIDGE", "building": "BUISGL",
    "buoy_cardinal": "BOYCAR", "buoy_installation": "BOYINB",
    "buoy_isolated_danger": "BOYISD", "buoy_lateral": "BOYLAT",
    "buoy_safe_water": "BOYSAW", "buoy_special_purpose": "BOYSPP",
    "cable_area": "CBLARE", "cable_overhead": "CBLOHD",
    "cable_submarine": "CBLSUB", "canal": "CANALS", "causeway": "CAUSWY",
    "checkpoint": "CHKPNT", "coastguard_station": "CGUSTA",
    "control_point": "CTRPNT", "crane": "CRANES", "notice": "notmrk",
    "dam": "DAMCON", "daymark": "DAYMAR",
    "distance_mark": "DISMAR", "dredged_area": "DRGARE", "dry_dock": "DRYDOC",
    "dumping_ground": "DMPGRD", "dyke": "DYKCON", "fairway": "FAIRWY",
    "ferry_route": "FERYRT", "fishing_facility": "FSHFAC",
    "fishing_ground": "FSHGRD", "floating_dock": "FLODOC",
    "fog_signal": "FOGSIG", "fortified_structure": "FORSTC", "gate": "GATCON",
    "gridiron": "GRIDRN", "harbour": "HRBFAC", "harbour_area": "HRBARE",
    "hulk": "HULKES", "inshore_traffic_zone": "ISTZNE", "landmark": "LNDMRK",
    "light": "LIGHTS", "light_major": "LIGHTS", "light_minor": "LIGHTS",
    "light_float": "LITFLT", "light_vessel": "LITVES", "lock_basin":
    "LOKBSN", "marine_farm": "MARCUL", "military_area": "MIPARE",
    "mooring": "MORFAC", "navigation_line": "NAVLNE", "obstruction":
    "OBSTRN", "offshore_platform": "OFSPLF", "pile": "PILPNT",
    "pilot_boarding": "PILBOP", "pipeline_area": "PIPARE",
    "pipeline_overhead": "PIPOHD", "pipeline_submarine": "PIPSOL",
    "platform": "OFSPLF", "pontoon": "PONTON", "precautionary_area":
    "PRCARE", "production_area": "PRDARE", "radar_reflector": "RADRFL",
    "radar_station": "RADSTA", "radar_transponder": "RTPBCN",
    "radio_station": "RDOSTA", "recommended_track": "RECTRC",
    "rescue_station": "RSCSTA", "restricted_area": "RESARE", "rock":
    "UWTROC", "sea_area": "SEAARE", "seabed_area": "SBDARE",
    "seaplane_landing_area": "SPLARE", "separation_boundary": "TSSBND",
    "separation_crossing": "TSSCRS", "separation_lane": "TSSLPT",
    "separation_line": "TSELNE", "separation_roundabout": "TSSRON",
    "separation_zone": "TSEZNE", "shoreline_construction": "SLCONS",
    "signal_station_traffic": "SISTAT", "signal_station_warning": "SISTAW",
    "small_craft_facility": "SMCFAC", "spring": "SPRING", "tank": "SILTNK",
    "silo": "SILTNK", "topmark": "TOPMAR", "turning_basin": "TURNBS",
    "two-way_route": "TWRTPT", "vehicle_transfer": "VEHTRF",
    "waterway_gauge": "WATTUR", "weed": "WEDKLP", "wreck": "WRECKS",
}

# OpenSeaMap attribute key -> S-57 attribute acronym
KEY_MAP = {
    "name": "OBJNAM", "colour": "COLOUR", "colour_pattern": "COLPAT",
    "system": "MARSYS", "construction": "NATCON", "conspicuity": "CONVIS",
    "reflectivity": "CONRAD", "height": "HEIGHT", "elevation": "ELEVAT",
    "status": "STATUS", "function": "FUNCTN", "water_level": "WATLEV",
    "depth": "VALSOU", "minimum_depth": "DRVAL1", "maximum_depth": "DRVAL2",
    "clearance_height": "VERCLR", "clearance_width": "HORCLR",
    "clearance_height_closed": "VERCCL", "clearance_height_open": "VERCOP",
    "character": "LITCHR", "period": "SIGPER", "group": "SIGGRP",
    "sequence": "SIGSEQ", "range": "VALNMR", "sector_start": "SECTR1",
    "sector_end": "SECTR2", "orientation": "ORIENT", "visibility": "LITVIS",
    "exhibition": "EXCLIT", "radius": "RADIUS", "information": "INFORM",
    "surface": "NATSUR", "quality": "NATQUA", "channel": "COMCHA",
    "length": "HORLEN", "width": "HORWID", "restriction": "RESTRN",
    "product": "PRODCT", "multiple": "MLTYLT", "vertical_length": "VERLEN",
    "nationality": "NATION", "radar_conspicuity": "CONRAD",
    "topmark": None,
}

CATEGORY_ATTR = {
    "BOYLAT": "CATLAM", "BCNLAT": "CATLAM", "BOYCAR": "CATCAM",
    "BCNCAR": "CATCAM", "BOYSPP": "CATSPM", "BCNSPP": "CATSPM",
    "HRBFAC": "CATHAF", "SMCFAC": "CATSCF", "MORFAC": "CATMOR",
    "LNDMRK": "CATLMK", "WRECKS": "CATWRK", "OBSTRN": "CATOBS",
    "ACHARE": "CATACH", "ACHBRT": "CATACH", "RESARE": "CATREA",
    "LIGHTS": "CATLIT", "CBLSUB": "CATCBL", "CBLOHD": "CATCBL",
    "PIPSOL": "CATPIP", "PIPARE": "CATPIP", "OFSPLF": "CATOFP",
    "PILPNT": "CATPLE", "BRIDGE": "CATBRG", "SISTAT": "CATSIT",
    "SISTAW": "CATSIW", "RSCSTA": "CATRSC", "RDOSTA": "CATROS",
    "RTPBCN": "CATRTB", "FOGSIG": "CATFOG", "MARCUL": "CATMFA",
    "MIPARE": "CATMPA", "PRDARE": "CATPRA", "SEAARE": "CATSEA",
    "SLCONS": "CATSLC", "PILBOP": "CATPIL", "NAVLNE": "CATNAV",
    "RECTRC": "CATTRK", "GATCON": "CATGAT", "DAMCON": "CATDAM",
    "FSHFAC": "CATFIF", "FERYRT": "CATFRY", "DISMAR": "CATDIS",
    "BOYINB": "CATINB", "HRBARE": None, "SILTNK": "CATSIL",
    "BERTHS": None, "BUISGL": "FUNCTN", "DMPGRD": "CATDPG",
    "TSSLPT": None, "LITFLT": None, "DAYMAR": None, "CRANES": "CATCRN",
    "notmrk": "catnmk",
}

SHAPE_ATTR = {"BOY": "BOYSHP", "BCN": "BCNSHP", "TOPMAR": "TOPSHP",
              "DAYMAR": "TOPSHP", "BUISGL": "BUISHP", "MORFAC": "BOYSHP",
              "LITFLT": "BOYSHP", "LITVES": "BOYSHP", "SILTNK": "BUISHP"}

# Explicit value tables where OpenSeaMap wording differs from the S-57
# expected-input meanings.  Keys are normalised (lowercase, '_' -> ' ').
VALUES = {
    "CATLAM": {"port": 1, "starboard": 2, "preferred channel starboard": 3,
               "preferred channel port": 4, "waterway right": 5,
               "waterway left": 6, "channel right": 7, "channel left": 8},
    "COLPAT": {"horizontal": 1, "vertical": 2, "diagonal": 3, "squared": 4,
               "stripes": 5, "border": 6},
    "BCNSHP": {"stake": 1, "pole": 1, "perch": 1, "post": 1, "withy": 2,
               "tower": 3, "lattice": 4, "pile": 5, "cairn": 6,
               "buoyant": 7},
    "BOYSHP": {"conical": 1, "nun": 1, "can": 2, "cylindrical": 2,
               "spherical": 3, "pillar": 4, "spar": 5, "barrel": 6,
               "super buoy": 7, "ice buoy": 8},
    "TOPSHP": {"cone point up": 1, "cone point down": 2, "sphere": 3,
               "2 spheres": 4, "cylinder": 5, "board": 6, "x shape": 7,
               "cross": 8, "cube point up": 9, "2 cones point together": 10,
               "2 cones base together": 11, "rhombus": 12, "2 cones up": 13,
               "2 cones down": 14, "besom point up": 15,
               "besom point down": 16, "flag": 17,
               "sphere over rhombus": 18, "square": 19,
               "rectangle horizontal": 20, "rectangle vertical": 21,
               "trapezium up": 22, "trapezium down": 23,
               "triangle point up": 24, "triangle point down": 25,
               "circle": 26, "2 upright crosses": 27, "t shape": 28,
               "triangle point up over circle": 29,
               "upright cross over circle": 30, "rhombus over circle": 31,
               "circle over triangle point up": 32, "other": 33},
    "LITCHR": {"f": 1, "fl": 2, "lfl": 3, "q": 4, "vq": 5, "uq": 6,
               "iso": 7, "oc": 8, "iq": 9, "ivq": 10, "iuq": 11, "mo": 12,
               "ffl": 13, "fllfl": 14, "ocfl": 15, "flfl": 16, "al oc": 17,
               "al lfl": 18, "al fl": 19, "al gr": 20, "q lfl": 25,
               "vq lfl": 26, "uq lfl": 27, "al": 28, "al ffl": 29},
    "WATLEV": {"partly submerged": 1, "dry": 2, "submerged": 3,
               "covers": 4, "awash": 5, "floods": 6, "floating": 7},
    "CATWRK": {"non dangerous": 1, "dangerous": 2,
               "distributed remains": 3, "mast showing": 4,
               "hull showing": 5},
    "CATHAF": {"roro": 1, "ferry": 3, "fishing": 4, "marina": 5,
               "yacht": 5, "naval": 6, "tanker": 7, "passenger": 8,
               "shipyard": 9, "container": 10, "bulk": 11,
               "marina no facilities": 5},
    "CATSCF": {"visitor berth": 1, "nautical club": 2, "boat hoist": 3,
               "sailmaker": 4, "boatyard": 5, "public inn": 6,
               "restaurant": 7, "chandler": 8, "provisions": 9,
               "doctor": 10, "pharmacy": 11, "water tap": 12,
               "fuel station": 13, "electricity": 14, "bottle gas": 15,
               "showers": 16, "laundrette": 17, "toilets": 18,
               "post box": 19, "telephone": 20, "refuse bin": 21,
               "car park": 22, "boats trailers park": 23,
               "caravan site": 24, "camping site": 25, "pump out": 26,
               "emergency telephone": 27, "slipway": 28,
               "visitors mooring": 29, "scrubbing berth": 30,
               "picnic area": 31, "mechanics workshop": 32,
               "security service": 33},
    "CATMOR": {"dolphin": 1, "deviation dolphin": 2, "bollard": 3,
               "wall": 4, "pile": 5, "post": 5, "chain": 6, "buoy": 7},
    "CATSPM": {"no anchoring": 20, "no berthing": 21, "no overtaking": 22,
               "no two way traffic": 23, "warning": 27, "tss": 19,
               "no entry": 50, "unknown purpose": 52, "odas": 9,
               "lanby": 15, "gps": 48},
    "MARSYS": {"iala a": 1, "iala b": 2, "none": 9, "other": 10},
    "CONVIS": {"conspicuous": 1, "not conspicuous": 2},
    "CONRAD": {"conspicuous": 1, "not conspicuous": 2, "reflector": 3},
    "NATCON": {"masonry": 1, "concreted": 2, "loose boulders": 3,
               "hard surfaced": 4, "unsurfaced": 5, "wooden": 6,
               "metal": 7, "grp": 8, "painted": 9},
    "EXCLIT": {"24h": 1, "day": 2, "fog": 3, "night": 4},
    "LITVIS": {"high": 1, "low": 2, "faint": 3, "intensified": 4,
               "unintensified": 5, "restricted": 6, "obscured": 7,
               "part obscured": 8},
    "CATLIT": {"directional": 1, "leading": 4, "aero": 5,
               "air obstruction": 6, "fog detector": 7, "floodlight": 8,
               "strip light": 9, "subsidiary": 10, "spotlight": 11,
               "front": 12, "rear": 13, "lower": 14, "upper": 15,
               "moire": 16, "emergency": 17, "bearing": 18,
               "horizontal": 19, "vertical": 20},
    "CATACH": {"unrestricted": 1, "deep water": 2, "tanker": 3,
               "explosives": 4, "quarantine": 5, "seaplane": 6,
               "small craft": 7, "small craft mooring": 8,
               "24 hour": 9},
    "CATOBS": {"stump": 1, "snag": 1},
}

# Objects that are derived from sub-keys of another seamark
# (e.g. the topmark on a buoy).  S-57 models them as separate objects
# sharing the position of the master object.
SLAVES = {"topmark": "TOPMAR", "daymark": "DAYMAR", "fog_signal": "FOGSIG",
          "radar_transponder": "RTPBCN", "radar_reflector": "RADRFL",
          "radio_station": "RDOSTA"}

PLACE_TYPES = {"city", "town", "village"}
PIER_TYPES = {"pier": 4, "breakwater": 1, "groyne": 2}

_NUM = re.compile(r"[-+]?\d+(?:[.,]\d+)?")


def _norm(value):
    return re.sub(r"[^a-z0-9]+", " ", value.lower()).strip()


class Mapper:
    def __init__(self, catalog):
        self.cat = catalog
        self.unknown_types = {}

    # -- values -------------------------------------------------------
    def _enum(self, acr, value):
        value = value.strip()
        if value.isdigit():
            return int(value)
        n = _norm(value)
        table = VALUES.get(acr, {})
        if n in table:
            return table[n]
        if acr == "LITCHR":
            return table.get(n.replace(" ", ""))
        return self.cat.match_enum(acr, value)

    def convert(self, acr, value):
        """Convert an OSM value to the S-57 encoding of attribute ``acr``."""
        kind = self.cat.atype.get(acr)
        if kind is None:
            return None
        if kind == "E":
            return self._enum(acr, value.split(";")[0])
        if kind == "L":
            items = [self._enum(acr, v) for v in value.split(";")]
            items = [str(i) for i in items if i is not None]
            return ",".join(items) or None
        if kind in ("F", "I"):
            m = _NUM.search(value)
            if not m:
                return None
            num = float(m.group(0).replace(",", "."))
            return int(num) if kind == "I" else num
        if acr == "SIGGRP":
            value = value.strip()
            if not value.startswith("("):
                value = "(" + value + ")"
        return value

    def _attrs(self, objl, keys):
        """keys: {osm_subkey: value} -> {S-57 acronym: encoded value}"""
        attrs = {}
        for key, value in keys.items():
            if key == "category":
                acr = CATEGORY_ATTR.get(objl)
            elif key == "shape":
                acr = SHAPE_ATTR.get(objl) or SHAPE_ATTR.get(objl[:3])
            else:
                acr = KEY_MAP.get(key)
            if not acr or not value.strip():
                continue
            allowed = self.cat.allowed.get(objl)
            if allowed and acr not in allowed:
                continue
            enc = self.convert(acr, value)
            if enc is not None:
                attrs[acr] = enc
        return attrs

    # -- geometry -----------------------------------------------------
    def _fit(self, objl, geom, area_hint):
        """Choose a geometry the object class accepts."""
        prims = self.cat.prims.get(objl, {"Point", "Line", "Area"})
        if isinstance(geom, Point):
            return geom if "Point" in prims else None
        if isinstance(geom, (Polygon, MultiPolygon)):
            if "Area" in prims:
                return geom
            if "Line" in prims:
                return geom.boundary
            return geom.representative_point()
        if isinstance(geom, LineString):
            closed = geom.is_closed and len(geom.coords) >= 4
            if closed and "Area" in prims and (area_hint or
                                               "Line" not in prims):
                poly = Polygon(geom.coords)
                if poly.is_valid and poly.area > 0:
                    return poly
            if "Line" in prims:
                return geom
            if "Area" in prims and closed:
                return Polygon(geom.coords).buffer(0)
            return geom.interpolate(0.5, normalized=True)
        return None

    # -- main entry -----------------------------------------------------
    def map(self, tags, geom):
        """Return a list of (objl, attrs, geometry) for one OSM object."""
        out = []
        stype = tags.get("seamark:type")
        area_hint = tags.get("area") == "yes"
        if stype:
            out += self._map_seamark(stype, tags, geom, area_hint)
        elif tags.get("natural") == "coastline":
            g = self._fit("COALNE", geom, False)
            if g is not None:
                out.append(("COALNE", {}, g))
        elif tags.get("leisure") == "marina":
            out.append(("HRBFAC", {"CATHAF": "5",
                                   "OBJNAM": tags.get("name")},
                        self._fit("HRBFAC", geom, True)))
        elif tags.get("man_made") in PIER_TYPES:
            out.append(("SLCONS",
                        {"CATSLC": PIER_TYPES[tags["man_made"]],
                         "OBJNAM": tags.get("name")},
                        self._fit("SLCONS", geom, area_hint)))
        elif tags.get("place") in PLACE_TYPES and tags.get("name"):
            out.append(("BUAARE", {"OBJNAM": tags["name"]},
                        self._fit("BUAARE", geom, True)))
        return [(o, {k: v for k, v in a.items() if v is not None}, g)
                for o, a, g in out if g is not None and not g.is_empty]

    def _subkeys(self, tags, prefix):
        plen = len(prefix)
        return {k[plen:]: v for k, v in tags.items()
                if k.startswith(prefix) and ":" not in k[plen:]}

    def _map_seamark(self, stype, tags, geom, area_hint):
        out = []
        objl = TYPE_MAP.get(stype)
        point = geom if isinstance(geom, Point) else None
        if objl is None:
            self.unknown_types[stype] = self.unknown_types.get(stype, 0) + 1
        elif objl != "LIGHTS":
            keys = self._subkeys(tags, "seamark:%s:" % stype)
            for key in ("name", "information", "status"):
                keys.setdefault(key, tags.get("seamark:" + key) or "")
            if not keys["name"]:
                keys["name"] = tags.get("name") or ""
            attrs = self._attrs(objl, keys)
            g = self._fit(objl, geom, area_hint)
            if g is not None:
                out.append((objl, attrs, g))
        # Lights (also on buoys, beacons, ...): one LIGHTS object per sector.
        lights = self._lights(stype, tags)
        if lights:
            pos = point or geom.representative_point()
            for attrs in lights:
                out.append(("LIGHTS", attrs, pos))
        # Topmarks, fog signals, ... attached to the master object.
        if point is not None:
            for key, slave in SLAVES.items():
                if key == stype:
                    continue
                keys = self._subkeys(tags, "seamark:%s:" % key)
                if keys or tags.get("seamark:" + key) == "yes":
                    attrs = self._attrs(slave, keys)
                    out.append((slave, attrs, point))
        return out

    def _lights(self, stype, tags):
        base = self._subkeys(tags, "seamark:light:")
        if stype.startswith("light"):
            for k, v in self._subkeys(tags, "seamark:%s:" % stype).items():
                base.setdefault(k, v)
        sectors = {}
        for k, v in tags.items():
            m = re.match(r"seamark:light:(\d+):([^:]+)$", k)
            if m:
                sectors.setdefault(int(m.group(1)), {})[m.group(2)] = v
        if not sectors and not base and not stype.startswith("light"):
            return []
        if not sectors:
            sectors = {0: {}}
        result = []
        for _, keys in sorted(sectors.items()):
            merged = dict(base)
            merged.update(keys)
            merged.pop("name", None)
            result.append(self._attrs("LIGHTS", merged))
        return result
