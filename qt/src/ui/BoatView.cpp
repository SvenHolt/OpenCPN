#include "ui/BoatView.h"

#include <QButtonGroup>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>
#include <cmath>

#include "core/Automation.h"
#include "core/BoatSystems.h"
#include "ui/GlassPanel.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

namespace {
QLabel* label(const QString& text, const char* role = nullptr) {
  auto* l = new QLabel(text);
  if (role) l->setProperty("role", role);
  return l;
}

/** Room tint: blue when cold, amber when warm. */
QColor tempColor(double c, double alpha) {
  // 10 °C blue … 18 °C neutral … 26 °C orange.
  const QColor cold(70, 150, 255), mild(150, 160, 175), warm(255, 140, 50);
  const double f = std::clamp((c - 10) / 16, 0.0, 1.0);
  const auto mix = [](const QColor& a, const QColor& b, double t) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
  };
  QColor col = f < 0.5 ? mix(cold, mild, f * 2) : mix(mild, warm, f * 2 - 1);
  col.setAlphaF(alpha);
  return col;
}

QString degrees(double c) {
  return std::isnan(c) ? QStringLiteral("–")
                       : QStringLiteral("%1°").arg(c, 0, 'f', 1);
}

QString watts(double w) {
  return std::abs(w) >= 1000 ? QStringLiteral("%1 kW").arg(w / 1000, 0, 'f', 2)
                             : QStringLiteral("%1 W").arg(int(std::round(w)));
}

QLabel* iconLabelFor(const QString& icon) {
  auto* l = new QLabel;
  l->setFixedSize(26, 26);
  l->setPixmap(Icons::pixmap(icon, Theme::p().accentBright, 20));
  return l;
}

void drawCentered(QPainter& p, const QPointF& c, const QString& text,
                  const QFont& f, const QColor& col) {
  p.setFont(f);
  p.setPen(col);
  const QRectF r(c.x() - 120, c.y() - 20, 240, 40);
  p.drawText(r, Qt::AlignCenter, text);
}
}  // namespace

// --------------------------------------------------------------------------
// Cabin layout

CabinLayout::CabinLayout(QWidget* parent) : QWidget(parent) {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  // Light positions in boat coordinates (x: transom → bow, y: port → stbd).
  m_lightPos = {
      {"fwd_ceiling", {0.83, 0}},        {"fwd_read_p", {0.75, -0.55}},
      {"fwd_read_s", {0.75, 0.55}},      {"saloon_ceiling", {0.52, 0}},
      {"saloon_indirect", {0.6, -0.84}}, {"chart_table", {0.42, -0.7}},
      {"galley_spot", {0.45, 0.62}},     {"heads_ceiling", {0.3, 0.55}},
      {"aft_ceiling", {0.22, -0.52}},    {"aft_read", {0.08, -0.62}},
      {"courtesy", {0.395, 0}},          {"nav_lights", {0.985, 0}},
      {"steaming", {0.64, 0.12}},        {"anchor_light", {0.62, 0}},
      {"deck_light", {0.6, -0.14}}};
  setCursor(Qt::PointingHandCursor);
  connect(&BoatSystems::instance(), &BoatSystems::changed, this,
          qOverload<>(&QWidget::update));
  connect(&Theme::instance(), &Theme::changed, this,
          qOverload<>(&QWidget::update));
}

void CabinLayout::resizeEvent(QResizeEvent*) {
  // Fit a hull of 8.5 m × 2.9 m with room for the solar arch at the stern.
  const double ratio = 2.9;
  const QRectF avail = QRectF(rect()).adjusted(70, 20, -20, -20);
  double len = avail.width();
  if (len / ratio > avail.height()) len = avail.height() * ratio;
  const double beam = len / ratio;
  m_area = QRectF(avail.center().x() - len / 2, avail.center().y() - beam / 2,
                  len, beam);
  // Hull outline: transom at x = 0, widest around 40 %, bow at x = 1.
  QPainterPath h;
  h.moveTo(map(0, -0.8));
  h.cubicTo(map(0.15, -0.97), map(0.3, -1.0), map(0.42, -1.0));
  h.cubicTo(map(0.7, -1.0), map(0.9, -0.72), map(1.0, 0));
  h.cubicTo(map(0.9, 0.72), map(0.7, 1.0), map(0.42, 1.0));
  h.cubicTo(map(0.3, 1.0), map(0.15, 0.97), map(0, 0.8));
  h.closeSubpath();
  m_hull = h;
  m_shoreHit = QRectF(map(0.0, 0.62) - QPointF(16, 16), QSizeF(32, 32));
}

QPointF CabinLayout::map(double x, double y) const {
  return {m_area.left() + x * m_area.width(),
          m_area.center().y() + y * m_area.height() / 2};
}

QRectF CabinLayout::boatRect(double x0, double y0, double x1, double y1) const {
  return QRectF(map(x0, y0), map(x1, y1)).normalized();
}

QPointF CabinLayout::lightPos(const QString& id) const {
  const QPointF b = m_lightPos.value(id);
  return map(b.x(), b.y());
}

