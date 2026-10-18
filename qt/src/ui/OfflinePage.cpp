#include "ui/OfflinePage.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <cmath>

#include "core/AppSettings.h"
#include "core/NavStore.h"
#include "core/osm/OnlineOsm.h"
#include "ui/ChartView.h"
#include "ui/DrawerPages.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

namespace {
QLabel* label(const QString& text, const char* role = nullptr) {
  auto* l = new QLabel(text);
  if (role) l->setProperty("role", role);
  return l;
}

QString sizeText(qint64 bytes) {
  return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat);
}

/** Escapes a string for an Overpass QL regular expression literal. */
QString qlRegex(const QString& s) {
  QString out;
  for (const QChar c : s) {
    if (QStringLiteral("\\.^$|?*+()[]{}\"").contains(c)) out += '\\';
    out += c;
  }
  return out;
}

QString kindIcon(OsmPoiLayer::Kind k) {
  switch (k) {
    case OsmPoiLayer::Kind::Marina:
    case OsmPoiLayer::Kind::Harbour:
    case OsmPoiLayer::Kind::Anchorage:
      return "anchor";
    case OsmPoiLayer::Kind::Fuel:
      return "fuel";
    case OsmPoiLayer::Kind::Slipway:
      return "arrow-up";
    case OsmPoiLayer::Kind::Seamark:
      return "flag";
    default:
      return "pin";
  }
}
}  // namespace

// --------------------------------------------------------------------------

AreaSelectBar::AreaSelectBar(QWidget* parent) : GlassPanel(parent, 24) {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(kShadow + 18, kShadow + 12, kShadow + 14,
                            kShadow + 12);
  outer->setSpacing(10);

  auto* top = new QHBoxLayout;
  top->setSpacing(10);
  auto* icon = new QLabel;
  const auto setIcon = [icon] {
    icon->setPixmap(Icons::pixmap("crop", Theme::p().accentBright, 22));
  };
  setIcon();
  connect(&Theme::instance(), &Theme::changed, icon, setIcon);
  top->addWidget(icon);
  auto* col = new QVBoxLayout;
  col->setSpacing(1);
  m_size = label(QString(), "value");
  m_hint = label(QString(), "dim");
  col->addWidget(m_size);
  col->addWidget(m_hint);
  top->addLayout(col, 1);
  outer->addLayout(top);

  auto* presets = new QHBoxLayout;
  presets->setSpacing(6);
  auto* visible = new QPushButton(tr("Visible area"));
  auto* boat = new QPushButton(tr("Around boat"));
  m_route = new QPushButton(tr("Along route"));
  int i = 0;
  for (auto* b : {visible, boat, m_route}) {
    b->setProperty("role", "segment");
    b->setCursor(Qt::PointingHandCursor);
    const int preset = i++;
    connect(b, &QPushButton::clicked, this,
            [this, preset] { emit presetRequested(preset); });
    presets->addWidget(b);
  }
  outer->addLayout(presets);

  auto* bottom = new QHBoxLayout;
  bottom->setSpacing(8);
  m_name = new QLineEdit;
  m_name->setPlaceholderText(tr("Name, e.g. Kiel Fjord"));
  m_name->setMinimumWidth(200);
  bottom->addWidget(m_name, 1);
  auto* cancel = new QPushButton(tr("Cancel"));
  m_ok = new QPushButton(tr("Download"));
  m_ok->setProperty("role", "primary");
  m_ok->setIcon(Icons::icon("download", Qt::white, 18));
  for (auto* b : {cancel, m_ok}) {
    b->setCursor(Qt::PointingHandCursor);
    bottom->addWidget(b);
  }
  outer->addLayout(bottom);
  connect(cancel, &QPushButton::clicked, this, &AreaSelectBar::cancel);
  connect(m_ok, &QPushButton::clicked, this, &AreaSelectBar::accepted);
  connect(m_name, &QLineEdit::returnPressed, this, &AreaSelectBar::accepted);
  hide();
}

void AreaSelectBar::reset() {
  m_name->clear();
  auto& nav = NavStore::instance();
  m_route->setEnabled(!nav.routes().isEmpty());
}

QString AreaSelectBar::regionName() const { return m_name->text().trimmed(); }

