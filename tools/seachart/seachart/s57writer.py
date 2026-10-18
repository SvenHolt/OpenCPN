"""Minimal IHO S-57 (ENC, edition 3.1) cell writer.

Writes ISO 8211 encoded ``.000`` base cells with chain-node topology that
OpenCPN (and GDAL) can read.  Only the subset needed for generated charts is
implemented: isolated nodes (points and soundings), connected nodes, edges
and feature records with attributes.

GDAL's own S-57 driver cannot create geometry from non-S-57 sources, which is
why this small encoder exists.  The data descriptive record is identical to
the one GDAL writes.
"""

import datetime
import struct

UT = b"\x1f"   # unit terminator
FT = b"\x1e"   # field terminator

RCNM_DSID, RCNM_DSPM = 10, 20
RCNM_FE = 100
RCNM_VI, RCNM_VC, RCNM_VE = 110, 120, 130

PRIM_POINT, PRIM_LINE, PRIM_AREA = 1, 2, 3

# Features of the "skin of the earth" (S-57 group 1).
GROUP1 = {"DEPARE", "DRGARE", "FLODOC", "HULKES", "LNDARE", "PONTON",
          "UNSARE"}

# Keeps every ISO 8211 record well below the 99999 byte limit of its leader.
MAX_EDGE_POINTS = 1500
MAX_SOUNDINGS = 1500

# (tag, field controls, name, array descriptor, format controls)
_DDR_FIELDS = [
    ("0001", "0500;&   ", "ISO 8211 Record Identifier", None, "(b12)"),
    ("DSID", "1600;&   ", "Data set identification field",
     "RCNM!RCID!EXPP!INTU!DSNM!EDTN!UPDN!UADT!ISDT!STED!PRSP!PSDN!PRED!PROF!"
     "AGEN!COMT",
     "(b11,b14,b11,b11,A,A,A,A(8),A(8),R(4),b11,A,A,b11,b12,A)"),
    ("DSSI", "1600;&   ", "Data set structure information field",
     "DSTR!AALL!NALL!NOMR!NOCR!NOGR!NOLR!NOIN!NOCN!NOED!NOFA",
     "(b11,b11,b11,b14,b14,b14,b14,b14,b14,b14,b14)"),
    ("DSPM", "1600;&   ", "Data set parameter field",
     "RCNM!RCID!HDAT!VDAT!SDAT!CSCL!DUNI!HUNI!PUNI!COUN!COMF!SOMF!COMT",
     "(b11,b14,b11,b11,b11,b14,b11,b11,b11,b11,b14,b14,A)"),
    ("VRID", "1600;&   ", "Vector record identifier field",
     "RCNM!RCID!RVER!RUIN", "(b11,b14,b12,b11)"),
    ("VRPC", "1600;&   ", "Vector Record Pointer Control field",
     "VPUI!VPIX!NVPT", "(b11,b12,b12)"),
    ("VRPT", "2600;&   ", "Vector record pointer field",
     "*NAME!ORNT!USAG!TOPI!MASK", "(B(40),b11,b11,b11,b11)"),
    ("ATTV", "2600;&   ", "Vector record attribute field",
     "*ATTL!ATVL", "(b12,A)"),
    ("SGCC", "1600;&   ", "Coordinate Control Field",
     "CCUI!CCIX!CCNC", "(b11,b12,b12)"),
    ("SG2D", "2500;&   ", "2-D coordinate field",
     "*YCOO!XCOO", "(b24,b24)"),
    ("SG3D", "2500;&   ", "3-D coordinate (sounding array) field",
     "*YCOO!XCOO!VE3D", "(b24,b24,b24)"),
    ("FRID", "1600;&   ", "Feature record identifier field",
     "RCNM!RCID!PRIM!GRUP!OBJL!RVER!RUIN", "(b11,b14,b11,b11,b12,b12,b11)"),
    ("FOID", "1600;&   ", "Feature object identifier field",
     "AGEN!FIDN!FIDS", "(b12,b14,b12)"),
    ("ATTF", "2600;&   ", "Feature record attribute field",
     "*ATTL!ATVL", "(b12,A)"),
    ("NATF", "2600;&   ", "Feature record national attribute field",
     "*ATTL!ATVL", "(b12,A)"),
    ("FFPC", "1600;&   ",
     "Feature record to feature object pointer control field",
     "FFUI!FFIX!NFPT", "(b11,b12,b12)"),
    ("FFPT", "2600;&   ", "Feature record to feature object pointer field",
     "*LNAM!RIND!COMT", "(B(64),b11,A)"),
    ("FSPC", "1600;&   ",
     "Feature record to spatial record pointer control field",
     "FSUI!FSIX!NSPT", "(b11,b12,b12)"),
    ("FSPT", "2600;&   ", "Feature record to spatial record pointer field",
     "*NAME!ORNT!USAG!MASK", "(B(40),b11,b11,b11)"),
]

