// Polars, weather model forecasts, passage planning with them and voyages
// stored for offline use.
#include <QSignalSpy>
#include <QtTest>
#include <cmath>

#include "core/GeoMath.h"
#include "core/PassagePlanner.h"
#include "core/Polar.h"
#include "core/Voyage.h"
#include "core/WeatherModels.h"

namespace {
const char* kGrid =
    "TWA\\TWS\t6\t10\t16\t25\n"
    "40\t3.5\t4.8\t5.6\t5.9\n"
    "52\t4.4\t5.8\t6.5\t6.8\n"
    "90\t5.2\t6.6\t7.3\t7.8\n"
    "150\t4.2\t5.9\t7.0\t8.1\n"
    "180\t3.6\t5.1\t6.4\t7.4\n";

/** A constant wind field for a model forecast. */
ModelForecast constantForecast(const QString& model, double kn, double from,
                               const QDateTime& start, int hours) {
  ModelForecast f;
  f.model = model;
  f.points = {{10.0, 54.0}, {10.0, 54.5}};
  for (int h = 0; h <= hours; ++h)
    f.times << start.addSecs(h * 3600).toSecsSinceEpoch();
  for (int p = 0; p < f.points.size(); ++p) {
    f.speedKn << QVector<float>(f.times.size(), float(kn));
    f.dirFrom << QVector<float>(f.times.size(), float(from));
    f.gustKn << QVector<float>(f.times.size(), float(kn * 1.3));
  }
  f.fetched = start;
  return f;
}
}  // namespace

