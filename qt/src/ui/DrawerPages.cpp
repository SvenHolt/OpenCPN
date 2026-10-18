#include "ui/DrawerPages.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#ifdef OCPN_QT_HAVE_SERIAL
#include <QSerialPortInfo>
#endif

#include "core/AppSettings.h"
#include "core/Connections.h"
#include "core/GeoMath.h"
#include "core/GpxIO.h"
#include "core/charts/ChartDatabase.h"
#include "core/NavStore.h"
#include "ui/ChartView.h"
#include "ui/GlassPanel.h"
#include "ui/Icons.h"
#include "ui/Overlays.h"
#include "ui/Theme.h"

namespace {
QLabel* label(const QString& text, const char* role = nullptr,
              QWidget* parent = nullptr) {
  auto* l = new QLabel(text, parent);
  if (role) l->setProperty("role", role);
  return l;
}

QLabel* iconLabel(const QString& icon, const QColor& color, int size = 22) {
  auto* l = new QLabel;
  l->setFixedSize(size + 4, size + 4);
  l->setAlignment(Qt::AlignCenter);
  l->setPixmap(Icons::pixmap(icon, color, size));
  return l;
}

QPushButton* button(const QString& text, const QString& icon,
                    const char* role = nullptr) {
  auto* b = new QPushButton(text);
  b->setCursor(Qt::PointingHandCursor);
  if (role) b->setProperty("role", role);
  const QColor c = role && QByteArray(role) == "primary" ? QColor(Qt::white)
                                                         : Theme::p().text;
  if (!icon.isEmpty()) b->setIcon(Icons::icon(icon, c, 18));
  b->setIconSize(QSize(18, 18));
  return b;
}

QString degText(double v) {
  if (std::isnan(v)) return QStringLiteral("–");
  return QStringLiteral("%1°").arg(int(std::lround(v)) % 360, 3, 10,
                                   QChar('0'));
}

/** Title + subtitle column used in list rows. */
QWidget* textColumn(const QString& title, const QString& subtitle) {
  auto* w = new QWidget;
  auto* l = new QVBoxLayout(w);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(2);
  auto* t = label(title, "value");
  t->setTextFormat(Qt::PlainText);
  l->addWidget(t);
  if (!subtitle.isEmpty()) {
    auto* s = label(subtitle, "dim");
    s->setWordWrap(true);
    l->addWidget(s);
  }
  return w;
}

/** Horizontal "label ...... control" settings row. */
QWidget* settingRow(const QString& text, QWidget* control,
                    const QString& hint = {}) {
  auto* w = new QWidget;
  auto* l = new QHBoxLayout(w);
  l->setContentsMargins(4, 6, 4, 6);
  l->addWidget(textColumn(text, hint), 1);
  l->addWidget(control);
  return w;
}
}  // namespace

// --------------------------------------------------------------------------

ListRow::ListRow(QWidget* parent) : QWidget(parent) {
  setCursor(Qt::PointingHandCursor);
  setMinimumHeight(60);
}

void ListRow::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  QColor bg = underMouse() ? pal.panelPressed : pal.panelHover;
  p.setPen(Qt::NoPen);
  p.setBrush(bg);
  p.drawRoundedRect(QRectF(rect()), 16, 16);
  if (m_highlight.isValid()) {
    p.setBrush(m_highlight);
    p.drawRoundedRect(QRectF(0, 12, 4, height() - 24), 2, 2);
  }
}

void ListRow::mouseReleaseEvent(QMouseEvent* e) {
  if (rect().contains(e->position().toPoint())) emit clicked();
}

ListPage::ListPage(QWidget* parent) : QWidget(parent) {
  auto* l = new QVBoxLayout(this);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(12);
  m_actions = new QVBoxLayout;
  m_actions->setSpacing(8);
  l->addLayout(m_actions);
  auto* scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* content = new QWidget;
  content->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  auto* cl = new QVBoxLayout(content);
  cl->setContentsMargins(0, 0, 8, 0);
  cl->setSpacing(8);
  m_rows = new QVBoxLayout;
  m_rows->setSpacing(8);
  cl->addLayout(m_rows);
  m_empty = label(QString(), "dim");
  m_empty->setWordWrap(true);
  m_empty->setAlignment(Qt::AlignCenter);
  m_empty->setContentsMargins(20, 40, 20, 40);
  cl->addWidget(m_empty);
  cl->addStretch(1);
  scroll->setWidget(content);
  l->addWidget(scroll, 1);
}