_FIELD_TREE = ("0001DSIDDSIDDSSI0001DSPM0001VRIDVRIDATTVVRIDVRPCVRIDVRPTVRID"
               "SGCCVRIDSG2DVRIDSG3D0001FRIDFRIDFOIDFRIDATTFFRIDNATFFRIDFFPC"
               "FRIDFFPTFRIDFSPCFRIDFSPT")


def _digits(n):
    return max(1, len(str(n)))


def _record(fields, leader_mid, ddr=False):
    """Assemble an ISO 8211 record from [(tag, bytes)]."""
    size_len = _digits(max(len(d) for _, d in fields))
    total = sum(len(d) for _, d in fields)
    size_pos = _digits(total)
    if not ddr:
        size_len, size_pos = max(size_len, 5), max(size_pos, 5)
    directory = b""
    pos = 0
    for tag, data in fields:
        directory += (tag.encode("ascii") + b"%0*d" % (size_len, len(data))
                      + b"%0*d" % (size_pos, pos))
        pos += len(data)
    directory += FT
    base = 24 + len(directory)
    length = base + total
    if length > 99999:
        raise ValueError("ISO 8211 record too large (%d bytes)" % length)
    leader = (b"%05d" % length + leader_mid + b"%05d" % base
              + (b" ! " if ddr else b"   ") + b"%d%d04" % (size_len, size_pos))
    return leader + directory + b"".join(d for _, d in fields)


def _ddr():
    fields = [("0000", b"0000;&   " + UT + _FIELD_TREE.encode() + FT)]
    for tag, ctrl, name, arr, fmt in _DDR_FIELDS:
        body = ctrl.encode() + name.encode() + UT
        if arr is not None:
            body += arr.encode() + UT
        fields.append((tag, body + fmt.encode() + FT))
    return _record(fields, b"3LE1 09", ddr=True)


def _u8(v):
    return struct.pack("<B", v)


def _u16(v):
    return struct.pack("<H", v)


def _u32(v):
    return struct.pack("<I", v)


def _i32(v):
    return struct.pack("<i", v)


def _name(rcnm, rcid):
    return _u8(rcnm) + _u32(rcid)


def _ring_area(coords):
    a = 0.0
    for (x1, y1), (x2, y2) in zip(coords, coords[1:]):
        a += x1 * y2 - x2 * y1
    return a / 2.0


