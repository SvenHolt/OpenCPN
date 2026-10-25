// Tides and currents in passage planning: marine forecast, rivers, tidal
// current stations, the planner and the sail planner with current, water
// level limits and gauges.
#include <QtTest>

#include "core/Currents.h"
#include "core/GeoMath.h"
#include "core/SailPlan.h"
#include "core/osm/Overpass.h"

namespace {
PassageSettings settings(double windFrom, double windKn) {
  PassageSettings s;
  s.departure = QDateTime(QDate(2026, 6, 1), QTime(8, 0), Qt::UTC);
  s.manualWindFromDeg = windFrom;
  s.manualWindKn = windKn;
  s.hullSpeedKn = 6.5;
  s.motorSpeedKn = 5;
  s.strategy = PassageSettings::Strategy::Motor;
  return s;
}

/** 10 NM due north from 54.5 N 10.0 E. */
QVector<QPointF> north(double nm = 10) {
  return {QPointF(10.0, 54.5), QPointF(10.0, 54.5 + nm / 60)};
}

CurrentSource constant(double set, double drift) {
  return [set, drift](double, double, const QDateTime&, double& s, double& d) {
    s = set;
    d = drift;
    return true;
  };
}

/** An Open-Meteo marine answer: two locations, three hours. */
QByteArray marineJson(double cellLat2) {
  const auto loc = [](double lat, double lon, QJsonArray v, QJsonArray d,
                      QJsonArray l) {
    return QJsonObject{
        {"latitude", lat},
        {"longitude", lon},
        {"hourly",
         QJsonObject{{"time", QJsonArray{1780300800, 1780304400, 1780308000}},
                     {"ocean_current_velocity", v},
                     {"ocean_current_direction", d},
                     {"sea_level_height_msl", l}}}};
  };
  return QJsonDocument(QJsonArray{loc(54.0, 8.0, {1.852, 3.704, 1.852},
                                      {90, 90, 270}, {0.5, 1.0, 0.2}),
                                  loc(cellLat2, 8.5, {1.0, 1.0, 1.0}, {0, 0, 0},
                                      {0, 0, 0})})
      .toJson();
}
}  // namespace