void ListPage::clearRows() {
  // Rows may be cleared from a click handler of one of their children, so
  // defer the deletion.
  while (QLayoutItem* it = m_rows->takeAt(0)) {
    if (QWidget* w = it->widget()) {
      if (!w->property("persistent").toBool()) {
        w->hide();
        w->deleteLater();
      }
    }
    delete it;
  }
  m_empty->show();
}

void ListPage::addRow(QWidget* row) {
  m_rows->addWidget(row);
  m_empty->hide();
}

void ListPage::setEmptyText(const QString& text) { m_empty->setText(text); }

// --------------------------------------------------------------------------

RoutesPage::RoutesPage(ChartView* chart, QWidget* parent)
    : ListPage(parent), m_chart(chart) {
  auto* add = button(tr("New route"), "plus", "primary");
  connect(add, &QPushButton::clicked, this, &RoutesPage::newRouteRequested);
  actions()->addWidget(add);
  auto* io = new QHBoxLayout;
  auto* imp = button(tr("Import GPX"), "import");
  auto* exp = button(tr("Export GPX"), "export");
  connect(imp, &QPushButton::clicked, this, &RoutesPage::importGpx);
  connect(exp, &QPushButton::clicked, this, &RoutesPage::exportGpx);
  io->addWidget(imp);
  io->addWidget(exp);
  actions()->addLayout(io);
  setEmptyText(
      tr("No routes yet.\nCreate one by tapping points on the "
         "chart, or import a GPX file."));

  auto& store = NavStore::instance();
  connect(&store, &NavStore::routesChanged, this, &RoutesPage::rebuild);
  connect(&store, &NavStore::guidanceChanged, this, &RoutesPage::rebuild);
  connect(&AppSettings::instance(), &AppSettings::changed, this,
          &RoutesPage::rebuild);
  connect(&Theme::instance(), &Theme::changed, this, &RoutesPage::rebuild);
  rebuild();
}

void RoutesPage::rebuild() {
  clearRows();
  const auto& store = NavStore::instance();
  const Palette& pal = Theme::p();
  for (const Route& r : store.routes()) {
    const bool active =
        store.activeRouteId() == r.id && store.guidance().active;
    auto* row = new ListRow;
    if (active) row->setHighlight(pal.accentBright);
    auto* l = new QHBoxLayout(row);
    l->setContentsMargins(14, 10, 10, 10);
    l->addWidget(iconLabel("route", active ? pal.accentBright : pal.textDim));
    l->addWidget(
        textColumn(r.name,
                   tr("%n point(s)", nullptr, int(r.points.size())) + " · " +
                       AppSettings::instance().distanceText(r.lengthNm())),
        1);
    const QString id = r.id;
    auto* nav = new IconButton(active ? "stop" : "navigate",
                               active ? tr("Stop") : tr("Navigate"));
    connect(nav, &QAbstractButton::clicked, this, [id, active] {
      if (active)
        NavStore::instance().stopNavigation();
      else
        NavStore::instance().activateRoute(id);
    });
    auto* vis =
        new IconButton(r.visible ? "eye" : "eye-off", tr("Show on chart"));
    connect(vis, &QAbstractButton::clicked, this, [id] {
      if (const Route* rr = NavStore::instance().route(id)) {
        Route copy = *rr;
        copy.visible = !copy.visible;
        NavStore::instance().updateRoute(copy);
      }
    });
    auto* del = new IconButton("trash", tr("Delete"), nullptr, true);
    connect(del, &QAbstractButton::clicked, this, [this, id] {
      const Route* rr = NavStore::instance().route(id);
      if (!rr) return;
      if (QMessageBox::question(this, tr("Delete route"),
                                tr("Delete route \"%1\"?").arg(rr->name)) ==
          QMessageBox::Yes)
        NavStore::instance().removeRoute(id);
    });
    l->addWidget(nav);
    l->addWidget(vis);
    l->addWidget(del);
    connect(row, &ListRow::clicked, this, [this, id] {
      const Route* rr = NavStore::instance().route(id);
      if (!rr || rr->points.isEmpty()) return;
      double a = 90, b = 180, c = -90, d = -180;
      for (const auto& p : rr->points) {
        a = std::min(a, p.lat);
        b = std::min(b, p.lon);
        c = std::max(c, p.lat);
        d = std::max(d, p.lon);
      }
      m_chart->fitGeoBounds(a, b, c, d);
    });
    addRow(row);
  }
}