class S57Cell:
    """Collects features and writes them as one S-57 base cell."""

    def __init__(self, catalog, name, scale, intu, agency=540, comment="",
                 issue_date=None, edition=1, comf=10000000, somf=10,
                 vdat=3, sdat=3):
        self.cat = catalog
        self.name = name
        self.scale = int(scale)
        self.intu = int(intu)
        self.agency = agency
        self.comment = comment
        self.issue = (issue_date or datetime.date.today()).strftime("%Y%m%d")
        self.edition = edition
        self.comf = comf
        self.somf = somf
        self.vdat, self.sdat = vdat, sdat
        self.isolated = []      # (rcid, x, y, z or None)
        self.connected = {}     # (x, y) -> rcid
        self.edges = []         # (rcid, start, end, [(x, y)])
        self.features = []      # (prim, objl acronym, attrs, fspt)
        self._next_vid = 1
        self.skipped = 0

    # -- primitives ---------------------------------------------------
    def _q(self, lon, lat):
        return (int(round(lon * self.comf)), int(round(lat * self.comf)))

    def _vid(self):
        v = self._next_vid
        self._next_vid += 1
        return v

    def _node(self, xy):
        rcid = self.connected.get(xy)
        if rcid is None:
            rcid = self.connected[xy] = self._vid()
        return rcid

    def _clean(self, coords):
        out = []
        for c in coords:
            q = self._q(c[0], c[1])
            if not out or q != out[-1]:
                out.append(q)
        return out

    def _chain(self, pts):
        """Split a quantised polyline into edges; return FSPT entries."""
        fspt = []
        i = 0
        last = len(pts) - 1
        while i < last:
            j = min(i + MAX_EDGE_POINTS, last)
            start, end = self._node(pts[i]), self._node(pts[j])
            rcid = self._vid()
            self.edges.append((rcid, start, end, pts[i + 1:j]))
            fspt.append((RCNM_VE, rcid, 1))
            i = j
        return fspt

    # -- features -----------------------------------------------------
    def add_point(self, objl, attrs, lon, lat):
        x, y = self._q(lon, lat)
        rcid = self._vid()
        self.isolated.append((rcid, x, y, None))
        self.features.append((PRIM_POINT, objl, attrs,
                              [(RCNM_VI, rcid, 255, 255)]))

    def add_soundings(self, points, attrs=None):
        """points: iterable of (lon, lat, depth_m) with depth positive down."""
        points = list(points)
        for k in range(0, len(points), MAX_SOUNDINGS):
            chunk = points[k:k + MAX_SOUNDINGS]
            rcid = self._vid()
            self.isolated.append((rcid, None, None, [
                self._q(lon, lat) + (int(round(d * self.somf)),)
                for lon, lat, d in chunk]))
            self.features.append((PRIM_POINT, "SOUNDG", attrs or {},
                                  [(RCNM_VI, rcid, 255, 255)]))

    def add_line(self, objl, attrs, coords):
        pts = self._clean(coords)
        if len(pts) < 2:
            self.skipped += 1
            return
        fspt = [(r, i, o, 255) for r, i, o in self._chain(pts)]
        self.features.append((PRIM_LINE, objl, attrs, fspt))

    def add_area(self, objl, attrs, rings):
        """rings: [exterior, hole, hole, ...] as lists of (lon, lat)."""
        fspt = []
        for k, ring in enumerate(rings):
            pts = self._clean(ring)
            if pts and pts[0] != pts[-1]:
                pts.append(pts[0])
            if len(pts) < 4:
                if k == 0:
                    self.skipped += 1
                    return
                continue
            # S-57: exterior boundary clockwise, interior counter-clockwise.
            ccw = _ring_area(pts) > 0
            if (k == 0) == ccw:
                pts.reverse()
            usage = 1 if k == 0 else 2
            fspt += [(r, i, o, usage) for r, i, o in self._chain(pts)]
        self.features.append((PRIM_AREA, objl, attrs, fspt))

    # -- encoding -----------------------------------------------------
    def _attf(self, attrs):
        out = b""
        for acr, value in attrs.items():
            code = self.cat.attl.get(acr)
            if code is None or value is None or value == "":
                continue
            if isinstance(value, float):
                value = ("%.3f" % value).rstrip("0").rstrip(".")
            elif isinstance(value, (list, tuple)):
                value = ",".join(str(v) for v in value)
            text = str(value).encode("latin-1", errors="replace")
            out += _u16(code) + text + UT
        return out

    def to_bytes(self):
        recs = [_ddr()]
        rid = [0]

        def rec(fields):
            rid[0] += 1
            recs.append(_record(
                [("0001", _u16(rid[0] & 0xFFFF) + FT)] + fields, b" D     "))

        geo = [f for f in self.features if f[1] in self.cat.objl]
        self.skipped += len(self.features) - len(geo)
        n_meta = sum(1 for f in geo if f[1].startswith("M_"))

        dsid = (_u8(RCNM_DSID) + _u32(1) + _u8(1) + _u8(self.intu)
                + self.name.encode("ascii") + UT + str(self.edition).encode()
                + UT + b"0" + UT + self.issue.encode() + self.issue.encode()
                + b"03.1" + _u8(1) + UT + b"2.0" + UT + _u8(1)
                + _u16(self.agency)
                + self.comment.encode("latin-1", errors="replace") + UT + FT)
        dssi = (_u8(2) + _u8(1) + _u8(0) + _u32(n_meta) + _u32(0)
                + _u32(len(geo) - n_meta) + _u32(0) + _u32(len(self.isolated))
                + _u32(len(self.connected)) + _u32(len(self.edges)) + _u32(0)
                + FT)
        rec([("DSID", dsid), ("DSSI", dssi)])
        dspm = (_u8(RCNM_DSPM) + _u32(1) + _u8(2) + _u8(self.vdat)
                + _u8(self.sdat) + _u32(self.scale) + _u8(1) + _u8(1) + _u8(1)
                + _u8(1) + _u32(self.comf) + _u32(self.somf) + UT + FT)
        rec([("DSPM", dspm)])

        for rcid, x, y, sg3d in self.isolated:
            fields = [("VRID", _u8(RCNM_VI) + _u32(rcid) + _u16(1) + _u8(1)
                       + FT)]
            if sg3d is None:
                fields.append(("SG2D", _i32(y) + _i32(x) + FT))
            else:
                fields.append(("SG3D", b"".join(
                    _i32(py) + _i32(px) + _i32(pz) for px, py, pz in sg3d)
                    + FT))
            rec(fields)
        for (x, y), rcid in sorted(self.connected.items(), key=lambda t: t[1]):
            rec([("VRID", _u8(RCNM_VC) + _u32(rcid) + _u16(1) + _u8(1) + FT),
                 ("SG2D", _i32(y) + _i32(x) + FT)])
        for rcid, start, end, inner in self.edges:
            fields = [
                ("VRID", _u8(RCNM_VE) + _u32(rcid) + _u16(1) + _u8(1) + FT),
                ("VRPT", _name(RCNM_VC, start) + _u8(255) + _u8(255) + _u8(1)
                 + _u8(255) + _name(RCNM_VC, end) + _u8(255) + _u8(255)
                 + _u8(2) + _u8(255) + FT)]
            if inner:
                fields.append(("SG2D", b"".join(
                    _i32(y) + _i32(x) for x, y in inner) + FT))
            rec(fields)

        # Meta features first, then group 1, then everything else.
        order = sorted(geo, key=lambda f: (not f[1].startswith("M_"),
                                           f[1] not in GROUP1))
        for fidn, (prim, objl, attrs, fspt) in enumerate(order, start=1):
            grup = 1 if objl in GROUP1 else 2
            if objl.startswith("M_"):
                grup = 255
            fields = [
                ("FRID", _u8(RCNM_FE) + _u32(fidn) + _u8(prim) + _u8(grup)
                 + _u16(self.cat.objl[objl]) + _u16(1) + _u8(1) + FT),
                ("FOID", _u16(self.agency) + _u32(fidn) + _u16(1) + FT)]
            attf = self._attf(attrs)
            if attf:
                fields.append(("ATTF", attf + FT))
            fields.append(("FSPT", b"".join(
                _name(r, i) + _u8(o) + _u8(u) + _u8(255)
                for r, i, o, u in fspt) + FT))
            rec(fields)
        return b"".join(recs)

    def write(self, path):
        data = self.to_bytes()
        with open(path, "wb") as f:
            f.write(data)
        return len(data)
