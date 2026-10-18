"""Tests for the seachart tool.  Run with

    python3 -m unittest discover -s tools/seachart/tests

The S-57 output is checked with a small ISO 8211 parser below and, when
GDAL's ``ogrinfo`` is installed, by reading it back with GDAL.
"""

import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

import numpy as np
from shapely.geometry import LineString, Point, box

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from seachart import coast, depth, route  # noqa: E402
from seachart.catalog import Catalog  # noqa: E402
from seachart.cli import main  # noqa: E402
from seachart.s57map import Mapper  # noqa: E402
from seachart.s57writer import S57Cell  # noqa: E402
from seachart.store import Store  # noqa: E402

CAT = Catalog()


def iso8211_records(data):
    """Yield {tag: [field bytes]} for every record of an ISO 8211 file."""
    pos = 0
    while pos < len(data):
        length = int(data[pos:pos + 5])
        rec = data[pos:pos + length]
        base = int(rec[12:17])
        size_len, size_pos, size_tag = int(rec[20:21]), int(rec[21:22]), \
            int(rec[23:24])
        entry = size_tag + size_len + size_pos
        fields = {}
        d = 24
        while rec[d:d + 1] != b"\x1e":
            tag = rec[d:d + size_tag].decode()
            flen = int(rec[d + size_tag:d + size_tag + size_len])
            fpos = int(rec[d + size_tag + size_len:d + entry])
            fields.setdefault(tag, []).append(rec[base + fpos:
                                                  base + fpos + flen])
            d += entry
        assert all(f[-1:] == b"\x1e" for fl in fields.values() for f in fl)
        yield fields
        pos += length


def ogrinfo(path):
    if not shutil.which("ogrinfo"):
        return None
    return subprocess.run(["ogrinfo", "-ro", "-al", path], check=True,
                          capture_output=True, text=True).stdout


class WriterTest(unittest.TestCase):
    def build(self):
        cell = S57Cell(CAT, "TEST0001.000", 50000, 4)
        cell.add_area("M_COVR", {"CATCOV": 1},
                      [[(10, 54), (10.5, 54), (10.5, 54.5), (10, 54.5)]])
        cell.add_point("BOYLAT", {"CATLAM": 1, "COLOUR": "3", "BOYSHP": 2,
                                  "OBJNAM": "Kieler Förde 1"}, 10.2, 54.4)
        cell.add_line("DEPCNT", {"VALDCO": 10.0},
                      [(10.1, 54.3), (10.2, 54.35), (10.3, 54.3)])
        cell.add_area("DEPARE", {"DRVAL1": 5.0, "DRVAL2": 10.0},
                      [[(10.1, 54.3), (10.3, 54.3), (10.3, 54.45),
                        (10.1, 54.45)],
                       [(10.15, 54.35), (10.2, 54.35), (10.2, 54.4)]])
        cell.add_soundings([(10.15, 54.33, 7.3), (10.25, 54.4, 12.1)])
        # a long line is split into several edges
        cell.add_line("COALNE", {}, [(10 + i * 1e-4, 54.1) for i in
                                     range(4000)])
        return cell

    def test_structure(self):
        cell = self.build()
        ddr, *recs = iso8211_records(cell.to_bytes())
        self.assertIn("0000", ddr)
        dssi = recs[0]["DSSI"][0]
        nomr, nocr, nogr, nolr, noin, nocn, noed = struct.unpack(
            "<7I", dssi[3:31])
        frid = sum(1 for r in recs if "FRID" in r)
        self.assertEqual(nomr + nogr, frid)
        vr = [r["VRID"][0][0] for r in recs if "VRID" in r]
        self.assertEqual(vr.count(110), noin)
        self.assertEqual(vr.count(120), nocn)
        self.assertEqual(vr.count(130), noed)
        self.assertGreater(noed, 3)

    def test_gdal_roundtrip(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "TEST0001.000")
            self.build().write(path)
            out = ogrinfo(path)
            if out is None:
                self.skipTest("ogrinfo not installed")
            self.assertIn("OBJNAM (String) = Kieler Förde 1", out)
            self.assertIn("POINT (10.2 54.4)", out)
            self.assertIn("LINESTRING (10.1 54.3,10.2 54.35,10.3 54.3)",
                          out)
            self.assertIn("(10.15 54.35,10.2 54.35,10.2 54.4,10.15 54.35)",
                          out)
            self.assertIn("MULTIPOINT Z ((10.15 54.33 7.3),"
                          "(10.25 54.4 12.1))", out)
            self.assertIn("CATCOV (Integer) = 1", out)


