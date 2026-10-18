#include "ui/MainWindow.h"

#include <QKeyEvent>
#include <QMenu>

#include "core/AppSettings.h"
#include "core/NavStore.h"
#include "ui/DrawerPages.h"
#include "ui/Icons.h"
#include "ui/Overlays.h"
#include "ui/SideDrawer.h"
#include "ui/Theme.h"
#include "ui/TidesPage.h"
#include "ui/TracksPage.h"
#include "ui/WeatherPage.h"

namespace {
constexpr int kGap = 16;  // distance of floating panels to the edges
constexpr int kShadow = GlassPanel::kShadow;
}  // namespace

MainWindow::MainWindow(std::shared_ptr<const LandData> land, QWidget* parent)
    : QWidget(parent) {
  setWindowTitle(tr("OpenCPN"));
  setMinimumSize(960, 640);

  m_chart = new ChartView(this);
  m_chart->setLandData(std::move(land));
  m_instruments = new InstrumentCard(this);
  m_guidance = new GuidanceCard(this);
  m_controls = new MapControls(m_chart, this);
  m_target = new TargetCard(this);
  m_routeBar = new RouteEditBar(this);
  m_toast = new Toast(this);
  m_drawer = new SideDrawer(this);
  m_dock = new BottomDock(this);
  m_instrumentsAnim.setTargetObject(m_instruments);
  m_instrumentsAnim.setPropertyName("pos");
  m_instrumentsAnim.setDuration(260);
  m_instrumentsAnim.setEasingCurve(QEasingCurve::OutCubic);

  auto* routes = new RoutesPage(m_chart);
  auto* ais = new AisPage(m_chart);
  addSection("chart", tr("Charts"), new ChartsPage(m_chart));
  m_routesSection = addSection("route", tr("Routes"), routes);
  addSection("pin", tr("Marks"), new MarksPage(m_chart));
  addSection("flag", tr("Tracks"), new TracksPage(m_chart));
  m_chart->addLayer(new TrackLayer(this));
  addSection("ship", tr("AIS"), ais);
  addSection("tide", tr("Tides"), new TidesPage(m_chart));
  m_chart->addLayer(new TideLayer(this));
  addSection("wind", tr("Weather"), new WeatherPage(m_chart));
  m_chart->addLayer(new GribLayer(this));
  addOverlay(new WeatherTimeBar, Qt::BottomLeftCorner);
  addSection("signal", tr("Data"), new ConnectionsPage);
  addSection("sliders", tr("Settings"), new SettingsPage);

  connect(m_dock, &BottomDock::sectionRequested, m_drawer, &SideDrawer::toggle);
  connect(m_dock, &BottomDock::mobRequested, this,
          [] { NavStore::instance().manOverboard(); });
  connect(m_drawer, &SideDrawer::pageChanged, this, [this](int page) {
    m_dock->setActiveSection(page);
    layoutOverlays(true);
  });
  connect(routes, &RoutesPage::newRouteRequested, this,
          &MainWindow::beginRouteCreate);
  connect(ais, &AisPage::targetSelected, this, &MainWindow::selectAisTarget);
  connect(m_chart, &ChartView::aisTargetClicked, this,
          &MainWindow::selectAisTarget);
  connect(m_target, &TargetCard::closed, this,
          [this] { m_chart->setSelectedAis(0); });
  connect(m_chart, &ChartView::contextRequested, this,
          &MainWindow::showContextMenu);
  connect(m_chart, &ChartView::routeEditChanged, m_routeBar,
          &RouteEditBar::setPointCount);
  connect(m_routeBar, &RouteEditBar::undo, m_chart, &ChartView::undoRoutePoint);
  connect(m_routeBar, &RouteEditBar::cancel, this,
          [this] { endRouteCreate(false); });
  connect(m_routeBar, &RouteEditBar::done, this,
          [this] { endRouteCreate(true); });
  connect(&NavStore::instance(), &NavStore::alarm, this,
          [this](const QString& msg) {
            m_toast->showMessage(msg);
            layoutOverlays();
          });
  connect(&NavStore::instance(), &NavStore::guidanceChanged, this,
          [this] { layoutOverlays(); });

  layoutOverlays();
}

