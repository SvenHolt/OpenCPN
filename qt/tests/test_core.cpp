// Unit tests for the opencpn-qt core library.
#include <QtTest>

#include "core/Ais.h"
#include "core/GeoMath.h"
#include "core/GpxIO.h"
#include "core/NavStore.h"
#include "core/Nmea.h"
#include "core/Simulator.h"
#include "core/TrackRecorder.h"
#include "core/Tides.h"

class CoreTest : public QObject {
  Q_OBJECT
private slots:
  void mercatorRoundTrip() {
    for (double lat : {-60.0, 0.0, 12.5, 54.3}) {
      double la, lo;
      geo::fromMercator(geo::toMercator(lat, 10.25), la, lo);
      QVERIFY(qAbs(la - lat) < 1e-9);
      QVERIFY(qAbs(lo - 10.25) < 1e-9);
    }
  }

  void distanceAndBearing() {
    // One degree of latitude is 60 NM.
    QVERIFY(qAbs(geo::distanceNm(54, 10, 55, 10) - 60.04) < 0.1);
    QVERIFY(qAbs(geo::bearingDeg(54, 10, 55, 10)) < 1e-6);
    QVERIFY(qAbs(geo::bearingDeg(0, 0, 0, 1) - 90) < 1e-6);
    double lat, lon;
    geo::destination(54, 10, 90, 30, lat, lon);
    QVERIFY(qAbs(geo::distanceNm(54, 10, lat, lon) - 30) < 1e-6);
    QCOMPARE(geo::angleDiff(350, 10), 20.0);
    QCOMPARE(geo::angleDiff(10, 350), -20.0);
  }

  void crossTrack() {
    // Leg due north, point east of it: positive (right) XTE.
    const double xte = geo::crossTrackNm(54, 10, 55, 10, 54.5, 10.1);
    QVERIFY(xte > 3.4 && xte < 3.6);
  }

  void cpa() {
    // Head-on, 6 NM apart at 6+6 kn: CPA 0 after 30 minutes.
    auto c = geo::computeCpa(54, 10, 6, 0, 54.1, 10, 6, 180);
    QVERIFY(c.valid);
    QVERIFY(c.cpaNm < 0.01);
    QVERIFY(qAbs(c.tcpaMin - 30) < 0.5);
  }

  void nmeaChecksum() {
    const QString s = NmeaParser::makeSentence(
        "GPGLL,4916.45,N,12311.12,W,"
        "225444,A");
    QVERIFY(NmeaParser::verifyChecksum(s));
    QVERIFY(!NmeaParser::verifyChecksum(s.left(s.size() - 1) + "0"));
  }

  void nmeaRmc() {
    NmeaParser p;
    NavUpdate got;
    connect(&p, &NmeaParser::navUpdate, this,
            [&](const NavUpdate& u) { got = u; });
    QVERIFY(
        p.parseLine("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,"
                    "230394,003.1,W*6A"));
    QVERIFY(got.lat && got.lon && got.sog && got.cog);
    QVERIFY(qAbs(*got.lat - 48.1173) < 1e-4);
    QVERIFY(qAbs(*got.lon - 11.516667) < 1e-4);
    QCOMPARE(*got.sog, 22.4);
    QCOMPARE(*got.cog, 84.4);
  }

  void aisDecodeKnownVector() {
    NmeaParser p;
    std::optional<AisMessage> got;
    connect(&p, &NmeaParser::aisMessage, this,
            [&](const AisMessage& m) { got = m; });
    QVERIFY(p.parseLine("!AIVDM,1,1,,A,15RTgt0PAso;90TKcjM8h6g208CQ,0*4A"));
    QVERIFY(got);
    QCOMPARE(got->type, 1);
    QCOMPARE(got->mmsi, quint32(371798000));
    QCOMPARE(*got->sog, 12.3);
    QVERIFY(qAbs(*got->lat - 48.38163) < 1e-4);
    QVERIFY(qAbs(*got->lon + 123.395383) < 1e-4);
    QCOMPARE(*got->cog, 224.0);
    QCOMPARE(*got->hdg, 215.0);
  }