class CoastTest(unittest.TestCase):
    B = (10, 54, 10.4, 54.3)

    def test_land_left_of_coastline(self):
        north = coast.land_polygons(
            [LineString([(9.9, 54.1), (10.2, 54.15), (10.5, 54.12)])],
            self.B)
        south = coast.land_polygons(
            [LineString([(10.5, 54.12), (10.2, 54.15), (9.9, 54.1)])],
            self.B)
        self.assertGreater(north.centroid.y, 54.15)
        self.assertLess(south.centroid.y, 54.12)
        self.assertAlmostEqual(north.area + south.area, box(*self.B).area)

    def test_island_and_split_ways(self):
        ring = [(10.1, 54.1), (10.2, 54.1), (10.2, 54.2), (10.1, 54.1)]
        land = coast.land_polygons([LineString(ring[:3]),
                                    LineString(ring[2:])], self.B)
        self.assertAlmostEqual(land.area, 0.005)

    def test_open_water_default(self):
        self.assertIsNone(coast.land_polygons([], self.B))
        self.assertEqual(coast.land_polygons([], self.B,
                                             default_land=lambda: True).area,
                         box(*self.B).area)


class DepthTest(unittest.TestCase):
    def test_bands_cover_grid(self):
        y, x = np.mgrid[0:60, 0:80]
        g = (np.hypot(x - 40, y - 30) * 0.8 - 3).astype("f4")
        lines, areas = depth.depth_products(10, 54, 0.005, 0.005, g)
        total = sum(p.area for _, _, p in areas)
        self.assertAlmostEqual(total, 79 * 0.005 * 59 * 0.005, places=6)
        self.assertEqual(sorted({lv for lv, _ in lines}),
                         [2, 5, 10, 15, 20, 30])
        # each band lies within its depth range
        for d1, d2, poly in areas:
            pt = poly.representative_point()
            z = depth.sample(10, 54, 0.005, 0.005, g, pt.x, pt.y)
            self.assertTrue(d1 - 0.01 <= z <= d2 + 0.01, (d1, d2, z))

    def test_soundings_keep_shallowest(self):
        g = np.full((10, 10), 20.0, dtype="f4")
        g[3, 4] = 1.5
        snd = depth.soundings(0, 0, 0.01, 0.01, g, 0.05)
        self.assertIn(1.5, [d for _, _, d in snd])


class MapperTest(unittest.TestCase):
    def test_lateral_buoy_with_light_and_topmark(self):
        tags = {"seamark:type": "buoy_lateral",
                "seamark:name": "Kiel 3",
                "seamark:buoy_lateral:category": "starboard",
                "seamark:buoy_lateral:shape": "conical",
                "seamark:buoy_lateral:colour": "green",
                "seamark:buoy_lateral:system": "iala-a",
                "seamark:topmark:shape": "cone, point up",
                "seamark:topmark:colour": "green",
                "seamark:light:character": "Fl",
                "seamark:light:colour": "green",
                "seamark:light:period": "4",
                "seamark:light:group": "2"}
        objs = {o: a for o, a, _ in Mapper(CAT).map(tags, Point(10, 54))}
        self.assertEqual(objs["BOYLAT"], {"CATLAM": 2, "BOYSHP": 1,
                                          "COLOUR": "4", "MARSYS": 1,
                                          "OBJNAM": "Kiel 3"})
        self.assertEqual(objs["TOPMAR"], {"TOPSHP": 1, "COLOUR": "4"})
        self.assertEqual(objs["LIGHTS"], {"LITCHR": 2, "COLOUR": "4",
                                          "SIGPER": 4.0, "SIGGRP": "(2)"})

    def test_sector_light(self):
        tags = {"seamark:type": "light_major", "seamark:light:range": "12",
                "seamark:light:1:colour": "white",
                "seamark:light:1:sector_start": "10",
                "seamark:light:1:sector_end": "20",
                "seamark:light:2:colour": "red",
                "seamark:light:2:sector_start": "20",
                "seamark:light:2:sector_end": "40",
                "seamark:light:2:range": "9"}
        lights = [a for o, a, _ in Mapper(CAT).map(tags, Point(10, 54))
                  if o == "LIGHTS"]
        self.assertEqual(len(lights), 2)
        self.assertEqual(lights[0]["VALNMR"], 12.0)
        self.assertEqual(lights[1]["VALNMR"], 9.0)
        self.assertEqual((lights[1]["SECTR1"], lights[1]["SECTR2"]),
                         (20.0, 40.0))

    def test_harbour_and_wreck(self):
        m = Mapper(CAT)
        harbour = m.map({"seamark:type": "harbour",
                         "seamark:harbour:category": "marina",
                         "seamark:name": "Schilksee"},
                        LineString([(10, 54), (10.01, 54), (10.01, 54.01),
                                    (10, 54)]))
        self.assertEqual(harbour[0][0], "HRBFAC")
        self.assertEqual(harbour[0][1]["CATHAF"], "5")
        self.assertEqual(harbour[0][2].geom_type, "Polygon")
        wreck = m.map({"seamark:type": "wreck",
                       "seamark:wreck:category": "dangerous",
                       "seamark:wreck:depth": "4.2",
                       "seamark:wreck:water_level": "submerged"},
                      Point(10, 54))
        self.assertEqual(wreck[0][1], {"CATWRK": 2, "VALSOU": 4.2,
                                       "WATLEV": 3})


