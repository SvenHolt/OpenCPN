// Smoke tests for the opencpn-qt user interface (run offscreen).
#include <QtTest>

#include "core/AppSettings.h"
#include "core/BoatConfig.h"
#include "core/GeoMath.h"
#include "core/NavStore.h"
#include "core/Shapefile.h"
#include "ui/ChartView.h"
#include "ui/MainWindow.h"
#include "ui/OfflinePage.h"
#include "ui/SideDrawer.h"
#include "ui/Theme.h"
#include "core/Nmea.h"

class UiTest : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .removeRecursively();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(
        QSettings::IniFormat, QSettings::UserScope,
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    Theme::instance().apply();
    m_win = new MainWindow(std::make_shared<LandData>());
    m_win->resize(1400, 900);
    m_win->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_win));

    OwnShip own;
    own.valid = true;
    own.lat = 54.5;
    own.lon = 10.3;
    own.sog = 6;
    own.cog = own.hdg = 30;
    own.lastFix = QDateTime::currentDateTimeUtc();
    NavStore::instance().setOwnShip(own);
    emit NavStore::instance().ownShipChanged();
  }

  void cleanupTestCase() { delete m_win; }

  void createRouteByClicking() {
    ChartView* chart = m_win->chart();
    const int before = int(NavStore::instance().routes().size());
    chart->startRouteCreate();
    QCOMPARE(chart->mode(), ChartView::Mode::RouteCreate);
    for (QPoint p : {QPoint(500, 500), QPoint(700, 400), QPoint(900, 450)})
      QTest::mouseClick(chart, Qt::LeftButton, {}, p);
    QCOMPARE(chart->pendingRoutePoints(), 3);
    chart->undoRoutePoint();
    QCOMPARE(chart->pendingRoutePoints(), 2);
    const auto pts = chart->finishRouteCreate();
    QCOMPARE(pts.size(), 2);
    Route r;
    r.points = pts;
    const QString id = NavStore::instance().addRoute(r);
    QCOMPARE(int(NavStore::instance().routes().size()), before + 1);

    NavStore::instance().activateRoute(id, 1);
    QVERIFY(NavStore::instance().guidance().active);
    QVERIFY(NavStore::instance().guidance().dtwNm > 0);
  }

  void areaSelectionByDragging() {
    ChartView* chart = m_win->chart();
    chart->centerOn(54.5, 10.3, false);
    chart->setScale(2.0e5);
    chart->startAreaSelect();
    QCOMPARE(chart->mode(), ChartView::Mode::AreaSelect);
    const GeoBox start = chart->selectedArea();
    QVERIFY(start.isValid());
    const GeoBox visible = chart->visibleArea();
    QVERIFY(start.south > visible.south && start.north < visible.north);

    const auto drag = [chart](QPointF from, QPointF to) {
      const auto send = [chart](QEvent::Type t, QPointF pos,
                                Qt::MouseButtons buttons) {
        QMouseEvent e(t, pos, chart->mapToGlobal(pos), Qt::LeftButton, buttons,
                      Qt::NoModifier);
        QCoreApplication::sendEvent(chart, &e);
      };
      send(QEvent::MouseButtonPress, from, Qt::LeftButton);
      for (int i = 1; i <= 10; ++i)
        send(QEvent::MouseMove, from + (to - from) * (i / 10.0),
             Qt::LeftButton);
      send(QEvent::MouseButtonRelease, to, Qt::NoButton);
    };
    // South east corner handle resizes two edges.
    const QPointF se = chart->screenPos(start.south, start.east);
    drag(se, se + QPointF(80, 60));
    GeoBox b = chart->selectedArea();
    QVERIFY(b.east > start.east);
    QVERIFY(b.south < start.south);
    QCOMPARE(b.north, start.north);
    QCOMPARE(b.west, start.west);

    // Dragging inside moves the frame without resizing it.
    const QPointF mid =
        chart->screenPos((b.north + b.south) / 2, (b.west + b.east) / 2);
    drag(mid + QPointF(30, 0), mid + QPointF(-70, 0));
    const GeoBox moved = chart->selectedArea();
    QVERIFY(moved.west < b.west);
    QVERIFY(std::fabs((moved.east - moved.west) - (b.east - b.west)) < 1e-9);

    // Dragging outside pans the chart; the frame stays at its place.
    double lat0, lon0;
    chart->center(lat0, lon0);
    drag(QPointF(1300, 100), QPointF(1200, 150));
    double lat1, lon1;
    chart->center(lat1, lon1);
    QVERIFY(lon1 > lon0);
    QCOMPARE(chart->selectedArea().west, moved.west);

    // Edges cannot be dragged across each other.
    const QPointF n =
        chart->screenPos(moved.north, (moved.west + moved.east) / 2);
    drag(n, n + QPointF(0, 2000));
    QVERIFY(chart->selectedArea().north > chart->selectedArea().south);

    QTest::qWait(300);
    const QString shot = qEnvironmentVariable("OCPN_QT_TEST_AREA_SHOT");
    if (!shot.isEmpty()) m_win->grab().save(shot);
    chart->finishAreaSelect();
    QCOMPARE(chart->mode(), ChartView::Mode::Browse);
  }

  void poiClassification() {
    const auto kind = [](std::initializer_list<std::pair<QString, QString>> t) {
      OsmElement e;
      for (const auto& [k, v] : t) e.tags.insert(k, v);
      return OsmPoiLayer::classify(e);
    };
    using K = OsmPoiLayer::Kind;
    QCOMPARE(kind({{"leisure", "marina"}}), K::Marina);
    QCOMPARE(kind({{"seamark:type", "harbour"},
                   {"seamark:harbour:category", "marina"}}),
             K::Marina);
    QCOMPARE(kind({{"seamark:type", "harbour"}}), K::Harbour);
    QCOMPARE(kind({{"waterway", "fuel"}}), K::Fuel);
    QCOMPARE(kind({{"leisure", "slipway"}}), K::Slipway);
    QCOMPARE(kind({{"seamark:type", "anchorage"}}), K::Anchorage);
    QCOMPARE(kind({{"seamark:type", "buoy_lateral"}}), K::Seamark);
    QCOMPARE(kind({{"name", "x"}}), K::Other);
    QVERIFY(OsmPoiLayer::queryFor(GeoBox{54, 10, 54.1, 10.2})
                .contains("[bbox:54.000000,10.000000,54.100000,10.200000]"));
  }

  void screenToGeoRoundTrip() {
    ChartView* chart = m_win->chart();
    double lat, lon;
    chart->geoPos(QPointF(321, 234), lat, lon);
    const QPointF back = chart->screenPos(lat, lon);
    QVERIFY(qAbs(back.x() - 321) < 0.01);
    QVERIFY(qAbs(back.y() - 234) < 0.01);
  }

  void courseUpRotatesChart() {
    auto& s = AppSettings::instance();
    s.courseUp = true;
    s.notifyChanged();
    m_win->chart()->setFollow(true);
    for (int i = 0; i < 40; ++i) {
      emit NavStore::instance().ownShipChanged();
      QTest::qWait(30);
    }
    QVERIFY(m_win->chart()->rotation() > 5);
  }

  void cockpitWithEngine() {
    // Wind and engine data as an NMEA source would deliver them.
    NmeaParser parser;
    connect(&parser, &NmeaParser::navUpdate, &NavStore::instance(),
            &NavStore::applyNavUpdate);
    for (const char* s :
         {"WIMWV,320.0,R,18.5,N,A", "VWVHW,,T,,M,6.1,N,11.3,K",
          "SDDPT,12.4,0.5", "ERRPM,E,1,2350,,A",
          "ERXDR,C,84.0,C,ENGINE#0,P,3.6,B,ENGOIL#0,V,63,P,FUEL#0,U,14.1,V,"
          "BATTERY#0,G,1250.4,H,ENGINEHOURS#0"})
      QVERIFY(parser.parseLine(NmeaParser::makeSentence(s)));
    const auto& env = NavStore::instance().environment();
    QVERIFY(env.hasWind());
    QCOMPARE(env.awa, -40.0);
    QCOMPARE(env.aws, 18.5);
    QCOMPARE(env.stw, 6.1);
    QCOMPARE(env.depth, 12.9);
    // True wind derived from apparent wind and STW, from further aft.
    QVERIFY(env.tws < 18.5 && env.tws > 10);
    QVERIFY(env.twa < -40 && env.twa > -90);
    const auto& eng = NavStore::instance().engine();
    QVERIFY(eng.running());
    QCOMPARE(eng.rpm, 2350.0);
    QCOMPARE(eng.coolantC, 84.0);
    QCOMPARE(eng.oilBar, 3.6);
    QCOMPARE(eng.fuelPct, 63.0);
    QCOMPARE(eng.voltage, 14.1);

    SailConfig s;
    s.propulsion = SailConfig::Propulsion::MotorSailing;
    s.main = SailConfig::Main::Reef1;
    s.head = SailConfig::Head::Genoa;
    s.headFurlPct = 70;
    BoatConfig::instance().setSails(s);
    QCOMPARE(BoatConfig::instance().sails().summary(),
             QString("Main 1st reef · Genoa 70% · Engine"));
    m_win->setView(MainWindow::View::Cockpit);
    QTest::qWait(600);
    const QString shot = qEnvironmentVariable("OCPN_QT_TEST_COCKPIT_SHOT");
    if (!shot.isEmpty()) m_win->grab().save(shot);

    // Running downwind under spinnaker with the engine off: the engine
    // panel disappears.
    QVERIFY(parser.parseLine(NmeaParser::makeSentence("ERRPM,E,1,0,,A")));
    QVERIFY(
        parser.parseLine(NmeaParser::makeSentence("WIMWV,150.0,R,11.0,N,A")));
    QVERIFY(!NavStore::instance().engine().running());
    s = SailConfig();
    s.main = SailConfig::Main::Full;
    s.head = SailConfig::Head::None;
    s.downwind = SailConfig::Downwind::Spinnaker;
    BoatConfig::instance().setSails(s);
    QTest::qWait(1500);
    const QString shot2 = qEnvironmentVariable("OCPN_QT_TEST_COCKPIT_SHOT2");
    if (!shot2.isEmpty()) m_win->grab().save(shot2);
    m_win->setView(MainWindow::View::Chart);
  }

  void drawerAndMob() {
    m_win->drawer()->open(2);
    QVERIFY(m_win->drawer()->isOpen());
    const int marks = int(NavStore::instance().waypoints().size());
    NavStore::instance().manOverboard();
    QCOMPARE(int(NavStore::instance().waypoints().size()), marks + 1);
    QVERIFY(NavStore::instance().guidance().active);
    QTest::qWait(400);
    const QString shot = qEnvironmentVariable("OCPN_QT_TEST_SCREENSHOT");
    if (!shot.isEmpty()) m_win->grab().save(shot);
    m_win->drawer()->close();
  }

private:
  MainWindow* m_win = nullptr;
};

QTEST_MAIN(UiTest)
#include "test_ui.moc"