void CabinLayout::drawRoom(QPainter& p, const QString& zone, const QRectF& r,
                           const QPointF& labelAt) {
  const Palette& pal = Theme::p();
  const BoatZone* z = BoatSystems::instance().zone(zone);
  if (!z) return;
  p.setPen(Qt::NoPen);
  p.setBrush(tempColor(z->temperatureC, zone == "outside" ? 0.08 : 0.2));
  p.drawRect(r);
  const QPointF c = map(labelAt.x(), labelAt.y());
  drawCentered(p, c - QPointF(0, 14), z->name.toUpper(),
               Theme::font(10, QFont::DemiBold), pal.textDim);
  drawCentered(p, c + QPointF(0, 8), degrees(z->temperatureC),
               Theme::numberFont(22, QFont::Normal), pal.text);
}

void CabinLayout::drawFurniture(QPainter& p) {
  const Palette& pal = Theme::p();
  const QColor fill =
      pal.dark ? QColor(255, 255, 255, 20) : QColor(0, 0, 0, 16);
  QPen pen(pal.textFaint, 1.2);
  p.setPen(pen);
  p.setBrush(fill);
  const double rr = m_area.height() * 0.04;
  // Aft cabin berth, saloon settees, chart table.
  p.drawRoundedRect(boatRect(0.05, -0.9, 0.33, -0.22), rr, rr);
  p.drawRoundedRect(boatRect(0.47, -0.93, 0.68, -0.62), rr, rr);
  p.drawRoundedRect(boatRect(0.54, 0.62, 0.68, 0.93), rr, rr);
  p.drawRoundedRect(boatRect(0.38, -0.9, 0.46, -0.52), rr / 2, rr / 2);
  // Saloon table.
  p.drawRoundedRect(boatRect(0.53, -0.28, 0.63, 0.28), rr, rr);
  // Forward V-berth.
  QPolygonF v;
  v << map(0.72, -0.8) << map(0.92, -0.3) << map(0.92, 0.3) << map(0.72, 0.8);
  p.drawPolygon(v);
  // Heads: toilet and basin; galley: stove and sink.
  const double u = m_area.height() * 0.07;
  p.drawEllipse(map(0.33, 0.68), u * 0.8, u);
  p.drawRoundedRect(
      QRectF(map(0.3, 0.9) - QPointF(u, u * 0.5), QSizeF(2 * u, 1.2 * u)), 3,
      3);
  p.drawRoundedRect(boatRect(0.39, 0.66, 0.43, 0.9), 3, 3);
  for (double y : {0.62, 0.84}) p.drawEllipse(map(0.48, y), u * 0.55, u * 0.55);
  // Companionway steps.
  p.setBrush(Qt::NoBrush);
  for (int i = 0; i < 4; ++i) {
    const double x = 0.335 + i * 0.01;
    p.drawLine(map(x, -0.14), map(x, 0.14));
  }
  // Mast.
  p.setBrush(pal.textFaint);
  p.drawEllipse(map(0.62, 0), u * 0.35, u * 0.35);
}

