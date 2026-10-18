// Offline OSM regions and the local Overpass instance. The integration
// tests run when osm-3s is installed (OPENCPN_OSM3S_BIN, PATH or
// ~/.local/osm-3s/bin) and are skipped otherwise.
#include <QSignalSpy>
#include <QtTest>

#include "core/osm/OsmMerge.h"
#include "core/osm/Overpass.h"

namespace {
QByteArray sample() {
  QFile f(QStringLiteral(OCPN_QT_TEST_DATA_DIR "/harbour_sample.osm"));
  f.open(QIODevice::ReadOnly);
  return f.readAll();
}

QList<qint64> ids(const QByteArray& xml, const QString& type) {
  QList<qint64> out;
  QXmlStreamReader r(xml);
  while (!r.atEnd())
    if (r.readNext() == QXmlStreamReader::StartElement && r.name() == type)
      out << r.attributes().value("id").toLongLong();
  return out;
}

/** Runs a query synchronously. */
QByteArray runQuery(const QString& ql, QString* error) {
  QByteArray result;
  bool done = false;
  OverpassService::instance().query(
      ql, [&](const QByteArray& out, const QString& err) {
        result = out;
        *error = err;
        done = true;
      });
  if (!QTest::qWaitFor([&] { return done; }, 30000))
    *error = QStringLiteral("timeout");
  return result;
}
}  // namespace