class RouteTest(unittest.TestCase):
    def test_gpx_and_corridor(self):
        gpx = """<?xml version="1.0"?>
<gpx version="1.1" creator="OpenCPN" xmlns="http://www.topografix.com/GPX/1/1">
 <rte><name>Kiel-Laboe</name>
  <rtept lat="54.33" lon="10.15"/><rtept lat="54.42" lon="10.21"/>
 </rte></gpx>"""
        with tempfile.NamedTemporaryFile("w", suffix=".gpx",
                                         delete=False) as f:
            f.write(gpx)
        try:
            (name, pts), = route.read_gpx(f.name)
        finally:
            os.unlink(f.name)
        self.assertEqual(name, "Kiel-Laboe")
        area = route.corridor(pts, 2.0)
        # 2 NM north of the start is inside, 3 NM is not
        self.assertTrue(area.contains(Point(10.15, 54.33 + 1.9 / 60)))
        self.assertFalse(area.contains(Point(10.15, 54.33 - 3 / 60)))
        cells = route.cells_for(area, 0.25)
        self.assertAlmostEqual(sum(c[3].area for c in cells), area.area)


OSM_XML = """<?xml version="1.0"?>
<osm version="0.6">
 <node id="1" lat="54.40" lon="10.20">
  <tag k="seamark:type" v="buoy_safe_water"/>
  <tag k="seamark:buoy_safe_water:shape" v="spherical"/>
 </node>
 <node id="2" lat="54.30" lon="10.10"/><node id="3" lat="54.30" lon="10.30"/>
 <node id="4" lat="54.35" lon="10.30"/><node id="5" lat="54.35" lon="10.10"/>
 <node id="6" lat="54.31" lon="10.11"/><node id="7" lat="54.31" lon="10.12"/>
 <node id="8" lat="54.32" lon="10.12"/>
 <way id="10"><nd ref="2"/><nd ref="3"/><nd ref="4"/></way>
 <way id="11"><nd ref="4"/><nd ref="5"/><nd ref="2"/></way>
 <way id="12"><nd ref="6"/><nd ref="7"/><nd ref="8"/><nd ref="6"/></way>
 <relation id="20">
  <member type="way" ref="10" role="outer"/>
  <member type="way" ref="11" role="outer"/>
  <member type="way" ref="12" role="inner"/>
  <tag k="type" v="multipolygon"/>
  <tag k="seamark:type" v="restricted_area"/>
  <tag k="seamark:restricted_area:category" v="nature_reserve"/>
 </relation>
</osm>
"""


class ImportTest(unittest.TestCase):
    def check(self, store):
        feats = {i: (t, g) for i, t, g in store.features((10, 54, 11, 55))}
        self.assertEqual(feats["node/1"][1].geom_type, "Point")
        tags, geom = feats["relation/20"]
        self.assertEqual(geom.geom_type, "Polygon")
        self.assertEqual(len(geom.interiors), 1)
        objs = Mapper(CAT).map(tags, geom)
        self.assertEqual(objs[0][:2], ("RESARE", {"CATREA": "4"}))

    def test_osm_xml_and_pbf(self):
        from seachart import sources
        with tempfile.TemporaryDirectory() as tmp:
            xml = os.path.join(tmp, "x.osm")
            with open(xml, "w") as f:
                f.write(OSM_XML)
            store = Store(os.path.join(tmp, "a.db"))
            sources.import_osm_xml(store, xml)
            self.check(store)
            store.close()
            try:
                import osmium
            except ImportError:
                self.skipTest("pyosmium not installed")
            pbf = os.path.join(tmp, "x.osm.pbf")
            writer = osmium.SimpleWriter(pbf)

            class Copy(osmium.SimpleHandler):
                def node(self, n):
                    writer.add_node(n)

                def way(self, w):
                    writer.add_way(w)

                def relation(self, r):
                    writer.add_relation(r)
            Copy().apply_file(xml)
            writer.close()
            store = Store(os.path.join(tmp, "b.db"))
            sources.import_pbf(store, pbf)
            self.check(store)
            store.close()