void CabinLayout::drawEquipment(QPainter& p) {
  const Palette& pal = Theme::p();
  auto& sys = BoatSystems::instance();
  const EnergySystem& e = sys.energy();
  const Heater& h = sys.heater();
  const QFont small = Theme::font(10, QFont::DemiBold);

  // Tanks with their level.
  const auto tank = [&](const BoatTank& t, const QRectF& r, const QColor& c,
                        bool below = false) {
    p.setPen(QPen(pal.textDim, 1));
    p.setBrush(QColor(0, 0, 0, 40));
    p.drawRoundedRect(r, 3, 3);
    QRectF lvl = r.adjusted(1.5, 1.5, -1.5, -1.5);
    lvl.setTop(lvl.bottom() - lvl.height() * t.level);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(lvl, 2, 2);
    p.setFont(small);
    p.setPen(pal.text);
    p.drawText(below ? QRectF(r.center().x() - 22, r.bottom() + 2, 44, 16)
                     : QRectF(r.right() + 4, r.center().y() - 9, 44, 18),
               below ? Qt::AlignCenter : Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("%1 %").arg(int(std::round(t.level * 100))));
  };
  for (const BoatTank& t : sys.tanks()) {
    if (t.kind == BoatTank::Kind::FreshWater)
      tank(t, boatRect(0.79, -0.2, 0.89, 0.2), QColor(70, 160, 255, 200));
    if (t.kind == BoatTank::Kind::WasteWater)
      tank(t, boatRect(0.225, 0.5, 0.265, 0.74), QColor(160, 120, 70, 200),
           true);
    if (t.kind == BoatTank::Kind::Fuel)
      tank(t, boatRect(0.06, 0.62, 0.1, 0.9), QColor(240, 180, 40, 200));
  }

  // Battery bank under the port settee.
  {
    const QRectF r = boatRect(0.49, -0.9, 0.6, -0.68);
    const QColor c = e.soc > 0.5   ? pal.success
                     : e.soc > 0.2 ? pal.warning
                                   : pal.danger;
    p.setPen(QPen(c, 1.5));
    p.setBrush(QColor(0, 0, 0, 60));
    p.drawRoundedRect(r, 3, 3);
    QRectF lvl = r.adjusted(2, 2, -2, -2);
    lvl.setWidth(lvl.width() * e.soc);
    p.setPen(Qt::NoPen);
    QColor fillC = c;
    fillC.setAlpha(150);
    p.setBrush(fillC);
    p.drawRoundedRect(lvl, 2, 2);
    p.setFont(small);
    p.setPen(Qt::white);
    p.drawText(r, Qt::AlignCenter,
               QStringLiteral("%1 %").arg(int(std::round(e.soc * 100))));
  }

  // Webasto in the aft locker with its ducts to the rooms.
  {
    const QPointF hp = map(0.14, 0.75);
    const bool hot =
        h.state == Heater::State::Heating || h.state == Heater::State::Starting;
    const bool air = h.state != Heater::State::Off;
    QColor duct = hot   ? QColor(255, 140, 40, 170)
                  : air ? QColor(160, 200, 255, 120)
                        : pal.textFaint;
    if (!air) duct.setAlpha(60);
    QPen dp(duct, 2, Qt::DashLine, Qt::RoundCap);
    p.setPen(dp);
    p.setBrush(Qt::NoBrush);
    const QPointF outlets[] = {map(0.2, -0.3), map(0.5, -0.45), map(0.44, 0.4),
                               map(0.76, 0.6), map(0.3, 0.3)};
    for (const QPointF& o : outlets) {
      QPainterPath path(hp);
      path.cubicTo(QPointF(hp.x(), o.y()), QPointF(o.x(), hp.y()), o);
      p.drawPath(path);
      p.drawEllipse(o, 3, 3);
    }
    const double r = m_area.height() * 0.07;
    p.setPen(QPen(hot ? QColor(255, 140, 40) : pal.textDim, 1.5));
    p.setBrush(hot ? QColor(255, 120, 30, 80) : QColor(0, 0, 0, 50));
    p.drawRoundedRect(QRectF(hp - QPointF(r, r * 0.7), QSizeF(2 * r, 1.4 * r)),
                      4, 4);
    // Flame.
    QPainterPath flame;
    const QPointF f = hp + QPointF(0, r * 0.35);
    flame.moveTo(f);
    flame.cubicTo(f + QPointF(-r * 0.5, -r * 0.3), f + QPointF(0, -r * 0.6),
                  f + QPointF(0, -r * 0.9));
    flame.cubicTo(f + QPointF(r * 0.1, -r * 0.5),
                  f + QPointF(r * 0.5, -r * 0.3), f);
    p.setPen(Qt::NoPen);
    p.setBrush(hot ? QColor(255, 170, 40) : pal.textFaint);
    p.drawPath(flame);
  }

  // Solar panels on the stern arch.
  {
    const QRectF r = boatRect(-0.12, -0.75, -0.035, 0.75);
    const double f = e.solarPeakW > 0 ? e.solarW / e.solarPeakW : 0;
    p.setPen(QPen(pal.textDim, 1));
    p.setBrush(QColor::fromRgbF(0.15 + 0.5 * f, 0.25 + 0.45 * f, 0.55, 0.9));
    p.drawRoundedRect(r, 3, 3);
    p.setPen(QPen(QColor(255, 255, 255, 60), 1));
    for (int i = 1; i < 6; ++i) {
      const double y = r.top() + r.height() * i / 6;
      p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    }
    p.setFont(small);
    p.setPen(f > 0.05 ? QColor(255, 210, 80) : pal.textDim);
    p.save();
    p.translate(r.center() - QPointF(r.width() / 2 + 12, 0));
    p.rotate(-90);
    p.drawText(QRectF(-60, -10, 120, 20), Qt::AlignCenter, watts(e.solarW));
    p.restore();
  }

  // Shore power inlet at the transom (tap to plug in, simulator only).
  {
    const QPointF c = m_shoreHit.center();
    const QColor col = e.shoreConnected ? pal.success : pal.textFaint;
    p.setPen(QPen(col, 2));
    p.setBrush(e.shoreConnected ? QColor(40, 200, 120, 60)
                                : QColor(0, 0, 0, 50));
    p.drawEllipse(c, 11, 11);
    p.drawLine(c + QPointF(-4, -6), c + QPointF(-4, -1));
    p.drawLine(c + QPointF(4, -6), c + QPointF(4, -1));
    p.drawLine(c + QPointF(0, 3), c + QPointF(0, 7));
    if (e.shoreConnected) {
      p.setFont(small);
      p.setPen(pal.success);
      p.drawText(QRectF(c.x() - 80, c.y() + 12, 70, 16),
                 Qt::AlignRight | Qt::AlignVCenter, watts(e.shoreW));
    }
  }
}