class OsmTest : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("opencpn-qt-tests");
    QStandardPaths::setTestModeEnabled(true);
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
         "/overpass")
        .removeRecursively();
  }

  void mergeDeduplicatesAndSorts() {
    const QByteArray a =
        "<osm version='0.6'><node id='5' lat='1' lon='2'/>"
        "<way id='9'><nd ref='5'/><nd ref='3'/></way>"
        "<node id='3' lat='1.5' lon='2.5'><tag k='name' v='A &amp; B'/></node>"
        "</osm>";
    const QByteArray b =
        "<osm version='0.6'><bounds minlat='0' minlon='0' maxlat='1' "
        "maxlon='1'/><node id='3' lat='1.5' lon='2.5'>"
        "<tag k='name' v='A &amp; B'/></node><node id='1' lat='0' lon='0'/>"
        "<relation id='2'><member type='way' ref='9' role=''/></relation>"
        "</osm>";
    OsmMerger m;
    QVERIFY(m.add(a));
    QVERIFY(m.add(b));
    QCOMPARE(m.nodeCount(), 3);
    QCOMPARE(m.wayCount(), 1);
    QCOMPARE(m.relationCount(), 1);
    const QByteArray out = m.toByteArray();
    QCOMPARE(ids(out, "node"), (QList<qint64>{1, 3, 5}));
    QCOMPARE(ids(out, "way"), (QList<qint64>{9}));
    QCOMPARE(ids(out, "relation"), (QList<qint64>{2}));
    // Nodes before ways before relations, tags and escaping preserved.
    QVERIFY(out.indexOf("<node id=\"5\"") < out.indexOf("<way"));
    QVERIFY(out.indexOf("<way") < out.indexOf("<relation"));
    QVERIFY(out.contains("v=\"A &amp; B\""));
    QVERIFY(out.contains("<nd ref=\"3\""));
    QVERIFY(!out.contains("bounds"));
  }

  void mergeRejectsGarbage() {
    OsmMerger m;
    QString err;
    QVERIFY(!m.add("<html><body>Too many requests</body></html>", &err));
    QVERIFY(!err.isEmpty());
    QVERIFY(!m.add("<osm><node id='1'", &err));
  }

  void tilesCoverBox() {
    const GeoBox box{54.30, 10.10, 54.36, 10.16};
    const auto tiles = OverpassService::tiles(box, 0.025);
    QCOMPARE(tiles.size(), 9);
    double area = 0;
    for (const GeoBox& t : tiles) {
      QVERIFY(t.isValid());
      QVERIFY(t.north - t.south <= 0.025 + 1e-9);
      QVERIFY(t.south >= box.south && t.north <= box.north + 1e-12);
      area += t.areaDeg2();
    }
    QVERIFY(std::fabs(area - box.areaDeg2()) < 1e-9);
  }

  void geoBox() {
    const GeoBox b{54.0, 10.0, 54.5, 11.0};
    QCOMPARE(b.overpassBbox(), QStringLiteral("54.000000,10.000000,"
                                              "54.500000,11.000000"));
    QVERIFY(std::fabs(b.heightNm() - 30) < 1e-9);
    QVERIFY(std::fabs(b.widthNm() - 60 * std::cos(54.25 * M_PI / 180)) < 1e-9);
    QVERIFY(b.intersects(GeoBox{54.4, 10.9, 55, 12}));
    QVERIFY(!b.intersects(GeoBox{55, 10, 56, 11}));
  }

  void parseJson() {
    const QByteArray json = R"(encoding remark: Please enter your query
{ "version": 0.6, "elements": [
  { "type": "node", "id": 7, "lat": 54.1, "lon": 10.2,
    "tags": { "seamark:type": "buoy_lateral" } },
  { "type": "way", "id": 8, "center": { "lat": 54.3, "lon": 10.4 },
    "tags": { "leisure": "marina", "name": "Hafen" } },
  { "type": "relation", "id": 9, "tags": { "name": "no center" } }
]})";
    QString err;
    const auto e = OverpassService::parseJson(json, &err);
    QVERIFY2(err.isEmpty(), qPrintable(err));
    QCOMPARE(e.size(), 2);
    QCOMPARE(e[0].id, 7);
    QCOMPARE(e[0].tags.value("seamark:type"), QStringLiteral("buoy_lateral"));
    QCOMPARE(e[1].type, QStringLiteral("way"));
    QCOMPARE(e[1].name(), QStringLiteral("Hafen"));
    QCOMPARE(e[1].lat, 54.3);
    QVERIFY(OverpassService::parseJson("not json", &err).isEmpty());
    QVERIFY(!err.isEmpty());
  }

  void localImportAndQuery() {
    auto& svc = OverpassService::instance();
    if (svc.binDir().isEmpty())
      QSKIP("osm-3s is not installed (set OPENCPN_OSM3S_BIN)");
    svc.configure(OverpassService::Backend::Local, svc.binDir(), {}, {});
    QVERIFY(svc.localAvailable());

    const QString path = QDir::temp().filePath("ocpnqt_sample.osm");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(sample());
    f.close();

    QSignalSpy db(&svc, &OverpassService::databaseChanged);
    QSignalSpy errors(&svc, &OverpassService::error);
    QString err;
    QVERIFY2(svc.importFile(path, "Testhafen", &err), qPrintable(err));
    QVERIFY(QTest::qWaitFor([&] { return !svc.isBusy(); }, 60000));
    QVERIFY2(
        errors.isEmpty(),
        errors.isEmpty() ? "" : qPrintable(errors.first().first().toString()));
    QCOMPARE(db.size(), 1);
    QVERIFY(svc.databaseReady());
    QCOMPARE(svc.regions().size(), 1);
    const OsmRegion r = svc.regions().first();
    QCOMPARE(r.name, QStringLiteral("Testhafen"));
    QCOMPARE(r.nodes, 7);
    QCOMPARE(r.ways, 1);
    QCOMPARE(r.box.south, 54.35);
    QCOMPARE(r.source, QStringLiteral("file"));

    // Harbour details with way centers, as the chart layer asks for them.
    const QByteArray out = runQuery(
        "[out:json][bbox:54.34,10.14,54.38,10.19];"
        "(nwr[\"leisure\"~\"^(marina|slipway)$\"];nwr[\"seamark:type\"];"
        "nwr[\"waterway\"=\"fuel\"];);out center tags;",
        &err);
    QVERIFY2(err.isEmpty(), qPrintable(err));
    const auto elements = OverpassService::parseJson(out, &err);
    QCOMPARE(elements.size(), 4);
    bool marina = false;
    for (const auto& e : elements)
      if (e.type == "way") {
        marina = true;
        QCOMPARE(e.name(), QStringLiteral("Testhafen Düsternbrook"));
        QVERIFY(std::fabs(e.lat - 54.3615) < 1e-3);
        QVERIFY(std::fabs(e.lon - 10.162) < 1e-3);
      }
    QVERIFY(marina);

    // Case insensitive name search.
    const QByteArray found = runQuery(
        "[out:json];nwr[\"name\"~\"tankstelle\",i];out center tags;", &err);
    const auto hits = OverpassService::parseJson(found);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.first().id, 202);

    // Syntax errors are reported, not returned as empty results.
    runQuery("garbage(", &err);
    QVERIFY(!err.isEmpty());

    // Removing the last region empties the database.
    svc.removeRegion(r.id);
    QVERIFY(QTest::qWaitFor([&] { return !svc.isBusy(); }, 30000));
    QVERIFY(svc.regions().isEmpty());
    QVERIFY(!svc.databaseReady());
  }

  /** Downloads a small area (Overpass, then OpenStreetMap API fallback). */
  void downloadRegion() {
    if (qEnvironmentVariable("OCPN_QT_NETWORK_TESTS") != "1")
      QSKIP("set OCPN_QT_NETWORK_TESTS=1 to download from OpenStreetMap");
    auto& svc = OverpassService::instance();
    svc.configure(
        OverpassService::Backend::Local, svc.binDir(), {},
        qEnvironmentVariable("OCPN_QT_OVERPASS_DOWNLOAD", svc.downloadUrl()));
    QSignalSpy errors(&svc, &OverpassService::error);
    svc.addRegion("Kiel Blücherbrücke", GeoBox{54.325, 10.140, 54.335, 10.160});
    QVERIFY(QTest::qWaitFor([&] { return !svc.isBusy(); }, 600000));
    // Without osm-3s only the "not found" hint is reported.
    if (svc.localAvailable())
      QVERIFY2(errors.isEmpty(),
               errors.isEmpty()
                   ? ""
                   : qPrintable(errors.first().first().toString()));
    QCOMPARE(svc.regions().size(), 1);
    const OsmRegion r = svc.regions().first();
    qInfo("source %s, %d nodes, %d ways, %lld bytes", qPrintable(r.source),
          r.nodes, r.ways, r.bytes);
    QVERIFY(r.nodes > 100);
    QVERIFY(r.ways > 10);
    if (svc.localAvailable()) {
      QString err;
      const QByteArray out =
          runQuery("[out:json];nwr[\"building\"];out center 5;", &err);
      QVERIFY2(err.isEmpty(), qPrintable(err));
      QVERIFY(!OverpassService::parseJson(out).isEmpty());
    }
    svc.removeRegion(r.id);
    QVERIFY(QTest::qWaitFor([&] { return !svc.isBusy(); }, 30000));
  }
};

QTEST_GUILESS_MAIN(OsmTest)
#include "test_osm.moc"