def fixture_store(path):
    """A small synthetic coast: land in the south-west, a sloping sea bed,
    a lateral buoy, a light, a marina and a wreck."""
    elements = [
        {"type": "way", "id": 1, "tags": {"natural": "coastline"},
         "geometry": [{"lon": 9.9, "lat": 54.25},
                      {"lon": 10.1, "lat": 54.3},
                      {"lon": 10.2, "lat": 54.45},
                      {"lon": 10.1, "lat": 54.7}]},
        {"type": "node", "id": 2, "lon": 10.3, "lat": 54.4,
         "tags": {"seamark:type": "buoy_lateral",
                  "seamark:buoy_lateral:category": "port",
                  "seamark:buoy_lateral:shape": "can",
                  "seamark:buoy_lateral:colour": "red",
                  "seamark:light:colour": "red",
                  "seamark:light:character": "Fl"}},
        {"type": "node", "id": 3, "lon": 10.21, "lat": 54.46,
         "tags": {"seamark:type": "light_major",
                  "seamark:light:colour": "white",
                  "seamark:light:character": "Oc",
                  "seamark:light:range": "15"}},
        {"type": "way", "id": 4,
         "tags": {"seamark:type": "harbour", "seamark:name": "Testhafen",
                  "seamark:harbour:category": "marina"},
         "geometry": [{"lon": 10.17, "lat": 54.40},
                      {"lon": 10.20, "lat": 54.40},
                      {"lon": 10.20, "lat": 54.42},
                      {"lon": 10.17, "lat": 54.40}]},
        {"type": "node", "id": 5, "lon": 10.4, "lat": 54.5,
         "tags": {"seamark:type": "wreck",
                  "seamark:wreck:category": "dangerous"}},
    ]
    tmp = path + ".json"
    with open(tmp, "w") as f:
        json.dump({"elements": elements}, f)
    store = Store(path)
    from seachart import sources
    sources.import_overpass_json(store, tmp)
    store.mark_downloaded("osm", (9.5, 54.0, 11.0, 55.0))
    y, x = np.mgrid[0:121, 0:181]
    lon = 9.6 + x * 0.005
    grid = ((lon - 10.15) * 150).astype("f4")     # deeper towards the east
    store.put_grid("depth:test", 9.6, 54.1, 0.005, 0.005, grid)
    store.mark_downloaded("depth:test", (9.6, 54.1, 10.5, 54.7))
    store.close()


class EndToEndTest(unittest.TestCase):
    def test_route_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            db = os.path.join(tmp, "test.db")
            fixture_store(db)
            out = os.path.join(tmp, "charts")
            main(["--db", db, "route", "--waypoints",
                  "54.41,10.22 54.45,10.35 54.55,10.38", "--name", "Test",
                  "--width", "3", "--out", out])
            files = sorted(f for f in os.listdir(os.path.join(out, "Test"))
                           if f.endswith(".000"))
            self.assertGreaterEqual(len(files), 4)   # overview, route, 2 harb.
            text = ""
            for f in files:
                path = os.path.join(out, "Test", f)
                with open(path, "rb") as f:
                    recs = list(iso8211_records(f.read()))
                self.assertGreater(len(recs), 3)
                text += ogrinfo(path) or ""
            if not text:
                self.skipTest("ogrinfo not installed")
            for objl in ("BOYLAT", "LIGHTS", "HRBFAC", "WRECKS", "LNDARE",
                         "COALNE", "DEPARE", "DEPCNT", "SOUNDG", "M_COVR"):
                self.assertIn("OGRFeature(%s)" % objl, text)
            self.assertIn("OBJNAM (String) = Testhafen", text)


if __name__ == "__main__":
    unittest.main()