void RoutesPage::importGpx() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Import GPX"),
      QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
      tr("GPX files (*.gpx *.xml);;All files (*)"));
  if (path.isEmpty()) return;
  GpxData d;
  QString err;
  if (!GpxIO::read(path, d, &err)) {
    QMessageBox::warning(this, tr("Import failed"), err);
    return;
  }
  auto& store = NavStore::instance();
  for (const auto& w : d.waypoints) store.addWaypoint(w);
  for (auto r : d.routes) store.addRoute(r);
  for (auto t : d.tracks) {
    t.visible = true;
    if (t.name.isEmpty()) t.name = tr("Track");
    store.addRoute(t);
  }
}

void RoutesPage::exportGpx() {
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Export GPX"),
      QStandardPaths::writableLocation(QStandardPaths::HomeLocation) +
          "/opencpn-export.gpx",
      tr("GPX files (*.gpx)"));
  if (path.isEmpty()) return;
  GpxData d;
  d.waypoints = NavStore::instance().waypoints();
  d.routes = NavStore::instance().routes();
  if (!GpxIO::write(path, d))
    QMessageBox::warning(this, tr("Export failed"),
                         tr("Could not write %1").arg(path));
}

// --------------------------------------------------------------------------

MarksPage::MarksPage(ChartView* chart, QWidget* parent)
    : ListPage(parent), m_chart(chart) {
  auto* add = button(tr("Drop mark at boat"), "pin", "primary");
  connect(add, &QPushButton::clicked, this, [] {
    const auto& own = NavStore::instance().ownShip();
    if (!own.valid) return;
    Waypoint w;
    w.lat = own.lat;
    w.lon = own.lon;
    NavStore::instance().addWaypoint(w);
  });
  actions()->addWidget(add);
  setEmptyText(
      tr("No marks.\nRight-click the chart to drop "
         "a mark anywhere."));
  connect(&NavStore::instance(), &NavStore::waypointsChanged, this,
          &MarksPage::rebuild);
  connect(&Theme::instance(), &Theme::changed, this, &MarksPage::rebuild);
  // Distances change while moving.
  m_refresh.setInterval(5000);
  connect(&m_refresh, &QTimer::timeout, this, [this] {
    if (isVisible()) rebuild();
  });
  m_refresh.start();
  rebuild();
}

void MarksPage::rebuild() {
  clearRows();
  const auto& store = NavStore::instance();
  const auto& own = store.ownShip();
  const Palette& pal = Theme::p();
  for (const Waypoint& w : store.waypoints()) {
    auto* row = new ListRow;
    auto* l = new QHBoxLayout(row);
    l->setContentsMargins(14, 10, 10, 10);
    const bool mob = w.symbol == "mob";
    l->addWidget(
        iconLabel(mob ? "lifebuoy" : "pin", mob ? pal.danger : pal.warning));
    QString sub = geo::formatLat(w.lat) + "  " + geo::formatLon(w.lon);
    if (own.valid)
      sub = AppSettings::instance().distanceText(
                geo::distanceNm(own.lat, own.lon, w.lat, w.lon)) +
            " · " + degText(geo::bearingDeg(own.lat, own.lon, w.lat, w.lon));
    l->addWidget(textColumn(w.name, sub), 1);
    const QString id = w.id, name = w.name;
    const double lat = w.lat, lon = w.lon;
    auto* go = new IconButton("navigate", tr("Go to"));
    connect(go, &QAbstractButton::clicked, this,
            [name, lat, lon] { NavStore::instance().goTo(name, lat, lon); });
    auto* del = new IconButton("trash", tr("Delete"), nullptr, true);
    connect(del, &QAbstractButton::clicked, this,
            [id] { NavStore::instance().removeWaypoint(id); });
    l->addWidget(go);
    l->addWidget(del);
    connect(row, &ListRow::clicked, this,
            [this, lat, lon] { m_chart->centerOn(lat, lon); });
    addRow(row);
  }
}