class PlanningTest : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("opencpn-qt-tests");
    QStandardPaths::setTestModeEnabled(true);
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .removeRecursively();
  }

  void polarGrid() {
    Polar p;
    QString err;
    QVERIFY2(p.parse(kGrid, &err), qPrintable(err));
    QCOMPARE(p.tws, (QVector<double>{6, 10, 16, 25}));
    QCOMPARE(p.twa.size(), 5);
    QCOMPARE(p.speedAt(10, 90), 6.6);
    // Bilinear between 6 and 10 kn, 52° and 90°.
    QVERIFY(std::fabs(p.speedAt(8, 90) - 5.9) < 1e-9);
    QVERIFY(std::fabs(p.speedAt(10, 71) - (5.8 + 6.6) / 2) < 1e-9);
    // Symmetric, below the table to zero, above constant.
    QCOMPARE(p.speedAt(10, -90), p.speedAt(10, 90));
    QCOMPARE(p.speedAt(10, 270), p.speedAt(10, 90));
    QVERIFY(std::fabs(p.speedAt(3, 90) - 2.6) < 1e-9);
    QCOMPARE(p.speedAt(40, 90), 7.8);
    QCOMPARE(p.speedAt(10, 30), 0.0);  // inside the no-go zone
    QCOMPARE(p.maxSpeed(), 8.1);
    // Best VMG: within the table, upwind between 40 and 60°, downwind
    // between 140 and 180°.
    const double up = p.bestUpwindTwa(10), down = p.bestDownwindTwa(10);
    QVERIFY2(up >= 40 && up <= 60, qPrintable(QString::number(up)));
    QVERIFY2(down >= 140 && down <= 180, qPrintable(QString::number(down)));
    // Round trip through the text format.
    Polar q;
    QVERIFY(q.parse(p.toText(), &err));
    QCOMPARE(q.tws, p.tws);
    QCOMPARE(q.speed, p.speed);
  }

  void polarFormats() {
    // European CSV with decimal commas and unsorted angles.
    Polar csv;
    QString err;
    QVERIFY2(csv.parse("twa/tws;6;12\n90;5,2;6,9\n45;3,9;5,5\n", &err),
             qPrintable(err));
    QCOMPARE(csv.twa, (QVector<double>{45, 90}));
    QCOMPARE(csv.speedAt(12, 90), 6.9);
    // Expedition: one line per wind speed with angle/speed pairs.
    Polar exp;
    QVERIFY2(exp.parse("# Expedition\n"
                       "6 45 3.9 90 5.2 150 4.0\n"
                       "12 42 5.4 90 6.9 150 6.2\n",
                       &err),
             qPrintable(err));
    QCOMPARE(exp.tws, (QVector<double>{6, 12}));
    QCOMPARE(exp.twa, (QVector<double>{42, 45, 90, 150}));
    QCOMPARE(exp.speedAt(12, 90), 6.9);
    QCOMPARE(exp.speedAt(6, 42), 0.0);  // outside the 6 kn line's angles
    // Errors.
    Polar bad;
    QVERIFY(!bad.parse("hello\n", &err));
    QVERIFY(!bad.parse("TWA\\TWS\t10\t6\n90\t5\t4\n", &err));  // not ascending
    QVERIFY(err.contains("increase"));
    QVERIFY(!bad.parse("TWA\\TWS\t6\t10\n90\tx\t4\n", &err));
    // Generic polar: plausible shape.
    const Polar g = Polar::generic(7);
    QVERIFY(g.isValid());
    QVERIFY(g.speedAt(12, 90) > g.speedAt(12, 45));
    QVERIFY(g.speedAt(20, 90) <= 7 * 1.1 + 1e-9);
  }

  void polarStore() {
    auto& store = PolarStore::instance();
    QSignalSpy spy(&store, &PolarStore::changed);
    QVERIFY(!store.isCustom());
    Polar p;
    QVERIFY(p.parse(kGrid));
    p.name = "Test 34";
    store.setPolar(p);
    QCOMPARE(spy.size(), 1);
    QVERIFY(store.isCustom());
    QCOMPARE(store.polar().name, QStringLiteral("Test 34"));
    store.reset(6.5);
    QVERIFY(!store.isCustom());
  }

  void passageWithPolar() {
    // A polar with 5 kn at every angle from 50° and no speed closer.
    Polar flat;
    QVERIFY(flat.parse("TWA\\TWS\t4\t30\n50\t5\t5\n90\t5\t5\n180\t5\t5\n"));
    const QVector<QPointF> route{{10.0, 54.0}, {10.0, 54.0 + 10.0 / 60}};
    PassageSettings s;
    s.departure = QDateTime(QDate(2026, 6, 21), QTime(8, 0), Qt::UTC);
    s.polar = std::make_shared<Polar>(flat);
    s.manualWindFromDeg = 270;
    s.manualWindKn = 12;
    const PassagePlan reach = PassagePlanner::plan(route, s, {});
    QVERIFY(std::fabs(reach.hours - 2.0) < 0.01);  // 10 nm at 5 kn
    QCOMPARE(reach.tacks, 0);
    // Upwind: the polar's best angle (50°) decides the boards.
    s.manualWindFromDeg = 0;
    const PassagePlan up = PassagePlanner::plan(route, s, {});
    QCOMPARE(up.legs[0].mode, PassageLeg::Mode::Beat);
    QVERIFY(std::fabs(up.sailedNm - 10 / std::cos(50 * M_PI / 180)) < 0.1);
  }

  void modelForecastParse() {
    const QVector<QPointF> pts{{10.2, 54.4}, {10.4, 54.5}};
    // Two locations, two models (suffixed keys), one with a gap.
    const QByteArray json = R"([
      {"hourly": {"time": [1792281600, 1792285200],
        "wind_speed_10m_icon_seamless": [10, 14],
        "wind_direction_10m_icon_seamless": [270, 280],
        "wind_gusts_10m_icon_seamless": [15, 20],
        "wind_speed_10m_gfs_seamless": [null, null],
        "wind_direction_10m_gfs_seamless": [null, null],
        "wind_gusts_10m_gfs_seamless": [null, null]}},
      {"hourly": {"time": [1792281600, 1792285200],
        "wind_speed_10m_icon_seamless": [12, 16],
        "wind_direction_10m_icon_seamless": [270, 280],
        "wind_gusts_10m_icon_seamless": [17, 22],
        "wind_speed_10m_gfs_seamless": [null, null],
        "wind_direction_10m_gfs_seamless": [null, null],
        "wind_gusts_10m_gfs_seamless": [null, null]}}])";
    QString err;
    const auto f = WeatherModels::parse(
        json, pts, {"icon_seamless", "gfs_seamless"}, &err);
    QVERIFY2(err.isEmpty(), qPrintable(err));
    QCOMPARE(f.size(), 1);  // GFS without data is dropped
    const ModelForecast& icon = f.value("icon_seamless");
    QCOMPARE(icon.times.size(), 2);
    double kn = 0, from = 0, gust = 0;
    // At the first point, half way in time.
    QVERIFY(icon.wind(54.4, 10.2,
                      QDateTime::fromSecsSinceEpoch(1792283400, Qt::UTC), kn,
                      from, &gust));
    QVERIFY(std::fabs(kn - 12) < 0.2);
    // Vector mean: the stronger wind (280°) weighs more.
    QVERIFY(from > 275 && from < 278);
    QVERIFY(std::fabs(gust - 17.5) < 0.6);
    // Outside the forecast.
    QVERIFY(!icon.wind(54.4, 10.2,
                       QDateTime::fromSecsSinceEpoch(1792300000, Qt::UTC), kn,
                       from));
    // JSON round trip (stored voyages).
    const ModelForecast back = ModelForecast::fromJson(icon.toJson());
    QCOMPARE(back.times, icon.times);
    QCOMPARE(back.points, icon.points);
    QCOMPARE(back.speedKn, icon.speedKn);
    // Single location answers are an object with unsuffixed keys.
    const auto one = WeatherModels::parse(
        R"({"hourly": {"time": [1, 3601], "wind_speed_10m": [5, 6],
            "wind_direction_10m": [90, 90], "wind_gusts_10m": [7, 8]}})",
        {{10, 54}}, {"icon_seamless"}, &err);
    QCOMPARE(one.size(), 1);
  }

  void samplePoints() {
    const QVector<QPointF> route{{10.0, 54.0}, {10.0, 54.5}, {10.5, 54.5}};
    const auto pts = WeatherModels::samplePoints(route, 8);
    QVERIFY(pts.size() >= 6 && pts.size() <= 40);
    QCOMPARE(pts.first(), QPointF(10.0, 54.0));
    QCOMPARE(pts.last(), QPointF(10.5, 54.5));
    // Long passages are limited to 40 points.
    const auto many = WeatherModels::samplePoints({{0, 50}, {20, 60}}, 1);
    QVERIFY(many.size() <= 41);
  }

  void modelsDifferInPlans() {
    // Two models: headwind vs. beam wind give different plans.
    const QDateTime t0(QDate(2026, 6, 21), QTime(6, 0), Qt::UTC);
    const ModelForecast a = constantForecast("a", 14, 0, t0, 48);
    const ModelForecast b = constantForecast("b", 14, 270, t0, 48);
    const QVector<QPointF> route{{10.0, 54.0}, {10.0, 54.2}};
    PassageSettings s;
    s.departure = t0;
    const auto windOf = [](const ModelForecast& f) -> WindSource {
      return [f](double lat, double lon, const QDateTime& t, double& kn,
                 double& from) { return f.wind(lat, lon, t, kn, from); };
    };
    const PassagePlan pa = PassagePlanner::plan(route, s, windOf(a));
    const PassagePlan pb = PassagePlanner::plan(route, s, windOf(b));
    QVERIFY(pa.gribUsed && pb.gribUsed && !pa.manualUsed);
    QVERIFY(pa.tacks > 0);
    QCOMPARE(pb.tacks, 0);
    QVERIFY(pa.hours > pb.hours);
  }

  void corridor() {
    const QVector<QPointF> route{{10.1, 54.3}, {10.3, 54.5}, {11.0, 54.6}};
    const auto boxes = VoyageStore::corridor(route, 3, 0.2);
    QVERIFY(boxes.size() >= 2);
    for (const GeoBox& b : boxes) QVERIFY(b.areaDeg2() <= 0.2 + 1e-9);
    // Every point of the route is covered with its margin.
    for (int i = 1; i < route.size(); ++i)
      for (double t = 0; t <= 1; t += 0.05) {
        const QPointF p = route[i - 1] + (route[i] - route[i - 1]) * t;
        bool in = false;
        for (const GeoBox& b : boxes)
          in |= p.y() >= b.south + 2.9 / 60 && p.y() <= b.north - 2.9 / 60 &&
                p.x() >= b.west && p.x() <= b.east;
        QVERIFY2(in, qPrintable(QStringLiteral("%1 %2").arg(p.x()).arg(p.y())));
      }
  }

  void voyageStore() {
    auto& store = VoyageStore::instance();
    QSignalSpy spy(&store, &VoyageStore::changed);
    Voyage v;
    v.name = "Kiel → Laboe";
    v.fromName = "Kiel";
    v.toName = "Laboe";
    v.route = {{10.15, 54.33}, {10.22, 54.40}};
    v.settings = QJsonObject{{"window", 3}};
    const QDateTime t0 = QDateTime::currentDateTimeUtc();
    v.forecasts.insert("icon_seamless",
                       constantForecast("icon_seamless", 12, 270, t0, 24));
    const QString id = store.save(v);
    QVERIFY(!id.isEmpty());
    QCOMPARE(spy.size(), 1);
    const Voyage* stored = store.voyage(id);
    QVERIFY(stored);
    QVERIFY(stored->created.isValid());
    QVERIFY(std::fabs(stored->lengthNm() -
                      geo::distanceNm(54.33, 10.15, 54.40, 10.22)) < 1e-9);
    // On disk, readable without network.
    QFile f(store.folder(id) + "/voyage.json");
    QVERIFY(f.open(QIODevice::ReadOnly));
    const Voyage back =
        Voyage::fromJson(QJsonDocument::fromJson(f.readAll()).object());
    QCOMPARE(back.name, v.name);
    QCOMPARE(back.route, v.route);
    QCOMPARE(back.settings.value("window").toInt(), 3);
    QVERIFY(back.forecasts.value("icon_seamless").isValid());
    store.remove(id);
    QVERIFY(!store.voyage(id));
    QVERIFY(!QFileInfo::exists(store.folder(id) + "/voyage.json"));
  }

  /** Real forecasts from Open-Meteo. */
  void fetchModels() {
    if (qEnvironmentVariable("OCPN_QT_NETWORK_TESTS") != "1")
      QSKIP("set OCPN_QT_NETWORK_TESTS=1 to load weather models");
    QHash<QString, ModelForecast> result;
    QString error;
    bool done = false;
    const QVector<QPointF> pts =
        WeatherModels::samplePoints({{10.15, 54.33}, {10.6, 54.6}});
    QStringList models;
    for (const auto& m : WeatherModels::available()) models << m.id;
    WeatherModels::instance().fetch(
        pts, models,
        [&](const QHash<QString, ModelForecast>& f, const QString& e) {
          result = f;
          error = e;
          done = true;
        });
    QVERIFY(QTest::qWaitFor([&] { return done; }, 60000));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(result.size() >= 3);
    for (const auto& f : result) {
      double kn, from;
      QVERIFY(f.wind(54.4, 10.3, QDateTime::currentDateTimeUtc().addSecs(7200),
                     kn, from));
      QVERIFY(kn >= 0 && kn < 100);
      qInfo("%s: %.1f kn from %.0f°", qPrintable(f.model), kn, from);
    }
  }
};

QTEST_GUILESS_MAIN(PlanningTest)
#include "test_planning.moc"
