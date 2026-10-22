#include "ui/CockpitView.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>

#include "core/AppSettings.h"
#include "core/BoatConfig.h"
#include "core/GeoMath.h"
#include "core/NavStore.h"
#include "ui/GlassPanel.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/WeatherPage.h"

namespace {
constexpr int kColumn = 290;  // width of the side card columns
constexpr int kGap = 24;

QPointF dir(double deg) {
  const double r = deg * M_PI / 180;
  return {std::sin(r), -std::cos(r)};
}

double smoothAngle(double current, double target, double k) {
  return geo::normalizeDeg(current + geo::angleDiff(current, target) * k);
}

QString deg3(double v) {
  if (std::isnan(v)) return QStringLiteral("–––°");
  return QStringLiteral("%1°").arg(int(std::lround(geo::normalizeDeg(v))) % 360,
                                   3, 10, QChar('0'));
}

int beaufort(double kn) {
  static const double limits[] = {1, 4, 7, 11, 17, 22, 28, 34, 41, 48, 56, 64};
  int b = 0;
  while (b < 12 && kn >= limits[b]) ++b;
  return b;
}

void label(QPainter& p, const QRectF& r, const QString& text,
           Qt::Alignment a = Qt::AlignLeft | Qt::AlignVCenter) {
  p.setFont(Theme::font(10, QFont::DemiBold));
  p.setPen(Theme::p().textFaint);
  p.drawText(r, a, text.toUpper());
}

/** Big number with a smaller unit right after it. */
void bigValue(QPainter& p, const QPointF& baseline, const QString& value,
              const QString& unit, double size, QColor color = QColor()) {
  const Palette& pal = Theme::p();
  const QFont f = Theme::numberFont(size, QFont::Light);
  p.setFont(f);
  p.setPen(color.isValid() ? color : pal.text);
  p.drawText(baseline, value);
  if (unit.isEmpty()) return;
  const double w = QFontMetricsF(f).horizontalAdvance(value);
  p.setFont(Theme::font(size * 0.28, QFont::DemiBold));
  p.setPen(pal.textDim);
  p.drawText(baseline + QPointF(w + 6, 0), unit);
}
}  // namespace

CockpitView::CockpitView(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent);
  m_timer.setInterval(16);
  connect(&m_timer, &QTimer::timeout, this, &CockpitView::step);
  auto* rng = QRandomGenerator::global();
  for (int i = 0; i < 140; ++i)
    m_wind.push_back(
        {QPointF(rng->bounded(1.0), rng->bounded(1.0)), rng->bounded(1.0)});
  for (int i = 0; i < 60; ++i)
    m_foam.push_back(QPointF(rng->bounded(2.0) - 1, rng->bounded(2.0) - 1));
  m_clock.start();
}

void CockpitView::showEvent(QShowEvent*) {
  m_clock.restart();
  m_timer.start();
}

void CockpitView::hideEvent(QHideEvent*) { m_timer.stop(); }

void CockpitView::resizeEvent(QResizeEvent*) {
  // Particles are kept in normalized coordinates; scale on first use.
  for (auto& w : m_wind)
    if (w.pos.x() <= 1.0 && w.pos.y() <= 1.0)
      w.pos = QPointF(w.pos.x() * width(), w.pos.y() * height());
}

void CockpitView::step() {
  const double dt = std::min(0.1, m_clock.restart() / 1000.0);
  m_time += dt;
  const auto& store = NavStore::instance();
  const auto& own = store.ownShip();
  const auto& env = store.environment();
  const double hdg = own.hdg == own.hdg   ? own.hdg
                     : own.cog == own.cog ? own.cog
                                          : m_hdg;
  const double k = std::min(1.0, dt * 3);
  if (m_first) {
    m_hdg = hdg;
    m_first = false;
  }
  m_hdg = smoothAngle(m_hdg, hdg, k);
  if (env.hasWind())
    m_awa = geo::angleDiff(0, m_awa + geo::angleDiff(m_awa, env.awa) * k);
  if (own.sog == own.sog) m_sog += (own.sog - m_sog) * k;

  // Heel from the side force of the wind on the sails.
  const SailConfig& sails = BoatConfig::instance().sails();
  double heel = 0;
  if (env.hasWind() && sails.anySail())
    heel = std::clamp(env.aws * std::fabs(std::sin(m_awa * M_PI / 180)) *
                          sails.areaFactor() * 0.9,
                      0.0, 22.0);
  m_heel += (heel - m_heel) * k * 0.5;

  // Wind particles drift with the apparent wind.
  const double aws = env.hasWind() ? env.aws : 0;
  const QPointF v = dir(m_awa + 180) * (30 + aws * 9);
  auto* rng = QRandomGenerator::global();
  for (auto& w : m_wind) {
    w.pos += v * dt;
    w.life -= dt * 0.35;
    if (w.life <= 0 ||
        !rect().adjusted(-40, -40, 40, 40).contains(w.pos.toPoint())) {
      w.pos = QPointF(rng->bounded(double(width())),
                      rng->bounded(double(height())));
      w.life = 0.6 + rng->bounded(0.4);
    }
  }
  // Water flows past the hull towards the stern.
  for (auto& f : m_foam) {
    f.ry() += dt * (0.05 + m_sog * 0.06);
    if (f.y() > 1) {
      f.ry() -= 2;
      f.rx() = rng->bounded(2.0) - 1;
    }
  }
  update();
}