// --------------------------------------------------------------------------

AisPage::AisPage(ChartView* chart, QWidget* parent)
    : ListPage(parent), m_chart(chart) {
  m_summary = label(QString(), "dim");
  actions()->addWidget(m_summary);
  setEmptyText(tr("No AIS targets received."));
  m_refresh.setInterval(2000);
  connect(&m_refresh, &QTimer::timeout, this, [this] {
    if (isVisible()) rebuild();
    NavStore::instance().expireAis();
  });
  m_refresh.start();
  connect(&Theme::instance(), &Theme::changed, this, &AisPage::rebuild);
  rebuild();
}

void AisPage::rebuild() {
  clearRows();
  const Palette& pal = Theme::p();
  const auto& cfg = AppSettings::instance();
  QVector<AisTarget> list;
  for (const auto& t : NavStore::instance().aisTargets()) list << t;
  std::sort(list.begin(), list.end(),
            [](const AisTarget& a, const AisTarget& b) {
              if (a.dangerous != b.dangerous) return a.dangerous;
              const double ra = std::isnan(a.rangeNm) ? 1e9 : a.rangeNm;
              const double rb = std::isnan(b.rangeNm) ? 1e9 : b.rangeNm;
              return ra < rb;
            });
  int dangerous = 0;
  for (const AisTarget& t : list) {
    dangerous += t.dangerous;
    auto* row = new ListRow;
    if (t.dangerous) row->setHighlight(pal.danger);
    auto* l = new QHBoxLayout(row);
    l->setContentsMargins(14, 10, 14, 10);
    l->addWidget(iconLabel("ship", t.dangerous ? pal.danger : pal.textDim));
    l->addWidget(textColumn(t.displayName(), aisShipTypeName(t.shipType) +
                                                 " · " + cfg.speedText(t.sog)),
                 1);
    auto* right = new QWidget;
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(2);
    auto* range =
        label(cfg.distanceText(t.rangeNm) + "  " + degText(t.bearing), "value");
    range->setAlignment(Qt::AlignRight);
    QString cpaText;
    if (!std::isnan(t.cpaNm))
      cpaText = t.tcpaMin > 0.5 ? tr("CPA %1 in %2 min")
                                      .arg(cfg.distanceText(t.cpaNm))
                                      .arg(t.tcpaMin, 0, 'f', 0)
                                : tr("Opening");
    auto* cpa = label(cpaText, "dim");
    cpa->setAlignment(Qt::AlignRight);
    if (t.dangerous)
      cpa->setStyleSheet(QStringLiteral("color: %1;").arg(pal.danger.name()));
    rl->addWidget(range);
    rl->addWidget(cpa);
    l->addWidget(right);
    const quint32 mmsi = t.mmsi;
    const double lat = t.lat, lon = t.lon;
    const bool hasPos = t.hasPosition();
    connect(row, &ListRow::clicked, this, [this, mmsi, lat, lon, hasPos] {
      if (hasPos) m_chart->centerOn(lat, lon);
      emit targetSelected(mmsi);
    });
    addRow(row);
  }
  m_summary->setText(
      tr("%n target(s)", nullptr, int(list.size())) +
      (dangerous ? tr(" · %n dangerous", nullptr, dangerous) : QString()));
}

// --------------------------------------------------------------------------