void CabinLayout::drawLights(QPainter& p) {
  const Palette& pal = Theme::p();
  const double unit = m_area.height();
  for (const BoatLight& l : BoatSystems::instance().lights()) {
    if (!m_lightPos.contains(l.id)) continue;
    const QPointF c = lightPos(l.id);
    // Running lights: red port, green starboard at the bow, white stern.
    if (l.id == "nav_lights") {
      const struct {
        QPointF pos;
        QColor col;
      } nav[] = {{map(0.975, -0.06), QColor(255, 40, 40)},
                 {map(0.975, 0.06), QColor(40, 230, 90)},
                 {map(0.0, 0), QColor(255, 255, 255)}};
      for (const auto& n : nav) {
        if (l.on) {
          QRadialGradient g(n.pos, unit * 0.12);
          QColor glow = n.col;
          glow.setAlpha(120);
          g.setColorAt(0, glow);
          g.setColorAt(1, QColor(0, 0, 0, 0));
          p.setPen(Qt::NoPen);
          p.setBrush(g);
          p.drawEllipse(n.pos, unit * 0.12, unit * 0.12);
        }
        p.setPen(QPen(n.col, 1.5));
        p.setBrush(l.on ? n.col : Qt::transparent);
        p.drawEllipse(n.pos, 4, 4);
      }
      continue;
    }
    QColor col = l.color;
    if (l.on) {
      const double r = unit * (0.08 + 0.14 * l.brightness);
      QRadialGradient g(c, r);
      QColor glow = col;
      glow.setAlphaF(0.25 + 0.5 * l.brightness);
      g.setColorAt(0, glow);
      g.setColorAt(1, QColor(0, 0, 0, 0));
      p.setPen(Qt::NoPen);
      p.setBrush(g);
      p.drawEllipse(c, r, r);
      // Indirect light: a line along the hull.
      if (l.id == "saloon_indirect") {
        QColor strip = col;
        strip.setAlphaF(0.4 + 0.5 * l.brightness);
        p.setPen(QPen(strip, 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(map(0.42, -0.86), map(0.68, -0.86));
        p.drawLine(map(0.42, 0.86), map(0.68, 0.86));
      }
    }
    p.setPen(QPen(l.on ? col : pal.textDim, 1.5));
    p.setBrush(l.on ? col : QColor(0, 0, 0, 80));
    const double dot = l.navigation ? 4 : 6;
    p.drawEllipse(c, dot, dot);
  }
}

void CabinLayout::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  // Hull: deck color, rooms clipped to it.
  p.setPen(Qt::NoPen);
  p.setBrush(pal.dark ? QColor(30, 36, 48) : QColor(236, 238, 242));
  p.drawPath(m_hull);
  p.save();
  p.setClipPath(m_hull);
  drawRoom(p, "outside", boatRect(0, 0.08, 0.21, 1), {0.11, 0.42});
  drawRoom(p, "aft", boatRect(0, -1, 0.37, -0.06), {0.2, -0.55});
  drawRoom(p, "heads", boatRect(0.21, 0.08, 0.37, 1), {0.3, 0.32});
  drawRoom(p, "saloon", boatRect(0.37, -1, 0.695, 1), {0.58, 0.42});
  drawRoom(p, "galley", boatRect(0.37, 0.3, 0.53, 1), {0.45, 0.42});
  drawRoom(p, "forward", boatRect(0.695, -1, 1, 1), {0.82, -0.45});
  drawFurniture(p);
  // Bulkheads with the companionway and the forward door.
  p.setPen(QPen(pal.textDim, 2.5));
  p.drawLine(map(0.37, -1), map(0.37, -0.16));
  p.drawLine(map(0.37, 0.16), map(0.37, 1));
  p.drawLine(map(0.695, -1), map(0.695, -0.2));
  p.drawLine(map(0.695, 0.2), map(0.695, 1));
  p.drawLine(map(0, -0.06), map(0.21, -0.06));
  p.drawLine(map(0.21, -0.06), map(0.21, 1));
  p.drawLine(map(0.21, 0.08), map(0.37, 0.08));
  p.restore();
  p.setPen(QPen(pal.text, 2));
  p.setBrush(Qt::NoBrush);
  p.drawPath(m_hull);
  drawEquipment(p);
  drawLights(p);
}

void CabinLayout::mouseReleaseEvent(QMouseEvent* e) {
  const QPointF pos = e->position();
  auto& sys = BoatSystems::instance();
  if (m_shoreHit.contains(pos)) {
    sys.setShoreConnected(!sys.energy().shoreConnected);
    return;
  }
  QString best;
  double bestD = 22;
  for (auto it = m_lightPos.begin(); it != m_lightPos.end(); ++it) {
    const double d = QLineF(map(it->x(), it->y()), pos).length();
    if (d < bestD) {
      bestD = d;
      best = it.key();
    }
  }
  if (best.isEmpty()) return;
  for (const BoatLight& l : sys.lights())
    if (l.id == best) sys.setLight(best, !l.on);
}

// --------------------------------------------------------------------------
// Energy card

EnergyCard::EnergyCard(QWidget* parent) : QWidget(parent) {
  setMinimumHeight(230);
  connect(&BoatSystems::instance(), &BoatSystems::changed, this,
          qOverload<>(&QWidget::update));
}

void EnergyCard::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  const EnergySystem& e = BoatSystems::instance().energy();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  // Battery ring.
  const double r = 58;
  const QPointF c(r + 8, height() / 2.0 - 6);
  const QColor socCol = e.soc > 0.5   ? pal.success
                        : e.soc > 0.2 ? pal.warning
                                      : pal.danger;
  p.setPen(QPen(pal.panelPressed, 9, Qt::SolidLine, Qt::RoundCap));
  p.drawEllipse(c, r, r);
  p.setPen(QPen(socCol, 9, Qt::SolidLine, Qt::RoundCap));
  p.drawArc(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), 90 * 16,
            -int(360 * 16 * e.soc));
  drawCentered(p, c - QPointF(0, 6),
               QStringLiteral("%1 %").arg(int(std::round(e.soc * 100))),
               Theme::numberFont(28, QFont::Normal), pal.text);
  drawCentered(p, c + QPointF(0, 20),
               QStringLiteral("%1 V").arg(e.voltage, 0, 'f', 2),
               Theme::font(12), pal.textDim);
  QString status;
  if (e.current > 0.05)
    status = tr("+%1 A charging").arg(e.current, 0, 'f', 1);
  else if (!std::isnan(e.timeToGoH))
    status = tr("%1 A · %2 h left")
                 .arg(e.current, 0, 'f', 1)
                 .arg(std::min(999.0, e.timeToGoH), 0, 'f', 0);
  else
    status = tr("idle");
  drawCentered(p, QPointF(c.x(), c.y() + r + 22), status,
               Theme::font(11, QFont::DemiBold),
               e.current > 0.05 ? pal.success : pal.textDim);

  // Flows: solar and shore in, loads out.
  const double x0 = c.x() + r + 28;
  const double rowH = 58;
  const double top = c.y() - rowH * 1.5 + 6;
  const auto row = [&](int i, const QString& name, const QString& value,
                       const QString& detail, const QColor& col, bool active,
                       bool in) {
    const double y = top + i * rowH;
    const QRectF box(x0, y, width() - x0 - 6, rowH - 8);
    QColor bg = active ? col : pal.panelPressed;
    bg.setAlphaF(active ? 0.16 : 0.5);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(box, 10, 10);
    // Arrow towards / away from the battery.
    const double ay = box.center().y();
    QPen ap(active ? col : pal.textFaint, 2, Qt::SolidLine, Qt::RoundCap);
    p.setPen(ap);
    const double ax0 = c.x() + r + 6, ax1 = x0 - 4;
    p.drawLine(QPointF(ax0, ay), QPointF(ax1, ay));
    const QPointF tip = in ? QPointF(ax0, ay) : QPointF(ax1, ay);
    const double d = in ? 6 : -6;
    p.drawLine(tip, tip + QPointF(d, -4));
    p.drawLine(tip, tip + QPointF(d, 4));
    p.setFont(Theme::font(11, QFont::DemiBold));
    p.setPen(pal.textDim);
    p.drawText(box.adjusted(12, 6, -10, 0), Qt::AlignLeft | Qt::AlignTop,
               name.toUpper());
    p.setFont(Theme::numberFont(20, QFont::Normal));
    p.setPen(active ? pal.text : pal.textDim);
    p.drawText(box.adjusted(12, 0, -10, -6), Qt::AlignLeft | Qt::AlignBottom,
               value);
    p.setFont(Theme::font(11));
    p.setPen(pal.textDim);
    p.drawText(box.adjusted(12, 0, -10, -8), Qt::AlignRight | Qt::AlignBottom,
               detail);
  };
  row(0, tr("Solar"), watts(e.solarW),
      tr("%1 · %2 Wh today").arg(e.solarState).arg(int(e.solarYieldTodayWh)),
      QColor(255, 200, 60), e.solarW > 1, true);
  row(1, tr("Shore power"),
      e.shoreConnected ? watts(e.shoreW) : tr("not connected"),
      e.shoreConnected ? tr("%1 V · %2 A · %3")
                             .arg(int(std::round(e.shoreV)))
                             .arg(e.shoreA, 0, 'f', 1)
                             .arg(e.chargerState)
                       : QString(),
      pal.accentBright, e.shoreConnected, true);
  row(2, tr("Consumers"), watts(e.loadsW),
      tr("%1 Ah used").arg(int(std::round(e.consumedAh))), pal.warning,
      e.loadsW > 1, false);
}

