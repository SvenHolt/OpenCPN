#include "ui/ChartView.h"

#include <QContextMenuEvent>
#include <QGestureEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPinchGesture>
#include <QSettings>
#include <QWheelEvent>
#include <QtConcurrent>
#include <cmath>

#include "core/AppSettings.h"
#include "core/GeoMath.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

namespace {
constexpr int kLandMargin = 192;  // pixels rendered beyond the viewport
constexpr double kMinScale = 600;
constexpr double kMaxScale = 1 << 24;
constexpr double kDetailScale = 14000;  // switch to the detailed basemap

QPointF wrapWorld(QPointF w) {
  w.setX(w.x() - std::floor(w.x()));
  w.setY(qBound(0.0, w.y(), 1.0));
  return w;
}

void drawHaloText(QPainter& p, const QPointF& pos, const QString& text,
                  const QFont& font, const QColor& fg, const QColor& halo) {
  QPainterPath path;
  path.addText(pos, font, text);
  p.setPen(QPen(halo, 3.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.setBrush(Qt::NoBrush);
  p.drawPath(path);
  p.setPen(Qt::NoPen);
  p.setBrush(fg);
  p.drawPath(path);
}

void drawPill(QPainter& p, const QPointF& anchor, const QString& text,
              const QColor& bg, const QColor& fg) {
  const QFont f = Theme::font(12, QFont::DemiBold);
  const QFontMetricsF fm(f);
  const QRectF r(anchor.x(), anchor.y() - 11, fm.horizontalAdvance(text) + 16,
                 22);
  p.setPen(Qt::NoPen);
  p.setBrush(bg);
  p.drawRoundedRect(r, 11, 11);
  p.setFont(f);
  p.setPen(fg);
  p.drawText(r, Qt::AlignCenter, text);
}
}  // namespace

ChartView::ChartView(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setAttribute(Qt::WA_AcceptTouchEvents);
  grabGesture(Qt::PinchGesture);
  setFocusPolicy(Qt::StrongFocus);

  QSettings s;
  m_center = s.value("View/center", m_center).toPointF();
  m_scale =
      qBound(kMinScale, s.value("View/scale", 4.0e5).toDouble(), kMaxScale);
  m_courseUp = AppSettings::instance().courseUp;

  m_renderTimer.setSingleShot(true);
  m_renderTimer.setInterval(30);
  connect(&m_renderTimer, &QTimer::timeout, this, [this] {
    if (!m_land || m_landWatcher.isRunning()) return;
    m_pendingParams = params();
    m_landWatcher.setFuture(QtConcurrent::run(
        &ChartView::renderLand, m_land, m_pendingParams, Theme::p().land));
  });
  connect(&m_landWatcher, &QFutureWatcher<QImage>::finished, this, [this] {
    m_landImage = m_landWatcher.result();
    m_landParams = m_pendingParams;
    update();
    if (m_renderQueued) {
      m_renderQueued = false;
      if (!(params() == m_landParams)) scheduleLandRender();
    }
  });

  m_kinetic.setDuration(750);
  m_kinetic.setEasingCurve(QEasingCurve::OutCubic);
  m_kinetic.setStartValue(0.0);
  m_kinetic.setEndValue(1.0);
  connect(&m_kinetic, &QVariantAnimation::valueChanged, this,
          [this, applied = std::make_shared<qreal>(0)](const QVariant& v) {
            const qreal t = v.toReal();
            if (t < *applied) *applied = 0;  // restarted
            const QPointF total = m_velocity * 280.0;
            const QPointF step = total * (t - *applied);
            *applied = t;
            if (t >= 1.0) *applied = 0;
            const QTransform inv = worldToScreen().inverted();
            m_center =
                wrapWorld(m_center - (inv.map(step) - inv.map(QPointF(0, 0))));
            viewModified();
          });

  m_viewAnim.setDuration(420);
  m_viewAnim.setEasingCurve(QEasingCurve::InOutCubic);
  m_viewAnim.setStartValue(0.0);
  m_viewAnim.setEndValue(1.0);
  connect(&m_viewAnim, &QVariantAnimation::valueChanged, this,
          [this](const QVariant& v) {
            const double t = v.toDouble();
            QPointF d = m_animToCenter - m_animFromCenter;
            if (d.x() > 0.5) d.rx() -= 1;
            if (d.x() < -0.5) d.rx() += 1;
            m_center = wrapWorld(m_animFromCenter + d * t);
            m_scale = std::exp(
                std::log(m_animFromScale) +
                (std::log(m_animToScale) - std::log(m_animFromScale)) * t);
            viewModified();
          });

  m_shipAnim.setDuration(980);
  m_shipAnim.setStartValue(0.0);
  m_shipAnim.setEndValue(1.0);
  connect(&m_shipAnim, &QVariantAnimation::valueChanged, this,
          [this](const QVariant& v) {
            const double t = v.toDouble();
            m_shipShown = m_shipFrom + (m_shipTo - m_shipFrom) * t;
            if (m_follow) followShip();
            update();
          });

  m_pulseTimer.setInterval(40);
  connect(&m_pulseTimer, &QTimer::timeout, this, [this] {
    m_pulse = std::fmod(m_pulse + 0.025, 1.0);
    update();
  });

  auto& store = NavStore::instance();
  connect(&store, &NavStore::ownShipChanged, this, [this] {
    const auto& own = NavStore::instance().ownShip();
    if (!own.valid) {
      update();
      return;
    }
    const QPointF w = geo::toMercator(own.lat, own.lon);
    const bool jump =
        m_shipShown.isNull() ||
        std::hypot(w.x() - m_shipShown.x(), w.y() - m_shipShown.y()) * m_scale >
            600;
    if (jump) {
      m_shipShown = m_shipFrom = m_shipTo = w;
      if (m_follow) followShip();
      update();
      return;
    }
    m_shipFrom = m_shipShown;
    m_shipTo = w;
    m_shipAnim.stop();
    m_shipAnim.start();
  });
  connect(&store, &NavStore::aisChanged, this, [this] {
    bool danger = false;
    for (const auto& t : NavStore::instance().aisTargets())
      danger |= t.dangerous;
    if (danger && !m_pulseTimer.isActive()) m_pulseTimer.start();
    if (!danger) m_pulseTimer.stop();
    update();
  });
  connect(&store, &NavStore::routesChanged, this,
          qOverload<>(&QWidget::update));
  connect(&store, &NavStore::waypointsChanged, this,
          qOverload<>(&QWidget::update));
  connect(&store, &NavStore::guidanceChanged, this,
          qOverload<>(&QWidget::update));
  connect(&Theme::instance(), &Theme::changed, this, [this] {
    m_landParams.size = QSize();  // force re-render in the new colors
    scheduleLandRender();
    update();
  });
  connect(&AppSettings::instance(), &AppSettings::changed, this, [this] {
    setCourseUp(AppSettings::instance().courseUp);
    update();
  });
}

ChartView::~ChartView() {
  m_landWatcher.waitForFinished();
  QSettings s;
  s.setValue("View/center", m_center);
  s.setValue("View/scale", m_scale);
}

void ChartView::setLandData(std::shared_ptr<const LandData> land) {
  m_land = std::move(land);
  scheduleLandRender();
}

// --------------------------------------------------------------------------
// View transform

ChartView::ViewParams ChartView::params() const {
  return {m_center, m_scale, m_rotation, size()};
}

QTransform ChartView::worldToScreen(const ViewParams& v, double wrap) {
  QTransform t;
  t.translate(v.size.width() / 2.0, v.size.height() / 2.0);
  t.rotate(-v.rotation);
  t.scale(v.scale, v.scale);
  t.translate(-v.center.x() + wrap, -v.center.y());
  return t;
}

QPointF ChartView::screenPosWorld(QPointF w) const {
  const double wrap = -std::round(w.x() - m_center.x());
  return worldToScreen(wrap).map(w);
}

QPointF ChartView::screenPos(double lat, double lon) const {
  return screenPosWorld(geo::toMercator(lat, lon));
}

void ChartView::geoPos(const QPointF& screen, double& lat, double& lon) const {
  geo::fromMercator(wrapWorld(worldToScreen().inverted().map(screen)), lat,
                    lon);
}

void ChartView::center(double& lat, double& lon) const {
  geo::fromMercator(m_center, lat, lon);
}

double ChartView::nmPerPixel() const {
  double lat, lon;
  center(lat, lon);
  return geo::nmPerMercatorUnit(lat) / m_scale;
}

void ChartView::viewModified() {
  scheduleLandRender();
  update();
  emit viewChanged();
}

void ChartView::setScale(double s) {
  m_scale = qBound(kMinScale, s, kMaxScale);
  viewModified();
}

void ChartView::centerOn(double lat, double lon, bool animated) {
  setFollow(false);
  const QPointF c = geo::toMercator(lat, lon);
  if (animated)
    animateTo(c, m_scale);
  else {
    m_center = c;
    viewModified();
  }
}

void ChartView::zoomBy(double factor, bool animated) {
  const double target = qBound(kMinScale, m_scale * factor, kMaxScale);
  if (animated)
    animateTo(m_center, target);
  else
    setScale(target);
}

void ChartView::fitGeoBounds(double lat1, double lon1, double lat2,
                             double lon2) {
  setFollow(false);
  const QRectF r =
      QRectF(geo::toMercator(lat1, lon1), geo::toMercator(lat2, lon2))
          .normalized();
  const double freeW = std::max(100, width() - m_insetLeft - 160);
  const double freeH = std::max(100, height() - m_insetBottom - 200);
  double s = std::min(freeW / std::max(r.width(), 1e-9),
                      freeH / std::max(r.height(), 1e-9));
  s = qBound(kMinScale, std::min(s, 1.5e6), kMaxScale);
  const QPointF desired(m_insetLeft + (width() - m_insetLeft) / 2.0,
                        (height() - m_insetBottom) / 2.0);
  const QPointF v = desired - QPointF(width() / 2.0, height() / 2.0);
  QTransform inv;
  inv.rotate(m_rotation);
  inv.scale(1 / s, 1 / s);
  animateTo(wrapWorld(r.center() - inv.map(v)), s);
}

void ChartView::zoomAt(const QPointF& screen, double factor) {
  const double target = qBound(kMinScale, m_scale * factor, kMaxScale);
  if (m_follow) {
    setScale(target);
    followShip();
    return;
  }
  const QPointF before = worldToScreen().inverted().map(screen);
  m_scale = target;
  const QPointF after = worldToScreen().inverted().map(screen);
  m_center = wrapWorld(m_center + before - after);
  viewModified();
}

void ChartView::animateTo(QPointF center, double scale) {
  m_kinetic.stop();
  m_viewAnim.stop();
  m_animFromCenter = m_center;
  m_animToCenter = center;
  m_animFromScale = m_scale;
  m_animToScale = qBound(kMinScale, scale, kMaxScale);
  m_viewAnim.start();
}

void ChartView::setFollow(bool on) {
  if (on == m_follow) return;
  m_follow = on;
  if (on) {
    m_kinetic.stop();
    const auto& own = NavStore::instance().ownShip();
    if (own.valid) {
      // Fly to the ship, then keep tracking it.
      m_follow = false;
      const QPointF before = m_center;
      m_shipShown = geo::toMercator(own.lat, own.lon);
      m_follow = true;
      followShip();
      const QPointF target = m_center;
      m_center = before;
      animateTo(target, m_scale);
    }
  }
  emit followChanged(on);
}

void ChartView::setCourseUp(bool on) {
  if (m_courseUp == on) return;
  m_courseUp = on;
  if (!on) m_rotation = 0;
  if (m_follow) followShip();
  viewModified();
}

void ChartView::followShip() {
  if (m_viewAnim.state() == QAbstractAnimation::Running &&
      m_animToScale != m_animFromScale)
    return;  // let a zoom animation finish first
  const auto& own = NavStore::instance().ownShip();
  if (m_courseUp && own.valid) {
    const double target = own.hdg == own.hdg   ? own.hdg
                          : own.cog == own.cog ? own.cog
                                               : m_rotation;
    m_rotation = geo::normalizeDeg(m_rotation +
                                   geo::angleDiff(m_rotation, target) * 0.15);
  }
  // Place the ship in the middle of the free area; in course-up mode lower
  // so more of the water ahead is visible.
  const double freeW = width() - m_insetLeft;
  const double freeH = height() - m_insetBottom;
  const QPointF desired(m_insetLeft + freeW / 2,
                        m_courseUp ? freeH * 0.70 : freeH / 2);
  const QPointF v = desired - QPointF(width() / 2.0, height() / 2.0);
  QTransform inv;
  inv.rotate(m_rotation);
  inv.scale(1 / m_scale, 1 / m_scale);
  m_center = wrapWorld(m_shipShown - inv.map(v));
  if (m_viewAnim.state() == QAbstractAnimation::Running) {
    m_animToCenter = m_center;
    m_center = m_animFromCenter;
    return;
  }
  viewModified();
}

// --------------------------------------------------------------------------
// Land rendering

void ChartView::scheduleLandRender() {
  if (!m_land) return;
  if (m_landWatcher.isRunning()) {
    m_renderQueued = true;
    return;
  }
  if (!m_renderTimer.isActive()) m_renderTimer.start();
}

QImage ChartView::renderLand(std::shared_ptr<const LandData> land, ViewParams v,
                             QColor color) {
  const QSize imgSize = v.size + QSize(2 * kLandMargin, 2 * kLandMargin);
  QImage img(imgSize, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::transparent);
  if (!land || v.size.isEmpty()) return img;
  ViewParams iv = v;
  iv.size = imgSize;

  const auto& polys = (v.scale < kDetailScale || land->detailed.isEmpty())
                          ? land->crude
                          : land->detailed;
  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing);
  p.setBrush(color);
  // A hairline in the fill color hides seams between basemap tiles.
  QPen pen(color, 1.0);
  pen.setCosmetic(true);
  p.setPen(pen);
  const QRectF imgRect(QPointF(0, 0), QSizeF(imgSize));
  const double minExtent = 0.8 / v.scale;
  for (int wrap = -1; wrap <= 1; ++wrap) {
    const QTransform t = worldToScreen(iv, wrap);
    const QRectF visible = t.inverted().mapRect(imgRect);
    if (!visible.intersects(QRectF(0, 0, 1, 1))) continue;
    p.setTransform(t);
    for (const auto& poly : polys) {
      if (!poly.bounds.intersects(visible)) continue;
      if (poly.bounds.width() < minExtent && poly.bounds.height() < minExtent)
        continue;
      p.drawPath(poly.path);
    }
  }
  return img;
}

// --------------------------------------------------------------------------
// Painting

void ChartView::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  QPainter p(this);
  p.fillRect(rect(), pal.water);