void AreaSelectBar::setArea(const GeoBox& box) {
  const auto& cfg = AppSettings::instance();
  m_size->setText(
      tr("%1 × %2 · %3 km²")
          .arg(cfg.distanceText(box.widthNm()),
               cfg.distanceText(box.heightNm()))
          .arg(QLocale().toString(
              box.widthNm() * box.heightNm() * 1.852 * 1.852, 'f', 0)));
  const auto& svc = OverpassService::instance();
  if (box.areaDeg2() > OverpassService::kMaxApiArea)
    m_hint->setText(
        svc.downloadUrl().isEmpty()
            ? tr("Too large for the OpenStreetMap API – make it smaller")
            : tr("Large area: the Overpass server may need a few minutes"));
  else
    m_hint->setText(
        tr("Drag the corners or edges, move the frame or "
           "pan the chart around it"));
  m_ok->setEnabled(box.isValid() &&
                   (box.areaDeg2() <= OverpassService::kMaxApiArea ||
                    !svc.downloadUrl().isEmpty()));
}

// --------------------------------------------------------------------------

OsmPoiLayer::OsmPoiLayer(ChartView* chart, QObject* parent)
    : MapLayer(parent), m_chart(chart) {
  setEnabled(QSettings().value("Overpass/showPois", true).toBool());
  m_debounce.setSingleShot(true);
  m_debounce.setInterval(700);
  connect(&m_debounce, &QTimer::timeout, this, &OsmPoiLayer::refresh);
  connect(chart, &ChartView::viewChanged, &m_debounce,
          qOverload<>(&QTimer::start));
  auto& svc = OverpassService::instance();
  connect(&svc, &OverpassService::databaseChanged, this, [this] {
    m_loaded = {};
    m_debounce.start();
  });
  connect(this, &MapLayer::changed, &m_debounce, qOverload<>(&QTimer::start));
  connect(&OnlineOsm::instance(), &OnlineOsm::tileLoaded, this, [this] {
    m_loaded = {};
    m_debounce.start(150);
  });
  m_debounce.start();
}

QString OsmPoiLayer::queryFor(const GeoBox& box) {
  return queryTemplate().arg(box.overpassBbox());
}

QString OsmPoiLayer::queryTemplate() {
  return QStringLiteral(
      "[out:json][timeout:25][bbox:%1];"
      "(nwr[\"leisure\"~\"^(marina|slipway)$\"];"
      "nwr[\"seamark:type\"];"
      "nwr[\"waterway\"=\"fuel\"];"
      "nwr[\"amenity\"=\"fuel\"][\"boat\"=\"yes\"];"
      "nwr[\"harbour\"=\"yes\"];);"
      "out center tags 3000;");
}

OsmPoiLayer::Kind OsmPoiLayer::classify(const OsmElement& e) {
  const auto& t = e.tags;
  const QString seamark = t.value("seamark:type");
  if (t.value("leisure") == "marina" ||
      t.value("seamark:harbour:category") == "marina")
    return Kind::Marina;
  if (t.value("leisure") == "slipway" || seamark == "small_craft_facility")
    return t.value("seamark:small_craft_facility:category") == "fuel_station"
               ? Kind::Fuel
               : Kind::Slipway;
  if (t.value("waterway") == "fuel" || t.value("amenity") == "fuel")
    return Kind::Fuel;
  if (seamark == "anchorage" || seamark == "anchor_berth")
    return Kind::Anchorage;
  if (seamark == "harbour" || t.value("harbour") == "yes") return Kind::Harbour;
  if (!seamark.isEmpty()) return Kind::Seamark;
  return Kind::Other;
}