// --------------------------------------------------------------------------
// Tanks card

TanksCard::TanksCard(QWidget* parent) : QWidget(parent) {
  setMinimumHeight(130);
  connect(&BoatSystems::instance(), &BoatSystems::changed, this,
          qOverload<>(&QWidget::update));
}

void TanksCard::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const auto& tanks = BoatSystems::instance().tanks();
  const double w = double(width()) / std::max<qsizetype>(1, tanks.size());
  for (int i = 0; i < tanks.size(); ++i) {
    const BoatTank& t = tanks[i];
    QColor col = t.kind == BoatTank::Kind::FreshWater ? QColor(70, 160, 255)
                 : t.kind == BoatTank::Kind::Fuel     ? QColor(240, 180, 40)
                                                      : QColor(170, 120, 70);
    // Warnings: little water / fuel, nearly full holding tank.
    const bool warn =
        t.kind == BoatTank::Kind::WasteWater ? t.level > 0.8 : t.level < 0.15;
    const QRectF bar(i * w + w / 2 - 16, 6, 32, height() - 52);
    p.setPen(QPen(warn ? pal.danger : pal.textFaint, 1.2));
    p.setBrush(pal.panelPressed);
    p.drawRoundedRect(bar, 8, 8);
    QRectF lvl = bar.adjusted(3, 3, -3, -3);
    lvl.setTop(lvl.bottom() - lvl.height() * t.level);
    p.setPen(Qt::NoPen);
    p.setBrush(col);
    p.drawRoundedRect(lvl, 6, 6);
    p.setFont(Theme::font(11, QFont::DemiBold));
    p.setPen(pal.text);
    p.drawText(QRectF(i * w, height() - 44, w, 18), Qt::AlignCenter, t.name);
    p.setFont(Theme::font(11));
    p.setPen(warn ? pal.danger : pal.textDim);
    p.drawText(QRectF(i * w, height() - 24, w, 18), Qt::AlignCenter,
               QStringLiteral("%1 % · %2 l")
                   .arg(int(std::round(t.level * 100)))
                   .arg(int(std::round(t.level * t.capacityL))));
  }
}

