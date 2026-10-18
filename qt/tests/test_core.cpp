// Unit tests for the opencpn-qt core library.
#include <QtTest>

#include "core/Ais.h"
#include "core/GeoMath.h"
#include "core/GpxIO.h"
#include "core/Nmea.h"
#include "core/Simulator.h"

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

QTEST_GUILESS_MAIN(CoreTest)
#include "test_core.moc"
