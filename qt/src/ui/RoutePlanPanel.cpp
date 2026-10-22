#include "ui/RoutePlanPanel.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <cmath>

#include "core/AppSettings.h"
#include "core/GeoMath.h"
#include "core/Grib.h"
#include "core/NavStore.h"
#include "core/Voyage.h"
#include "core/osm/OnlineOsm.h"
#include "core/osm/Overpass.h"
#include "ui/ChartView.h"
#include "ui/DrawerPages.h"
#include "ui/GlassPanel.h"
#include "ui/Icons.h"
#include "ui/OfflinePage.h"
#include "ui/PassagePanel.h"
#include "ui/Theme.h"

namespace {
QLabel* label(const QString& text, const char* role = nullptr) {
  auto* l = new QLabel(text);
  if (role) l->setProperty("role", role);
  return l;
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

QString kindIcon(const QString& kind) {
  if (kind == "position") return "locate";
  if (kind == "mark") return "pin";
  if (kind == "place") return "flag";
  return "anchor";
}

/** Bounding box of all offline regions; invalid if there are none. */
GeoBox regionsBox() {
  const auto& regions = OverpassService::instance().regions();
  if (regions.isEmpty()) return {};
  GeoBox all = regions.first().box;
  for (const auto& r : regions)
    all = GeoBox{
        std::min(all.south, r.box.south), std::min(all.west, r.box.west),
        std::max(all.north, r.box.north), std::max(all.east, r.box.east)};
  return all;
}

GeoBox geoBoxOf(const QRectF& mercator) {
  double north, west, south, east;
  geo::fromMercator(mercator.topLeft(), north, west);
  geo::fromMercator(mercator.bottomRight(), south, east);
  return GeoBox{south, west, north, east};
}

}  // namespace

// --------------------------------------------------------------------------

void PlannedRouteLayer::paint(QPainter& p, const ChartView& view) {
  if (m_points.size() < 2) return;
  const Palette& pal = Theme::p();
  QPolygonF line;
  for (const QPointF& g : m_points) line << view.screenPos(g.y(), g.x());
  p.setRenderHint(QPainter::Antialiasing);
  QColor glow = pal.accentBright;
  glow.setAlpha(60);
  p.setPen(QPen(glow, 10, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.drawPolyline(line);
  // Legs colored by how they are sailed (passage plan), else accent.
  for (int i = 1; i < line.size(); ++i) {
    QColor c = pal.accentBright;
    if (i - 1 < m_modes.size()) {
      switch (PassageLeg::Mode(m_modes[i - 1])) {
        case PassageLeg::Mode::Beat:
          c = pal.warning;
          break;
        case PassageLeg::Mode::Run:
          c = pal.success;
          break;
        case PassageLeg::Mode::Motor:
          c = pal.textDim;
          break;
        default:
          break;
      }
    }
    p.setPen(QPen(c, 3.5, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(line[i - 1], line[i]);
  }
  // Boards of the sail plan: thin line, tacks/gybes and sail changes.
  if (m_track.size() > 2) {
    QPolygonF zz;
    for (const QPointF& g : m_track) zz << view.screenPos(g.y(), g.x());
    p.setPen(QPen(pal.text, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(zz);
  }
  for (const SailEvent& e : m_events) {
    const QPointF c = view.screenPos(e.pos.y(), e.pos.x());
    if (e.kind == SailEvent::Kind::Tack || e.kind == SailEvent::Kind::Gybe) {
      p.setPen(QPen(Qt::white, 1.5));
      p.setBrush(pal.warning);
      p.drawPolygon(QPolygonF({c + QPointF(0, -6), c + QPointF(6, 0),
                               c + QPointF(0, 6), c + QPointF(-6, 0)}));
    } else if (e.kind == SailEvent::Kind::SailChange) {
      p.setPen(QPen(Qt::white, 1.5));
      p.setBrush(pal.success);
      p.drawRect(QRectF(c - QPointF(4.5, 4.5), QSizeF(9, 9)));
    }
  }
  for (int i = 0; i < line.size(); ++i) {
    const bool end = i == 0 || i == line.size() - 1;
    p.setPen(QPen(Qt::white, 2));
    p.setBrush(end ? pal.accent : pal.accentBright);
    p.drawEllipse(line[i], end ? 8 : 4.5, end ? 8 : 4.5);
  }
}

// --------------------------------------------------------------------------

PlaceField::PlaceField(const QString& icon, const QString& placeholder,
                       QWidget* parent)
    : QWidget(parent) {
  auto* l = new QVBoxLayout(this);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(4);
  m_edit = new QLineEdit;
  m_edit->setPlaceholderText(placeholder);
  m_edit->setClearButtonEnabled(true);
  m_edit->addAction(Icons::icon(icon, Theme::p().accentBright, 18),
                    QLineEdit::LeadingPosition);
  m_edit->installEventFilter(this);
  l->addWidget(m_edit);
  m_listBox = new QWidget;
  m_list = new QVBoxLayout(m_listBox);
  m_list->setContentsMargins(0, 0, 0, 4);
  m_list->setSpacing(2);
  m_listBox->hide();
  l->addWidget(m_listBox);

  m_debounce.setSingleShot(true);
  m_debounce.setInterval(300);
  connect(&m_debounce, &QTimer::timeout, this, &PlaceField::search);
  connect(m_edit, &QLineEdit::textEdited, this, [this] {
    if (m_place.valid) {
      m_place = {};
      emit placeChanged();
    }
    m_debounce.start();
  });
}

void PlaceField::setPlace(const Place& p) {
  m_place = p;
  m_edit->setText(p.name);
  m_edit->setCursorPosition(0);
  m_listBox->hide();
  emit placeChanged();
}

bool PlaceField::eventFilter(QObject* o, QEvent* e) {
  if (o == m_edit) {
    if (e->type() == QEvent::FocusIn && !m_place.valid) search();
    if (e->type() == QEvent::FocusOut)
      // Later, so that a click on a suggestion still arrives.
      QTimer::singleShot(250, this, [this] {
        if (!m_edit->hasFocus()) m_listBox->hide();
      });
    if (e->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
      m_listBox->hide();
      return true;
    }
  }
  return QWidget::eventFilter(o, e);
}

void PlaceField::search() {
  const QString text = m_edit->text().trimmed();
  const int id = ++m_searchId;
  QVector<Place> places;
  const auto& nav = NavStore::instance();
  const auto& own = nav.ownShip();
  if (own.valid &&
      (text.isEmpty() || tr("My position").contains(text, Qt::CaseInsensitive)))
    places << Place{tr("My position"), "position", own.lat, own.lon, true};
  for (const Waypoint& w : nav.waypoints())
    if (!w.name.isEmpty() &&
        (text.isEmpty() || w.name.contains(text, Qt::CaseInsensitive)))
      if (places.size() < 6)
        places << Place{w.name, "mark", w.lat, w.lon, true};
  m_results = places;
  showSuggestions(m_results);
  if (text.size() < 2) return;
  QPointer<PlaceField> self(this);
  // Adds results of one source, without duplicates of earlier ones.
  // Ordered by rank: own position and marks, then online results (the
  // initial planning), then offline data.
  const auto merge = [self, id](QVector<Place> more, int rank) {
    if (!self || id != self->m_searchId) return;
    for (Place& p : more) {
      p.rank = rank;
      bool dup = false;
      for (const Place& q : self->m_results)
        dup |= q.name == p.name &&
               geo::distanceNm(q.lat, q.lon, p.lat, p.lon) < 0.5;
      if (!dup) self->m_results << p;
    }
    std::stable_sort(
        self->m_results.begin(), self->m_results.end(),
        [](const Place& a, const Place& b) { return a.rank < b.rank; });
    if (self->m_results.size() > 16) self->m_results.resize(16);
    self->showSuggestions(self->m_results);
  };
  const QPointF near = m_near ? m_near() : QPointF();
  auto& online = OnlineOsm::instance();
  const bool isOnline = online.isEnabled() && online.isOnline();

  // Online: marinas and harbours by name around the chart (Overpass).
  if (isOnline) {
    const GeoBox around{near.y() - 0.75, near.x() - 1.2, near.y() + 0.75,
                        near.x() + 1.2};
    const QString re = qlRegex(text);
    const QString q =
        QStringLiteral(
            "[out:json][timeout:20][bbox:%1];("
            "nwr[\"leisure\"=\"marina\"][\"name\"~\"%2\",i];"
            "nwr[\"seamark:type\"=\"harbour\"][\"name\"~\"%2\",i];"
            "nwr[\"harbour\"=\"yes\"][\"name\"~\"%2\",i];"
            "nwr[\"seamark:type\"=\"anchorage\"][\"name\"~\"%2\",i];"
            ");out center tags 20;")
            .arg(around.overpassBbox(), re);
    online.query(q, [merge](const QByteArray& out, const QString& err) {
      if (!err.isEmpty()) return;
      QVector<Place> found;
      for (const OsmElement& e : OverpassService::parseJson(out))
        if (!e.name().isEmpty())
          found << Place{e.name(), "harbour", e.lat, e.lon, true, tr("online")};
      merge(found, 1);
    });
  }

  // Offline: stored harbour tiles (voyages, recently viewed areas).
  {
    QVector<Place> found;
    for (const OsmElement& e : online.searchCached(text, 12)) {
      QString name = e.name();
      if (name.isEmpty()) name = e.tags.value("seamark:name");
      const bool harbour =
          OsmPoiLayer::classify(e) != OsmPoiLayer::Kind::Seamark;
      found << Place{
          name, harbour ? "harbour" : "mark", e.lat, e.lon, true, tr("stored")};
    }
    merge(found, 3);
  }

  // Harbours, marinas and towns from the offline OSM regions.
  auto& svc = OverpassService::instance();
  const GeoBox box = regionsBox();
  if (box.isValid() && svc.databaseReady()) {
    const QString re = qlRegex(text);
    const QString q =
        QStringLiteral(
            "[out:json][timeout:15][bbox:%1];("
            "nwr[\"leisure\"=\"marina\"][\"name\"~\"%2\",i];"
            "nwr[\"seamark:type\"=\"harbour\"][\"name\"~\"%2\",i];"
            "nwr[\"seamark:type\"=\"harbour\"][\"seamark:name\"~\"%2\",i];"
            "nwr[\"harbour\"=\"yes\"][\"name\"~\"%2\",i];"
            "nwr[\"seamark:type\"=\"anchorage\"][\"name\"~\"%2\",i];"
            "node[\"place\"~\"^(city|town|village|suburb)$\"][\"name\"~"
            "\"%2\",i];"
            ");out center tags 25;")
            .arg(box.overpassBbox(), re);
    svc.query(q, [merge](const QByteArray& out, const QString& err) {
      if (!err.isEmpty()) return;
      QVector<Place> found;
      for (const OsmElement& e : OverpassService::parseJson(out)) {
        QString name = e.name();
        if (name.isEmpty()) name = e.tags.value("seamark:name");
        if (name.isEmpty()) continue;
        const bool town = e.tags.contains("place");
        found << Place{name,  town ? "place" : "harbour",
                       e.lat, e.lon,
                       true,  tr("offline")};
      }
      merge(found, 2);
    });
  }

  // Online: Nominatim finds harbours, marinas and places worldwide,
  // preferring those near the chart.
  online.searchPlaces(
      text, near.y(), near.x(), [merge](const QVector<OsmElement>& elements) {
        QVector<Place> found;
        for (const OsmElement& e : elements) {
          const bool harbour = e.tags.value("leisure") == "marina" ||
                               e.tags.contains("harbour") ||
                               e.tags.value("seamark:type") == "harbour" ||
                               e.tags.value("waterway") == "dock";
          const bool place =
              e.tags.contains("place") || e.tags.contains("boundary");
          // Region, e.g. "Laboe, Kreis Plön".
          const QStringList parts = e.tags.value("display_name").split(", ");
          found << Place{
              e.name(), harbour ? "harbour" : place ? "place" : "mark",
              e.lat,    e.lon,
              true,     parts.mid(1, 2).join(", ")};
        }
        // Harbours first.
        std::stable_sort(found.begin(), found.end(),
                         [](const Place& a, const Place& b) {
                           return (a.kind == "harbour") > (b.kind == "harbour");
                         });
        merge(found, 1);
      });
}

void PlaceField::showSuggestions(const QVector<Place>& places) {
  while (QLayoutItem* it = m_list->takeAt(0)) {
    if (QWidget* w = it->widget()) {
      w->hide();
      w->deleteLater();
    }
    delete it;
  }
  const Palette& pal = Theme::p();
  for (const Place& p : places) {
    auto* row = new ListRow;
    auto* l = new QHBoxLayout(row);
    l->setContentsMargins(10, 6, 10, 6);
    auto* icon = new QLabel;
    icon->setPixmap(Icons::pixmap(kindIcon(p.kind), pal.accent, 18));
    l->addWidget(icon);
    auto* col = new QVBoxLayout;
    col->setSpacing(0);
    auto* name = label(p.name, "value");
    name->setTextFormat(Qt::PlainText);
    col->addWidget(name);
    if (!p.detail.isEmpty()) {
      auto* detail = label(p.detail, "dim");
      detail->setTextFormat(Qt::PlainText);
      col->addWidget(detail);
    }
    l->addLayout(col, 1);
    connect(row, &ListRow::clicked, this, [this, p] { setPlace(p); });
    m_list->addWidget(row);
  }
  m_listBox->setVisible(!places.isEmpty() && m_edit->hasFocus());
}

// --------------------------------------------------------------------------

RoutePlanPanel::RoutePlanPanel(ChartView* chart, QWidget* parent)
    : QWidget(parent), m_chart(chart) {
  m_layer = new PlannedRouteLayer(this);
  chart->addLayer(m_layer);
  connect(m_layer, &MapLayer::changed, chart, qOverload<>(&QWidget::update));

  auto* l = new QVBoxLayout(this);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(8);
  l->addWidget(label(tr("HARBOUR TO HARBOUR"), "section"));

  auto* fields = new QHBoxLayout;
  auto* col = new QVBoxLayout;
  col->setSpacing(6);
  m_from = new PlaceField("locate", tr("From: harbour, marina, mark…"));
  m_to = new PlaceField("flag", tr("To: harbour, marina, mark…"));
  const auto near = [chart] {
    double lat, lon;
    chart->center(lat, lon);
    return QPointF(lon, lat);
  };
  m_from->setNearProvider(near);
  m_to->setNearProvider(near);
  col->addWidget(m_from);
  col->addWidget(m_to);
  fields->addLayout(col, 1);
  auto* swap = new IconButton("refresh", tr("Swap start and destination"));
  connect(swap, &QAbstractButton::clicked, this, [this] {
    const Place a = m_from->place(), b = m_to->place();
    m_from->setPlace(b);
    m_to->setPlace(a);
  });
  fields->addWidget(swap, 0, Qt::AlignTop);
  l->addLayout(fields);

  m_calc = new QPushButton(tr("Plan route"));
  m_calc->setProperty("role", "primary");
  m_calc->setIcon(Icons::icon("route", Qt::white, 18));
  m_calc->setCursor(Qt::PointingHandCursor);
  connect(m_calc, &QPushButton::clicked, this, &RoutePlanPanel::calculate);
  l->addWidget(m_calc);

  m_result = label(QString(), "dim");
  m_result->setWordWrap(true);
  m_result->hide();
  l->addWidget(m_result);

  m_actions = new QWidget;
  auto* al = new QHBoxLayout(m_actions);
  al->setContentsMargins(0, 0, 0, 0);
  auto* saveBtn = new QPushButton(tr("Save"));
  auto* go = new QPushButton(tr("Save and navigate"));
  go->setProperty("role", "primary");
  go->setIcon(Icons::icon("navigate", Qt::white, 18));
  auto* discard = new QPushButton(tr("Discard"));
  discard->setProperty("role", "ghost");
  for (auto* b : {go, saveBtn, discard}) {
    b->setCursor(Qt::PointingHandCursor);
    al->addWidget(b);
  }
  connect(saveBtn, &QPushButton::clicked, this, [this] { save(false); });
  m_withTacks = new QCheckBox(tr("Tacks and gybes as waypoints"));
  m_withTacks->setChecked(
      QSettings().value("Passage/tacksAsWaypoints", true).toBool());
  connect(m_withTacks, &QCheckBox::toggled, this, [](bool on) {
    QSettings().setValue("Passage/tacksAsWaypoints", on);
  });
  connect(go, &QPushButton::clicked, this, [this] { save(true); });
  connect(discard, &QPushButton::clicked, this, &RoutePlanPanel::clearResult);
  auto* offline = new QPushButton(tr("Store voyage for offline use"));
  offline->setIcon(Icons::icon("download", Theme::p().text, 18));
  offline->setToolTip(
      tr("Keeps route, passage plan, weather models, GRIB file and the "
         "harbours and coastline along the route available without "
         "internet"));
  offline->setCursor(Qt::PointingHandCursor);
  connect(offline, &QPushButton::clicked, this, &RoutePlanPanel::storeVoyage);
  auto* actionsCol = new QWidget;
  auto* acl = new QVBoxLayout(actionsCol);
  acl->setContentsMargins(0, 0, 0, 0);
  acl->setSpacing(6);
  acl->addWidget(m_withTacks);
  acl->addWidget(m_actions);
  acl->addWidget(offline);
  m_actions->show();
  m_actionsBox = actionsCol;
  m_actionsBox->hide();

  m_passage = new PassagePanel;
  connect(m_passage, &PassagePanel::planChanged, this, [this] {
    QVector<int> modes;
    for (const PassageLeg& leg : m_passage->plan().legs) modes << int(leg.mode);
    m_layer->setLegModes(modes);
    const SailingPlan& sp = m_passage->sailingPlan();
    m_layer->setSailing(sp.track, sp.events);
  });
  l->addWidget(m_passage);
  l->addWidget(m_actionsBox);

  const auto update = [this] {
    m_calc->setEnabled(m_from->place().valid && m_to->place().valid &&
                       !m_watcher.isRunning() && !m_osmPending);
  };
  connect(m_from, &PlaceField::placeChanged, this, update);
  connect(m_to, &PlaceField::placeChanged, this, update);
  connect(&m_watcher, &QFutureWatcherBase::finished, this,
          &RoutePlanPanel::finished);
  update();
}

void RoutePlanPanel::setFrom(const Place& p) { m_from->setPlace(p); }
void RoutePlanPanel::setTo(const Place& p) { m_to->setPlace(p); }

void RoutePlanPanel::clearResult() {
  m_plan = {};
  m_layer->setPoints({});
  m_passage->setRoute({});
  m_result->hide();
  m_actionsBox->hide();
}

void RoutePlanPanel::calculate() {
  const Place a = m_from->place(), b = m_to->place();
  if (!a.valid || !b.valid || m_watcher.isRunning() || m_osmPending) return;
  clearResult();
  m_calc->setEnabled(false);
  m_calc->setText(tr("Calculating…"));

  RoutePlanRequest req;
  req.fromLat = a.lat;
  req.fromLon = a.lon;
  req.toLat = b.lat;
  req.toLon = b.lon;
  auto land = m_chart->landData();
  const auto run = [this, land](RoutePlanRequest r) {
    m_watcher.setFuture(QtConcurrent::run([land, r]() mutable {
      if (land) r.land = &land->detailed;
      return RoutePlanner::plan(r);
    }));
  };

  // Offline OSM data: coastline and breakwaters, much finer than the
  // basemap, inside the downloaded regions.
  auto& svc = OverpassService::instance();
  const GeoBox area =
      geoBoxOf(RoutePlanner::searchArea(a.lat, a.lon, b.lat, b.lon));
  QVector<QRectF> covered;
  for (const auto& r : svc.regions())
    if (r.box.intersects(area))
      covered << QRectF(geo::toMercator(r.box.north, r.box.west),
                        geo::toMercator(r.box.south, r.box.east))
                     .normalized();
  // Without offline data, the coastline comes from the public servers
  // (when connected and the area is not too large).
  auto& online = OnlineOsm::instance();
  const bool useOnline = !online.coveredOffline(area) && online.isEnabled() &&
                         online.isOnline() && area.areaDeg2() < 4.0;
  if (useOnline)
    covered = {RoutePlanner::searchArea(a.lat, a.lon, b.lat, b.lon)};
  if (covered.isEmpty() || (!svc.databaseReady() && !useOnline)) {
    run(req);
    return;
  }
  m_osmPending = true;
  m_calc->setText(useOnline ? tr("Loading coastline…") : tr("Calculating…"));
  const QString q = QStringLiteral(
                        "[out:json][timeout:60][bbox:%1];("
                        "way[\"natural\"=\"coastline\"];"
                        "way[\"man_made\"~\"^(breakwater|groyne)$\"];"
                        ");out geom;")
                        .arg(area.overpassBbox());
  QPointer<RoutePlanPanel> self(this);
  const auto handle = [self, req, covered, run](const QByteArray& out,
                                                const QString& err) mutable {
    if (!self) return;
    self->m_osmPending = false;
    self->m_calc->setText(tr("Calculating…"));
    if (err.isEmpty()) {
      for (const OsmElement& e : OverpassService::parseJson(out)) {
        if (e.geometry.size() < 2) continue;
        QPolygonF line;
        for (const QPointF& g : e.geometry)
          line << geo::toMercator(g.y(), g.x());
        if (e.tags.value("natural") == "coastline")
          req.coastlines << line;
        else
          req.barriers << line;
      }
      // Only trust OSM instead of the basemap where it has a coastline.
      if (!req.coastlines.isEmpty()) req.landIgnored = covered;
    }
    run(req);
  };
  if (useOnline)
    online.queryArea(area, q, handle);
  else
    svc.query(q, handle);
}

void RoutePlanPanel::finished() {
  if (m_watcher.isFinished() && m_watcher.future().resultCount() > 0 &&
      sender() == &m_watcher)
    m_plan = m_watcher.result();
  m_calc->setText(tr("Plan route"));
  m_calc->setEnabled(m_from->place().valid && m_to->place().valid);
  m_result->show();
  if (!m_plan.ok()) {
    m_result->setText(m_plan.error);
    m_actionsBox->hide();
    return;
  }
  m_result->setText(
      tr("%1 · %n waypoint(s)", nullptr, int(m_plan.points.size()))
          .arg(AppSettings::instance().distanceText(m_plan.lengthNm)));
  m_actionsBox->show();
  m_layer->setPoints(m_plan.points);
  m_passage->setWaterTest(waterTest(m_plan.points));
  m_passage->setRoute(m_plan.points);
  double s = 90, w = 180, n = -90, e = -180;
  for (const QPointF& p : m_plan.points) {
    s = std::min(s, p.y());
    n = std::max(n, p.y());
    w = std::min(w, p.x());
    e = std::max(e, p.x());
  }
  m_chart->fitGeoBounds(s, w, n, e);
}

WaterTest RoutePlanPanel::waterTest(const QVector<QPointF>& route) const {
  auto land = m_chart ? m_chart->landData() : nullptr;
  if (!land || land->detailed.isEmpty() || route.isEmpty()) return {};
  // Only the polygons near the route (lanes of a few NM).
  QRectF box;
  for (const QPointF& g : route) {
    const QPointF m = geo::toMercator(g.y(), g.x());
    box = box.isNull() ? QRectF(m, QSizeF(1e-9, 1e-9))
                       : box.united(QRectF(m, QSizeF(1e-9, 1e-9)));
  }
  const double pad = 10 / geo::nmPerMercatorUnit(route.first().y());
  box.adjust(-pad, -pad, pad, pad);
  QVector<int> near;
  for (int i = 0; i < land->detailed.size(); ++i)
    if (land->detailed[i].bounds.intersects(box)) near << i;
  return [land, near](double lat, double lon) {
    const QPointF m = geo::toMercator(lat, lon);
    for (int i : near) {
      const MercatorPolygon& poly = land->detailed[i];
      if (poly.bounds.contains(m) && poly.path.contains(m)) return false;
    }
    return true;
  };
}

void RoutePlanPanel::save(bool navigate) {
  if (!m_plan.ok()) return;
  const Place a = m_from->place(), b = m_to->place();
  Route r;
  r.name = QStringLiteral("%1 → %2").arg(a.name, b.name);
  // With tacks and gybes as waypoints: follow the boards of the sail plan.
  const SailingPlan& sp = m_passage->sailingPlan();
  if (m_withTacks->isChecked() && sp.tacks + sp.gybes > 0) {
    int wp = 0, tack = 0, gybe = 0;
    for (const SailEvent& e : sp.events) {
      Waypoint w;
      w.lat = e.pos.y();
      w.lon = e.pos.x();
      w.time = e.time;
      switch (e.kind) {
        case SailEvent::Kind::Start:
          w.name = a.name;
          break;
        case SailEvent::Kind::Arrival:
          w.name = b.name;
          break;
        case SailEvent::Kind::Waypoint:
          w.name = QStringLiteral("WP%1").arg(++wp, 2, 10, QChar('0'));
          break;
        case SailEvent::Kind::Tack:
          w.name = tr("T%1").arg(++tack);
          w.description = e.text;
          break;
        case SailEvent::Kind::Gybe:
          w.name = tr("G%1").arg(++gybe);
          w.description = e.text;
          break;
        default:
          continue;
      }
      // Sails of the board that starts here.
      for (const SailBoard& bd : sp.boards)
        if (bd.start >= e.time && e.kind != SailEvent::Kind::Arrival) {
          if (!bd.motor)
            w.description +=
                (w.description.isEmpty() ? "" : " · ") + bd.advice.summary();
          break;
        }
      r.points << w;
    }
    const QString id = NavStore::instance().addRoute(r);
    if (navigate) NavStore::instance().activateRoute(id);
    clearResult();
    return;
  }
  for (int i = 0; i < m_plan.points.size(); ++i) {
    Waypoint w;
    w.lat = m_plan.points[i].y();
    w.lon = m_plan.points[i].x();
    if (i == 0)
      w.name = a.name;
    else if (i == m_plan.points.size() - 1)
      w.name = b.name;
    else
      w.name = QStringLiteral("WP%1").arg(i, 2, 10, QChar('0'));
    // Passage plan: ETA and how the following leg is sailed.
    const auto& legs = m_passage->plan().legs;
    if (i > 0 && i - 1 < legs.size())
      w.time = legs[i - 1].end;
    else if (i == 0 && !legs.isEmpty())
      w.time = legs[0].start;
    if (i < legs.size())
      w.description =
          tr("ETD %1 · %2 · %3° · wind %4 kn")
              .arg(legs[i].start.toLocalTime().toString("ddd HH:mm"),
                   PassagePanel::modeName(legs[i].mode))
              .arg(int(std::round(legs[i].courseDeg)))
              .arg(int(std::round(legs[i].windKn)));
    r.points << w;
  }
  const QString id = NavStore::instance().addRoute(r);
  if (navigate) NavStore::instance().activateRoute(id);
  clearResult();
}

void RoutePlanPanel::storeVoyage() {
  if (!m_plan.ok()) return;
  Voyage v;
  v.fromName = m_from->place().name;
  v.toName = m_to->place().name;
  v.name = QStringLiteral("%1 → %2").arg(v.fromName, v.toName);
  v.route = m_plan.points;
  v.settings = m_passage->saveState();
  v.forecasts = m_passage->forecasts();
  auto& store = VoyageStore::instance();
  const QString id = store.save(v);
  store.prepareOffline(id, OsmPoiLayer::queryTemplate());
}

void RoutePlanPanel::openVoyage(const Voyage& v) {
  if (v.route.size() < 2) return;
  m_from->setPlace(Place{v.fromName, "harbour", v.route.first().y(),
                         v.route.first().x(), true});
  m_to->setPlace(
      Place{v.toName, "harbour", v.route.last().y(), v.route.last().x(), true});
  m_plan = {};
  m_plan.points = v.route;
  m_plan.lengthNm = v.lengthNm();
  m_result->setText(
      tr("%1 · %n waypoint(s) · stored voyage", nullptr, int(v.route.size()))
          .arg(AppSettings::instance().distanceText(m_plan.lengthNm)));
  m_result->show();
  m_actionsBox->show();
  m_layer->setPoints(v.route);
  // The GRIB file stored with the voyage, if none is open.
  if (!v.gribFile.isEmpty() && !GribManager::instance().file())
    GribManager::instance().open(VoyageStore::instance().folder(v.id) + "/" +
                                 v.gribFile);
  m_passage->restoreState(v.settings);
  // Fresh forecasts when online (setRoute loads them), the stored ones
  // otherwise or until they arrive.
  m_passage->setWaterTest(waterTest(v.route));
  m_passage->setRoute(v.route);
  if (!v.forecasts.isEmpty()) m_passage->setForecasts(v.forecasts, true);
  double s = 90, w = 180, n = -90, e = -180;
  for (const QPointF& p : v.route) {
    s = std::min(s, p.y());
    n = std::max(n, p.y());
    w = std::min(w, p.x());
    e = std::max(e, p.x());
  }
  m_chart->fitGeoBounds(s, w, n, e);
}