// --------------------------------------------------------------------------
// The view

BoatView::BoatView(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent);
  auto& sys = BoatSystems::instance();
  auto* l = new QHBoxLayout(this);
  l->setContentsMargins(16, 16, 16, 16);
  l->setSpacing(16);

  // Left: title, cabin layout, light scenes.
  m_left = new QWidget;
  auto* left = new QVBoxLayout(m_left);
  left->setContentsMargins(0, 0, 0, 0);
  left->setSpacing(10);
  auto* head = new QHBoxLayout;
  auto* title = label(tr("Boat"));
  title->setStyleSheet("font-size: 26px; font-weight: 600;");
  head->addWidget(title);
  auto* hint = label(sys.simulated()
                         ? tr("Simulated values · Signal K connection follows")
                         : tr("Signal K"),
                     "dim");
  head->addWidget(hint, 1, Qt::AlignRight | Qt::AlignVCenter);
  left->addLayout(head);
  m_layout = new CabinLayout;
  left->addWidget(m_layout, 1);
  left->addWidget(label(
      tr("Tap a light to switch it, the shore power inlet at the stern to "
         "plug in (simulation)."),
      "dim"));
  auto* scenes = new QHBoxLayout;
  scenes->setSpacing(8);
  m_scenes = new QButtonGroup(this);
  m_scenes->setExclusive(false);
  m_sceneRow = new QGridLayout;
  m_sceneRow->setSpacing(8);
  scenes->addLayout(m_sceneRow, 1);
  auto* editScenes = new QPushButton;
  editScenes->setObjectName("editScenes");
  editScenes->setToolTip(tr("Edit light scenes"));
  editScenes->setCursor(Qt::PointingHandCursor);
  editScenes->setMinimumHeight(48);
  editScenes->setIcon(Icons::icon("palette", Theme::p().text, 20));
  editScenes->setIconSize(QSize(20, 20));
  connect(editScenes, &QPushButton::clicked, this,
          &BoatView::editScenesRequested);
  scenes->addWidget(editScenes);
  rebuildScenes();
  left->addLayout(scenes);
  l->addWidget(m_left, 1);

  // Right: cards.
  auto* scroll = new QScrollArea;
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setFixedWidth(420);
  scroll->setStyleSheet("QScrollArea { background: transparent; }");
  auto* content = new QWidget;
  content->setAttribute(Qt::WA_TranslucentBackground);
  auto* col = new QVBoxLayout(content);
  col->setContentsMargins(0, 0, 8, 0);
  col->setSpacing(12);
  scroll->setWidget(content);
  scroll->viewport()->setAutoFillBackground(false);
  content->setAutoFillBackground(false);

  col->addWidget(card(tr("ENERGY · VICTRON"), new EnergyCard));

  // Heater.
  auto* heater = new QWidget;
  auto* hl = new QVBoxLayout(heater);
  hl->setContentsMargins(0, 0, 0, 0);
  hl->setSpacing(8);
  auto* hrow = new QHBoxLayout;
  m_heaterState = label(QString(), "value");
  m_heaterState->setStyleSheet("font-size: 18px; font-weight: 600;");
  hrow->addWidget(m_heaterState, 1);
  m_heaterOn = new QPushButton;
  m_heaterOn->setCursor(Qt::PointingHandCursor);
  connect(m_heaterOn, &QPushButton::clicked, this, [] {
    auto& s = BoatSystems::instance();
    const auto st = s.heater().state;
    s.setHeaterOn(st == Heater::State::Off || st == Heater::State::CoolDown ||
                  st == Heater::State::Fault);
  });
  hrow->addWidget(m_heaterOn);
  hl->addLayout(hrow);
  auto* srow = new QHBoxLayout;
  srow->addWidget(label(tr("Target"), "dim"));
  auto* minus = new QPushButton(QStringLiteral("−"));
  auto* plus = new QPushButton(QStringLiteral("+"));
  m_setpoint = label(QString(), "value");
  m_setpoint->setStyleSheet("font-size: 24px;");
  m_setpoint->setAlignment(Qt::AlignCenter);
  m_setpoint->setMinimumWidth(80);
  for (auto* b : {minus, plus}) {
    b->setFixedSize(44, 40);
    b->setCursor(Qt::PointingHandCursor);
  }
  connect(minus, &QPushButton::clicked, this, [] {
    auto& s = BoatSystems::instance();
    s.setHeaterSetpoint(s.heater().setpointC - 0.5);
  });
  connect(plus, &QPushButton::clicked, this, [] {
    auto& s = BoatSystems::instance();
    s.setHeaterSetpoint(s.heater().setpointC + 0.5);
  });
  srow->addStretch(1);
  srow->addWidget(minus);
  srow->addWidget(m_setpoint);
  srow->addWidget(plus);
  hl->addLayout(srow);
  m_heaterValues = label(QString(), "dim");
  m_heaterValues->setWordWrap(true);
  hl->addWidget(m_heaterValues);
  m_zones = label(QString(), "dim");
  m_zones->setWordWrap(true);
  hl->addWidget(m_zones);
  auto* wrow = new QHBoxLayout;
  wrow->addWidget(iconLabelFor("drop"));
  m_water = label(QString());
  m_water->setWordWrap(true);
  wrow->addWidget(m_water, 1);
  m_waterOn = new ToggleSwitch;
  m_waterOn->setToolTip(tr("Hot water boiler"));
  connect(m_waterOn, &QAbstractButton::toggled, this,
          [](bool on) { BoatSystems::instance().setWaterHeaterOn(on); });
  wrow->addWidget(m_waterOn);
  hl->addLayout(wrow);
  col->addWidget(card(tr("HEATING · WEBASTO · HOT WATER"), heater));

  // Automations: one tap runs them.
  auto* autos = new QWidget;
  m_automations = new QVBoxLayout(autos);
  m_automations->setContentsMargins(0, 0, 0, 0);
  m_automations->setSpacing(6);
  auto* editAutos = new QPushButton(tr("Edit"));
  editAutos->setProperty("role", "ghost");
  editAutos->setCursor(Qt::PointingHandCursor);
  connect(editAutos, &QPushButton::clicked, this,
          &BoatView::automationsRequested);
  col->addWidget(card(tr("AUTOMATIONS"), autos, editAutos));
  rebuildAutomations();

  col->addWidget(card(tr("TANKS"), new TanksCard));

  // Consumers.
  auto* consumers = new QWidget;
  m_consumers = new QVBoxLayout(consumers);
  m_consumers->setContentsMargins(0, 0, 0, 0);
  m_consumers->setSpacing(4);
  for (const BoatConsumer& c : sys.consumers()) {
    auto* row = new QWidget;
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 2, 0, 2);
    auto* name = label(c.name);
    auto* power = label(QString(), "dim");
    power->setObjectName("power");
    rl->addWidget(name, 1);
    rl->addWidget(power);
    auto* sw = new ToggleSwitch;
    sw->setEnabled(c.switchable);
    connect(sw, &QAbstractButton::toggled, this, [id = c.id](bool on) {
      BoatSystems::instance().setConsumer(id, on);
    });
    rl->addWidget(sw);
    m_consumers->addWidget(row);
    m_consumerSwitch.insert(c.id, row);
  }
  col->addWidget(card(tr("CONSUMERS"), consumers));
  col->addStretch(1);
  l->addWidget(scroll);

  connect(&sys, &BoatSystems::changed, this, &BoatView::sync);
  connect(&sys, &BoatSystems::scenesChanged, this, [this] {
    rebuildScenes();
    rebuildAutomations();
  });
  connect(&AutomationStore::instance(), &AutomationStore::changed, this,
          &BoatView::rebuildAutomations);
  connect(&Theme::instance(), &Theme::changed, this, [this] {
    rebuildScenes();
    rebuildAutomations();
    update();
  });
  sync();
}