  if (!m_landImage.isNull()) {
    ViewParams lp = m_landParams;
    lp.size += QSize(2 * kLandMargin, 2 * kLandMargin);
    const QTransform m = worldToScreen(lp).inverted() * worldToScreen(params());
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setTransform(m);
    p.drawImage(0, 0, m_landImage);
    p.restore();
  }
  if (!(m_landParams == params())) scheduleLandRender();

  // Vignette that makes floating panels stand out from the map.
  QRadialGradient vg(rect().center(), std::max(width(), height()) * 0.75);
  vg.setColorAt(0.55, QColor(0, 0, 0, 0));
  vg.setColorAt(1.0, QColor(0, 0, 0, pal.dark ? 120 : 28));
  p.fillRect(rect(), vg);

  p.setRenderHint(QPainter::Antialiasing);
  if (AppSettings::instance().showGraticule) drawGraticule(p);
  drawRoutes(p);
  drawWaypoints(p);
  drawPendingRoute(p);
  drawAis(p);
  drawOwnShip(p);
  drawScaleBar(p);
}

void ChartView::drawGraticule(QPainter& p) {
  const Palette& pal = Theme::p();
  // Visible geographic bounds from the four screen corners.
  double minLat = 90, maxLat = -90, minLon = 1e9, maxLon = -1e9;
  const QTransform inv = worldToScreen().inverted();
  for (QPointF c : {QPointF(0, 0), QPointF(width(), 0), QPointF(0, height()),
                    QPointF(width(), height())}) {
    const QPointF w = inv.map(c);
    double lat, lon;
    geo::fromMercator(QPointF(w.x(), qBound(0.0, w.y(), 1.0)), lat, lon);
    minLat = std::min(minLat, lat);
    maxLat = std::max(maxLat, lat);
    minLon = std::min(minLon, lon);
    maxLon = std::max(maxLon, lon);
  }
  static const double kStepsMin[] = {0.5, 1,   2,   5,   10,   15,  30,
                                     60,  120, 300, 600, 1200, 1800};
  const double wantDeg = 170 * nmPerPixel() / 60.0;
  double step = 30;
  for (double m : kStepsMin)
    if (m / 60.0 >= wantDeg) {
      step = m / 60.0;
      break;
    }
  if (maxLon - minLon > 720) return;

  p.save();
  QPen pen(pal.grid, 1);
  p.setPen(pen);
  const QTransform t = worldToScreen();
  QVector<QPair<double, QPointF>> latLabels, lonLabels;
  for (double lat = std::floor(minLat / step) * step; lat <= maxLat;
       lat += step) {
    if (std::fabs(lat) > geo::kMaxMercatorLat) continue;
    const double y = geo::toMercator(lat, 0).y();
    const QPointF a = t.map(QPointF((minLon + 180) / 360.0, y));
    const QPointF b = t.map(QPointF((maxLon + 180) / 360.0, y));
    p.drawLine(a, b);
    if (m_rotation == 0) latLabels.push_back({lat, QPointF(0, a.y())});
  }
  for (double lon = std::floor(minLon / step) * step; lon <= maxLon;
       lon += step) {
    const double x = (lon + 180) / 360.0;
    const QPointF a = t.map(QPointF(x, geo::toMercator(maxLat, 0).y()));
    const QPointF b = t.map(QPointF(x, geo::toMercator(minLat, 0).y()));
    p.drawLine(a, b);
    if (m_rotation == 0) lonLabels.push_back({lon, QPointF(a.x(), 0)});
  }
  p.setFont(Theme::font(10, QFont::DemiBold));
  p.setPen(pal.gridText);
  auto fmt = [step](double v, QChar pos, QChar neg) {
    double a = std::fabs(v);
    int d = int(a + 1e-9);
    int m = int(std::round((a - d) * 60));
    if (m == 60) {
      ++d;
      m = 0;
    }
    QString s = QString::number(d) + "°";
    if (step < 1) s += QString::number(m).rightJustified(2, '0') + "′";
    return s + (v >= 0 ? pos : neg);
  };
  for (const auto& l : latLabels)
    p.drawText(QPointF(m_insetLeft + 10, l.second.y() - 5),
               fmt(l.first, 'N', 'S'));
  for (const auto& l : lonLabels)
    p.drawText(QPointF(l.second.x() + 5, height() - m_insetBottom - 8),
               fmt(l.first, 'E', 'W'));
  p.restore();
}