ConnectionsPage::ConnectionsPage(QWidget* parent) : ListPage(parent) {
  auto* form = new QWidget;
  auto* fl = new QVBoxLayout(form);
  fl->setContentsMargins(0, 0, 0, 0);
  fl->setSpacing(2);
  fl->addWidget(label(tr("ADD SOURCE"), "section"));
  m_type = new QComboBox;
  m_type->addItem(tr("UDP (listen)"), int(ConnectionConfig::Type::Udp));
  m_type->addItem(tr("TCP (client)"), int(ConnectionConfig::Type::Tcp));
#ifdef OCPN_QT_HAVE_SERIAL
  m_type->addItem(tr("Serial port"), int(ConnectionConfig::Type::Serial));
#endif
  m_type->addItem(tr("Simulator"), int(ConnectionConfig::Type::Simulator));
  fl->addWidget(settingRow(tr("Type"), m_type));
  m_host = new QLineEdit("192.168.4.1");
  m_host->setMinimumWidth(170);
  m_hostRow = settingRow(tr("Host"), m_host);
  fl->addWidget(m_hostRow);
  m_port = new QSpinBox;
  m_port->setRange(1, 65535);
  m_port->setValue(10110);
  m_port->setMinimumWidth(110);
  m_portRow = settingRow(tr("Port"), m_port);
  fl->addWidget(m_portRow);
  m_serial = new QComboBox;
  m_serial->setEditable(true);
  m_serial->setMinimumWidth(170);
#ifdef OCPN_QT_HAVE_SERIAL
  for (const auto& info : QSerialPortInfo::availablePorts())
    m_serial->addItem(info.systemLocation());
#endif
  m_serialRow = settingRow(tr("Device"), m_serial);
  fl->addWidget(m_serialRow);
  m_baud = new QComboBox;
  for (int b : {4800, 9600, 19200, 38400, 57600, 115200})
    m_baud->addItem(QString::number(b), b);
  m_baud->setCurrentIndex(m_baud->findData(38400));
  m_baudRow = settingRow(tr("Baud rate"), m_baud);
  fl->addWidget(m_baudRow);
  auto* add = button(tr("Add source"), "plus", "primary");
  fl->addSpacing(6);
  fl->addWidget(add);
  actions()->addWidget(form);
  actions()->addWidget(label(tr("SOURCES"), "section"));

  connect(m_type, &QComboBox::currentIndexChanged, this,
          &ConnectionsPage::updateForm);
  connect(add, &QPushButton::clicked, this, [this] {
    ConnectionConfig c;
    c.type = ConnectionConfig::Type(m_type->currentData().toInt());
    c.host = m_host->text().trimmed();
    c.port = quint16(m_port->value());
    c.serialPort = m_serial->currentText();
    c.baud = m_baud->currentData().toInt();
    ConnectionManager::instance().add(c);
  });
  updateForm();

  m_monitor = new QLabel;
  m_monitor->setWordWrap(true);
  m_monitor->setTextInteractionFlags(Qt::TextSelectableByMouse);
  QFont mono("monospace");
  mono.setStyleHint(QFont::Monospace);
  mono.setPixelSize(11);
  m_monitor->setFont(mono);
  m_monitor->setProperty("role", "dim");
  m_monitor->setProperty("persistent", true);
  connect(&ConnectionManager::instance().parser(),
          &NmeaParser::sentenceReceived, this, [this](const QString& s) {
            m_lastSentences << s;
            while (m_lastSentences.size() > 8) m_lastSentences.removeFirst();
            if (isVisible()) m_monitor->setText(m_lastSentences.join('\n'));
          });

  auto& mgr = ConnectionManager::instance();
  connect(&mgr, &ConnectionManager::connectionsChanged, this,
          &ConnectionsPage::rebuild);
  connect(&mgr, &ConnectionManager::statusChanged, this,
          &ConnectionsPage::rebuild);
  connect(&Theme::instance(), &Theme::changed, this, &ConnectionsPage::rebuild);
  setEmptyText(tr("No data sources configured."));
  rebuild();
}