  void aisEncodeRoundTrip() {
    NmeaParser p;
    QVector<AisMessage> got;
    connect(&p, &NmeaParser::aisMessage, this,
            [&](const AisMessage& m) { got << m; });
    for (const auto& s : AisEncoder::sentences(AisEncoder::positionReport(
             211234567, 54.1234, -7.5678, 11.5, 271.3, 270)))
      QVERIFY(p.parseLine(s));
    for (const auto& s : AisEncoder::sentences(
             AisEncoder::staticReport(211234567, "SEA BREEZE", "DA1234", 37), 3,
             2))
      QVERIFY(p.parseLine(s));
    QCOMPARE(got.size(), 2);
    QCOMPARE(got[0].mmsi, quint32(211234567));
    QVERIFY(qAbs(*got[0].lat - 54.1234) < 1e-5);
    QVERIFY(qAbs(*got[0].lon + 7.5678) < 1e-5);
    QCOMPARE(*got[0].sog, 11.5);
    QCOMPARE(*got[0].cog, 271.3);
    QCOMPARE(got[1].type, 5);
    QCOMPARE(*got[1].name, QString("SEA BREEZE"));
    QCOMPARE(*got[1].callsign, QString("DA1234"));
    QCOMPARE(*got[1].shipType, 37);
  }

  void trackRecording();

  void windAndEngineSentences() {
    // Apparent 45° stbd at 15 kn, boat 6 kn: true wind 11.56 kn at 66.6°.
    double twa, tws;
    trueWindFromApparent(45, 15, 6, twa, tws);
    QVERIFY(qAbs(tws - 11.56) < 0.01);
    QVERIFY(qAbs(twa - 66.6) < 0.1);
    trueWindFromApparent(-30, 10, 0, twa, tws);  // at rest: unchanged
    QVERIFY(qAbs(tws - 10) < 1e-9 && qAbs(twa + 30) < 1e-9);

    NmeaParser p;
    QVector<NavUpdate> got;
    connect(&p, &NmeaParser::navUpdate, this,
            [&](const NavUpdate& u) { got << u; });
    QVERIFY(p.parseLine(NmeaParser::makeSentence("IIMWV,090,R,10.0,M,A")));
    QCOMPARE(*got.last().awa, 90.0);
    QVERIFY(qAbs(*got.last().aws - 19.44) < 0.01);  // m/s -> kn
    QVERIFY(!p.parseLine(NmeaParser::makeSentence("IIMWV,090,R,10.0,N,V")));
    QVERIFY(
        p.parseLine(NmeaParser::makeSentence("IIMWD,245.0,T,242.0,M,"
                                             "18.2,N,9.4,M")));
    QCOMPARE(*got.last().twd, 245.0);
    QCOMPARE(*got.last().tws, 18.2);
    QVERIFY(p.parseLine(NmeaParser::makeSentence("SDDBT,32.8,f,10.0,M,5.5,F")));
    QCOMPARE(*got.last().depth, 10.0);
    QVERIFY(!p.parseLine(NmeaParser::makeSentence("IIRPM,S,1,1200,,A")));
    QVERIFY(p.parseLine(
        NmeaParser::makeSentence("IIXDR,C,358.15,K,ENGINE#0,P,350000,P,"
                                 "ENGOIL#0,E,4.2,L,FUELRATE#0")));
    QVERIFY(qAbs(*got.last().coolantC - 85.0) < 1e-9);
    QVERIFY(qAbs(*got.last().oilBar - 3.5) < 1e-9);
    QCOMPARE(*got.last().fuelRateLph, 4.2);
  }
  void tidePrediction();

  void gpxRoundTrip() {
    GpxData d;
    Waypoint w;
    w.id = "abc";
    w.name = "Helgoland";
    w.lat = 54.18;
    w.lon = 7.89;
    d.waypoints << w;
    Route r;
    r.id = "r1";
    r.name = "Test & <route>";
    r.points << w << w;
    d.routes << r;
    GpxData back;
    QVERIFY(GpxIO::parse(GpxIO::serialize(d), back));
    QCOMPARE(back.waypoints.size(), 1);
    QCOMPARE(back.waypoints[0].id, QString("abc"));
    QCOMPARE(back.waypoints[0].name, QString("Helgoland"));
    QCOMPARE(back.routes.size(), 1);
    QCOMPARE(back.routes[0].name, r.name);
    QCOMPARE(back.routes[0].id, QString("r1"));
    QCOMPARE(back.routes[0].points.size(), 2);
  }
};