void OsmPoiLayer::refresh() {
  const auto clear = [this] {
    if (m_elements.isEmpty()) return;
    m_elements.clear();
    m_loaded = {};
    m_chart->update();
  };
  if (!isEnabled()) return clear();
  // Only zoomed in: a harbour scale view covers well below one degree.
  const GeoBox visible = m_chart->visibleArea();
  if (!visible.isValid() || visible.areaDeg2() > 1.0) return clear();

  auto& online = OnlineOsm::instance();
  auto& svc = OverpassService::instance();
  bool localData = false;
  for (const auto& r : svc.regions()) localData |= r.box.intersects(visible);
  localData &= svc.databaseReady();

  // Without (complete) offline data: tiles from the public servers,
  // loaded while panning and cached on disk.
  QVector<OsmElement> tileElements;
  if (!online.coveredOffline(visible) && online.isEnabled()) {
    const QStringList keys = OnlineOsm::tilesFor(visible);
    if (keys.size() <= 16) {
      QStringList missing;
      for (const QString& k : keys) {
        QVector<OsmElement> e;
        if (online.tile(k, &e))
          tileElements += e;
        else
          missing << k;
      }
      if (!missing.isEmpty()) online.requestTiles(missing, queryTemplate());
    }
  }

  if (!localData) {
    m_elements = tileElements;
    m_loaded = {};
    m_chart->update();
    return;
  }

  const auto inside = [](const GeoBox& outer, const GeoBox& b) {
    return b.south >= outer.south && b.north <= outer.north &&
           b.west >= outer.west && b.east <= outer.east;
  };
  if (m_loaded.isValid() && inside(m_loaded, visible) && tileElements.isEmpty())
    return;
  if (m_running) {
    m_again = true;
    return;
  }
  // Load a margin around the view so small pans need no new query.
  const double mh = (visible.north - visible.south) * 0.5;
  const double mw = (visible.east - visible.west) * 0.5;
  const GeoBox box{
      std::max(-85.0, visible.south - mh), std::max(-180.0, visible.west - mw),
      std::min(85.0, visible.north + mh), std::min(180.0, visible.east + mw)};
  m_running = true;
  svc.query(queryFor(box), [this, box, tileElements](const QByteArray& out,
                                                     const QString& err) {
    m_running = false;
    if (err.isEmpty()) {
      // Offline region data plus online tiles, without duplicates.
      QVector<OsmElement> all = OverpassService::parseJson(out);
      QSet<QString> seen;
      for (const auto& e : all) seen.insert(e.type + QString::number(e.id));
      for (const auto& e : tileElements)
        if (!seen.contains(e.type + QString::number(e.id))) all << e;
      m_elements = all;
      m_loaded = box;
      m_chart->update();
    }
    if (m_again) {
      m_again = false;
      m_debounce.start();
    }
  });
}