void ConnectionsPage::updateForm() {
  const auto t = ConnectionConfig::Type(m_type->currentData().toInt());
  m_hostRow->setVisible(t == ConnectionConfig::Type::Tcp);
  m_portRow->setVisible(t == ConnectionConfig::Type::Tcp ||
                        t == ConnectionConfig::Type::Udp);
  m_serialRow->setVisible(t == ConnectionConfig::Type::Serial);
  m_baudRow->setVisible(t == ConnectionConfig::Type::Serial);
}

void ConnectionsPage::rebuild() {
  clearRows();
  const Palette& pal = Theme::p();
  for (Connection* c : ConnectionManager::instance().connections()) {
    const auto& cfg = c->config();
    auto* row = new ListRow;
    row->setCursor(Qt::ArrowCursor);
    auto* l = new QHBoxLayout(row);
    l->setContentsMargins(14, 10, 10, 10);
    QColor dot = pal.textFaint;
    switch (c->status()) {
      case Connection::Status::Connected:
        dot = pal.success;
        break;
      case Connection::Status::Connecting:
        dot = pal.warning;
        break;
      case Connection::Status::Error:
        dot = pal.danger;
        break;
      default:
        break;
    }
    if (c->status() == Connection::Status::Connected)
      row->setHighlight(pal.success);
    l->addWidget(iconLabel(
        cfg.type == ConnectionConfig::Type::Simulator ? "ship" : "signal",
        dot));
    QString sub = cfg.typeName();
    if (!c->statusText().isEmpty()) sub += " · " + c->statusText();
    if (c->sentencesPerMinute() > 0)
      sub += tr(" · %1/min").arg(c->sentencesPerMinute());
    l->addWidget(textColumn(cfg.label(), sub), 1);
    auto* sw = new ToggleSwitch;
    sw->setChecked(cfg.enabled);
    const QString id = cfg.id;
    connect(sw, &QAbstractButton::toggled, this, [id](bool on) {
      QTimer::singleShot(
          180, [id, on] { ConnectionManager::instance().setEnabled(id, on); });
    });
    auto* del = new IconButton("trash", tr("Remove"), nullptr, true);
    connect(del, &QAbstractButton::clicked, this, [id] {
      QTimer::singleShot(0, [id] { ConnectionManager::instance().remove(id); });
    });
    l->addWidget(sw);
    l->addWidget(del);
    addRow(row);
  }
  auto* monitorTitle = label(tr("DATA MONITOR"), "section");
  addRow(monitorTitle);
  addRow(m_monitor);
}

// --------------------------------------------------------------------------

ChartsPage::ChartsPage(ChartView* chart, QWidget* parent)
    : ListPage(parent), m_chart(chart) {
  auto* add = button(tr("Add chart folder"), "plus", "primary");
  connect(add, &QPushButton::clicked, this, [this] {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Chart folder"),
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
    if (!dir.isEmpty()) ChartDatabase::instance().addDirectory(dir);
  });
  auto* rescan = button(tr("Rescan"), "undo");
  connect(rescan, &QPushButton::clicked, this,
          [] { ChartDatabase::instance().rescan(); });
  auto* row = new QHBoxLayout;
  row->addWidget(add, 1);
  row->addWidget(rescan);
  actions()->addLayout(row);
  m_status = label(QString(), "dim");
  m_status->setWordWrap(true);
  actions()->addWidget(m_status);
  setEmptyText(
      tr("Add a folder with S-57 ENC cells (*.000) or BSB/KAP "
         "raster charts. Free official ENCs are available e.g. "
         "from NOAA (charts.noaa.gov) and many inland waterway "
         "authorities."));
  auto& db = ChartDatabase::instance();
  connect(&db, &ChartDatabase::chartsChanged, this, &ChartsPage::rebuild);
  connect(&db, &ChartDatabase::scanStarted, this,
          [this] { m_status->setText(tr("Scanning chart folders…")); });
  connect(&Theme::instance(), &Theme::changed, this, &ChartsPage::rebuild);
  rebuild();
}