void BoatView::rebuildScenes() {
  for (QAbstractButton* b : m_scenes->buttons()) {
    m_scenes->removeButton(b);
    b->hide();
    b->deleteLater();
  }
  // The scenes as buttons, six per row.
  int n = 0;
  for (const LightScene& s : BoatSystems::instance().scenes()) {
    const int index = n++;
    auto* b = new QPushButton(s.name);
    b->setProperty("role", "segment");
    b->setProperty("scene", s.id);
    b->setCheckable(true);
    b->setCursor(Qt::PointingHandCursor);
    b->setMinimumHeight(48);
    b->setMinimumWidth(60);
    b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    b->setToolTip(s.name);
    b->setIcon(Icons::icon(
        s.icon, s.id == "night" ? QColor(255, 60, 40) : Theme::p().text, 18));
    if (s.id == "night")
      b->setStyleSheet(
          "QPushButton:checked { background: #b3261e; color: white; }");
    m_scenes->addButton(b);
    m_sceneRow->addWidget(b, index / 6, index % 6);
    connect(b, &QPushButton::clicked, this,
            [id = s.id] { BoatSystems::instance().applyScene(id); });
    b->setChecked(s.id == BoatSystems::instance().activeScene());
  }
}

void BoatView::rebuildAutomations() {
  while (QLayoutItem* it = m_automations->takeAt(0)) {
    if (QWidget* w = it->widget()) {
      w->hide();
      w->deleteLater();
    }
    delete it;
  }
  const Palette& pal = Theme::p();
  for (const Automation& a : AutomationStore::instance().automations()) {
    auto* b = new QPushButton(a.name);
    b->setObjectName("automation-" + a.id);
    b->setCursor(Qt::PointingHandCursor);
    b->setMinimumHeight(44);
    b->setIcon(Icons::icon(a.icon, pal.accentBright, 18));
    b->setIconSize(QSize(18, 18));
    b->setStyleSheet("text-align: left; padding-left: 14px;");
    QStringList what;
    for (const auto& act : a.actions) what << act.describe();
    b->setToolTip(a.schedule() + "\n" + what.join("\n"));
    connect(b, &QPushButton::clicked, this,
            [id = a.id] { AutomationStore::instance().run(id); });
    m_automations->addWidget(b);
  }
}