MainWindow::~MainWindow() = default;

int MainWindow::addSection(const QString& icon, const QString& title,
                           QWidget* page) {
  const int index = m_drawer->addPage(title, page);
  const int dockIndex = m_dock->addSection(icon, title);
  Q_ASSERT(index == dockIndex);
  Q_UNUSED(dockIndex);
  return index;
}

void MainWindow::addOverlay(QWidget* widget, Qt::Corner corner) {
  // Widgets that were explicitly hidden stay hidden (they show themselves
  // when they have something to display).
  const bool keepHidden =
      widget->testAttribute(Qt::WA_WState_ExplicitShowHide) &&
      widget->isHidden();
  widget->setParent(this);
  m_overlays.push_back({widget, corner});
  widget->installEventFilter(this);
  widget->setVisible(!keepHidden);
  layoutOverlays();
}

void MainWindow::addMapLayer(MapLayer* layer) { m_chart->addLayer(layer); }

void MainWindow::showMessage(const QString& text, bool alarm) {
  m_toast->showMessage(text, alarm);
  layoutOverlays();
}

void MainWindow::openSection(int section) { m_drawer->open(section); }

NavStore& MainWindow::navigation() const { return NavStore::instance(); }

bool MainWindow::eventFilter(QObject* o, QEvent* e) {
  // Re-layout when an overlay is shown, hidden or resized.
  if (e->type() == QEvent::Show || e->type() == QEvent::Hide ||
      e->type() == QEvent::LayoutRequest)
    QMetaObject::invokeMethod(
        this, [this] { layoutOverlays(); }, Qt::QueuedConnection);
  return QWidget::eventFilter(o, e);
}

void MainWindow::selectAisTarget(quint32 mmsi) {
  m_chart->setSelectedAis(mmsi);
  m_target->setTarget(mmsi);
  layoutOverlays();
}

void MainWindow::resizeEvent(QResizeEvent*) { layoutOverlays(); }

void MainWindow::layoutOverlays(bool animate) {
  const int w = width(), h = height();
  const int dockH = BottomDock::kHeight;
  const int mapH = h - dockH;
  m_dock->setGeometry(0, mapH, w, dockH);
  m_chart->setGeometry(0, 0, w, mapH);

  m_drawer->place(
      QRect(kGap - kShadow, kGap - kShadow, 0, mapH - 2 * kGap + 2 * kShadow));
  const bool drawerOpen = m_drawer->isOpen();
  const int left = drawerOpen ? SideDrawer::kWidth + kGap : 0;
  m_chart->setInsets(left, 0);

  m_instruments->resize(m_instruments->sizeHint());
  const QPoint instPos(left + kGap - kShadow, kGap - kShadow);
  if (animate) {
    m_instrumentsAnim.stop();
    m_instrumentsAnim.setStartValue(m_instruments->pos());
    m_instrumentsAnim.setEndValue(instPos);
    m_instrumentsAnim.start();
  } else if (m_instrumentsAnim.state() != QAbstractAnimation::Running) {
    m_instruments->move(instPos);
  }

  // Top center of the free area: guidance, then route editing toolbar.
  const int freeX = left + 300 + kGap;  // right of the instrument card
  const int freeW = w - freeX - 90;
  int y = kGap - kShadow;
  m_guidance->resize(m_guidance->sizeHint());
  m_guidance->move(freeX + std::max(0, (freeW - m_guidance->width()) / 2), y);
  if (m_guidance->isVisible()) y += m_guidance->height() - kShadow;
  m_routeBar->adjustSize();
  m_routeBar->move(freeX + std::max(0, (freeW - m_routeBar->width()) / 2), y);
  if (m_routeBar->isVisible()) y += m_routeBar->height() - kShadow;
  m_toast->move(std::min(freeX + std::max(0, (freeW - m_toast->width()) / 2),
                         w - m_toast->width()),
                y);

  m_controls->adjustSize();
  m_controls->move(w - m_controls->width() - kGap,
                   (mapH - m_controls->height()) / 2);
  m_target->resize(m_target->sizeHint());
  m_target->move(w - m_target->width() - kGap + kShadow,
                 mapH - m_target->height() - kGap + kShadow);

  // Plugin overlays, stacked from their corner.
  int bottomLeft = mapH - kGap + kShadow;
  int bottomRight =
      m_target->isVisible() ? m_target->y() + kShadow : mapH - kGap + kShadow;
  int topRight = kGap - kShadow;
  for (const auto& [widget, corner] : m_overlays) {
    if (!widget->isVisible()) continue;
    widget->resize(widget->sizeHint());
    switch (corner) {
      case Qt::BottomLeftCorner:
        bottomLeft -= widget->height();
        widget->move(left + kGap - kShadow, bottomLeft);
        bottomLeft += kShadow;
        break;
      case Qt::TopRightCorner:
        widget->move(w - widget->width() - kGap + kShadow - 70, topRight);
        topRight += widget->height() - kShadow;
        break;
      default:
        bottomRight -= widget->height();
        widget->move(w - widget->width() - kGap + kShadow, bottomRight);
        bottomRight += kShadow;
    }
  }
  const bool leftStack = bottomLeft < mapH - kGap + kShadow;
  m_chart->setInsets(left, leftStack ? mapH - bottomLeft : 0);

  m_chart->lower();
  m_instruments->raise();
  m_guidance->raise();
  m_controls->raise();
  m_target->raise();
  for (const auto& o : m_overlays) o.first->raise();
  m_drawer->raise();
  m_routeBar->raise();
  m_toast->raise();
  m_dock->raise();
}