void ChartView::drawRoutes(QPainter& p) {
  const Palette& pal = Theme::p();
  const auto& store = NavStore::instance();
  const Guidance& g = store.guidance();
  const QString activeId = store.activeRouteId();
  const QFont labelFont = Theme::font(11, QFont::DemiBold);

  for (const Route& r : store.routes()) {
    if (!r.visible || r.points.isEmpty()) continue;
    const bool active = r.id == activeId && g.active;
    QVector<QPointF> pts;
    pts.reserve(r.points.size());
    for (const auto& w : r.points) pts << screenPos(w.lat, w.lon);

    for (int i = 1; i < pts.size(); ++i) {
      const bool passed = active && i < g.legIndex;
      const bool current = active && i >= g.legIndex;
      if (current) {
        p.setPen(QPen(
            QColor(pal.route.red(), pal.route.green(), pal.route.blue(), 70),
            14, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(pts[i - 1], pts[i]);
        p.setPen(QPen(pal.route, 6, Qt::SolidLine, Qt::RoundCap));
      } else {
        QColor c = active || passed ? pal.routeInactive : pal.route;
        if (!active) c.setAlpha(170);
        if (passed) c.setAlpha(110);
        p.setPen(QPen(c, 3.5, Qt::SolidLine, Qt::RoundCap));
      }
      p.drawLine(pts[i - 1], pts[i]);
    }
    for (int i = 0; i < pts.size(); ++i) {
      const bool target = active && i == g.legIndex;
      p.setPen(QPen(target ? QColor(Qt::white) : pal.route, target ? 3 : 2.5));
      p.setBrush(target ? pal.route : pal.water);
      p.drawEllipse(pts[i], target ? 9 : 6, target ? 9 : 6);
      if (m_scale > 20000 || target)
        drawHaloText(p, pts[i] + QPointF(12, 4), r.points[i].name, labelFont,
                     target ? pal.text : pal.textDim, pal.water);
    }
  }

  // Direct line from own ship to the active waypoint.
  if (g.active && store.ownShip().valid) {
    const Route* r = store.route(activeId);
    if (r && g.legIndex < r->points.size()) {
      const auto& wp = r->points[g.legIndex];
      QPen pen(pal.accentBright, 2, Qt::DashLine, Qt::RoundCap);
      pen.setDashPattern({2, 4});
      p.setPen(pen);
      p.drawLine(screenPosWorld(m_shipShown), screenPos(wp.lat, wp.lon));
    }
  }
}

void ChartView::drawWaypoints(QPainter& p) {
  const Palette& pal = Theme::p();
  const QFont labelFont = Theme::font(12, QFont::DemiBold);
  for (const Waypoint& w : NavStore::instance().waypoints()) {
    const QPointF s = screenPos(w.lat, w.lon);
    if (!rect().adjusted(-40, -40, 40, 40).contains(s.toPoint())) continue;
    const bool mob = w.symbol == "mob";
    const QColor c = mob ? pal.danger : pal.warning;
    // Map pin: teardrop pointing at the position.
    QPainterPath pin;
    pin.moveTo(s);
    pin.cubicTo(s + QPointF(-4, -8), s + QPointF(-11, -13),
                s + QPointF(-11, -21));
    pin.arcTo(QRectF(s.x() - 11, s.y() - 32, 22, 22), 180, -180);
    pin.cubicTo(s + QPointF(11, -13), s + QPointF(4, -8), s);
    p.setPen(QPen(pal.dark ? QColor(0, 0, 0, 120) : QColor(255, 255, 255), 2));
    p.setBrush(c);
    p.drawPath(pin);
    if (mob) {
      Icons::paint(&p, "lifebuoy", QRectF(s.x() - 8, s.y() - 29, 16, 16),
                   Qt::white);
    } else {
      p.setPen(Qt::NoPen);
      p.setBrush(Qt::white);
      p.drawEllipse(QPointF(s.x(), s.y() - 21), 4, 4);
    }
    drawHaloText(p, s + QPointF(14, -16), w.name, labelFont, pal.text,
                 pal.water);
  }
}

void ChartView::drawPendingRoute(QPainter& p) {
  if (m_mode != Mode::RouteCreate) return;
  const Palette& pal = Theme::p();
  QVector<QPointF> pts;
  for (const auto& w : m_pending) pts << screenPos(w.lat, w.lon);
  QPen pen(pal.accentBright, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);
  if (pts.size() > 1) p.drawPolyline(pts.constData(), int(pts.size()));
  if (!pts.isEmpty() && rect().contains(m_mousePos.toPoint())) {
    QPen rubber(pal.accentBright, 2, Qt::DashLine, Qt::RoundCap);
    p.setPen(rubber);
    p.drawLine(pts.last(), m_mousePos);
    double lat, lon;
    geoPos(m_mousePos, lat, lon);
    const auto& last = m_pending.last();
    const double d = geo::distanceNm(last.lat, last.lon, lat, lon);
    const double b = geo::bearingDeg(last.lat, last.lon, lat, lon);
    drawPill(p, m_mousePos + QPointF(16, 18),
             QStringLiteral("%1 · %2°")
                 .arg(AppSettings::instance().distanceText(d))
                 .arg(int(std::round(b)) % 360, 3, 10, QChar('0')),
             pal.accent, Qt::white);
  }
  for (int i = 0; i < pts.size(); ++i) {
    p.setPen(QPen(Qt::white, 2.5));
    p.setBrush(pal.accent);
    p.drawEllipse(pts[i], 8, 8);
    p.setPen(Qt::white);
    p.setFont(Theme::font(9, QFont::Bold));
    p.drawText(QRectF(pts[i].x() - 8, pts[i].y() - 8, 16, 16), Qt::AlignCenter,
               QString::number(i + 1));
  }
}

void ChartView::drawAis(QPainter& p) {
  const Palette& pal = Theme::p();
  const auto& cfg = AppSettings::instance();
  const double nmPx = nmPerPixel();
  const QFont labelFont = Theme::font(11, QFont::DemiBold);
  for (const AisTarget& t : NavStore::instance().aisTargets()) {
    if (!t.hasPosition()) continue;
    const QPointF s = screenPos(t.lat, t.lon);
    if (!rect().adjusted(-60, -60, 60, 60).contains(s.toPoint())) continue;
    const bool moving = t.sog == t.sog && t.sog > 0.3;
    const double course = t.cog == t.cog ? t.cog : t.hdg == t.hdg ? t.hdg : 0;
    QColor fill = t.dangerous ? pal.danger : pal.ais;
    if (!moving && !t.dangerous) fill.setAlpha(150);

    if (t.dangerous) {
      // Pulsing warning ring.
      QColor ring = pal.danger;
      ring.setAlphaF(0.6 * (1 - m_pulse));
      p.setPen(QPen(ring, 2));
      p.setBrush(Qt::NoBrush);
      p.drawEllipse(s, 14 + 22 * m_pulse, 14 + 22 * m_pulse);
    }
    if (moving) {
      const double len = t.sog * cfg.predictorMinutes / 60.0 / nmPx;
      const double a = (course - m_rotation) * M_PI / 180;
      QPen vp(fill, 1.6, Qt::SolidLine, Qt::RoundCap);
      p.setPen(vp);
      p.drawLine(s, s + QPointF(std::sin(a), -std::cos(a)) * len);
    }
    p.save();
    p.translate(s);
    p.rotate(course - m_rotation);
    QPainterPath tri;
    const double k = moving ? 1.0 : 0.8;
    tri.moveTo(0, -12 * k);
    tri.lineTo(7.5 * k, 9 * k);
    tri.lineTo(0, 5 * k);
    tri.lineTo(-7.5 * k, 9 * k);
    tri.closeSubpath();
    p.setPen(
        QPen(pal.aisBorder, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(fill);
    p.drawPath(tri);
    p.restore();
    if (t.mmsi == m_selectedAis) {
      p.setPen(QPen(pal.accentBright, 2.5));
      p.setBrush(Qt::NoBrush);
      p.drawEllipse(s, 18, 18);
    }
    if (cfg.showAisNames && m_scale > 30000)
      drawHaloText(p, s + QPointF(14, 4), t.displayName(), labelFont,
                   t.dangerous ? pal.danger : pal.textDim, pal.water);
  }
}

void ChartView::drawOwnShip(QPainter& p) {
  const auto& own = NavStore::instance().ownShip();
  if (m_shipShown.isNull()) return;
  const Palette& pal = Theme::p();
  const QPointF s = screenPosWorld(m_shipShown);
  const double heading = own.hdg == own.hdg   ? own.hdg
                         : own.cog == own.cog ? own.cog
                                              : 0;
  const QColor color = own.valid ? pal.ownShip : pal.textFaint;

  // Course predictor
  if (own.valid && own.sog == own.sog && own.sog > 0.2 && own.cog == own.cog) {
    const double len = own.sog * AppSettings::instance().predictorMinutes /
                       60.0 / nmPerPixel();
    const double a = (own.cog - m_rotation) * M_PI / 180;
    const QPointF end = s + QPointF(std::sin(a), -std::cos(a)) * len;
    QLinearGradient lg(s, end);
    lg.setColorAt(0, color);
    QColor fade = color;
    fade.setAlpha(40);
    lg.setColorAt(1, fade);
    p.setPen(QPen(QBrush(lg), 3, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(s, end);
  }

  // Halo
  QRadialGradient halo(s, 46);
  QColor h = color;
  h.setAlpha(pal.dark ? 85 : 60);
  halo.setColorAt(0, h);
  h.setAlpha(0);
  halo.setColorAt(1, h);
  p.setPen(Qt::NoPen);
  p.setBrush(halo);
  p.drawEllipse(s, 46, 46);

  // Navigation arrow
  p.save();
  p.translate(s);
  p.rotate(heading - m_rotation);
  QPainterPath arrow;
  arrow.moveTo(0, -17);
  arrow.lineTo(12.5, 13);
  arrow.lineTo(0, 6.5);
  arrow.lineTo(-12.5, 13);
  arrow.closeSubpath();
  QLinearGradient fill(0, -17, 0, 13);
  fill.setColorAt(0, color.lighter(125));
  fill.setColorAt(1, color);
  p.setPen(QPen(Qt::white, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.setBrush(fill);
  p.drawPath(arrow);
  p.restore();
}

void ChartView::drawScaleBar(QPainter& p) {
  const Palette& pal = Theme::p();
  const auto& cfg = AppSettings::instance();
  const bool km = cfg.distanceUnit == AppSettings::DistanceUnit::Kilometers;
  const double unitPerPx = nmPerPixel() * (km ? 1.852 : 1.0);
  static const double kNice[] = {0.01, 0.02, 0.05, 0.1,  0.2,  0.5,
                                 1,    2,    5,    10,   20,   50,
                                 100,  200,  500,  1000, 2000, 5000};
  double len = kNice[0];
  for (double n : kNice)
    if (n / unitPerPx <= 110) len = n;
  const double px = len / unitPerPx;
  const QPointF o(m_insetLeft + 24, height() - m_insetBottom - 42);
  QString label;
  if (len < (km ? 1 : 0.1))
    label = QStringLiteral("%1 m").arg(len * (km ? 1000 : 1852), 0, 'f', 0);
  else
    label = QStringLiteral("%1 %2").arg(len).arg(km ? "km" : "NM");
  p.setPen(QPen(pal.textDim, 2, Qt::SolidLine, Qt::RoundCap));
  p.drawLine(o, o + QPointF(px, 0));
  p.drawLine(o + QPointF(0, -4), o + QPointF(0, 4));
  p.drawLine(o + QPointF(px, -4), o + QPointF(px, 4));
  p.setFont(Theme::font(11, QFont::DemiBold));
  p.drawText(QPointF(o.x() + px + 10, o.y() + 4), label);
}

// --------------------------------------------------------------------------
// Interaction

void ChartView::resizeEvent(QResizeEvent*) {
  if (m_follow) followShip();
  viewModified();
}

void ChartView::mousePressEvent(QMouseEvent* e) {
  m_kinetic.stop();
  m_viewAnim.stop();
  m_pressPos = m_lastPos = e->position();
  m_dragging = false;
  m_velocity = {};
  m_moveClock.restart();
}

void ChartView::mouseMoveEvent(QMouseEvent* e) {
  m_mousePos = e->position();
  if (!(e->buttons() & Qt::LeftButton)) {
    if (m_mode == Mode::RouteCreate) update();
    return;
  }
  const QPointF d = e->position() - m_lastPos;
  if (!m_dragging && (e->position() - m_pressPos).manhattanLength() < 6) return;
  if (!m_dragging) {
    m_dragging = true;
    setFollow(false);
    setCursor(Qt::ClosedHandCursor);
  }
  const qint64 dt = std::max<qint64>(1, m_moveClock.restart());
  m_velocity = m_velocity * 0.4 + (d / double(dt)) * 0.6;
  const QTransform inv = worldToScreen().inverted();
  m_center = wrapWorld(m_center - (inv.map(d) - inv.map(QPointF(0, 0))));
  m_lastPos = e->position();
  viewModified();
}

void ChartView::mouseReleaseEvent(QMouseEvent* e) {
  if (m_dragging) {
    m_dragging = false;
    if (m_mode == Mode::RouteCreate)
      setCursor(Qt::CrossCursor);
    else
      unsetCursor();
    if (m_moveClock.elapsed() < 80 &&
        std::hypot(m_velocity.x(), m_velocity.y()) > 0.25)
      m_kinetic.start();
    return;
  }
  if (e->button() != Qt::LeftButton) return;
  const QPointF pos = e->position();

  if (m_mode == Mode::RouteCreate) {
    Waypoint w;
    geoPos(pos, w.lat, w.lon);
    w.name =
        QStringLiteral("RP%1").arg(m_pending.size() + 1, 2, 10, QChar('0'));
    m_pending.push_back(w);
    emit routeEditChanged(int(m_pending.size()));
    update();
    return;
  }
  // Hit test: AIS targets first, then marks.
  quint32 bestMmsi = 0;
  double best = 20;
  for (const auto& t : NavStore::instance().aisTargets()) {
    if (!t.hasPosition()) continue;
    const QPointF s = screenPos(t.lat, t.lon);
    const double d = std::hypot(s.x() - pos.x(), s.y() - pos.y());
    if (d < best) {
      best = d;
      bestMmsi = t.mmsi;
    }
  }
  if (bestMmsi) {
    emit aisTargetClicked(bestMmsi);
    return;
  }
  for (const auto& w : NavStore::instance().waypoints()) {
    const QPointF s = screenPos(w.lat, w.lon) - QPointF(0, 18);
    if (std::hypot(s.x() - pos.x(), s.y() - pos.y()) < 18) {
      emit waypointClicked(w.id);
      return;
    }
  }
  emit aisTargetClicked(0);
}

void ChartView::mouseDoubleClickEvent(QMouseEvent* e) {
  if (m_mode == Mode::RouteCreate) return;
  const double f = 2.0;
  const QPointF w = worldToScreen().inverted().map(e->position());
  if (m_follow) {
    zoomBy(f);
    return;
  }
  animateTo(wrapWorld(w + (m_center - w) / f), m_scale * f);
}

void ChartView::wheelEvent(QWheelEvent* e) {
  m_viewAnim.stop();
  const double f = std::pow(1.0018, e->angleDelta().y());
  zoomAt(e->position(), f);
  e->accept();
}

void ChartView::contextMenuEvent(QContextMenuEvent* e) {
  double lat, lon;
  geoPos(e->pos(), lat, lon);
  emit contextRequested(lat, lon, e->globalPos());
}

bool ChartView::event(QEvent* e) {
  if (e->type() == QEvent::Gesture) {
    handleGesture(static_cast<QGestureEvent*>(e));
    return true;
  }
  return QWidget::event(e);
}

void ChartView::handleGesture(QGestureEvent* e) {
  if (auto* pinch = static_cast<QPinchGesture*>(e->gesture(Qt::PinchGesture))) {
    if (pinch->changeFlags() & QPinchGesture::ScaleFactorChanged)
      zoomAt(mapFromGlobal(pinch->centerPoint().toPoint()),
             pinch->scaleFactor());
    e->accept(pinch);
  }
}

// --------------------------------------------------------------------------
// Route creation

void ChartView::startRouteCreate() {
  m_mode = Mode::RouteCreate;
  m_pending.clear();
  setCursor(Qt::CrossCursor);
  emit routeEditChanged(0);
  update();
}

void ChartView::undoRoutePoint() {
  if (!m_pending.isEmpty()) m_pending.removeLast();
  emit routeEditChanged(int(m_pending.size()));
  update();
}

QVector<Waypoint> ChartView::finishRouteCreate() {
  m_mode = Mode::Browse;
  unsetCursor();
  QVector<Waypoint> pts = m_pending;
  m_pending.clear();
  update();
  return pts;
}
