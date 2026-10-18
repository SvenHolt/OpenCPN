#include "ui/RoutePlanPanel.h"

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
#include "core/NavStore.h"
#include "core/osm/Overpass.h"
#include "ui/ChartView.h"
#include "ui/DrawerPages.h"
#include "ui/GlassPanel.h"
#include "ui/Icons.h"
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

QString durationText(double hours) {
  const int mins = int(std::round(hours * 60));
  if (mins < 60) return QObject::tr("%1 min").arg(mins);
  return QStringLiteral("%1 h %2 min")
      .arg(mins / 60)
      .arg(mins % 60, 2, 10, QChar('0'));
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
  p.setPen(
      QPen(pal.accentBright, 3, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
  p.drawPolyline(line);
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
  showSuggestions(places);

  // Harbours, marinas and towns from the offline OSM regions.
  auto& svc = OverpassService::instance();
  const GeoBox box = regionsBox();
  if (text.size() < 2 || !box.isValid() || !svc.databaseReady()) return;
  const QString re = qlRegex(text);
  const QString q =
      QStringLiteral(
          "[out:json][timeout:15][bbox:%1];("
          "nwr[\"leisure\"=\"marina\"][\"name\"~\"%2\",i];"
          "nwr[\"seamark:type\"=\"harbour\"][\"name\"~\"%2\",i];"
          "nwr[\"seamark:type\"=\"harbour\"][\"seamark:name\"~\"%2\",i];"
          "nwr[\"harbour\"=\"yes\"][\"name\"~\"%2\",i];"
          "nwr[\"seamark:type\"=\"anchorage\"][\"name\"~\"%2\",i];"
          "node[\"place\"~\"^(city|town|village|suburb)$\"][\"name\"~\"%2\",i];"
          ");out center tags 25;")
          .arg(box.overpassBbox(), re);
  QPointer<PlaceField> self(this);
  svc.query(q, [self, id, places](const QByteArray& out, const QString& err) {
    if (!self || id != self->m_searchId || !err.isEmpty()) return;
    QVector<Place> all = places;
    QSet<QString> seen;
    for (const OsmElement& e : OverpassService::parseJson(out)) {
      QString name = e.name();
      if (name.isEmpty()) name = e.tags.value("seamark:name");
      if (name.isEmpty() || seen.contains(name)) continue;
      seen.insert(name);
      const bool town = e.tags.contains("place");
      all << Place{name, town ? "place" : "harbour", e.lat, e.lon, true};
      if (all.size() >= 12) break;
    }
    self->showSuggestions(all);
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
    auto* name = label(p.name, "value");
    name->setTextFormat(Qt::PlainText);
    l->addWidget(name, 1);
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

  auto* row = new QHBoxLayout;
  row->addWidget(label(tr("Speed"), "dim"));
  m_speed = new QDoubleSpinBox;
  m_speed->setRange(1, 40);
  m_speed->setDecimals(1);
  m_speed->setSuffix(tr(" kn"));
  m_speed->setValue(QSettings().value("Routes/planSpeed", 6.0).toDouble());
  connect(m_speed, &QDoubleSpinBox::valueChanged, this, [this](double v) {
    QSettings().setValue("Routes/planSpeed", v);
    if (m_plan.ok()) finished();
  });
  row->addWidget(m_speed);
  m_calc = new QPushButton(tr("Plan route"));
  m_calc->setProperty("role", "primary");
  m_calc->setIcon(Icons::icon("route", Qt::white, 18));
  m_calc->setCursor(Qt::PointingHandCursor);
  connect(m_calc, &QPushButton::clicked, this, &RoutePlanPanel::calculate);
  row->addWidget(m_calc, 1);
  l->addLayout(row);

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
  connect(go, &QPushButton::clicked, this, [this] { save(true); });
  connect(discard, &QPushButton::clicked, this, &RoutePlanPanel::clearResult);
  m_actions->hide();
  l->addWidget(m_actions);

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
  m_result->hide();
  m_actions->hide();
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
  if (covered.isEmpty() || !svc.databaseReady()) {
    run(req);
    return;
  }
  m_osmPending = true;
  const QString q = QStringLiteral(
                        "[out:json][timeout:60][bbox:%1];("
                        "way[\"natural\"=\"coastline\"];"
                        "way[\"man_made\"~\"^(breakwater|groyne)$\"];"
                        ");out geom;")
                        .arg(area.overpassBbox());
  QPointer<RoutePlanPanel> self(this);
  svc.query(q, [self, req, covered, run](const QByteArray& out,
                                         const QString& err) mutable {
    if (!self) return;
    self->m_osmPending = false;
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
  });
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
    m_actions->hide();
    return;
  }
  const double hours = m_plan.lengthNm / std::max(0.5, m_speed->value());
  m_result->setText(
      tr("%1 · %2 at %3 · %n waypoint(s)", nullptr, int(m_plan.points.size()))
          .arg(AppSettings::instance().distanceText(m_plan.lengthNm),
               durationText(hours),
               AppSettings::instance().speedText(m_speed->value())));
  m_actions->show();
  if (sender() != &m_watcher) return;  // only the speed changed
  m_layer->setPoints(m_plan.points);
  double s = 90, w = 180, n = -90, e = -180;
  for (const QPointF& p : m_plan.points) {
    s = std::min(s, p.y());
    n = std::max(n, p.y());
    w = std::min(w, p.x());
    e = std::max(e, p.x());
  }
  m_chart->fitGeoBounds(s, w, n, e);
}

void RoutePlanPanel::save(bool navigate) {
  if (!m_plan.ok()) return;
  const Place a = m_from->place(), b = m_to->place();
  Route r;
  r.name = QStringLiteral("%1 → %2").arg(a.name, b.name);
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
    r.points << w;
  }
  const QString id = NavStore::instance().addRoute(r);
  if (navigate) NavStore::instance().activateRoute(id);
  clearResult();
}