void MainWindow::showContextMenu(double lat, double lon, const QPoint& global) {
  if (m_chart->mode() == ChartView::Mode::RouteCreate) return;
  const Palette& pal = Theme::p();
  QMenu menu(this);
  menu.addAction(Icons::icon("pin", pal.text, 18), tr("Drop mark here"),
                 [lat, lon] {
                   Waypoint w;
                   w.lat = lat;
                   w.lon = lon;
                   NavStore::instance().addWaypoint(w);
                 });
  menu.addAction(
      Icons::icon("navigate", pal.text, 18), tr("Navigate here"), [lat, lon] {
        NavStore::instance().goTo(QObject::tr("Destination"), lat, lon);
      });
  menu.addAction(Icons::icon("locate", pal.text, 18), tr("Center chart here"),
                 [this, lat, lon] { m_chart->centerOn(lat, lon); });
  menu.addSeparator();
  menu.addAction(Icons::icon("route", pal.text, 18), tr("New route…"), this,
                 &MainWindow::beginRouteCreate);
  menu.exec(global);
}

void MainWindow::beginRouteCreate() {
  m_drawer->close();
  m_chart->startRouteCreate();
  m_routeBar->setPointCount(0);
  m_routeBar->show();
  layoutOverlays();
}

void MainWindow::endRouteCreate(bool save) {
  const QVector<Waypoint> pts = m_chart->finishRouteCreate();
  m_routeBar->hide();
  if (save && pts.size() >= 2) {
    Route r;
    r.points = pts;
    NavStore::instance().addRoute(r);
    m_drawer->open(m_routesSection);
  }
  layoutOverlays();
}

void MainWindow::keyPressEvent(QKeyEvent* e) {
  switch (e->key()) {
    case Qt::Key_Plus:
    case Qt::Key_Equal:
      m_chart->zoomBy(2.0);
      break;
    case Qt::Key_Minus:
      m_chart->zoomBy(0.5);
      break;
    case Qt::Key_F:
      m_chart->setFollow(true);
      break;
    case Qt::Key_F11:
      setWindowState(windowState() ^ Qt::WindowFullScreen);
      break;
    case Qt::Key_Escape:
      if (m_chart->mode() == ChartView::Mode::RouteCreate)
        endRouteCreate(false);
      else
        m_drawer->close();
      break;
    case Qt::Key_Backspace:
      if (m_chart->mode() == ChartView::Mode::RouteCreate)
        m_chart->undoRoutePoint();
      break;
    default:
      QWidget::keyPressEvent(e);
  }
}
