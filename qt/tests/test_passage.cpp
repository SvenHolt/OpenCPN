// Passage planner, harbour list and logbook of the Qt6 frontend (offscreen).
#include <QPainter>
#include <QtTest>

#include <SQLiteCpp/SQLiteCpp.h>

#include "core/NavStore.h"
#include "core/Passage.h"
#include "core/TrackRecorder.h"
#include "ui/ChartView.h"
#include "ui/PassagePage.h"
#include "ui/Theme.h"

namespace {
/** 0.2 x 0.2 degree of water with an island, as stored by tools/seachart. */
void writeNavGrid(const QString& db) {
  SQLite::Database d(db.toStdString(), SQLite::OPEN_READWRITE);
  const int n = 41;
  std::vector<float> g(n * n, 12.0f);
  for (int y = 15; y <= 25; ++y)
    for (int x = 15; x <= 25; ++x) g[y * n + x] = -1;
  SQLite::Statement q(d,
                      "INSERT INTO navgrid (lon0, lat0, dlon, dlat, nx, ny,"
                      " data) VALUES (10, 54, 0.005, 0.005, ?, ?, ?)");
  q.bind(1, n);
  q.bind(2, n);
  q.bind(3, g.data(), int(g.size() * sizeof(float)));
  q.exec();
  // steady 12 kn from the north for two days
  const qint64 t0 = QDateTime::currentSecsSinceEpoch() / 3600 * 3600 - 3600;
  for (double lat : {53.9, 54.3})
    for (double lon : {9.9, 10.3}) {
      for (int h = 0; h < 48; ++h) {
        SQLite::Statement w(d,
                            "INSERT INTO weather VALUES (?,?,?,12,0,16,0.5,0)");
        w.bind(1, lon);
        w.bind(2, lat);
        w.bind(3, (int64_t)(t0 + h * 3600));
        w.exec();
      }
      SQLite::Statement m(d, "INSERT INTO weather_meta VALUES (?,?,?)");
      m.bind(1, lon);
      m.bind(2, lat);
      m.bind(3, (int64_t)t0);
      m.exec();
    }
}
}  // namespace

class PassageTest : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       m_dir.path() + "/settings");
    // never touch the user's real database
    QSettings().setValue("passage/database", m_dir.path() + "/seachart.db");
    Theme::instance().apply();
    auto& svc = PassageService::instance();
    QCOMPARE(svc.databasePath(), m_dir.path() + "/seachart.db");
    QVERIFY(!svc.isOpen());  // not created just by starting
    QVERIFY(svc.ensureOpen());
    writeNavGrid(svc.databasePath());
    QVERIFY(svc.openDatabase(svc.databasePath()));
  }

  void harbours() {
    auto& svc = PassageService::instance();
    seachart::Harbour west, east;
    west.kind = "marina";
    west.name = "Westhafen";
    west.lat = 54.1;
    west.lon = 10.01;
    east.kind = "anchorage";
    east.name = "Ostbucht";
    east.lat = 54.1;
    east.lon = 10.19;
    east.notes = "Sand, 4 m";
    QVERIFY(svc.saveHarbour(west));
    QVERIFY(svc.saveHarbour(east));
    QCOMPARE(int(svc.harbours().size()), 2);
    QVERIFY(svc.nearest(54.1005, 10.1905));
    QCOMPARE(svc.nearest(54.1005, 10.1905)->name, std::string("Ostbucht"));
    QVERIFY(!svc.nearest(54.0, 10.1, 0.5));
    m_west = west.id;
    m_east = east.id;
  }

  void planAroundTheIsland() {
    auto& svc = PassageService::instance();
    QSignalSpy spy(&svc, &PassageService::planChanged);
    const auto p = svc.plan(m_west, m_east, QDateTime::currentDateTime());
    QVERIFY2(p.ok, p.error.c_str());
    QCOMPARE(spy.count(), 1);
    QVERIFY(p.grid_checked);
    QVERIFY(p.used_weather);
    QVERIFY(p.distance_nm > 5.4);  // longer than the direct 5.3 NM
    QVERIFY(svc.weatherSource().contains("Offline forecast"));
    // engine only on the Sails page: everything under engine
    seachart::BoatModel b = svc.boat();
    QVERIFY(svc.effectiveBoat().hull_speed_kn > 0);
    Q_UNUSED(b);
  }

  void routeAndPassage() {
    auto& svc = PassageService::instance();
    auto& nav = NavStore::instance();
    svc.plan(m_west, m_east, QDateTime::currentDateTime());
    const QString id = svc.createRoute();
    QVERIFY(!id.isEmpty());
    const Route* r = nav.route(id);
    QVERIFY(r);
    QCOMPARE(r->points.first().name, QString("Westhafen"));
    QCOMPARE(r->points.last().name, QString("Ostbucht"));
    QCOMPARE(svc.createRoute(), id);  // not created twice

    QVERIFY(svc.startPassage());
    QVERIFY(svc.underway());
    QCOMPARE(nav.activeRouteId(), id);
    QVERIFY(TrackRecorder::instance().isRecording());
    const qint64 trip = svc.stopPassage();
    QVERIFY(trip > 0);
    QVERIFY(!svc.underway());
    QVERIFY(!TrackRecorder::instance().isRecording());
    seachart::Trip t;
    QVERIFY(svc.logbook().GetTrip(trip, t));
    QVERIFY(t.ended >= t.started);
    QCOMPARE(t.from, std::string("Westhafen"));
    const auto entries = svc.logbook().Entries(trip);
    QVERIFY(entries.size() >= 2);  // departure and arrival
    const QString report = svc.tripReport(trip);
    QVERIFY(report.contains("Westhafen – Ostbucht"));
    QVERIFY(report.contains("Arrival at Ostbucht"));
  }

  void pageShowsData() {
    ChartView chart;
    chart.resize(800, 600);
    PassagePage page(&chart);
    page.resize(420, 800);
    page.show();
    QVERIFY(QTest::qWaitForWindowExposed(&page));
    page.showView(PassagePage::Harbours);
    page.harbours()->edit(m_east);
    page.showView(PassagePage::Logbook);
    // the layer paints harbours and the plan without crashing
    PassageLayer layer;
    QImage img(800, 600, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&img);
    layer.paint(p, chart);
  }

private:
  QTemporaryDir m_dir;
  qint64 m_west = 0, m_east = 0;
};

QTEST_MAIN(PassageTest)
#include "test_passage.moc"