void OsmPoiLayer::paint(QPainter& p, const ChartView& view) {
  if (m_elements.isEmpty()) return;
  const Palette& pal = Theme::p();
  p.setRenderHint(QPainter::Antialiasing);
  const double nmpp = view.nmPerPixel();
  const QFont font = Theme::font(11, QFont::DemiBold);
  const QFontMetricsF fm(font);
  const QColor halo =
      pal.dark ? QColor(0, 0, 0, 170) : QColor(255, 255, 255, 210);
  const QRectF area = QRectF(view.rect()).adjusted(-20, -20, 20, 20);

  // Seamarks below the symbols, only at harbour scale (S-57 charts usually
  // show them too).
  if (nmpp <= 0.004) {
    for (const OsmElement& e : m_elements) {
      if (classify(e) != Kind::Seamark) continue;
      const QPointF s = view.screenPos(e.lat, e.lon);
      if (!area.contains(s)) continue;
      const QString colour =
          e.tags.value("seamark:" + e.tags.value("seamark:type") + ":colour");
      QColor c = pal.textDim;
      if (colour.startsWith("red")) c = QColor(230, 60, 60);
      if (colour.startsWith("green")) c = QColor(40, 190, 90);
      if (colour.startsWith("yellow")) c = QColor(240, 200, 40);
      if (colour.startsWith("black")) c = QColor(30, 30, 30);
      p.setPen(QPen(Qt::white, 1.2));
      p.setBrush(c);
      QPolygonF d;
      d << s + QPointF(0, -5) << s + QPointF(4, 0) << s + QPointF(0, 5)
        << s + QPointF(-4, 0);
      p.drawPolygon(d);
    }
  }

  // Symbols by priority; whatever would overlap a shown one is left out.
  static const Kind order[] = {Kind::Marina, Kind::Harbour, Kind::Fuel,
                               Kind::Anchorage, Kind::Slipway};
  QVector<QRectF> taken;
  const auto free = [&taken](const QRectF& r) {
    for (const QRectF& t : taken)
      if (t.intersects(r)) return false;
    return true;
  };
  for (Kind kind : order) {
    for (const OsmElement& e : m_elements) {
      if (classify(e) != kind) continue;
      const QPointF s = view.screenPos(e.lat, e.lon);
      if (!area.contains(s)) continue;
      const QRectF sym(s.x() - 13, s.y() - 13, 26, 26);
      if (!free(sym)) continue;
      taken << sym;
      const QColor bg = kind == Kind::Fuel     ? pal.warning
                        : kind == Kind::Marina ? pal.accent
                                               : pal.success;
      p.setPen(QPen(Qt::white, 2));
      p.setBrush(bg);
      p.drawEllipse(s, 12, 12);
      p.drawPixmap(QPointF(s.x() - 8, s.y() - 8),
                   Icons::pixmap(kindIcon(kind), Qt::white, 16));
      const QString name = e.name();
      if (name.isEmpty() || nmpp > 0.01) continue;
      const QRectF text(s.x() + 16, s.y() - 8, fm.horizontalAdvance(name), 16);
      if (!free(text)) continue;
      taken << text;
      QPainterPath path;
      path.addText(s + QPointF(16, 4), font, name);
      p.setPen(QPen(halo, 3.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      p.setBrush(Qt::NoBrush);
      p.drawPath(path);
      p.setPen(Qt::NoPen);
      p.setBrush(pal.text);
      p.drawPath(path);
    }
  }
}

// --------------------------------------------------------------------------

OfflinePage::OfflinePage(ChartView* chart, OsmPoiLayer* layer, QWidget* parent)
    : QWidget(parent), m_chart(chart), m_layer(layer) {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  auto* scroll = new QScrollArea;
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* content = new QWidget;
  content->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  auto* l = new QVBoxLayout(content);
  l->setContentsMargins(0, 0, 8, 0);
  l->setSpacing(10);
  scroll->setWidget(content);
  outer->addWidget(scroll);
  const Palette& pal = Theme::p();
  auto& svc = OverpassService::instance();

  m_backendState = label(QString(), "dim");
  m_backendState->setWordWrap(true);
  l->addWidget(m_backendState);

  m_select = new QPushButton(tr("Select area on chart"));
  m_select->setProperty("role", "primary");
  m_select->setIcon(Icons::icon("crop", Qt::white, 18));
  m_select->setCursor(Qt::PointingHandCursor);
  connect(m_select, &QPushButton::clicked, this,
          &OfflinePage::selectAreaRequested);
  l->addWidget(m_select);

  auto* io = new QHBoxLayout;
  auto* imp = new QPushButton(tr("Import OSM file"));
  imp->setIcon(Icons::icon("import", pal.text, 18));
  auto* reimport = new QPushButton(tr("Rebuild"));
  reimport->setIcon(Icons::icon("database", pal.text, 18));
  reimport->setToolTip(tr("Re-import all regions into the Overpass database"));
  io->addWidget(imp, 1);
  io->addWidget(reimport);
  l->addLayout(io);
  connect(imp, &QPushButton::clicked, this, [this] {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import OpenStreetMap data"),
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
        tr("OSM XML (*.osm *.xml);;All files (*)"));
    if (path.isEmpty()) return;
    QString err;
    if (!OverpassService::instance().importFile(path, QString(), &err))
      QMessageBox::warning(this, tr("Import failed"), err);
  });
  connect(reimport, &QPushButton::clicked, &svc,
          &OverpassService::rebuildDatabase);

  m_progress = new QProgressBar;
  m_progress->setTextVisible(false);
  m_progress->setFixedHeight(6);
  m_progress->setStyleSheet(
      QStringLiteral("QProgressBar { background: %1; border: none; "
                     "border-radius: 3px; } QProgressBar::chunk { "
                     "background: %2; border-radius: 3px; }")
          .arg(pal.panelPressed.name(QColor::HexArgb), pal.accent.name()));
  m_progress->hide();
  auto* progressRow = new QHBoxLayout;
  progressRow->addWidget(m_progress, 1);
  m_cancel = new QPushButton(tr("Cancel"));
  m_cancel->setProperty("role", "ghost");
  m_cancel->hide();
  connect(m_cancel, &QPushButton::clicked, &svc, &OverpassService::cancel);
  progressRow->addWidget(m_cancel);
  l->addLayout(progressRow);
  m_status = label(QString(), "dim");
  m_status->setWordWrap(true);
  l->addWidget(m_status);

  l->addWidget(label(tr("REGIONS"), "section"));
  m_regions = new QVBoxLayout;
  m_regions->setSpacing(8);
  l->addLayout(m_regions);
  m_empty = label(tr("No offline regions yet. Select an area on the chart "
                     "to download harbours, marinas, seamarks and the "
                     "coastline into the local Overpass database."),
                  "dim");
  m_empty->setWordWrap(true);
  m_empty->setAlignment(Qt::AlignCenter);
  m_empty->setContentsMargins(16, 16, 16, 16);
  l->addWidget(m_empty);

  // Offline search
  l->addWidget(label(tr("SEARCH OFFLINE"), "section"));
  m_search = new QLineEdit;
  m_search->setPlaceholderText(tr("Marina, harbour, buoy…"));
  m_search->addAction(Icons::icon("search", pal.textDim, 18),
                      QLineEdit::LeadingPosition);
  m_search->setClearButtonEnabled(true);
  connect(m_search, &QLineEdit::returnPressed, this, &OfflinePage::runSearch);
  l->addWidget(m_search);
  m_results = new QVBoxLayout;
  m_results->setSpacing(6);
  l->addLayout(m_results);

  // Display
  l->addWidget(label(tr("CHART"), "section"));
  auto* showRow = new QHBoxLayout;
  showRow->addWidget(label(tr("Show harbours and seamarks from OSM")), 1);
  auto* show = new ToggleSwitch;
  show->setChecked(m_layer->isEnabled());
  connect(show, &QAbstractButton::toggled, this, [this](bool on) {
    m_layer->setEnabled(on);
    QSettings().setValue("Overpass/showPois", on);
    m_chart->update();
  });
  showRow->addWidget(show);
  l->addLayout(showRow);
  auto* onlineRow = new QHBoxLayout;
  auto* onlineLabel =
      label(tr("Load harbours online when connected (Overpass, Nominatim)"));
  onlineLabel->setWordWrap(true);
  onlineRow->addWidget(onlineLabel, 1);
  auto* online = new ToggleSwitch;
  online->setChecked(OnlineOsm::instance().isEnabled());
  connect(online, &QAbstractButton::toggled, this, [this](bool on) {
    OnlineOsm::instance().setEnabled(on);
    m_layer->setEnabled(m_layer->isEnabled());
    m_chart->update();
  });
  onlineRow->addWidget(online);
  l->addLayout(onlineRow);

  // Backend settings, collapsed by default.
  auto* settingsBtn = new QPushButton(tr("Overpass settings"));
  settingsBtn->setProperty("role", "ghost");
  settingsBtn->setIcon(Icons::icon("sliders", pal.textDim, 18));
  settingsBtn->setCheckable(true);
  l->addWidget(settingsBtn, 0, Qt::AlignLeft);
  m_settings = new QWidget;
  auto* sl = new QVBoxLayout(m_settings);
  sl->setContentsMargins(0, 0, 0, 0);
  sl->setSpacing(6);
  sl->addWidget(label(tr("Query backend"), "dim"));
  m_backend = new QComboBox;
  m_backend->addItem(tr("Local osm-3s instance"));
  m_backend->addItem(tr("HTTP (Docker / server)"));
  sl->addWidget(m_backend);
  sl->addWidget(label(tr("osm-3s bin folder"), "dim"));
  auto* binRow = new QHBoxLayout;
  m_binDir = new QLineEdit;
  m_binDir->setPlaceholderText(tr("Folder with osm3s_query"));
  auto* browse = new QPushButton(tr("…"));
  connect(browse, &QPushButton::clicked, this, [this] {
    const QString d = QFileDialog::getExistingDirectory(
        this, tr("osm-3s bin folder"), m_binDir->text());
    if (!d.isEmpty()) m_binDir->setText(d);
  });
  binRow->addWidget(m_binDir, 1);
  binRow->addWidget(browse);
  sl->addLayout(binRow);
  sl->addWidget(label(tr("HTTP interpreter URL"), "dim"));
  m_httpUrl = new QLineEdit;
  sl->addWidget(m_httpUrl);
  sl->addWidget(
      label(tr("Download server (empty: OpenStreetMap API only)"), "dim"));
  m_downloadUrl = new QLineEdit;
  sl->addWidget(m_downloadUrl);
  auto* apply = new QPushButton(tr("Apply"));
  connect(apply, &QPushButton::clicked, this, &OfflinePage::applySettings);
  sl->addWidget(apply, 0, Qt::AlignRight);
  m_settings->hide();
  connect(settingsBtn, &QPushButton::toggled, m_settings, &QWidget::setVisible);
  l->addWidget(m_settings);
  l->addStretch(1);

  connect(&svc, &OverpassService::regionsChanged, this, &OfflinePage::rebuild);
  connect(&svc, &OverpassService::databaseChanged, this,
          &OfflinePage::updateStatus);
  connect(&svc, &OverpassService::busyChanged, this, [this](bool busy) {
    m_progress->setVisible(busy);
    m_cancel->setVisible(busy);
    if (!busy) updateStatus();
    rebuild();
  });
  connect(&svc, &OverpassService::progress, this,
          [this](int done, int total, const QString& text) {
            m_progress->setRange(0, total);
            m_progress->setValue(done);
            m_status->setText(text);
          });
  connect(&svc, &OverpassService::error, this,
          [this](const QString& msg) { m_status->setText(msg); });
  connect(&Theme::instance(), &Theme::changed, this, &OfflinePage::rebuild);
  loadSettings();
  updateStatus();
  rebuild();
}

void OfflinePage::loadSettings() {
  auto& svc = OverpassService::instance();
  m_backend->setCurrentIndex(svc.backend() == OverpassService::Backend::Http);
  m_binDir->setText(svc.binDir());
  m_httpUrl->setText(svc.httpUrl());
  m_downloadUrl->setText(svc.downloadUrl());
}

void OfflinePage::applySettings() {
  auto& svc = OverpassService::instance();
  const bool backendChanged =
      (m_backend->currentIndex() == 1) !=
          (svc.backend() == OverpassService::Backend::Http) ||
      m_binDir->text().trimmed() != svc.binDir();
  svc.configure(m_backend->currentIndex() == 1
                    ? OverpassService::Backend::Http
                    : OverpassService::Backend::Local,
                m_binDir->text().trimmed(), m_httpUrl->text().trimmed(),
                m_downloadUrl->text().trimmed());
  // A newly configured local instance needs the regions imported.
  if (backendChanged && svc.backend() == OverpassService::Backend::Local &&
      svc.localAvailable() && !svc.databaseReady() && !svc.regions().isEmpty())
    svc.rebuildDatabase();
  updateStatus();
}

void OfflinePage::updateStatus() {
  auto& svc = OverpassService::instance();
  QString text;
  if (svc.backend() == OverpassService::Backend::Http) {
    text = tr("Queries go to %1. Downloaded regions are stored as "
              "combined.osm for importing into that instance.")
               .arg(svc.httpUrl());
  } else if (!svc.localAvailable()) {
    text =
        tr("osm-3s was not found. Regions can be downloaded, but offline "
           "queries need the Overpass binaries (see Overpass settings) "
           "or an HTTP instance such as the Docker image.");
  } else if (svc.databaseReady()) {
    text = tr("Local Overpass instance ready");
  } else {
    text = tr("Local Overpass instance found · database empty");
  }
  m_backendState->setText(text);
  m_backendState->setToolTip(svc.binDir());
  m_select->setEnabled(!svc.isBusy());
}

void OfflinePage::rebuild() {
  while (QLayoutItem* it = m_regions->takeAt(0)) {
    if (QWidget* w = it->widget()) {
      w->hide();
      w->deleteLater();
    }
    delete it;
  }
  const Palette& pal = Theme::p();
  auto& svc = OverpassService::instance();
  const auto& regions = svc.regions();
  m_empty->setVisible(regions.isEmpty());
  for (const OsmRegion& r : regions) {
    auto* row = new ListRow;
    auto* l = new QHBoxLayout(row);
    l->setContentsMargins(14, 10, 10, 10);
    auto* icon = new QLabel;
    icon->setPixmap(Icons::pixmap("layers", pal.success, 22));
    l->addWidget(icon);
    auto* col = new QVBoxLayout;
    col->setSpacing(2);
    auto* name = label(r.name, "value");
    const auto& cfg = AppSettings::instance();
    auto* sub =
        label(tr("%1 × %2 · %3")
                  .arg(cfg.distanceText(r.box.widthNm()),
                       cfg.distanceText(r.box.heightNm()), sizeText(r.bytes)),
              "dim");
    auto* sub2 =
        label(QLocale().toString(r.updated.toLocalTime(), QLocale::ShortFormat),
              "dim");
    for (auto* lbl : {name, sub, sub2}) lbl->setWordWrap(true);
    row->setToolTip(tr("%1 nodes · %2 ways · %3 relations · source: %4")
                        .arg(QLocale().toString(r.nodes),
                             QLocale().toString(r.ways),
                             QLocale().toString(r.relations), r.source));
    col->addWidget(name);
    col->addWidget(sub);
    col->addWidget(sub2);
    l->addLayout(col, 1);
    const QString id = r.id;
    const GeoBox box = r.box;
    connect(row, &ListRow::clicked, this, [this, box] {
      m_chart->fitGeoBounds(box.south, box.west, box.north, box.east);
    });
    auto* upd = new IconButton("refresh", tr("Update"));
    upd->setEnabled(!svc.isBusy() && r.source != "file");
    connect(upd, &QAbstractButton::clicked, this,
            [id] { OverpassService::instance().updateRegion(id); });
    auto* ren = new IconButton("edit", tr("Rename"));
    connect(ren, &QAbstractButton::clicked, this, [this, id, n = r.name] {
      bool ok = false;
      const QString name = QInputDialog::getText(
          this, tr("Rename region"), tr("Name"), QLineEdit::Normal, n, &ok);
      if (ok && !name.trimmed().isEmpty())
        OverpassService::instance().renameRegion(id, name.trimmed());
    });
    auto* del = new IconButton("trash", tr("Delete"), nullptr, true);
    del->setEnabled(!svc.isBusy());
    connect(del, &QAbstractButton::clicked, this, [this, id] {
      if (QMessageBox::question(this, tr("Delete region"),
                                tr("Delete this offline region?")) ==
          QMessageBox::Yes)
        OverpassService::instance().removeRegion(id);
    });
    l->addWidget(upd);
    l->addWidget(ren);
    l->addWidget(del);
    m_regions->addWidget(row);
  }
  updateStatus();
}

void OfflinePage::runSearch() {
  while (QLayoutItem* it = m_results->takeAt(0)) {
    if (QWidget* w = it->widget()) {
      w->hide();
      w->deleteLater();
    }
    delete it;
  }
  const QString text = m_search->text().trimmed();
  auto& svc = OverpassService::instance();
  if (text.isEmpty() || svc.regions().isEmpty()) return;
  GeoBox all = svc.regions().first().box;
  for (const auto& r : svc.regions())
    all = GeoBox{
        std::min(all.south, r.box.south), std::min(all.west, r.box.west),
        std::max(all.north, r.box.north), std::max(all.east, r.box.east)};
  const QString re = qlRegex(text);
  const QString q =
      QStringLiteral(
          "[out:json][timeout:25][bbox:%1];"
          "(nwr[\"name\"~\"%2\",i];nwr[\"seamark:name\"~\"%2\",i];);"
          "out center tags 40;")
          .arg(all.overpassBbox(), re);
  QPointer<QLabel> busy = label(tr("Searching…"), "dim");
  m_results->addWidget(busy);
  QPointer<OfflinePage> self(this);
  svc.query(q, [self, busy](const QByteArray& out, const QString& err) {
    // A newer search cleared the results in the meantime.
    if (!self || !busy) return;
    busy->setText(err);
    if (!err.isEmpty()) return;
    const auto elements = OverpassService::parseJson(out);
    busy->setText(elements.isEmpty() ? tr("No results") : QString());
    busy->setVisible(elements.isEmpty());
    const Palette& pal = Theme::p();
    for (const OsmElement& e : elements) {
      auto* row = new ListRow;
      auto* l = new QHBoxLayout(row);
      l->setContentsMargins(12, 8, 10, 8);
      auto* icon = new QLabel;
      icon->setPixmap(
          Icons::pixmap(kindIcon(OsmPoiLayer::classify(e)), pal.accent, 20));
      l->addWidget(icon);
      QString name = e.name();
      if (name.isEmpty()) name = e.tags.value("seamark:name");
      QString kind = e.tags.value("seamark:type");
      if (kind.isEmpty()) kind = e.tags.value("leisure");
      if (kind.isEmpty()) kind = e.tags.value("amenity");
      if (kind.isEmpty()) kind = e.tags.value("place");
      auto* col = new QVBoxLayout;
      col->setSpacing(1);
      col->addWidget(label(name, "value"));
      col->addWidget(label(kind.replace('_', ' '), "dim"));
      l->addLayout(col, 1);
      const double lat = e.lat, lon = e.lon;
      QObject::connect(row, &ListRow::clicked, self, [self, lat, lon] {
        self->m_chart->fitGeoBounds(lat - 0.01, lon - 0.016, lat + 0.01,
                                    lon + 0.016);
      });
      self->m_results->addWidget(row);
    }
  });
}