QWidget* BoatView::card(const QString& title, QWidget* content,
                        QWidget* action) {
  auto* w = new QWidget;
  w->setObjectName("boatCard");
  w->setAttribute(Qt::WA_StyledBackground);
  const Palette& pal = Theme::p();
  w->setStyleSheet(QStringLiteral("#boatCard { background: %1; border: 1px "
                                  "solid %2; border-radius: 18px; }")
                       .arg(pal.panel.name(QColor::HexArgb),
                            pal.panelBorder.name(QColor::HexArgb)));
  auto* l = new QVBoxLayout(w);
  l->setContentsMargins(18, 14, 18, 16);
  l->setSpacing(10);
  if (action) {
    auto* head = new QHBoxLayout;
    head->addWidget(label(title, "section"), 1);
    head->addWidget(action);
    l->addLayout(head);
  } else {
    l->addWidget(label(title, "section"));
  }
  l->addWidget(content);
  return w;
}

void BoatView::setLeftInset(int px) {
  layout()->setContentsMargins(16 + px, 16, 16, 16);
}

void BoatView::sync() {
  auto& sys = BoatSystems::instance();
  // Scenes: the active one checked (none after a manual change).
  for (QAbstractButton* b : m_scenes->buttons())
    b->setChecked(b->property("scene").toString() == sys.activeScene());

  const Heater& h = sys.heater();
  const bool running = h.state == Heater::State::Starting ||
                       h.state == Heater::State::Heating ||
                       h.state == Heater::State::Ventilation;
  m_heaterState->setText(Heater::stateName(h.state));
  m_heaterOn->setText(running ? tr("Switch off") : tr("Switch on"));
  m_heaterOn->setProperty("role", running ? "danger" : "primary");
  m_heaterOn->style()->unpolish(m_heaterOn);
  m_heaterOn->style()->polish(m_heaterOn);
  m_setpoint->setText(QStringLiteral("%1°").arg(h.setpointC, 0, 'f', 1));
  const BoatZone* room = sys.zone(h.zone);
  m_heaterValues->setText(
      tr("%1 %2 · output %3 % · outlet %4 · %5 l/h · %6 V · %7 h")
          .arg(room ? room->name : QString(),
               room ? degrees(room->temperatureC) : QString())
          .arg(int(std::round(h.outputPct)))
          .arg(degrees(h.outletC))
          .arg(h.fuelLph, 0, 'f', 2)
          .arg(h.supplyV, 0, 'f', 1)
          .arg(int(h.operatingHours)));
  // Rooms with an own target (duct flap).
  QStringList zones;
  for (const BoatZone& z : sys.zones()) {
    if (z.id == h.zone || !h.zoneSetpointC.contains(z.id)) continue;
    const double t = h.zoneSetpointC.value(z.id);
    zones << tr("%1 %2 → %3 (flap %4)")
                 .arg(z.name, degrees(z.temperatureC), degrees(t),
                      z.temperatureC > t + 0.2 ? tr("closed") : tr("open"));
  }
  m_zones->setText(zones.join("\n"));
  m_zones->setVisible(!zones.isEmpty());
  const WaterHeater& w = sys.waterHeater();
  {
    const QSignalBlocker block(m_waterOn);
    m_waterOn->setChecked(w.on);
  }
  m_water->setText(tr("Hot water %1 → %2 · %3")
                       .arg(degrees(w.tempC), degrees(w.targetC),
                            !w.on       ? tr("off")
                            : w.heating ? (sys.energy().shoreConnected
                                               ? tr("heating (shore power)")
                                               : tr("heating (inverter)"))
                                        : tr("ready")));

  for (const BoatConsumer& c : sys.consumers()) {
    QWidget* row = m_consumerSwitch.value(c.id);
    if (!row) continue;
    if (auto* sw = row->findChild<ToggleSwitch*>()) {
      const QSignalBlocker block(sw);
      sw->setChecked(c.on);
    }
    if (auto* power = row->findChild<QLabel*>("power"))
      power->setText(c.on && c.powerW > 0 ? watts(c.powerW) : QString());
  }
}

void BoatView::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  QPainter p(this);
  QLinearGradient g(0, 0, 0, height());
  g.setColorAt(0, pal.dark ? QColor(14, 17, 23) : QColor(244, 246, 249));
  g.setColorAt(1, pal.dark ? QColor(8, 10, 14) : QColor(228, 232, 238));
  p.fillRect(rect(), g);
}