void CoreTest::trackRecording() {
  QStandardPaths::setTestModeEnabled(true);
  auto& rec = TrackRecorder::instance();
  rec.start();
  QDateTime t = QDateTime::currentDateTimeUtc();
  double lat = 54.5, lon = 10.2, cog = 0;
  int stored = 0;
  for (int i = 0; i < 240; ++i) {
    if (i == 120) cog = 90;  // a turn
    geo::destination(lat, lon, cog, 6.0 / 3600, lat, lon);
    stored += rec.addFix(lat, lon, cog, t.addSecs(i));
  }
  // 6 kn for 4 minutes = 0.4 NM; thinned to a point every ~60 m.
  QVERIFY(stored > 8 && stored < 20);
  QVERIFY(qAbs(rec.activeTrack().lengthNm() - 0.39) < 0.05);
  rec.stop();
  QCOMPARE(rec.tracks().size(), 1);
  QCOMPARE(rec.tracks()[0].points.size(), stored);

  // GPX round trip keeps track points and times.
  GpxData d;
  d.tracks = rec.tracks();
  GpxData back;
  QVERIFY(GpxIO::parse(GpxIO::serialize(d), back));
  QCOMPARE(back.tracks.size(), 1);
  QCOMPARE(back.tracks[0].points.size(), stored);
  QCOMPARE(back.tracks[0].points.last().time.toSecsSinceEpoch(),
           rec.tracks()[0].points.last().time.toSecsSinceEpoch());
  rec.removeTrack(rec.tracks()[0].id);
}

void CoreTest::tidePrediction() {
  const QString dir = QStringLiteral(OCPN_QT_SOURCE_DATA_DIR) + "/tcdata";
  QElapsedTimer timer;
  timer.start();
  QString err;
  auto data = TideDatabase::load(dir + "/HARMONICS_NO_US",
                                 dir + "/HARMONICS_NO_US.IDX", &err);
  QVERIFY2(data, qPrintable(err));
  qInfo("Tide data: %lld stations, %lld harmonic sets, %lld ms",
        qlonglong(data->stations.size()), qlonglong(data->harmonics.size()),
        qlonglong(timer.elapsed()));
  QVERIFY(data->stations.size() > 3000);
  TideDatabase::instance().setData(data);
  auto& db = TideDatabase::instance();

  const TideStation* cux = nullptr;
  for (const auto& s : db.stations())
    if (s.name.startsWith("Cuxhaven, Steubenhoft")) cux = &s;
  QVERIFY(cux);
  QCOMPARE(db.unit(*cux), QString("m"));
  const QDateTime from(QDate(2026, 6, 1), QTime(0, 0), Qt::UTC);
  const auto ext = db.extremes(*cux, from, from.addDays(2));
  // Semi-diurnal: about 4 extremes per day, alternating.
  QVERIFY(ext.size() >= 7 && ext.size() <= 9);
  for (int i = 1; i < ext.size(); ++i) {
    QVERIFY(ext[i].high != ext[i - 1].high);
    const double h = ext[i - 1].time.secsTo(ext[i].time) / 3600.0;
    QVERIFY2(h > 5 && h < 7.5, qPrintable(QString::number(h)));
    const double range = std::fabs(ext[i].value - ext[i - 1].value);
    // Mean tidal range at Cuxhaven is about 3 m.
    QVERIFY2(range > 2.0 && range < 4.5, qPrintable(QString::number(range)));
  }
  // Cross-check against the official Canadian Hydrographic Service
  // prediction for Halifax (api-iwls.dfo-mpo.gc.ca, wlp-hilo, 2026-06-01).
  const TideStation* hfx = nullptr;
  for (const auto& s : db.stations())
    if (s.name == "Halifax, Nova Scotia") hfx = &s;
  QVERIFY(hfx);
  const struct {
    const char* time;
    double value;
  } chs[] = {{"2026-06-01T06:43:00Z", 0.272}, {"2026-06-01T12:48:00Z", 1.63},
             {"2026-06-01T18:52:00Z", 0.686}, {"2026-06-02T00:36:00Z", 1.71},
             {"2026-06-02T07:17:00Z", 0.315}, {"2026-06-02T13:26:00Z", 1.625}};
  const auto hext =
      db.extremes(*hfx, from.addSecs(4 * 3600), from.addSecs(40 * 3600));
  for (const auto& ref : chs) {
    const QDateTime t = QDateTime::fromString(ref.time, Qt::ISODate);
    const TideExtreme* match = nullptr;
    for (const auto& e : hext)
      if (std::abs(e.time.secsTo(t)) < 3600) match = &e;
    QVERIFY2(match, ref.time);
    const qint64 dtMin = std::abs(match->time.secsTo(t)) / 60;
    qInfo("%s CHS %.2f m, ours %s %.2f m (%lld min)", ref.time, ref.value,
          qPrintable(match->time.toString(Qt::ISODate)), match->value, dtMin);
    QVERIFY(dtMin <= 15);
    QVERIFY(std::fabs(match->value - ref.value) < 0.15);
  }

  // Nearest station lookup.
  const auto near = db.nearest(53.87, 8.70, 1, TideStation::Type::Tide);
  QCOMPARE(near.size(), 1);
  QVERIFY(geo::distanceNm(53.87, 8.70, near[0]->lat, near[0]->lon) < 5);
}

QTEST_GUILESS_MAIN(CoreTest)
#include "test_core.moc"