void CockpitView::layout() {
  const double freeLeft = m_leftInset + kGap + kColumn;
  const double freeRight = width() - kGap - kColumn;
  const bool engineOn = NavStore::instance().engine().running() ||
                        BoatConfig::instance().sails().motoring();
  // Vertical space: sail pill on top, heading pill (and engine panel) below;
  // markers and labels need ~70 px outside the ring.
  const double top = 70;
  const double bottom = height() - kGap - (engineOn ? 166 : 0) - 58 - 24;
  m_radius = std::max(100.0, std::min((freeRight - freeLeft) / 2 - 40,
                                      (bottom - top) / 2 - 64));
  m_center = QPointF((freeLeft + freeRight) / 2, (top + bottom) / 2);
}

// --------------------------------------------------------------------------

void CockpitView::paintEvent(QPaintEvent*) {
  layout();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  drawBackground(p);
  drawWind(p);
  drawRose(p);
  drawBoat(p);
  drawCards(p);
}

void CockpitView::drawBackground(QPainter& p) {
  const Palette& pal = Theme::p();
  QRadialGradient g(m_center, std::max(width(), height()) * 0.7);
  g.setColorAt(0, pal.dark ? QColor("#121a25") : QColor("#eef3f8"));
  g.setColorAt(1, pal.dark ? QColor("#07090c") : QColor("#d4dde7"));
  p.fillRect(rect(), g);
}

