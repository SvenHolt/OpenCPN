// Smoke tests for the opencpn-qt user interface (run offscreen).
#include <QtTest>

#include "core/AppSettings.h"
#include "core/GeoMath.h"
#include "core/NavStore.h"
#include "core/Shapefile.h"
#include "ui/ChartView.h"
#include "ui/MainWindow.h"
#include "ui/SideDrawer.h"
#include "ui/Theme.h"

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