class CurrentsTest : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("opencpn-qt-tests");
    QStandardPaths::setTestModeEnabled(true);
  }

  void groundSpeed() {
    QCOMPARE(PassagePlanner::groundSpeed(5, 0, 0), 5.0);
    QCOMPARE(PassagePlanner::groundSpeed(5, 1, 0), 6.0);
    QCOMPARE(PassagePlanner::groundSpeed(5, -2, 0), 3.0);
    // Across: the boat steers into it and loses speed along the course.
    QVERIFY(qAbs(PassagePlanner::groundSpeed(5, 0, 3) - 4.0) < 1e-9);
    // Stronger than the boat: a small minimum.
    QVERIFY(PassagePlanner::groundSpeed(2, -5, 0) > 0);
  }

  void marineForecast() {
    const QVector<QPointF> pts = {QPointF(8.0, 54.0), QPointF(8.5, 54.2)};
    QString err;
    MarineForecast m = MarineForecast::parse(marineJson(54.2), pts, &err);
    QVERIFY2(m.isValid(), qPrintable(err));
    QCOMPARE(m.times.size(), 3);
    const QDateTime t0 = QDateTime::fromSecsSinceEpoch(1780300800, Qt::UTC);
    double set, drift;
    QVERIFY(m.current(54.0, 8.0, t0, set, drift));
    QVERIFY(qAbs(drift - 1.0) < 1e-3);  // 1.852 km/h = 1 kn
    QVERIFY(qAbs(set - 90) < 1e-3);
    // Halfway between 1 and 2 kn to the east.
    QVERIFY(m.current(54.0, 8.0, t0.addSecs(1800), set, drift));
    QVERIFY(qAbs(drift - 1.5) < 1e-3);
    // Turning tide: east 2 kn to west 1 kn, the vectors are averaged.
    QVERIFY(m.current(54.0, 8.0, t0.addSecs(3600 + 2400), set, drift));
    QVERIFY(drift < 1.0);
    // Level against mean sea level.
    double lvl;
    QVERIFY(m.level(54.0, 8.0, t0.addSecs(1800), lvl));
    QVERIFY(qAbs(lvl - 0.75) < 1e-3);
    // Outside the time span or far from the points: nothing.
    QVERIFY(!m.current(54.0, 8.0, t0.addSecs(-3600), set, drift));
    QVERIFY(!m.current(50.0, 3.0, t0, set, drift));
    // Stored with the voyage.
    const MarineForecast back = MarineForecast::fromJson(m.toJson());
    QVERIFY(back.current(54.0, 8.0, t0.addSecs(1800), set, drift));
    QVERIFY(qAbs(drift - 1.5) < 0.01);

    // A grid cell far from the point (point on land): not used there.
    m = MarineForecast::parse(marineJson(54.4), pts);
    QVERIFY(m.isValid());
    QVERIFY(!m.current(54.2, 8.5, t0, set, drift));
  }

  void rivers() {
    QCOMPARE(RiverNetwork::typicalSpeedKn("Rhein", "river"), 3.0);
    QCOMPARE(RiverNetwork::typicalSpeedKn("Elbe", "river"), 1.6);
    QCOMPARE(RiverNetwork::typicalSpeedKn("Havel", "river"), 0.3);
    QCOMPARE(RiverNetwork::typicalSpeedKn("Nord-Ostsee-Kanal", "canal"), 0.0);
    QCOMPARE(RiverNetwork::typicalSpeedKn("Unbekannt", "river"), 1.0);

    // Ways are drawn downstream: the Rhine flows north here.
    OsmElement rhine;
    rhine.type = "way";
    rhine.tags = {{"waterway", "river"}, {"name", "Rhein"}};
    rhine.geometry = {QPointF(7.60, 50.30), QPointF(7.60, 50.40)};
    OsmElement canal;
    canal.type = "way";
    canal.tags = {{"waterway", "canal"}, {"name", "Kanal"}};
    canal.geometry = {QPointF(8.0, 50.0), QPointF(8.1, 50.0)};
    OsmElement tidal;
    tidal.type = "way";
    tidal.tags = {{"waterway", "river"}, {"name", "Elbe"}, {"tidal", "yes"}};
    tidal.geometry = {QPointF(9.0, 53.5), QPointF(9.1, 53.6)};
    RiverNetwork net = RiverNetwork::fromOsm({rhine, canal, tidal});
    QCOMPARE(net.ways().size(), 3);
    double set, drift;
    QString name;
    QVERIFY(net.current(50.35, 7.6005, set, drift, &name));
    QCOMPARE(name, QString("Rhein"));
    QVERIFY(set < 1 || set > 359);
    QCOMPARE(drift, 3.0);
    QVERIFY(net.current(50.0, 8.05, set, drift));
    QCOMPARE(drift, 0.0);
    // Off the river: nothing; tidal reaches are left to the tide.
    QVERIFY(!net.current(50.35, 7.7, set, drift));
    QVERIFY(!net.current(53.55, 9.05, set, drift));
    // The skipper's speed, stored with the voyage.
    net.setSpeed("Rhein", 4.5);
    const RiverNetwork back = RiverNetwork::fromJson(net.toJson());
    QVERIFY(back.current(50.35, 7.6005, set, drift));
    QCOMPARE(drift, 4.5);
    QCOMPARE(back.summary().size(), 2);  // without the tidal reach
    QVERIFY(RiverNetwork::query(50, 7, 51, 8).contains("waterway"));

    // The model prefers the river.
    CurrentModel model;
    model.rivers = net;
    model.useStations = false;
    QVERIFY(model.current(50.35, 7.6005, QDateTime::currentDateTimeUtc(), set,
                          drift, &name));
    QCOMPARE(name, QString("Rhein"));
    model.mode = CurrentModel::Mode::Manual;
    model.manualSetDeg = 200;
    model.manualDriftKn = 1.2;
    QVERIFY(model.current(50.35, 7.6005, {}, set, drift));
    QCOMPARE(set, 200.0);
    model.mode = CurrentModel::Mode::Off;
    QVERIFY(!model.current(50.35, 7.6005, {}, set, drift));
    QVERIFY(!model.source());
  }

  void plannerWithCurrent() {
    const PassageSettings s = settings(270, 12);
    const PassagePlan still = PassagePlanner::plan(north(), s, {});
    QVERIFY(qAbs(still.hours - 2.0) < 0.02);
    QVERIFY(!still.currentUsed);
    // 1 kn with us: 6 kn over ground.
    const PassagePlan with =
        PassagePlanner::plan(north(), s, {}, constant(0, 1));
    QVERIFY(with.currentUsed);
    QVERIFY2(qAbs(with.hours - 10.0 / 6) < 0.02,
             qPrintable(QString::number(with.hours)));
    QVERIFY(with.currentGainH > 0.3);
    QVERIFY(qAbs(with.legs[0].sogKn - 6) < 0.01);
    // 1 kn against us.
    const PassagePlan against =
        PassagePlanner::plan(north(), s, {}, constant(180, 1));
    QVERIFY(qAbs(against.hours - 2.5) < 0.02);
    QVERIFY(against.currentGainH < -0.4);
    // Across: the boat steers into it, a little slower.
    const PassagePlan cross =
        PassagePlanner::plan(north(), s, {}, constant(90, 3));
    QVERIFY(qAbs(cross.hours - 2.5) < 0.02);
    QVERIFY(cross.legs[0].currentCrossKn > 2.9);
    // Strong current gets a warning.
    const PassagePlan strong =
        PassagePlanner::plan(north(), s, {}, constant(180, 3));
    QVERIFY(!strong.warnings.filter("current").isEmpty());

    // The window picks the departure with the tide: the current turns at
    // 10:00 from foul to fair.
    PassageSettings w = s;
    w.latestDeparture = w.departure.addSecs(4 * 3600);
    const QDateTime turn = w.departure.addSecs(2 * 3600);
    const CurrentSource tide = [turn](double, double, const QDateTime& t,
                                      double& set, double& drift) {
      set = t < turn ? 180 : 0;
      drift = 2;
      return true;
    };
    QVector<DepartureOption> opts;
    const PassagePlan best =
        PassagePlanner::planWindow(north(), w, {}, &opts, tide);
    QVERIFY(best.departure >= turn);
    QVERIFY(best.hours < 1.5);
  }

  void levelLimit() {
    PassageSettings s = settings(270, 12);
    s.latestDeparture = s.departure.addSecs(6 * 3600);
    // A tidal harbour: water from 11:00 on.
    const QDateTime high = s.departure.addSecs(3 * 3600);
    s.levelAt = [high](double, double, const QDateTime& t) {
      return t >= high ? 3.0 : 1.0;
    };
    s.minDepartureLevelM = 2.0;
    const PassagePlan p = PassagePlanner::planWindow(north(), s, {});
    QVERIFY(p.levelOk);
    QVERIFY(p.departure >= high);
    QCOMPARE(p.departureLevelM, 3.0);
    // A single departure too early: warning.
    PassageSettings one = s;
    one.latestDeparture = {};
    const PassagePlan early = PassagePlanner::plan(north(), one, {});
    QVERIFY(!early.levelOk);
    QVERIFY(!early.warnings.filter("level").isEmpty());
  }

  void sailPlanWithCurrent() {
    PassageSettings s = settings(0, 12);  // dead upwind
    s.strategy = PassageSettings::Strategy::Sail;
    s.minSailSpeedKn = 1;  // keep sailing against the tide
    s.noGoDeg = 45;
    s.runDeg = 150;
    const SailInventory inv;
    const SailingPlan still = SailPlanner::plan(north(), s, {}, inv, 2.0);
    const SailingPlan fair =
        SailPlanner::plan(north(), s, {}, inv, 2.0, {}, constant(0, 1.5));
    const SailingPlan foul =
        SailPlanner::plan(north(), s, {}, inv, 2.0, {}, constant(180, 1.0));
    QVERIFY(still.tacks > 0 && fair.tacks > 0 && foul.tacks > 0);
    QVERIFY(fair.currentUsed && !still.currentUsed);
    QVERIFY2(fair.hours < still.hours && still.hours < foul.hours,
             qPrintable(QString("%1 %2 %3")
                            .arg(fair.hours)
                            .arg(still.hours)
                            .arg(foul.hours)));
    // The boards still reach the destination.
    const QPointF end = fair.track.last();
    QVERIFY(geo::distanceNm(end.y(), end.x(), north().last().y(),
                            north().last().x()) < 0.2);
    // Over ground the boat goes faster with the current than through water.
    for (const SailBoard& b : fair.boards) {
      QVERIFY(b.currentDriftKn > 1.4);
      QVERIFY(b.sogKn > 0);
    }

    // Cross current on a reach: the heading points into the current, the
    // track stays on the rhumb line.
    PassageSettings reach = settings(270, 12);
    reach.strategy = PassageSettings::Strategy::Sail;
    const SailingPlan cr =
        SailPlanner::plan(north(), reach, {}, inv, 1.0, {}, constant(90, 1.5));
    QVERIFY(!cr.boards.isEmpty());
    const SailBoard& b = cr.boards.first();
    QVERIFY2(b.headingDeg > 300, qPrintable(QString::number(b.headingDeg)));
    QVERIFY(qAbs(b.cogDeg) < 1 || qAbs(b.cogDeg - 360) < 1);
    for (const QPointF& p : cr.track) QVERIFY(std::abs(p.x() - 10.0) < 0.002);
  }

  void tideStations() {
    const QString dir = QStringLiteral(OCPN_QT_SOURCE_DATA_DIR) + "/tcdata";
    QString err;
    auto data = TideDatabase::load(dir + "/HARMONICS_NO_US",
                                   dir + "/HARMONICS_NO_US.IDX", &err);
    QVERIFY2(data, qPrintable(err));
    TideDatabase::instance().setData(data);
    auto& db = TideDatabase::instance();
    // A subordinate current station with flood and ebb directions.
    const TideStation* sucia = nullptr;
    for (const auto& s : db.stations())
      if (s.name.startsWith("Sucia I., 0.5 mile WNW")) sucia = &s;
    QVERIFY(sucia);
    QCOMPARE(sucia->type, TideStation::Type::Current);
    QCOMPARE(sucia->floodDir, 25.0);
    QCOMPARE(sucia->ebbDir, 205.0);
    QCOMPARE(sucia->highMpy, 0.2);
    // Its current over a day: flood and ebb in the given directions.
    const QDateTime t0(QDate(2026, 6, 1), QTime(0, 0), Qt::UTC);
    bool flood = false, ebb = false;
    double maxKn = 0;
    for (int h = 0; h < 25; ++h) {
      double set, drift;
      if (!TidalCurrents::current(sucia->lat, sucia->lon, t0.addSecs(h * 3600),
                                  set, drift, 0.5))
        continue;
      if (qAbs(set - 25) < 1) flood = true;
      if (qAbs(set - 205) < 1) ebb = true;
      maxKn = std::max(maxKn, drift);
    }
    QVERIFY(flood && ebb);
    QVERIFY(maxKn > 0.1 && maxKn < 5);
    // No current stations in the German Bight.
    double set, drift;
    QVERIFY(!TidalCurrents::current(53.9, 8.7, t0, set, drift));

    // Water level at Cuxhaven from the tide station, with HW and LW.
    CurrentModel model;
    const LevelInfo li = model.level(53.87, 8.72, t0.addSecs(12 * 3600));
    QVERIFY(li.valid());
    QVERIFY(li.chartDatum);
    QVERIFY2(li.source.contains("Cuxhaven"), qPrintable(li.source));
    QVERIFY(li.levelM > -1 && li.levelM < 5);
    QVERIFY(li.extremes.size() >= 2);
    // Far from any station: no level without a marine forecast.
    QVERIFY(!model.level(40.0, -30.0, t0).valid());
  }

  void gauges() {
    const QByteArray json = R"([
      {"shortname":"KÖLN","longname":"KÖLN","km":688.0,
       "latitude":50.9369,"longitude":6.9629,
       "water":{"shortname":"RHEIN","longname":"RHEIN"},
       "timeseries":[{"shortname":"W","unit":"cm",
         "currentMeasurement":{"timestamp":"2026-10-25T10:00:00+01:00",
                               "value":312.0},
         "characteristicValues":[{"shortname":"MNW","value":139},
                                 {"shortname":"MW","value":299},
                                 {"shortname":"MHW","value":817}]}]},
      {"shortname":"BONN","longname":"BONN","km":654.8,
       "latitude":50.7366,"longitude":7.1069,
       "water":{"shortname":"RHEIN","longname":"RHEIN"},
       "timeseries":[{"shortname":"W","unit":"cm",
         "currentMeasurement":{"timestamp":"2026-10-25T10:00:00+01:00",
                               "value":280.0}}]}])";
    const QVector<Gauge> g = TideServices::parseGauges(json, 50.73, 7.10);
    QCOMPARE(g.size(), 2);
    QCOMPARE(g[0].name, QString("BONN"));  // nearest first
    QCOMPARE(g[1].water, QString("RHEIN"));
    QCOMPARE(g[1].valueCm, 312.0);
    QCOMPARE(g[1].mwCm, 299.0);
    QVERIFY(g[1].text().contains("312 cm"));
    QVERIFY(g[1].time.isValid());
    const Gauge back = Gauge::fromJson(g[1].toJson());
    QCOMPARE(back.mhwCm, 817.0);
    QVERIFY(std::isnan(Gauge::fromJson(g[0].toJson()).mwCm));
  }
};

QTEST_GUILESS_MAIN(CurrentsTest)
#include "test_currents.moc"