void CockpitView::drawWind(QPainter& p) {
  const auto& env = NavStore::instance().environment();
  if (!env.hasWind()) return;
  const Palette& pal = Theme::p();
  const QPointF d = dir(m_awa + 180) * (8 + env.aws * 0.9);
  for (const auto& w : m_wind) {
    QColor c = pal.dark ? QColor(160, 190, 230) : QColor(70, 100, 140);
    c.setAlphaF(0.22 * std::sin(w.life * M_PI));
    p.setPen(QPen(c, 1.4, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(w.pos - d, w.pos);
  }
}

void CockpitView::drawRose(QPainter& p) {
  const Palette& pal = Theme::p();
  const auto& store = NavStore::instance();
  const auto& own = store.ownShip();
  const auto& env = store.environment();
  const Guidance& g = store.guidance();
  const double R = m_radius;
  p.save();
  p.translate(m_center);

  // Disc with water streaks
  QRadialGradient disc(QPointF(0, 0), R);
  disc.setColorAt(
      0, pal.dark ? QColor(30, 60, 100, 90) : QColor(150, 190, 230, 90));
  disc.setColorAt(
      1, pal.dark ? QColor(10, 20, 35, 30) : QColor(200, 220, 240, 40));
  p.setPen(Qt::NoPen);
  p.setBrush(disc);
  p.drawEllipse(QPointF(0, 0), R, R);
  p.save();
  QPainterPath clip;
  clip.addEllipse(QPointF(0, 0), R * 0.97, R * 0.97);
  p.setClipPath(clip);
  QColor streak =
      pal.dark ? QColor(140, 180, 230, 40) : QColor(60, 100, 150, 45);
  p.setPen(QPen(streak, 1.5, Qt::SolidLine, Qt::RoundCap));
  for (const QPointF& f : m_foam) {
    const QPointF a(f.x() * R, f.y() * R);
    p.drawLine(a, a + QPointF(0, 6 + m_sog * 3));
  }
  p.restore();

  // Rings and ticks (heading up: bearing b is drawn at b - heading)
  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(pal.panelBorder, 1));
  p.drawEllipse(QPointF(0, 0), R * 0.74, R * 0.74);
  p.setPen(QPen(pal.textFaint, 2));
  p.drawEllipse(QPointF(0, 0), R, R);
  for (int b = 0; b < 360; b += 5) {
    const QPointF u = dir(b - m_hdg);
    const double len = b % 30 == 0 ? 16 : b % 10 == 0 ? 10 : 5;
    p.setPen(QPen(b % 30 == 0 ? pal.text : pal.textFaint, b % 30 == 0 ? 2 : 1));
    p.drawLine(u * R, u * (R - len));
  }
  for (int b = 0; b < 360; b += 30) {
    const QPointF c = dir(b - m_hdg) * (R - 32);
    QString t;
    QFont f = Theme::font(13, QFont::DemiBold);
    QColor col = pal.textDim;
    switch (b) {
      case 0:
        t = "N", f = Theme::font(20, QFont::Bold), col = pal.danger;
        break;
      case 90:
        t = tr("E"), f = Theme::font(18, QFont::Bold), col = pal.text;
        break;
      case 180:
        t = "S", f = Theme::font(18, QFont::Bold), col = pal.text;
        break;
      case 270:
        t = "W", f = Theme::font(18, QFont::Bold), col = pal.text;
        break;
      default:
        t = QString::number(b);
    }
    p.setFont(f);
    p.setPen(col);
    p.drawText(QRectF(c.x() - 24, c.y() - 12, 48, 24), Qt::AlignCenter, t);
  }

  // Markers outside the ring
  auto marker = [&](double bearing, const QColor& c, const QString& icon,
                    const QString& text) {
    const QPointF u = dir(bearing - m_hdg);
    const QPointF tip = u * (R + 4);
    QPainterPath tri;
    const QPointF n(-u.y(), u.x());
    tri.moveTo(tip);
    tri.lineTo(tip + u * 14 + n * 8);
    tri.lineTo(tip + u * 14 - n * 8);
    tri.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawPath(tri);
    const QPointF badge = u * (R + 38);
    p.setBrush(c);
    p.drawEllipse(badge, 15, 15);
    Icons::paint(&p, icon, QRectF(badge.x() - 9, badge.y() - 9, 18, 18),
                 Qt::white);
    if (!text.isEmpty()) {
      p.setFont(Theme::font(11, QFont::Bold));
      p.setPen(c);
      const QPointF tp = u * (R + 66);
      p.drawText(QRectF(tp.x() - 40, tp.y() - 9, 80, 18), Qt::AlignCenter,
                 text);
    }
  };
  if (g.active && g.btwDeg == g.btwDeg)
    marker(g.btwDeg, pal.success, "flag", deg3(g.btwDeg));
  if (own.cog == own.cog && own.sog > 0.3)
    marker(own.cog, pal.accentBright, "navigate", QString());
  if (env.hasWind() && env.twd == env.twd) {
    // Wind arrow points from the outside towards the center.
    const QPointF u = dir(env.twd - m_hdg);
    const QColor wc = windColor(env.tws);
    const QPointF base = u * (R + 70), tip = u * (R + 10);
    const QPointF n(-u.y(), u.x());
    QPainterPath arrow;
    arrow.moveTo(tip);
    arrow.lineTo(tip + u * 22 + n * 13);
    arrow.lineTo(tip + u * 18 + n * 4);
    arrow.lineTo(base + n * 4);
    arrow.lineTo(base - n * 4);
    arrow.lineTo(tip + u * 18 - n * 4);
    arrow.lineTo(tip + u * 22 - n * 13);
    arrow.closeSubpath();
    p.setPen(QPen(pal.dark ? QColor(0, 0, 0, 120) : Qt::white, 1.5));
    p.setBrush(wc);
    p.drawPath(arrow);
    p.setFont(Theme::font(12, QFont::Bold));
    p.setPen(wc);
    const QPointF tp = u * (R + 90);
    p.drawText(QRectF(tp.x() - 40, tp.y() - 10, 80, 20), Qt::AlignCenter,
               QStringLiteral("%1 kn").arg(env.tws, 0, 'f', 0));
  }
  // Apparent wind: small chevron inside the ring, relative to the bow.
  if (env.hasWind()) {
    const QPointF u = dir(m_awa);
    const QPointF n(-u.y(), u.x());
    const QPointF tip = u * (R * 0.76);
    QPainterPath chev;
    chev.moveTo(tip);
    chev.lineTo(tip + u * 16 + n * 9);
    chev.lineTo(tip + u * 11);
    chev.lineTo(tip + u * 16 - n * 9);
    chev.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(pal.warning);
    p.drawPath(chev);
  }

  // Lubber line and heading readout
  QPainterPath lub;
  lub.moveTo(0, -R + 2);
  lub.lineTo(-9, -R - 14);
  lub.lineTo(9, -R - 14);
  lub.closeSubpath();
  p.setPen(Qt::NoPen);
  p.setBrush(pal.text);
  p.drawPath(lub);
  p.restore();
}

void CockpitView::drawBoat(QPainter& p) {
  const Palette& pal = Theme::p();
  const auto& store = NavStore::instance();
  const SailConfig& sails = BoatConfig::instance().sails();
  const double L = m_radius * 1.05;
  const double B = L * 0.34;
  const int lee = m_awa >= 0 ? -1 : 1;  // sails swing to leeward

  p.save();
  p.translate(m_center);
  // Gentle yaw/bob animation
  p.rotate(std::sin(m_time * 1.1) * (0.8 + m_heel * 0.04));

  // Wake
  if (m_sog > 0.3) {
    const double len = std::min(L * 1.2, L * 0.15 * m_sog);
    for (int s : {-1, 1}) {
      QLinearGradient lg(QPointF(0, L / 2), QPointF(0, L / 2 + len));
      QColor c =
          pal.dark ? QColor(200, 225, 255, 110) : QColor(255, 255, 255, 200);
      lg.setColorAt(0, c);
      c.setAlpha(0);
      lg.setColorAt(1, c);
      QPen pen(QBrush(lg), 3, Qt::DashLine, Qt::RoundCap);
      pen.setDashOffset(-m_time * (2 + m_sog));
      p.setPen(pen);
      p.drawLine(QPointF(s * B * 0.38, L / 2),
                 QPointF(s * (B * 0.38 + len * 0.35), L / 2 + len));
    }
  }
  // Propeller wash
  if (store.engine().running() || sails.motoring()) {
    for (int i = 0; i < 4; ++i) {
      const double ph = std::fmod(m_time * 1.6 + i * 0.25, 1.0);
      QColor c = pal.dark ? QColor(180, 210, 255) : QColor(255, 255, 255);
      c.setAlphaF(0.45 * (1 - ph));
      p.setPen(QPen(c, 2));
      p.setBrush(Qt::NoBrush);
      p.drawEllipse(QPointF(0, L / 2 + 8 + ph * 30), 6 + ph * 18, 3 + ph * 8);
    }
  }

  // Hull shadow, offset to leeward with heel
  QPainterPath hull;
  hull.moveTo(0, -L / 2);
  hull.cubicTo(B * 0.35, -L * 0.40, B / 2, -L * 0.15, B / 2, L * 0.05);
  hull.cubicTo(B / 2, L * 0.25, B * 0.45, L * 0.45, B * 0.40, L / 2);
  hull.lineTo(-B * 0.40, L / 2);
  hull.cubicTo(-B * 0.45, L * 0.45, -B / 2, L * 0.25, -B / 2, L * 0.05);
  hull.cubicTo(-B / 2, -L * 0.15, -B * 0.35, -L * 0.40, 0, -L / 2);
  p.save();
  p.translate(lee * (4 + m_heel * 0.6), 6);
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(0, 0, 0, pal.dark ? 120 : 50));
  p.drawPath(hull);
  p.restore();

  // Hull, lit from windward when heeling
  QLinearGradient hg(QPointF(-B / 2, 0), QPointF(B / 2, 0));
  const QColor light = pal.dark ? QColor("#eef2f7") : QColor("#ffffff");
  const QColor dark = pal.dark ? QColor("#9aa6b4") : QColor("#c3cbd5");
  const double h = std::min(1.0, m_heel / 20);
  hg.setColorAt(0, lee < 0 ? dark : light);
  hg.setColorAt(0.5, light);
  hg.setColorAt(1, lee < 0 ? light : dark);
  if (h < 0.05) {
    hg.setColorAt(0, dark);
    hg.setColorAt(1, dark);
  }
  p.setPen(QPen(pal.dark ? QColor(0, 0, 0, 160) : QColor(80, 90, 100), 1.5));
  p.setBrush(hg);
  p.drawPath(hull);
  // Deck: cabin top, cockpit, mast
  p.setPen(Qt::NoPen);
  p.setBrush(pal.dark ? QColor("#c8d0da") : QColor("#e3e8ee"));
  p.drawRoundedRect(QRectF(-B * 0.31, -L * 0.24, B * 0.62, L * 0.36), 10, 10);
  p.setBrush(pal.dark ? QColor("#5a6573") : QColor("#9aa5b1"));
  p.drawRoundedRect(QRectF(-B * 0.30, L * 0.18, B * 0.60, L * 0.25), 6, 6);
  p.setBrush(pal.dark ? QColor("#2a323c") : QColor("#5b6672"));
  p.drawEllipse(QPointF(0, L * 0.38), B * 0.08, B * 0.08);  // helm

  drawSails(p, L);

  p.setBrush(QColor("#3a4350"));
  p.setPen(QPen(Qt::white, 1));
  p.drawEllipse(QPointF(0, -L * 0.13), 4.5, 4.5);  // mast
  p.restore();
}

