// Smoke tests for the opencpn-qt user interface (run offscreen).
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QtTest>

#include "core/AppSettings.h"
#include "core/BoatConfig.h"
#include "core/GeoMath.h"
#include "core/NavStore.h"
#include "core/Shapefile.h"
#include "ui/ChartView.h"
#include "ui/DrawerPages.h"
#include "ui/MainWindow.h"
#include "core/osm/OnlineOsm.h"
#include "ui/OfflinePage.h"
#include "core/BoatSystems.h"
#include "ui/BoatPages.h"
#include "ui/SupplyPages.h"
#include "ui/LogView.h"
#include "core/Legs.h"
#include "core/Logbook.h"
#include <QPlainTextEdit>
#include "core/Provisions.h"
#include "core/Range.h"
#include "ui/BoatView.h"
#include "ui/Overlays.h"
#include "core/Automation.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSlider>
#include "ui/PassagePanel.h"
#include "ui/PolarPanel.h"
#include "ui/RoutePlanPanel.h"
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
    // No requests to public servers from the tests.
    OnlineOsm::instance().setEnabled(false);
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

  void planHarbourToHarbour() {
    auto* panel = m_win->findChild<RoutePlanPanel*>();
    QVERIFY(panel);
    QPushButton* plan = nullptr;
    QPushButton* save = nullptr;
    for (auto* b : panel->findChildren<QPushButton*>()) {
      if (b->text() == "Plan route") plan = b;
      if (b->text() == "Save and navigate") save = b;
    }
    QVERIFY(plan && save);
    QVERIFY(!plan->isEnabled());
    panel->setFrom(Place{"Kiel", "harbour", 54.33, 10.15, true});
    panel->setTo(Place{"Laboe", "harbour", 54.41, 10.22, true});
    QVERIFY(plan->isEnabled());
    const int before = int(NavStore::instance().routes().size());
    plan->click();
    QVERIFY(QTest::qWaitFor([save, panel] { return save->isVisibleTo(panel); },
                            20000));
    // Wind from ahead: the sail plan beats with tacks.
    auto* passage = panel->findChild<PassagePanel*>();
    QSpinBox* windDir = nullptr;
    for (auto* sb : passage->findChildren<QSpinBox*>())
      if (sb->suffix() == QStringLiteral("°")) windDir = sb;
    QVERIFY(windDir);
    const int oldDir = windDir->value();
    windDir->setValue(30);
    QVERIFY(QTest::qWaitFor(
        [passage] { return passage->sailingPlan().tacks > 0; }, 3000));
    const SailingPlan& sp = passage->sailingPlan();
    QVERIFY(!sp.boards.isEmpty());
    QVERIFY(!sp.boards.first().advice.summary().isEmpty());
    QTest::qWait(50);
    // Board rows: a tap shows the trim.
    ListRow* boardRow = nullptr;
    for (auto* r : passage->findChildren<ListRow*>("sailBoard"))
      if (!r->isHidden() && !boardRow) boardRow = r;
    QVERIFY(boardRow);
    auto* trim = boardRow->findChild<QLabel*>("trim");
    QVERIFY(trim && trim->isHidden());
    QTest::mouseClick(boardRow, Qt::LeftButton, {}, QPoint(20, 20));
    QVERIFY(!trim->isHidden());
    QVERIFY(trim->text().contains("Boom"));
    QVERIFY(!passage->findChildren<QLabel*>("sailTurn").isEmpty());
    if (const QString shot = qEnvironmentVariable("OCPN_QT_TEST_SAILPLAN_SHOT");
        !shot.isEmpty()) {
      m_win->drawer()->open(m_win->pageIndex("routes"));
      QTest::qWait(400);
      for (QWidget* w = boardRow->parentWidget(); w; w = w->parentWidget())
        if (auto* sa = qobject_cast<QScrollArea*>(w)) {
          sa->ensureWidgetVisible(boardRow, 0, 160);
          break;
        }
      QTest::qWait(200);
      m_win->grab().save(shot);
    }
    const int tacks = sp.tacks;
    save->click();
    windDir->setValue(oldDir);
    QCOMPARE(int(NavStore::instance().routes().size()), before + 1);
    const Route& r = NavStore::instance().routes().last();
    QCOMPARE(r.name, QStringLiteral("Kiel → Laboe"));
    QCOMPARE(r.points.first().name, QStringLiteral("Kiel"));
    QCOMPARE(r.points.last().name, QStringLiteral("Laboe"));
    // Tacks as waypoints, with time and sails.
    int tackPoints = 0;
    for (const Waypoint& w : r.points)
      if (w.name.startsWith("T") && w.name.size() <= 3) {
        ++tackPoints;
        QVERIFY(w.time.isValid());
        QVERIFY(w.description.contains("Tack"));
      }
    QCOMPARE(tackPoints, tacks);
    QCOMPARE(NavStore::instance().activeRouteId(), r.id);
    NavStore::instance().stopNavigation();
  }

  void passageWithModels() {
    auto* passage = m_win->findChild<PassagePanel*>();
    QVERIFY(passage);
    passage->setRoute({{10.0, 54.0}, {10.0, 54.2}});
    // Two models: headwind (ICON) and beam wind (GFS).
    const auto forecast = [](const QString& id, double from) {
      ModelForecast f;
      f.model = id;
      f.points = {{10.0, 54.0}, {10.0, 54.2}};
      const qint64 t0 = QDateTime::currentSecsSinceEpoch() - 3600;
      for (int h = 0; h < 120; ++h) f.times << t0 + h * 3600;
      for (int p = 0; p < 2; ++p) {
        f.speedKn << QVector<float>(120, 14);
        f.dirFrom << QVector<float>(120, float(from));
        f.gustKn << QVector<float>(120, 18);
      }
      f.fetched = QDateTime::currentDateTimeUtc();
      return f;
    };
    passage->setForecasts({{"icon_seamless", forecast("icon_seamless", 0)},
                           {"gfs_seamless", forecast("gfs_seamless", 270)}},
                          true);
    QTest::qWait(400);
    QCOMPARE(passage->source(), QStringLiteral("icon_seamless"));
    QVERIFY(passage->plan().tacks > 0);
    QVERIFY(passage->plan().gribUsed);
    auto* matrix = passage->findChild<ModelMatrix*>();
    QVERIFY(matrix);
    // ICON, GFS and manual wind.
    QVERIFY(matrix->height() >= 3 * 28);
    // State for stored voyages.
    const QJsonObject state = passage->saveState();
    QCOMPARE(state.value("source").toString(), QStringLiteral("icon_seamless"));
    passage->restoreState(state);
    passage->setRoute({});
  }

  void routesPageStableWhileNavigating() {
    // A position fix must not rebuild the routes page (it made the page and
    // the planner on it jump every second).
    Route r;
    r.name = "Stable";
    for (const auto& [lat, lon] :
         std::initializer_list<std::pair<double, double>>{{54.5, 10.3},
                                                          {54.6, 10.4}}) {
      Waypoint w;
      w.lat = lat;
      w.lon = lon;
      r.points << w;
    }
    const QString id = NavStore::instance().addRoute(r);
    NavStore::instance().activateRoute(id, 1);
    QTest::qWait(50);
    auto* page = m_win->findChild<RoutesPage*>();
    QVERIFY(page);
    const QList<ListRow*> rows = page->findChildren<ListRow*>();
    QVERIFY(!rows.isEmpty());
    QList<QPointer<ListRow>> watched;
    for (ListRow* row : rows) watched << row;
    OwnShip own = NavStore::instance().ownShip();
    for (int i = 0; i < 5; ++i) {
      own.lat += 0.001;
      own.lastFix = QDateTime::currentDateTimeUtc();
      NavStore::instance().setOwnShip(own);
      QTest::qWait(20);
    }
    for (const auto& row : watched) QVERIFY(row);
    // Stopping the navigation does rebuild it.
    NavStore::instance().stopNavigation();
    QTest::qWait(50);
    bool rebuilt = false;
    for (const auto& row : watched) rebuilt |= row.isNull();
    QVERIFY(rebuilt);
  }

  void boatView() {
    m_win->setView(MainWindow::View::Boat);
    auto* view = m_win->findChild<BoatView*>();
    QVERIFY(view && view->isVisible());
    auto* cabin = view->findChild<CabinLayout*>();
    QVERIFY(cabin && cabin->width() > 300);
    auto& sys = BoatSystems::instance();
    const auto isOn = [&sys](const QString& id) {
      for (const auto& l : sys.lights())
        if (l.id == id) return l.on;
      return false;
    };
    // Tapping a light in the layout switches it.
    const bool before = isOn("galley_spot");
    QTest::mouseClick(cabin, Qt::LeftButton, {},
                      cabin->lightPos("galley_spot").toPoint());
    QCOMPARE(isOn("galley_spot"), !before);
    // The night sailing button applies the scene.
    for (auto* b : view->findChildren<QPushButton*>())
      if (b->property("scene").toString() == "night") b->click();
    QCOMPARE(sys.activeScene(), QStringLiteral("night"));
    QTest::qWait(200);
    const QString shot = qEnvironmentVariable("OCPN_QT_TEST_BOAT_SHOT");
    if (!shot.isEmpty()) m_win->grab().save(shot);
    sys.applyScene("evening");
    m_win->setView(MainWindow::View::Chart);
  }

  void mainMenu() {
    // Main menu: settings first, then the navigation sections.
    auto* dock = m_win->findChild<BottomDock*>();
    QVERIFY(dock);
    QStringList labels;
    for (auto* b : dock->findChildren<DockButton*>()) labels << b->text();
    const QStringList main = {tr("Settings"), tr("Routes"),  tr("Marks"),
                              tr("Tracks"),   tr("AIS"),     tr("Tides"),
                              tr("Weather"),  tr("Supplies")};
    int last = -1;
    for (const QString& m : main) {
      const int i = int(labels.indexOf(m));
      QVERIFY2(i > last, qPrintable(m + " in " + labels.join(",")));
      last = i;
    }
    // Configuration pages are not in the main menu any more.
    for (const char* gone : {"Charts", "Offline", "Sails", "Data"})
      QVERIFY2(!labels.contains(gone), gone);

    // Settings opens the hub; its rows open sub-pages with a back button.
    SideDrawer* drawer = m_win->drawer();
    const int settings = m_win->pageIndex("settings");
    QCOMPARE(settings, 0);
    drawer->open(settings);
    SettingsHub* hub = nullptr;  // settings and supplies use hubs
    for (auto* h : m_win->findChildren<SettingsHub*>())
      if (h->isVisible()) hub = h;
    QVERIFY(hub);
    for (const char* key : {"general", "charts", "data", "offline", "sails",
                            "scenes", "automations"}) {
      const int page = m_win->pageIndex(key);
      QVERIFY2(page > 0, key);
      QCOMPARE(drawer->parentOf(page), settings);
      QCOMPARE(drawer->rootOf(page), settings);
    }
    auto* row = hub->findChild<ListRow*>("settings-download");
    QVERIFY(row);
    QTest::mouseClick(row, Qt::LeftButton, {}, row->rect().center());
    QCOMPARE(drawer->currentPage(), m_win->pageIndex("offline"));
    QVERIFY(m_win->findChild<OfflinePage*>()->isVisible());
    // The settings button stays highlighted on a sub-page.
    DockButton* settingsButton = nullptr;
    for (auto* b : dock->findChildren<DockButton*>())
      if (b->text() == tr("Settings")) settingsButton = b;
    QVERIFY(settingsButton && settingsButton->isChecked());
    // Back to the hub.
    IconButton* back = nullptr;
    for (auto* b : drawer->findChildren<IconButton*>())
      if (b->toolTip() == tr("Back")) back = b;
    QVERIFY(back && back->isVisible());
    back->click();
    QCOMPARE(drawer->currentPage(), settings);
    QVERIFY(!back->isVisible());
    // Plugins are appended after the main sections (media plugin, if
    // loaded) and pages by number still work.
    QCOMPARE(m_win->pageIndex("3"), 3);
    QCOMPARE(m_win->pageIndex("nonsense"), -1);
    drawer->close();
  }

  void lightSceneEditor() {
    auto& sys = BoatSystems::instance();
    sys.applyScene("off");
    m_win->drawer()->open(m_win->pageIndex("scenes"));
    LightScenesPage* page = m_win->scenesPage();
    QVERIFY(page && page->isVisible());
    QVERIFY(page->findChild<ListRow*>("scene-shower"));
    page->editScene({});
    SceneEditor* ed = page->editor();
    QVERIFY(ed->isVisible());
    ed->findChild<QLineEdit*>()->setText("Blue saloon");
    // Include the saloon indirect light: on, 40 %, blue.
    auto* row = ed->findChild<QWidget*>("sceneLight-saloon_indirect");
    QVERIFY(row);
    row->findChild<QCheckBox*>()->setChecked(true);
    row->findChild<ToggleSwitch*>()->setChecked(true);
    row->findChild<QSlider*>()->setValue(40);
    row->findChild<ColorButton*>()->setColor(QColor(40, 110, 255));
    // The preview switches the LED right away.
    const auto lightOf = [&sys](const QString& id) {
      for (const auto& l : sys.lights())
        if (l.id == id) return l;
      return BoatLight();
    };
    QVERIFY(lightOf("saloon_indirect").on);
    QCOMPARE(lightOf("saloon_indirect").color, QColor(40, 110, 255));
    // Leave the forward cabin out of the scene.
    ed->findChild<QWidget*>("sceneLight-fwd_ceiling")
        ->findChild<QCheckBox*>()
        ->setChecked(false);
    const LightScene s = ed->scene();
    QVERIFY(!s.find("fwd_ceiling"));
    if (const QString shot = qEnvironmentVariable("OCPN_QT_TEST_SCENE_SHOT");
        !shot.isEmpty()) {
      m_win->setView(MainWindow::View::Boat);
      QTest::qWait(200);
      m_win->grab().save(shot);
      m_win->setView(MainWindow::View::Chart);
    }
    QCOMPARE(s.find("saloon_indirect")->brightness, 0.4);
    const int before = int(sys.scenes().size());
    QPushButton* save = nullptr;
    for (auto* b : ed->findChildren<QPushButton*>())
      if (b->text() == tr("Save")) save = b;
    QVERIFY(save);
    save->click();
    QCOMPARE(int(sys.scenes().size()), before + 1);
    const LightScene& added = sys.scenes().last();
    QCOMPARE(added.name, QStringLiteral("Blue saloon"));
    QCOMPARE(sys.activeScene(), added.id);
    QVERIFY(!ed->isVisible());
    QTest::qWait(50);
    QVERIFY(page->findChild<ListRow*>("scene-" + added.id));
    // The boat view lists the new scene.
    auto* view = m_win->findChild<BoatView*>();
    bool listed = false;
    for (auto* b : view->findChildren<QPushButton*>())
      if (b->property("scene").toString() == added.id) listed = true;
    QVERIFY(listed);
    // Cancel restores the lights after a preview.
    const BoatLight galley = lightOf("galley_spot");
    page->editScene("night");
    QWidget* galleyRow = nullptr;  // the rows of the last edit are deleted
    for (auto* w : ed->findChildren<QWidget*>("sceneLight-galley_spot"))
      if (!w->isHidden()) galleyRow = w;
    QVERIFY(galleyRow);
    QCOMPARE(lightOf("galley_spot").color.green(), 28);  // night preview
    galleyRow->findChild<ColorButton*>()->setColor(QColor(0, 255, 0));
    QCOMPARE(lightOf("galley_spot").color, QColor(0, 255, 0));
    for (auto* b : ed->findChildren<QPushButton*>())
      if (b->text() == tr("Cancel")) b->click();
    QCOMPARE(lightOf("galley_spot").color, galley.color);
    QCOMPARE(lightOf("galley_spot").on, galley.on);
    sys.removeScene(added.id);
    m_win->drawer()->close();
  }

  void automationEditor() {
    auto& store = AutomationStore::instance();
    m_win->drawer()->open(m_win->pageIndex("automations"));
    AutomationsPage* page = m_win->automationsPage();
    QVERIFY(page && page->isVisible());
    QVERIFY(page->findChild<ListRow*>("automation-winter_shower"));
    // The winter shower opens with its five actions.
    page->editAutomation("winter_shower");
    AutomationEditor* ed = page->editor();
    QCOMPARE(ed->automation().actions.size(), 5);
    QCOMPARE(ed->automation().months, Automation::kWinter);
    if (const QString shot =
            qEnvironmentVariable("OCPN_QT_TEST_AUTOMATION_EDITOR_SHOT");
        !shot.isEmpty()) {
      QTest::qWait(200);
      m_win->grab().save(shot);
    }
    // A new automation at a time of day.
    page->editAutomation({});
    ed->findChild<QLineEdit*>()->setText("Evening heat");
    AutomationAction act;
    act.type = AutomationAction::Type::ZoneSetpoint;
    act.target = "forward";
    act.value = 19.5;
    ed->addAction(act);
    const Automation a = ed->automation();
    QCOMPARE(a.name, QStringLiteral("Evening heat"));
    QCOMPARE(a.actions.size(), 2);  // heater on (default) + forward cabin
    QCOMPARE(a.actions[1].type, AutomationAction::Type::ZoneSetpoint);
    QCOMPARE(a.actions[1].target, QStringLiteral("forward"));
    QCOMPARE(a.actions[1].value, 19.5);
    for (auto* b : ed->findChildren<QPushButton*>())
      if (b->text() == tr("Save")) b->click();
    const Automation* saved = nullptr;
    for (const auto& x : store.automations())
      if (x.name == "Evening heat") saved = &x;
    QVERIFY(saved);
    const QString id = saved->id;
    QTest::qWait(50);
    // Run from the list.
    auto* row = page->findChild<ListRow*>("automation-" + id);
    QVERIFY(row);
    row->findChild<IconButton*>("run")->click();
    auto& sys = BoatSystems::instance();
    QCOMPARE(sys.heater().setpointFor("forward"), 19.5);
    QVERIFY(sys.heater().running());
    // And from the boat view.
    m_win->drawer()->close();
    m_win->setView(MainWindow::View::Boat);
    QTest::qWait(50);  // rebuilt buttons are shown by the event loop
    auto* view = m_win->findChild<BoatView*>();
    auto* shower = view->findChild<QPushButton*>("automation-winter_shower");
    QVERIFY(shower && shower->isVisible());
    shower->click();
    QCOMPARE(sys.activeScene(), QStringLiteral("shower"));
    QVERIFY(sys.waterHeater().on);
    QCOMPARE(sys.heater().setpointFor("heads"), 23.0);
    QTest::qWait(200);
    const QString shot = qEnvironmentVariable("OCPN_QT_TEST_AUTOMATION_SHOT");
    if (!shot.isEmpty()) m_win->grab().save(shot);
    store.remove(id);
    store.run("leave_boat");
    sys.setZoneSetpoint("heads", std::numeric_limits<double>::quiet_NaN());
    sys.setZoneSetpoint("forward", std::numeric_limits<double>::quiet_NaN());
    sys.applyScene("evening");
    m_win->setView(MainWindow::View::Chart);
  }

  void supplies() {
    SideDrawer* drawer = m_win->drawer();
    const int hub = m_win->pageIndex("supplies");
    QVERIFY(hub > 0);
    QCOMPARE(drawer->parentOf(hub), -1);  // main menu
    for (const char* key : {"range", "provisions", "provision-check"})
      QCOMPARE(drawer->parentOf(m_win->pageIndex(key)), hub);

    // Range: a cold forecast shortens the diesel range.
    const QDate today = QDate::currentDate();
    QVector<DayForecast> mild, cold;
    for (int i = 0; i < 14; ++i) {
      mild << DayForecast{today.addDays(i), 15, 22, 10};
      cold << DayForecast{today.addDays(i), -2, 3, 4};
    }
    auto& fc = TemperatureForecast::instance();
    fc.setDays(mild, QPointF(10.3, 54.5), QDateTime::currentDateTimeUtc());
    drawer->open(m_win->pageIndex("range"));
    RangePage* page = m_win->rangePage();
    QVERIFY(page->isVisible());
    page->setMode(BoatMode::Anchor);
    const double dieselMild =
        page->result().resource(RangeResource::Diesel).days;
    fc.setDays(cold, QPointF(10.3, 54.5), QDateTime::currentDateTimeUtc());
    const RangeResult coldResult = page->result();
    QVERIFY2(
        coldResult.resource(RangeResource::Diesel).days < dieselMild,
        qPrintable(QStringLiteral("%1 vs %2")
                       .arg(coldResult.resource(RangeResource::Diesel).days)
                       .arg(dieselMild)));
    QVERIFY(coldResult.projection.first().forecast);
    for (int k = 0; k < 4; ++k) {
      auto* row = page->findChild<QWidget*>(QStringLiteral("range-%1").arg(k));
      QVERIFY(row);
      QVERIFY(!row->findChild<QLabel*>("days")->text().isEmpty());
    }
    // Underway: the engine.
    page->setMode(BoatMode::Underway);
    QVERIFY(page->result().rates.engineFuelLpd > 0);
    page->setWindow(RangeWindow::LastTrip);
    page->setWindow(RangeWindow::Days7);
    QTest::qWait(400);
    if (const QString shot = qEnvironmentVariable("OCPN_QT_TEST_RANGE_SHOT");
        !shot.isEmpty())
      m_win->grab().save(shot);
    page->setMode(BoatMode::Anchor);

    // Provisions: examples, filter, take one, edit.
    auto& store = ProvisionStore::instance();
    store.loadExamples(today);
    drawer->open(m_win->pageIndex("provisions"));
    ProvisionsPage* prov = m_win->provisionsPage();
    QVERIFY(prov->isVisible());
    QTest::qWait(50);
    const ProvisionItem* tuna = nullptr;
    for (const auto& i : store.items())
      if (i.name == "Tuna") tuna = &i;
    QVERIFY(tuna);
    const QString tunaId = tuna->id;
    const double before = tuna->quantity;
    prov->setCategory(int(ProvisionCategory::Cans));
    QTest::qWait(50);
    ListRow* row = nullptr;
    for (auto* r : prov->findChildren<ListRow*>("provision-" + tunaId))
      if (!r->isHidden()) row = r;
    QVERIFY(row);
    // Only cans are listed.
    for (auto* r : prov->findChildren<ListRow*>())
      if (!r->isHidden() && r->objectName().startsWith("provision-")) {
        const auto* item = store.find(r->objectName().mid(10));
        QVERIFY(item && item->category == ProvisionCategory::Cans);
      }
    row->findChild<IconButton*>("take")->click();
    QCOMPARE(store.find(tunaId)->quantity, before - 1);
    QCOMPARE(store.find(tunaId)->usage.size(), 1);
    // A new item through the editor.
    prov->editItem({});
    ProvisionEditor* ed = prov->editor();
    QVERIFY(ed->isVisible());
    ed->findChild<QLineEdit*>()->setText("Peaches");
    ProvisionItem peaches = ed->item();
    QCOMPARE(peaches.category, ProvisionCategory::Cans);
    QVERIFY(peaches.bestBefore.isValid());  // typical shelf life
    for (auto* b : ed->findChildren<QPushButton*>())
      if (b->text() == tr("Save")) b->click();
    bool found = false;
    for (const auto& i : store.items())
      if (i.name == "Peaches") found = i.category == ProvisionCategory::Cans;
    QVERIFY(found);
    QTest::qWait(50);
    if (const QString shot =
            qEnvironmentVariable("OCPN_QT_TEST_PROVISIONS_SHOT");
        !shot.isEmpty()) {
      prov->setCategory(-1);
      QTest::qWait(100);
      m_win->grab().save(shot);
    }

    // The check lists the problems and a shopping list.
    drawer->open(m_win->pageIndex("provision-check"));
    QTest::qWait(50);
    auto* check = m_win->findChild<ProvisionCheckPage*>();
    QVERIFY(check && check->isVisible());
    int issues = 0;
    for (auto* r : check->findChildren<ListRow*>("issue"))
      if (!r->isHidden()) ++issues;
    QCOMPARE(issues, int(store.check(today).size()));
    QVERIFY(issues > 0);
    QLabel* shopping = nullptr;
    for (auto* l : check->findChildren<QLabel*>("shopping"))
      if (!l->isHidden()) shopping = l;
    QVERIFY(shopping && shopping->text().contains("Bread rolls"));
    if (const QString shot = qEnvironmentVariable("OCPN_QT_TEST_CHECK_SHOT");
        !shot.isEmpty())
      m_win->grab().save(shot);
    drawer->close();

    // The boat view shows the range.
    m_win->setView(MainWindow::View::Boat);
    QVERIFY(m_win->findChild<BoatView*>()->findChild<RangeCard*>());
    if (const QString shot =
            qEnvironmentVariable("OCPN_QT_TEST_BOAT_RANGE_SHOT");
        !shot.isEmpty()) {
      QTest::qWait(400);
      m_win->grab().save(shot);
    }
    m_win->setView(MainWindow::View::Chart);
    store.clear();
  }

  void logView() {
    // The dock switches to the log view.
    auto* dock = m_win->findChild<BottomDock*>();
    DockButton* logButton = nullptr;
    for (auto* b : dock->findChildren<DockButton*>())
      if (b->text() == tr("Log")) logButton = b;
    QVERIFY(logButton);
    logButton->click();
    QCOMPARE(m_win->view(), MainWindow::View::Log);
    LogView* view = m_win->logView();
    QVERIFY(view->isVisible());
    QTest::qWait(100);

    // Overview: one row per leg; the newest is shown.
    const auto& legs = LegStore::instance().legs();
    QVERIFY(legs.size() >= 4);
    QCOMPARE(view->detail()->legId(), legs.first().id);
    const QString older = legs[1].id;
    ListRow* row = nullptr;
    for (auto* r : view->findChildren<ListRow*>("leg-" + older))
      if (!r->isHidden()) row = r;
    QVERIFY(row);
    QTest::mouseClick(row, Qt::LeftButton, {}, row->rect().center());
    QCOMPARE(view->detail()->legId(), older);
    // Charts: speed, heel, wind, energy, diesel, temperature …
    QStringList titles;
    for (TimeChart* c : view->detail()->charts()) {
      titles << c->title();
      QVERIFY2(c->pointCount(0) > 50, qPrintable(c->title()));
    }
    for (const QString& t : {tr("Speed"), tr("Heel"), tr("Wind"), tr("Energy"),
                             tr("Diesel"), tr("Outside temperature")})
      QVERIFY2(titles.contains(t), qPrintable(t));
    // Tapping the track moves the marker in the charts.
    TrackMap* map = view->detail()->map();
    const Leg* leg = LegStore::instance().leg(older);
    const int mid = int(leg->samples.size()) / 2;
    QSignalSpy picked(map, &TrackMap::timePicked);
    QTest::mouseClick(map, Qt::LeftButton, {}, map->screenPos(mid).toPoint());
    QCOMPARE(picked.size(), 1);
    const qint64 t = picked.first().first().toLongLong();
    QVERIFY(std::llabs(t - leg->samples[mid].t) < 30 * 60);
    // Hovering a chart moves the map marker (no crash, value box).
    TimeChart* speed = view->detail()->charts().first();
    QTest::mouseMove(speed, QPoint(speed->width() / 2, speed->height() / 2));
    QTest::qWait(150);
    if (const QString shot = qEnvironmentVariable("OCPN_QT_TEST_LEGS_SHOT");
        !shot.isEmpty())
      m_win->grab().save(shot);

    // Logbook for the day of the leg.
    QPushButton* toBook = nullptr;
    for (auto* b : view->detail()->findChildren<QPushButton*>())
      if (b->text() == tr("Logbook")) toBook = b;
    QVERIFY(toBook);
    toBook->click();
    QVERIFY(view->logbookShown());
    LogbookEditor* ed = view->editor();
    const QDate day = leg->start().toLocalTime().date();
    QCOMPARE(ed->date(), day);
    ed->insertWeather();
    QVERIFY(ed->weather()->text().contains("kn"));
    ed->crew()->setText("Sven, Anna");
    ed->report()->setPlainText("Reefed early, a fine day under sail.");
    ed->flush();
    QCOMPARE(Logbook::instance().entry(day).report,
             QStringLiteral("Reefed early, a fine day under sail."));
    QCOMPARE(Logbook::instance().entry(day).crew, QStringLiteral("Sven, Anna"));
    QTest::qWait(100);
    if (const QString shot = qEnvironmentVariable("OCPN_QT_TEST_LOGBOOK_SHOT");
        !shot.isEmpty())
      m_win->grab().save(shot);
    // Another day from the list, then back: the text is still there.
    ListRow* today = nullptr;
    for (auto* r : view->findChildren<ListRow*>(
             "day-" + QDate::currentDate().toString(Qt::ISODate)))
      if (!r->isHidden()) today = r;
    QVERIFY(today);
    QTest::mouseClick(today, Qt::LeftButton, {}, today->rect().center());
    QCOMPARE(ed->date(), QDate::currentDate());
    QVERIFY(ed->report()->toPlainText().isEmpty() ||
            day == QDate::currentDate());
    view->showLogbook(day);
    QCOMPARE(ed->report()->toPlainText(),
             QStringLiteral("Reefed early, a fine day under sail."));
    // The day's leg opens from the logbook.
    QTest::qWait(50);  // new buttons are shown by the event loop
    QPushButton* dayLeg = nullptr;
    for (auto* b : ed->findChildren<QPushButton*>("dayLeg"))
      if (!b->isHidden()) dayLeg = b;
    QVERIFY(dayLeg);
    dayLeg->click();
    QVERIFY(!view->logbookShown());
    QCOMPARE(view->detail()->legId(), older);
    m_win->setView(MainWindow::View::Chart);
  }

  void polarEditorRoundTrip() {
    Polar p = Polar::generic(7.5);
    PolarEditor editor(p);
    Polar out;
    QString err;
    QVERIFY2(editor.result(&out, &err), qPrintable(err));
    QCOMPARE(out.tws, p.tws);
    QCOMPARE(out.twa, p.twa);
    QVERIFY(std::fabs(out.speedAt(12, 90) - p.speedAt(12, 90)) < 0.01);
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