void ChartsPage::rebuild() {
  clearRows();
  const Palette& pal = Theme::p();
  auto& db = ChartDatabase::instance();
  const auto charts = db.charts();
  m_status->setText(db.isScanning() ? tr("Scanning chart folders…")
                                    : tr("%n chart(s) installed", nullptr,
                                         int(charts->size())));
  if (!db.directories().isEmpty()) addRow(label(tr("FOLDERS"), "section"));
  for (const QString& dir : db.directories()) {
    auto* r = new ListRow;
    r->setCursor(Qt::ArrowCursor);
    auto* l = new QHBoxLayout(r);
    l->setContentsMargins(14, 8, 10, 8);
    l->addWidget(iconLabel("layers", pal.textDim));
    l->addWidget(textColumn(QFileInfo(dir).fileName(), dir), 1);
    auto* del = new IconButton("trash", tr("Remove folder"), nullptr, true);
    connect(del, &QAbstractButton::clicked, this, [dir] {
      QTimer::singleShot(
          0, [dir] { ChartDatabase::instance().removeDirectory(dir); });
    });
    l->addWidget(del);
    addRow(r);
  }
  if (!charts->isEmpty()) addRow(label(tr("CHARTS"), "section"));
  // Most detailed first in the list.
  for (auto it = charts->crbegin(); it != charts->crend(); ++it) {
    const ChartPtr c = *it;
    auto* r = new ListRow;
    auto* l = new QHBoxLayout(r);
    l->setContentsMargins(14, 8, 10, 8);
    const bool vector = c->type() == Chart::Type::Vector;
    l->addWidget(iconLabel(vector ? "chart" : "layers",
                           vector ? pal.accentBright : pal.warning));
    l->addWidget(
        textColumn(c->name(),
                   (vector ? tr("Vector ENC") : tr("Raster chart")) +
                       QStringLiteral(" · 1:%L1").arg(c->nativeScale())),
        1);
    connect(r, &ListRow::clicked, this, [this, c] {
      double lat1, lon1, lat2, lon2;
      geo::fromMercator(c->bounds().bottomLeft(), lat1, lon1);
      geo::fromMercator(c->bounds().topRight(), lat2, lon2);
      m_chart->fitGeoBounds(lat1, lon1, lat2, lon2);
    });
    addRow(r);
  }
}