void CockpitView::drawSails(QPainter& p, double L) {
  const Palette& pal = Theme::p();
  const SailConfig& s = BoatConfig::instance().sails();
  const auto& env = NavStore::instance().environment();
  const int lee = m_awa >= 0 ? -1 : 1;
  const double awa = std::fabs(m_awa);
  // Sheet angle off the centerline grows as the wind goes aft.
  const double phi = std::clamp(4 + awa * 0.48, 12.0, 82.0);
  const bool luffing = env.hasWind() && awa < 28;
  const double flutter = luffing ? std::sin(m_time * 14) * 4 : 0;
  const QColor cloth =
      pal.dark ? QColor(245, 248, 252, 235) : QColor(255, 255, 255, 245);
  const QPen edge(pal.dark ? QColor(40, 60, 90, 220) : QColor(90, 105, 125),
                  1.4);
  // Sails cast a soft shadow on deck and water so they stand out.
  auto drawSail = [&](const QPainterPath& path, const QBrush& brush) {
    p.save();
    p.translate(4, 5);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, pal.dark ? 110 : 55));
    p.drawPath(path);
    p.restore();
    p.setPen(edge);
    p.setBrush(brush);
    p.drawPath(path);
  };

  auto sailShape = [&](QPointF a, QPointF b, double camber) {
    const QPointF mid = (a + b) / 2;
    const QPointF ab = b - a;
    QPointF n(-ab.y(), ab.x());
    n /= std::hypot(n.x(), n.y());
    if ((n.x() > 0) != (lee > 0)) n = -n;  // bulge to leeward
    QPainterPath path;
    path.moveTo(a);
    path.quadTo(mid + n * (camber + flutter), b);
    path.quadTo(mid + n * (camber * 0.55), a);
    return path;
  };

  // Mainsail
  if (s.main != SailConfig::Main::Down) {
    const QPointF mast(0, -L * 0.13);
    const double boom = L * 0.42;
    const double rad = phi * M_PI / 180;
    const QPointF clew =
        mast + QPointF(lee * std::sin(rad), std::cos(rad)) * boom;
    const int reefs = int(s.main) - int(SailConfig::Main::Full);
    const double camber = boom * (0.24 - reefs * 0.04);
    p.setPen(QPen(QColor("#4b5563"), 3, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(mast, clew);  // boom
    drawSail(sailShape(mast, clew, camber), cloth);
    // Reef points along the boom
    p.setPen(Qt::NoPen);
    p.setBrush(pal.accent);
    for (int r = 1; r <= reefs; ++r)
      for (int i = 1; i <= 4; ++i) {
        const QPointF q = mast + (clew - mast) * (i / 5.0);
        p.drawEllipse(q + QPointF(-lee * r * 4.0, 0), 1.6, 1.6);
      }
  }
  // Headsail
  if (s.head != SailConfig::Head::None) {
    const QPointF tack(0, -L * 0.47);
    double len = s.head == SailConfig::Head::Genoa ? L * 0.62
                 : s.head == SailConfig::Head::Jib ? L * 0.45
                                                   : L * 0.28;
    if (s.head == SailConfig::Head::Genoa)
      len *= std::max(0.3, s.headFurlPct / 100.0);
    const double rad = std::clamp(phi * 0.8 + 6, 10.0, 75.0) * M_PI / 180;
    const QPointF clew =
        tack + QPointF(lee * std::sin(rad), std::cos(rad)) * len;
    drawSail(sailShape(tack, clew, len * 0.18), cloth);
    p.setPen(QPen(QColor(120, 130, 140), 1));
    p.drawLine(tack, QPointF(0, -L * 0.13));  // forestay
  }
  // Downwind sail: a colored balloon to leeward of the bow
  if (s.downwind != SailConfig::Downwind::None) {
    const QPointF tack(0, -L * 0.5);
    const double size = s.downwind == SailConfig::Downwind::Spinnaker ? L * 0.55
                        : s.downwind == SailConfig::Downwind::Gennaker
                            ? L * 0.5
                            : L * 0.42;
    const double out = std::clamp((awa - 60) / 100, 0.15, 1.0);
    const QPointF clew = tack + QPointF(lee * size * (0.5 + out * 0.5),
                                        size * (0.6 - out * 0.4));
    QPainterPath balloon;
    balloon.moveTo(tack);
    balloon.cubicTo(tack + QPointF(lee * size * 0.2, -size * 0.55),
                    clew + QPointF(lee * size * 0.45, -size * 0.45), clew);
    balloon.quadTo((tack + clew) / 2 + QPointF(lee * size * 0.05, size * 0.05),
                   tack);
    QLinearGradient bg(tack, clew);
    if (s.downwind == SailConfig::Downwind::Spinnaker) {
      bg.setColorAt(0, QColor("#e5484d"));
      bg.setColorAt(0.5, QColor("#ffffff"));
      bg.setColorAt(1, QColor("#3e6ae1"));
    } else if (s.downwind == SailConfig::Downwind::Gennaker) {
      bg.setColorAt(0, pal.accentBright);
      bg.setColorAt(1, QColor("#8b5cf6"));
    } else {
      bg.setColorAt(0, QColor("#dfe7f0"));
      bg.setColorAt(1, QColor("#a9bbd1"));
    }
    drawSail(balloon, bg);
  }
}

// --------------------------------------------------------------------------

void CockpitView::drawCards(QPainter& p) {
  const Palette& pal = Theme::p();
  const auto& store = NavStore::instance();
  const auto& own = store.ownShip();
  const auto& env = store.environment();
  const auto& cfg = AppSettings::instance();
  const Guidance& g = store.guidance();
  const double x0 = m_leftInset + kGap;
  const double x1 = width() - kGap - kColumn;

  // Heading readout below the rose, on a glass pill
  const QRectF hp(m_center.x() - 78, m_center.y() + m_radius + 18, 156, 58);
  paintGlass(p, hp, 20);
  label(p, QRectF(hp.left(), hp.top() + 8, hp.width(), 12), tr("Heading"),
        Qt::AlignCenter);
  p.setFont(Theme::numberFont(30, QFont::Normal));
  p.setPen(pal.text);
  p.drawText(QRectF(hp.left(), hp.top() + 18, hp.width(), 36), Qt::AlignCenter,
             deg3(m_hdg));

  // Sail configuration pill (top center)
  const QString sails = BoatConfig::instance().sails().summary();
  p.setFont(Theme::font(13, QFont::DemiBold));
  const double pw = QFontMetricsF(p.font()).horizontalAdvance(sails) + 64;
  m_sailPill = QRectF(m_center.x() - pw / 2, 18, pw, 38);
  paintGlass(p, m_sailPill, 19);
  Icons::paint(&p, "sail",
               QRectF(m_sailPill.left() + 14, m_sailPill.top() + 9, 20, 20),
               pal.accentBright);
  p.setFont(Theme::font(13, QFont::DemiBold));
  p.setPen(pal.text);
  p.drawText(m_sailPill.adjusted(42, 0, -14, 0),
             Qt::AlignVCenter | Qt::AlignLeft, sails);

  // --- Left column: speed and course --------------------------------------
  QRectF r(x0, 24, kColumn, 190);
  paintGlass(p, r, 24);
  label(p, QRectF(r.left() + 22, r.top() + 16, 200, 14),
        tr("Speed over ground"));
  const auto sp = cfg.speed(own.valid ? own.sog : kNaN);
  bigValue(p, QPointF(r.left() + 20, r.top() + 100), sp.first,
           sp.second.isEmpty() ? cfg.speed(0).second : sp.second, 72);
  p.setFont(Theme::font(13, QFont::Medium));
  p.setPen(pal.textDim);
  QString sub;
  if (env.stw == env.stw) sub += tr("STW %1").arg(cfg.speedText(env.stw));
  if (env.depth == env.depth)
    sub += (sub.isEmpty() ? "" : "   ") +
           tr("Depth %1 m").arg(env.depth, 0, 'f', 1);
  p.drawText(QRectF(r.left() + 22, r.bottom() - 52, r.width() - 40, 18),
             Qt::AlignLeft | Qt::AlignVCenter, sub);
  if (env.depth == env.depth && env.depth < cfg.safetyDepth) {
    p.setPen(pal.danger);
    p.drawText(QRectF(r.left() + 22, r.bottom() - 32, r.width() - 40, 18),
               Qt::AlignLeft | Qt::AlignVCenter, tr("SHALLOW WATER"));
  }

  r = QRectF(x0, r.bottom() + 16, kColumn, 130);
  paintGlass(p, r, 24);
  label(p, QRectF(r.left() + 22, r.top() + 16, 200, 14),
        tr("Course over ground"));
  bigValue(p, QPointF(r.left() + 20, r.top() + 78),
           deg3(own.valid ? own.cog : kNaN), "", 46, pal.accentBright);
  p.setFont(Theme::font(13, QFont::Medium));
  p.setPen(pal.textDim);
  p.drawText(QRectF(r.left() + 22, r.bottom() - 34, r.width() - 40, 18),
             Qt::AlignLeft | Qt::AlignVCenter,
             own.valid
                 ? geo::formatLat(own.lat) + "  " + geo::formatLon(own.lon)
                 : tr("No position"));

  // --- Right column: wind and next waypoint -------------------------------
  r = QRectF(x1, 24, kColumn, 190);
  paintGlass(p, r, 24);
  label(p, QRectF(r.left() + 22, r.top() + 16, 200, 14), tr("True wind"));
  if (env.hasWind()) {
    const QColor wc = windColor(env.tws);
    bigValue(p, QPointF(r.left() + 20, r.top() + 90),
             QString::number(env.tws, 'f', 0), "kn", 60, wc);
    p.setFont(Theme::font(14, QFont::DemiBold));
    p.setPen(pal.text);
    p.drawText(
        QRectF(r.left() + 22, r.top() + 100, r.width() - 40, 20),
        Qt::AlignLeft | Qt::AlignVCenter,
        tr("from %1 · Bft %2").arg(deg3(env.twd)).arg(beaufort(env.tws)));
    p.setFont(Theme::font(13, QFont::Medium));
    p.setPen(pal.textDim);
    p.drawText(QRectF(r.left() + 22, r.bottom() - 52, r.width() - 40, 18),
               Qt::AlignLeft | Qt::AlignVCenter,
               tr("Apparent %1 kn · %2° %3")
                   .arg(env.aws, 0, 'f', 1)
                   .arg(std::lround(std::fabs(env.awa)))
                   .arg(env.awa >= 0 ? tr("stbd") : tr("port")));
    p.drawText(QRectF(r.left() + 22, r.bottom() - 32, r.width() - 40, 18),
               Qt::AlignLeft | Qt::AlignVCenter,
               tr("True wind angle %1°").arg(std::lround(std::fabs(env.twa))));
  } else {
    p.setFont(Theme::font(14, QFont::Medium));
    p.setPen(pal.textDim);
    p.drawText(r.adjusted(22, 40, -20, -20), Qt::AlignLeft | Qt::TextWordWrap,
               tr("No wind data. Connect a wind instrument (NMEA MWV/MWD)."));
  }

  r = QRectF(x1, r.bottom() + 16, kColumn, 170);
  m_wpCard = r;
  paintGlass(p, r, 24);
  label(p, QRectF(r.left() + 22, r.top() + 16, 200, 14), tr("Next waypoint"));
  if (g.active) {
    p.setFont(Theme::font(16, QFont::DemiBold));
    p.setPen(pal.success);
    p.drawText(QRectF(r.left() + 22, r.top() + 34, r.width() - 40, 22),
               Qt::AlignLeft | Qt::AlignVCenter,
               p.fontMetrics().elidedText(g.waypointName, Qt::ElideRight,
                                          int(r.width() - 40)));
    const auto d = cfg.distance(g.dtwNm);
    bigValue(p, QPointF(r.left() + 20, r.top() + 104), d.first, d.second, 46);
    p.setFont(Theme::font(13, QFont::Medium));
    p.setPen(pal.textDim);
    QString line = tr("BTW %1").arg(deg3(g.btwDeg));
    if (g.eta.isValid()) line += tr(" · ETA %1").arg(g.eta.toString("HH:mm"));
    p.drawText(QRectF(r.left() + 22, r.bottom() - 52, r.width() - 40, 18),
               Qt::AlignLeft | Qt::AlignVCenter, line);
    QString xte = cfg.distanceText(std::fabs(g.xteNm));
    if (std::fabs(g.xteNm) > 0.005) xte += g.xteNm > 0 ? " R" : " L";
    p.drawText(
        QRectF(r.left() + 22, r.bottom() - 32, r.width() - 40, 18),
        Qt::AlignLeft | Qt::AlignVCenter,
        tr("XTE %1 · %2 left").arg(xte, cfg.distanceText(g.remainingNm)));
  } else {
    p.setFont(Theme::font(14, QFont::Medium));
    p.setPen(pal.textDim);
    p.drawText(r.adjusted(22, 40, -20, -20), Qt::AlignLeft | Qt::TextWordWrap,
               tr("No active route. Tap to plan or activate a route."));
  }

  // --- Engine (bottom center) ---------------------------------------------
  const bool engineOn =
      store.engine().running() || BoatConfig::instance().sails().motoring();
  if (engineOn) {
    const double ew = std::min(720.0, double(width() - m_leftInset - 2 * kGap));
    drawEngine(p, QRectF(m_leftInset + (width() - m_leftInset - ew) / 2,
                         height() - 150 - kGap, ew, 150));
  }
}

void CockpitView::drawEngine(QPainter& p, const QRectF& r) {
  const Palette& pal = Theme::p();
  const auto& store = NavStore::instance();
  const EngineData& e = store.engine();
  const BoatConfig& boat = BoatConfig::instance();
  paintGlass(p, r, 26);
  const bool data = e.running() ||
                    (e.lastUpdate.isValid() &&
                     e.lastUpdate.secsTo(QDateTime::currentDateTimeUtc()) < 10);
  Icons::paint(&p, "engine", QRectF(r.left() + 20, r.top() + 16, 20, 20),
               e.running() ? pal.success : pal.textDim);
  label(p, QRectF(r.left() + 46, r.top() + 19, 200, 14),
        e.running() ? tr("Engine running") : tr("Engine"));
  if (!data) {
    p.setFont(Theme::font(14, QFont::Medium));
    p.setPen(pal.textDim);
    p.drawText(r.adjusted(24, 46, -24, -16), Qt::AlignLeft | Qt::TextWordWrap,
               tr("No engine data. Connect an engine interface sending NMEA "
                  "RPM and XDR (temperature, oil pressure, fuel level)."));
    return;
  }

  // RPM arc gauge
  const QRectF gauge(r.left() + 24, r.top() + 42, 96, 96);
  const double frac = std::clamp(e.rpm / boat.maxRpm, 0.0, 1.0);
  p.setPen(QPen(pal.panelPressed, 8, Qt::SolidLine, Qt::RoundCap));
  p.setBrush(Qt::NoBrush);
  p.drawArc(gauge, 225 * 16, -270 * 16);
  const QColor rc = frac > 0.85 ? pal.danger : pal.accentBright;
  p.setPen(QPen(rc, 8, Qt::SolidLine, Qt::RoundCap));
  p.drawArc(gauge, 225 * 16, int(-270 * 16 * frac));
  p.setFont(Theme::numberFont(22, QFont::Normal));
  p.setPen(pal.text);
  p.drawText(gauge.adjusted(0, -6, 0, -6), Qt::AlignCenter,
             QString::number(std::lround(e.rpm)));
  p.setFont(Theme::font(10, QFont::Bold));
  p.setPen(pal.textDim);
  p.drawText(gauge.adjusted(0, 30, 0, 0), Qt::AlignCenter, "RPM");

  // Stats grid
  const double sx = gauge.right() + 26;
  const double cw = (r.right() - 20 - sx) / 3;
  auto stat = [&](int col, int row, const QString& name, const QString& value,
                  const QColor& c) {
    const QRectF cell(sx + col * cw, r.top() + 42 + row * 52, cw, 50);
    label(p, QRectF(cell.left(), cell.top(), cell.width(), 14), name);
    p.setFont(Theme::numberFont(19, QFont::Normal));
    p.setPen(c);
    p.drawText(QRectF(cell.left(), cell.top() + 16, cell.width(), 26),
               Qt::AlignLeft | Qt::AlignVCenter, value);
  };
  const QColor tc = e.coolantC > 98   ? pal.danger
                    : e.coolantC > 90 ? pal.warning
                                      : pal.text;
  stat(0, 0, tr("Coolant"),
       std::isnan(e.coolantC)
           ? "–"
           : QStringLiteral("%1 °C").arg(e.coolantC, 0, 'f', 0),
       tc);
  const QColor oc = e.running() && e.oilBar < 1.0 ? pal.danger : pal.text;
  stat(1, 0, tr("Oil"),
       std::isnan(e.oilBar) ? "–"
                            : QStringLiteral("%1 bar").arg(e.oilBar, 0, 'f', 1),
       oc);
  stat(2, 0, tr("Battery"),
       std::isnan(e.voltage) ? "–"
                             : QStringLiteral("%1 V").arg(e.voltage, 0, 'f', 1),
       e.voltage < 12.2 ? pal.warning : pal.text);

  // Fuel and range
  const double pct = std::isnan(e.fuelPct) ? boat.manualFuelPct : e.fuelPct;
  const double liters = pct / 100 * boat.tankLiters;
  double lph = e.fuelRateLph;
  if (std::isnan(lph) && e.rpm == e.rpm)
    lph = boat.consumptionLph *
          std::pow(std::max(e.rpm, 600.0) / boat.cruiseRpm, 2.5);
  const double sog = store.ownShip().sog;
  QString range = "–";
  if (lph > 0.05) {
    const double hours = liters / lph;
    range = sog == sog && sog > 0.5
                ? AppSettings::instance().distanceText(hours * sog)
                : QStringLiteral("%1 h").arg(hours, 0, 'f', 1);
  }
  stat(0, 1, tr("Fuel %1 l").arg(liters, 0, 'f', 0),
       QStringLiteral("%1 %").arg(pct, 0, 'f', 0),
       pct < 15   ? pal.danger
       : pct < 25 ? pal.warning
                  : pal.text);
  stat(1, 1, tr("Range"), range, pal.text);
  stat(2, 1, tr("Hours"),
       std::isnan(e.hours) ? "–"
                           : QStringLiteral("%1 h").arg(e.hours, 0, 'f', 0),
       pal.text);
  // Fuel bar under the fuel value
  const QRectF bar(sx, r.top() + 42 + 52 + 44, cw - 16, 4);
  p.setPen(Qt::NoPen);
  p.setBrush(pal.panelPressed);
  p.drawRoundedRect(bar, 2, 2);
  p.setBrush(pct < 15 ? pal.danger : pct < 25 ? pal.warning : pal.success);
  p.drawRoundedRect(QRectF(bar.left(), bar.top(), bar.width() * pct / 100, 4),
                    2, 2);
}

void CockpitView::mouseReleaseEvent(QMouseEvent* e) {
  if (m_sailPill.contains(e->position()))
    emit sailConfigRequested();
  else if (m_wpCard.contains(e->position()))
    emit routesRequested();
}