// --------------------------------------------------------------------------

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent) {
  auto& s = AppSettings::instance();
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  auto* scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* content = new QWidget;
  content->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  auto* l = new QVBoxLayout(content);
  l->setContentsMargins(0, 0, 8, 0);
  l->setSpacing(2);
  scroll->setWidget(content);
  outer->addWidget(scroll);

  auto combo = [](const QStringList& items, int current, auto apply) {
    auto* c = new QComboBox;
    c->addItems(items);
    c->setCurrentIndex(current);
    c->setMinimumWidth(140);
    QObject::connect(c, &QComboBox::currentIndexChanged, c, [apply](int i) {
      apply(i);
      AppSettings::instance().notifyChanged();
    });
    return c;
  };
  auto toggle = [](bool on, auto apply) {
    auto* t = new ToggleSwitch;
    t->setChecked(on);
    QObject::connect(t, &QAbstractButton::toggled, t, [apply](bool v) {
      apply(v);
      AppSettings::instance().notifyChanged();
    });
    return t;
  };
  auto dspin = [](double value, double min, double max, double step,
                  const QString& suffix, int decimals, auto apply) {
    auto* sp = new QDoubleSpinBox;
    sp->setRange(min, max);
    sp->setSingleStep(step);
    sp->setDecimals(decimals);
    sp->setValue(value);
    sp->setSuffix(suffix);
    sp->setMinimumWidth(96);
    sp->setAlignment(Qt::AlignRight);
    QObject::connect(sp, &QDoubleSpinBox::valueChanged, sp, [apply](double v) {
      apply(v);
      AppSettings::instance().notifyChanged();
    });
    return sp;
  };

  l->addWidget(label(tr("APPEARANCE"), "section"));
  l->addWidget(settingRow(
      tr("Theme"),
      combo({tr("Night"), tr("Day"), tr("Automatic")}, int(s.theme),
            [](int i) {
              AppSettings::instance().theme = AppSettings::ThemeMode(i);
            }),
      tr("Automatic switches at 07:00 and 19:00")));

  l->addWidget(label(tr("UNITS"), "section"));
  l->addWidget(settingRow(
      tr("Speed"),
      combo({tr("Knots"), tr("km/h"), tr("m/s")}, int(s.speedUnit), [](int i) {
        AppSettings::instance().speedUnit = AppSettings::SpeedUnit(i);
      })));
  l->addWidget(
      settingRow(tr("Distance"), combo({tr("Nautical miles"), tr("Kilometers")},
                                       int(s.distanceUnit), [](int i) {
                                         AppSettings::instance().distanceUnit =
                                             AppSettings::DistanceUnit(i);
                                       })));

  l->addWidget(label(tr("CHART"), "section"));
  l->addWidget(settingRow(
      tr("Course up"),
      toggle(s.courseUp, [](bool v) { AppSettings::instance().courseUp = v; }),
      tr("Rotate the chart with the boat")));
  l->addWidget(
      settingRow(tr("Lat/lon grid"), toggle(s.showGraticule, [](bool v) {
                   AppSettings::instance().showGraticule = v;
                 })));
  l->addWidget(settingRow(tr("AIS names"), toggle(s.showAisNames, [](bool v) {
                            AppSettings::instance().showAisNames = v;
                          })));
  l->addWidget(settingRow(
      tr("Course predictor"),
      dspin(s.predictorMinutes, 1, 60, 1, tr(" min"), 0, [](double v) {
        AppSettings::instance().predictorMinutes = int(v);
      })));

  l->addWidget(settingRow(
      tr("Chart outlines"),
      toggle(s.showChartOutlines,
             [](bool v) { AppSettings::instance().showChartOutlines = v; }),
      tr("Show coverage of charts not drawn at this zoom")));
  l->addWidget(settingRow(
      tr("Shallow depth"),
      dspin(s.shallowDepth, 0, 50, 0.5, " m", 1,
            [](double v) { AppSettings::instance().shallowDepth = v; })));
  l->addWidget(settingRow(
      tr("Safety depth"),
      dspin(s.safetyDepth, 0, 100, 0.5, " m", 1,
            [](double v) { AppSettings::instance().safetyDepth = v; }),
      tr("Highlights the safety contour on ENCs")));
  l->addWidget(settingRow(tr("Deep water"),
                          dspin(s.deepDepth, 1, 500, 1, " m", 0, [](double v) {
                            AppSettings::instance().deepDepth = v;
                          })));

  l->addWidget(label(tr("ALARMS & NAVIGATION"), "section"));
  l->addWidget(settingRow(
      tr("CPA alarm"),
      dspin(s.cpaAlarmNm, 0.05, 5, 0.05, " NM", 2,
            [](double v) { AppSettings::instance().cpaAlarmNm = v; }),
      tr("Closest point of approach")));
  l->addWidget(settingRow(
      tr("TCPA limit"),
      dspin(s.tcpaAlarmMin, 1, 120, 1, tr(" min"), 0,
            [](double v) { AppSettings::instance().tcpaAlarmMin = v; })));
  l->addWidget(settingRow(
      tr("Arrival radius"),
      dspin(s.arrivalRadiusNm, 0.01, 1, 0.01, " NM", 2,
            [](double v) { AppSettings::instance().arrivalRadiusNm = v; })));

  l->addWidget(label(tr("ABOUT"), "section"));
  auto* about =
      label(tr("OpenCPN Qt %1 · Qt %2\nChart plotter and "
               "navigation software. Licensed under the GPL.")
                .arg(QCoreApplication::applicationVersion(), qVersion()),
            "dim");
  about->setWordWrap(true);
  about->setContentsMargins(4, 6, 4, 6);
  l->addWidget(about);
  l->addStretch(1);
}
