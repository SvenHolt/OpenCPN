#include "ui/LogView.h"

#include <QDialog>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <cmath>

#include "core/GeoMath.h"
#include "ui/ChartView.h"
#include "ui/DrawerPages.h"
#include "ui/GlassPanel.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

namespace {
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

QLabel* label(const QString& text, const char* role = nullptr) {
  auto* l = new QLabel(text);
  if (role) l->setProperty("role", role);
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

QScrollArea* scrollFor(QWidget* content) {
  auto* scroll = new QScrollArea;
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setStyleSheet("QScrollArea { background: transparent; }");
  scroll->viewport()->setAutoFillBackground(false);
  content->setAttribute(Qt::WA_TranslucentBackground);
  content->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  scroll->setWidget(content);
  return scroll;
}

void clearLayout(QLayout* l) {
  while (QLayoutItem* it = l->takeAt(0)) {
    if (QWidget* w = it->widget()) {
      w->hide();
      w->deleteLater();
    } else if (QLayout* sub = it->layout()) {
      clearLayout(sub);
    }
    delete it;
  }
}

/** Rounded panel like the boat view cards. */
QWidget* panel(QWidget* content, const QString& title = {}) {
  auto* w = new QWidget;
  w->setObjectName("logCard");
  w->setAttribute(Qt::WA_StyledBackground);
  const Palette& pal = Theme::p();
  w->setStyleSheet(QStringLiteral("#logCard { background: %1; border: 1px "
                                  "solid %2; border-radius: 18px; }")
                       .arg(pal.panel.name(QColor::HexArgb),
                            pal.panelBorder.name(QColor::HexArgb)));
  auto* l = new QVBoxLayout(w);
  l->setContentsMargins(16, 12, 16, 14);
  l->setSpacing(8);
  if (!title.isEmpty()) l->addWidget(label(title, "section"));
  l->addWidget(content);
  return w;
}

QString hoursText(double h) {
  const int m = int(std::round(h * 60));
  return QStringLiteral("%1:%2").arg(m / 60).arg(m % 60, 2, 10, QChar('0'));
}

QDateTime local(qint64 t) {
  return QDateTime::fromSecsSinceEpoch(t, Qt::UTC).toLocalTime();
}

/** Blue (slow) … green … orange (fast). */
QColor speedColor(double kn) {
  const double f = std::clamp(kn / 8.0, 0.0, 1.0);
  return QColor::fromHsvF((0.62 - 0.55 * f), 0.75, 0.95);
}

/** A stat tile: value large, label small. */
QWidget* tile(const QString& value, const QString& name) {
  auto* w = new QWidget;
  auto* l = new QVBoxLayout(w);
  l->setContentsMargins(0, 2, 0, 2);
  l->setSpacing(0);
  auto* v = label(value);
  v->setFont(Theme::numberFont(22, QFont::Normal));
  l->addWidget(v);
  l->addWidget(label(name, "dim"));
  return w;
}
}  // namespace

// --------------------------------------------------------------------------
// TrackMap

TrackMap::TrackMap(QWidget* parent) : QWidget(parent) {
  setMinimumHeight(300);
  setCursor(Qt::CrossCursor);
}

void TrackMap::setLeg(const Leg* leg) {
  m_samples = leg ? leg->samples : QVector<LegSample>();
  m_from = leg ? leg->from : QString();
  m_to = leg ? leg->to : QString();
  m_merc.clear();
  for (const auto& s : m_samples) m_merc << geo::toMercator(s.lat, s.lon);
  m_marker = 0;
  fit();
  update();
}

void TrackMap::setMarker(qint64 t) {
  m_marker = t;
  update();
}

void TrackMap::resizeEvent(QResizeEvent*) { fit(); }

void TrackMap::fit() {
  if (m_merc.isEmpty() || width() < 10) return;
  double x0 = 1, y0 = 1, x1 = 0, y1 = 0;
  for (const QPointF& p : m_merc) {
    x0 = std::min(x0, p.x());
    x1 = std::max(x1, p.x());
    y0 = std::min(y0, p.y());
    y1 = std::max(y1, p.y());
  }
  // Some sea around the track, at least about 3 NM.
  const double minSpan = 3 / geo::nmPerMercatorUnit(m_samples.first().lat);
  const double w = std::max(x1 - x0, minSpan), h = std::max(y1 - y0, minSpan);
  const QPointF c((x0 + x1) / 2, (y0 + y1) / 2);
  m_scale = std::min(width() / (w * 1.25), height() / (h * 1.25));
  m_offset = QPointF(width() / 2.0, height() / 2.0) - c * m_scale;
  m_view = QRectF(-m_offset / m_scale, QSizeF(width(), height()) / m_scale);
}

QPointF TrackMap::toScreen(const QPointF& merc) const {
  return merc * m_scale + m_offset;
}

QPointF TrackMap::screenPos(int index) const {
  return index >= 0 && index < m_merc.size() ? toScreen(m_merc[index])
                                             : QPointF();
}

void TrackMap::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  QPainterPath clip;
  clip.addRoundedRect(QRectF(rect()), 14, 14);
  p.setClipPath(clip);
  p.fillRect(rect(), pal.water);
  if (m_merc.isEmpty()) {
    p.setPen(pal.textDim);
    p.drawText(rect(), Qt::AlignCenter, tr("No track"));
    return;
  }
  // Land.
  if (m_land) {
    const auto& polys = m_scale < 14000 || m_land->detailed.isEmpty()
                            ? m_land->crude
                            : m_land->detailed;
    p.save();
    p.translate(m_offset);
    p.scale(m_scale, m_scale);
    p.setPen(Qt::NoPen);  // tile seams of the basemap stay invisible
    p.setBrush(pal.land);
    for (const auto& poly : polys)
      if (poly.bounds.intersects(m_view)) p.drawPath(poly.path);
    p.restore();
  }
  // Track colored by speed.
  for (int i = 1; i < m_merc.size(); ++i) {
    if (m_samples[i].t - m_samples[i - 1].t > 900) continue;
    p.setPen(
        QPen(speedColor(m_samples[i].sog), 3.5, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(toScreen(m_merc[i - 1]), toScreen(m_merc[i]));
  }
  // Wind arrows (true wind, pointing downwind) every ~70 px of track.
  double run = 0;
  for (int i = 1; i < m_merc.size(); ++i) {
    run += QLineF(toScreen(m_merc[i - 1]), toScreen(m_merc[i])).length();
    const LegSample& s = m_samples[i];
    if (run < 70 || std::isnan(s.twd) || std::isnan(s.tws)) continue;
    run = 0;
    const QPointF c = toScreen(m_merc[i]);
    const double a = (s.twd + 180) * M_PI / 180;  // blowing towards
    const QPointF d(std::sin(a), -std::cos(a));
    const QPointF base = c + QPointF(-d.y(), d.x()) * 16;  // beside the track
    const double len = 8 + std::min(14.0, s.tws * 0.6);
    QColor col = pal.textDim;
    p.setPen(QPen(col, 1.5));
    p.drawLine(base - d * len / 2, base + d * len / 2);
    const QPointF tip = base + d * len / 2;
    const QPointF n(-d.y(), d.x());
    p.setBrush(col);
    p.drawPolygon(QPolygonF({tip, tip - d * 5 + n * 3, tip - d * 5 - n * 3}));
  }
  // Start, end.
  const auto pin = [&](const QPointF& at, const QColor& c, const QString& t) {
    p.setPen(QPen(Qt::white, 2));
    p.setBrush(c);
    p.drawEllipse(at, 6, 6);
    if (t.isEmpty()) return;
    p.setFont(Theme::font(12, QFont::DemiBold));
    p.setPen(pal.text);
    p.drawText(at + QPointF(10, 4), t);
  };
  pin(toScreen(m_merc.first()), pal.success, m_from);
  pin(toScreen(m_merc.last()), pal.danger, m_to);
  // Time marker from the charts.
  if (m_marker > 0) {
    auto it =
        std::lower_bound(m_samples.begin(), m_samples.end(), m_marker,
                         [](const LegSample& s, qint64 v) { return s.t < v; });
    const int i =
        std::clamp(int(it - m_samples.begin()), 0, int(m_samples.size()) - 1);
    const QPointF at = toScreen(m_merc[i]);
    p.setPen(QPen(Qt::white, 2.5));
    p.setBrush(pal.accentBright);
    p.drawEllipse(at, 8, 8);
    // Heading.
    if (!std::isnan(m_samples[i].hdg)) {
      const double a = m_samples[i].hdg * M_PI / 180;
      p.drawLine(at, at + QPointF(std::sin(a), -std::cos(a)) * 22);
    }
  }
  // Scale bar and legend.
  const double nmPerPx =
      geo::nmPerMercatorUnit(m_samples.first().lat) / m_scale;
  double bar = 1;
  for (double c : {0.5, 1.0, 2.0, 5.0, 10.0, 20.0})
    if (c / nmPerPx < 140) bar = c;
  const double px = bar / nmPerPx;
  const QPointF o(14, height() - 16);
  p.setPen(QPen(pal.text, 1.5));
  p.drawLine(o, o + QPointF(px, 0));
  p.drawLine(o, o + QPointF(0, -5));
  p.drawLine(o + QPointF(px, 0), o + QPointF(px, -5));
  p.setFont(Theme::font(11));
  p.drawText(o + QPointF(px + 6, 4), QStringLiteral("%1 NM").arg(bar));
  const QRectF leg(width() - 150, height() - 24, 136, 8);
  QLinearGradient g(leg.topLeft(), leg.topRight());
  for (int k = 0; k <= 4; ++k) g.setColorAt(k / 4.0, speedColor(k * 2.0));
  p.setPen(Qt::NoPen);
  p.setBrush(g);
  p.drawRoundedRect(leg, 4, 4);
  p.setPen(pal.textDim);
  p.drawText(QRectF(leg.left(), leg.top() - 18, leg.width(), 16), Qt::AlignLeft,
             "0 kn");
  p.drawText(QRectF(leg.left(), leg.top() - 18, leg.width(), 16),
             Qt::AlignRight, "8 kn");
}

void TrackMap::mouseReleaseEvent(QMouseEvent* e) {
  // The sample nearest to the click.
  int best = -1;
  double bestD = 30;
  for (int i = 0; i < m_merc.size(); ++i) {
    const double d = QLineF(toScreen(m_merc[i]), e->position()).length();
    if (d < bestD) {
      bestD = d;
      best = i;
    }
  }
  if (best >= 0) {
    setMarker(m_samples[best].t);
    emit timePicked(m_samples[best].t);
  }
}

// --------------------------------------------------------------------------
// TimeChart

TimeChart::TimeChart(const QString& title, const QString& unit,
                     QVector<Series> series, QWidget* parent)
    : QWidget(parent),
      m_title(title),
      m_unit(unit),
      m_series(std::move(series)) {
  setMinimumHeight(140);
  setMouseTracking(true);
}

int TimeChart::pointCount(int i) const {
  return i < m_points.size() ? int(m_points[i].size()) : 0;
}

void TimeChart::setLeg(const Leg* leg) {
  m_points.clear();
  m_motoring.clear();
  m_marker = 0;
  if (!leg || leg->samples.isEmpty()) {
    update();
    return;
  }
  m_t0 = leg->samples.first().t;
  m_t1 = std::max(m_t0 + 1, leg->samples.last().t);
  double lo = 1e18, hi = -1e18;
  for (const Series& s : m_series) {
    QVector<QPointF> pts;
    for (const LegSample& x : leg->samples) {
      const double v = s.value(x);
      if (std::isnan(v)) continue;
      pts << QPointF(double(x.t), v);
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }
    m_points << pts;
  }
  // Motoring periods.
  qint64 start = -1;
  for (const LegSample& x : leg->samples) {
    const bool motor = !std::isnan(x.rpm) && x.rpm > 300;
    if (motor && start < 0) start = x.t;
    if (!motor && start >= 0) {
      m_motoring << qMakePair(start, x.t);
      start = -1;
    }
  }
  if (start >= 0) m_motoring << qMakePair(start, m_t1);

  if (lo > hi) lo = 0, hi = 1;
  if (m_fixedMax > m_fixedMin) {
    lo = m_fixedMin;
    hi = m_fixedMax;
  } else if (m_symmetric) {
    const double m = std::max({std::abs(lo), std::abs(hi), 5.0});
    lo = -m;
    hi = m;
  } else {
    const double pad = std::max(0.5, (hi - lo) * 0.1);
    lo = lo >= 0 && lo - pad < 0 ? 0 : lo - pad;
    hi += pad;
  }
  m_min = lo;
  m_max = hi;
  update();
}

void TimeChart::setMarker(qint64 t) {
  m_marker = t;
  update();
}

QRectF TimeChart::plotRect() const {
  return QRectF(46, 26, width() - 58, height() - 46);
}

void TimeChart::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF r = plotRect();
  const auto x = [&](double t) {
    return r.left() + r.width() * (t - m_t0) / double(m_t1 - m_t0);
  };
  const auto y = [&](double v) {
    return r.bottom() - r.height() * (v - m_min) / (m_max - m_min);
  };
  // Title and legend.
  p.setFont(Theme::font(12, QFont::DemiBold));
  p.setPen(pal.text);
  p.drawText(QRectF(0, 0, width(), 20), Qt::AlignLeft | Qt::AlignVCenter,
             m_unit.isEmpty() ? m_title
                              : QStringLiteral("%1 (%2)").arg(m_title, m_unit));
  p.setFont(Theme::font(11));
  double lx = width();
  for (int i = int(m_series.size()) - 1; i >= 0; --i) {
    const QString n = m_series[i].name;
    const double w = QFontMetricsF(p.font()).horizontalAdvance(n);
    lx -= w + 26;
    p.setPen(Qt::NoPen);
    p.setBrush(m_series[i].color);
    p.drawRoundedRect(QRectF(lx, 7, 12, 6), 3, 3);
    p.setPen(pal.textDim);
    p.drawText(QPointF(lx + 16, 14), n);
  }
  // Motoring.
  QColor motor = pal.warning;
  motor.setAlpha(28);
  for (const auto& m : m_motoring)
    p.fillRect(
        QRectF(QPointF(x(m.first), r.top()), QPointF(x(m.second), r.bottom())),
        motor);
  // Grid.
  p.setFont(Theme::font(10));
  for (int k = 0; k <= 2; ++k) {
    const double v = m_min + (m_max - m_min) * k / 2;
    p.setPen(QPen(pal.panelBorder, 1));
    p.drawLine(QPointF(r.left(), y(v)), QPointF(r.right(), y(v)));
    p.setPen(pal.textFaint);
    p.drawText(QRectF(0, y(v) - 8, r.left() - 6, 16),
               Qt::AlignRight | Qt::AlignVCenter,
               QString::number(v, 'f', std::abs(m_max - m_min) < 5 ? 1 : 0));
  }
  // Hours along the bottom.
  const qint64 span = m_t1 - m_t0;
  const qint64 step = span > 8 * 3600 ? 7200 : 3600;
  for (qint64 t = (m_t0 / step + 1) * step; t < m_t1; t += step) {
    const double px = x(double(t));
    p.setPen(QPen(pal.panelBorder, 1, Qt::DotLine));
    p.drawLine(QPointF(px, r.top()), QPointF(px, r.bottom()));
    p.setPen(pal.textFaint);
    p.drawText(QRectF(px - 30, r.bottom() + 3, 60, 14), Qt::AlignCenter,
               local(t).toString("HH:mm"));
  }
  // Series.
  for (int i = 0; i < m_points.size(); ++i) {
    const auto& pts = m_points[i];
    if (pts.isEmpty()) continue;
    QPainterPath path;
    bool started = false;
    double lastT = 0;
    for (const QPointF& pt : pts) {
      const QPointF s(x(pt.x()), y(pt.y()));
      if (!started || pt.x() - lastT > 900)
        path.moveTo(s);
      else
        path.lineTo(s);
      started = true;
      lastT = pt.x();
    }
    if (m_series[i].fill) {
      QPainterPath area = path;
      area.lineTo(x(pts.last().x()), y(std::max(m_min, 0.0)));
      area.lineTo(x(pts.first().x()), y(std::max(m_min, 0.0)));
      area.closeSubpath();
      QColor f = m_series[i].color;
      f.setAlpha(45);
      p.fillPath(area, f);
    }
    p.setPen(QPen(m_series[i].color, 1.8));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
  }
  if (m_symmetric) {
    p.setPen(QPen(pal.textFaint, 1));
    p.drawLine(QPointF(r.left(), y(0)), QPointF(r.right(), y(0)));
  }
  // Crosshair with values.
  if (m_marker >= m_t0 && m_marker <= m_t1) {
    const double px = x(double(m_marker));
    p.setPen(QPen(pal.accentBright, 1.2));
    p.drawLine(QPointF(px, r.top()), QPointF(px, r.bottom()));
    QStringList vals;
    for (int i = 0; i < m_points.size(); ++i) {
      const auto& pts = m_points[i];
      auto it = std::lower_bound(
          pts.begin(), pts.end(), double(m_marker),
          [](const QPointF& a, double t) { return a.x() < t; });
      if (it == pts.end()) continue;
      p.setPen(Qt::NoPen);
      p.setBrush(m_series[i].color);
      p.drawEllipse(QPointF(px, y(it->y())), 3.5, 3.5);
      vals << QStringLiteral("%1 %2").arg(it->y(), 0, 'f', 1).arg(m_unit);
    }
    const QString text =
        local(m_marker).toString("HH:mm") + "  " + vals.join(" · ");
    p.setFont(Theme::font(11, QFont::DemiBold));
    const double w = QFontMetricsF(p.font()).horizontalAdvance(text) + 14;
    QRectF box(std::clamp(px - w / 2, r.left(), r.right() - w), r.top(), w, 18);
    p.setPen(Qt::NoPen);
    p.setBrush(pal.panelPressed);
    p.drawRoundedRect(box, 6, 6);
    p.setPen(pal.text);
    p.drawText(box, Qt::AlignCenter, text);
  }
}

void TimeChart::mouseMoveEvent(QMouseEvent* e) {
  const QRectF r = plotRect();
  if (e->position().x() < r.left() || e->position().x() > r.right()) return;
  const qint64 t =
      m_t0 + qint64((e->position().x() - r.left()) / r.width() * (m_t1 - m_t0));
  emit hovered(t);
}

void TimeChart::leaveEvent(QEvent*) {}

// --------------------------------------------------------------------------
// LegDetail

LegDetail::LegDetail(QWidget* parent) : QWidget(parent) {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  auto* content = new QWidget;
  auto* l = new QVBoxLayout(content);
  l->setContentsMargins(0, 0, 10, 0);
  l->setSpacing(12);
  outer->addWidget(scrollFor(content));

  auto* head = new QHBoxLayout;
  auto* titles = new QVBoxLayout;
  titles->setSpacing(2);
  m_title = label(QString());
  m_title->setStyleSheet("font-size: 22px; font-weight: 600;");
  titles->addWidget(m_title);
  m_when = label(QString(), "dim");
  titles->addWidget(m_when);
  head->addLayout(titles, 1);
  auto* rename = button(tr("Rename"), "edit");
  connect(rename, &QPushButton::clicked, this, [this] {
    const Leg* leg = LegStore::instance().leg(m_id);
    if (!leg) return;
    // Inline dialog: name, from, to.
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Rename leg"));
    auto* dl = new QGridLayout(&dlg);
    auto* name = new QLineEdit(leg->name);
    auto* from = new QLineEdit(leg->from);
    auto* to = new QLineEdit(leg->to);
    dl->addWidget(label(tr("Name")), 0, 0);
    dl->addWidget(name, 0, 1);
    dl->addWidget(label(tr("From")), 1, 0);
    dl->addWidget(from, 1, 1);
    dl->addWidget(label(tr("To")), 2, 0);
    dl->addWidget(to, 2, 1);
    auto* ok = button(tr("Save"), "check", "primary");
    connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    dl->addWidget(ok, 3, 1, Qt::AlignRight);
    dlg.setMinimumWidth(380);
    if (dlg.exec() == QDialog::Accepted)
      LegStore::instance().rename(m_id, name->text().trimmed(),
                                  from->text().trimmed(), to->text().trimmed());
  });
  head->addWidget(rename);
  m_logbook = button(tr("Logbook"), "book");
  connect(m_logbook, &QPushButton::clicked, this, [this] {
    if (const Leg* leg = LegStore::instance().leg(m_id))
      emit logbookRequested(leg->start().toLocalTime().date());
  });
  head->addWidget(m_logbook);
  l->addLayout(head);

  auto* tiles = new QWidget;
  m_tiles = new QGridLayout(tiles);
  m_tiles->setContentsMargins(0, 0, 0, 0);
  m_tiles->setHorizontalSpacing(18);
  m_tiles->setVerticalSpacing(8);
  l->addWidget(panel(tiles));

  auto* mapBox = new QWidget;
  auto* ml = new QVBoxLayout(mapBox);
  ml->setContentsMargins(0, 0, 0, 0);
  m_map = new TrackMap;
  m_map->setMinimumHeight(360);
  ml->addWidget(m_map);
  m_hover = label(tr("Move over a chart or tap the track."), "dim");
  ml->addWidget(m_hover);
  l->addWidget(panel(mapBox, tr("TRACK")));
  connect(m_map, &TrackMap::timePicked, this, &LegDetail::setMarker);

  using S = TimeChart::Series;
  const Palette& pal = Theme::p();
  m_charts << new TimeChart(tr("Speed"), "kn",
                            {S{tr("SOG"), pal.accentBright,
                               [](const LegSample& s) { return s.sog; }, true},
                             S{tr("STW"), QColor(120, 200, 255),
                               [](const LegSample& s) { return s.stw; }}});
  auto* heel =
      new TimeChart(tr("Heel"), "°",
                    {S{tr("Heel (+ stbd)"), QColor(180, 140, 255),
                       [](const LegSample& s) { return s.heel; }, true}});
  heel->setSymmetric(true);
  m_charts << heel;
  m_charts << new TimeChart(tr("Wind"), "kn",
                            {S{tr("True"), QColor(80, 200, 160),
                               [](const LegSample& s) { return s.tws; }, true},
                             S{tr("Apparent"), QColor(200, 220, 120),
                               [](const LegSample& s) { return s.aws; }}});
  auto* twd = new TimeChart(tr("Wind direction"), "°",
                            {S{tr("True from"), QColor(80, 200, 160),
                               [](const LegSample& s) { return s.twd; }}});
  twd->setRange(0, 360);
  m_charts << twd;
  m_charts << new TimeChart(
      tr("Energy"), "W",
      {S{tr("Consumption"), QColor(255, 120, 90),
         [](const LegSample& s) { return s.loadsW; }, true},
       S{tr("Charging"), QColor(60, 200, 110),
         [](const LegSample& s) { return s.chargeW; }}});
  auto* soc = new TimeChart(tr("Battery"), "%",
                            {S{tr("State of charge"), QColor(60, 200, 110),
                               [](const LegSample& s) {
                                 return std::isnan(s.soc) ? s.soc : s.soc * 100;
                               },
                               true}});
  soc->setRange(0, 100);
  m_charts << soc;
  m_charts << new TimeChart(
      tr("Diesel"), "l/h",
      {S{tr("Engine and heater"), QColor(255, 176, 50),
         [](const LegSample& s) { return s.fuelLph; }, true}});
  m_charts << new TimeChart(tr("Outside temperature"), "°C",
                            {S{tr("Outside"), QColor(255, 140, 80),
                               [](const LegSample& s) { return s.tempC; }}});
  m_charts << new TimeChart(
      tr("Depth"), "m",
      {S{tr("Depth"), QColor(70, 150, 255),
         [](const LegSample& s) { return s.depth; }, true}});
  auto* charts = new QWidget;
  auto* cl = new QVBoxLayout(charts);
  cl->setContentsMargins(0, 0, 0, 0);
  cl->setSpacing(14);
  for (TimeChart* c : m_charts) {
    cl->addWidget(c);
    connect(c, &TimeChart::hovered, this, &LegDetail::setMarker);
  }
  auto* note = label(tr("Shaded: under engine."), "dim");
  cl->addWidget(note);
  l->addWidget(panel(charts, tr("DATA")));
  l->addStretch(1);
  connect(&LegStore::instance(), &LegStore::changed, this, [this] {
    if (!m_id.isEmpty()) showLeg(m_id);
  });
}

void LegDetail::setLand(std::shared_ptr<const LandData> land) {
  m_map->setLand(std::move(land));
}

void LegDetail::setMarker(qint64 t) {
  m_map->setMarker(t);
  for (TimeChart* c : m_charts) c->setMarker(t);
  const Leg* leg = LegStore::instance().leg(m_id);
  if (!leg) return;
  const int i = leg->indexAt(t);
  if (i < 0) return;
  const LegSample& s = leg->samples[i];
  const auto f = [](double v, int d = 1) {
    return std::isnan(v) ? QStringLiteral("–") : QString::number(v, 'f', d);
  };
  m_hover->setText(tr("%1 · %2 %3 · %4 kn, %5° · heel %6° · wind %7 kn "
                      "from %8°")
                       .arg(local(s.t).toString("HH:mm"), geo::formatLat(s.lat),
                            geo::formatLon(s.lon), f(s.sog), f(s.cog, 0),
                            f(s.heel, 0), f(s.tws, 0), f(s.twd, 0)));
}

void LegDetail::showLeg(const QString& id) {
  m_id = id;
  const Leg* leg = LegStore::instance().leg(id);
  m_map->setLeg(leg);
  for (TimeChart* c : m_charts) c->setLeg(leg);
  clearLayout(m_tiles);
  if (!leg) {
    m_title->setText(tr("No leg selected"));
    m_when->clear();
    return;
  }
  m_title->setText(leg->name);
  const QDateTime a = leg->start().toLocalTime(), b = leg->end().toLocalTime();
  m_when->setText(QLocale().toString(a.date(), QLocale::LongFormat) + " · " +
                  a.toString("HH:mm") + "–" + b.toString("HH:mm") +
                  (leg->simulated ? " · " + tr("simulated") : QString()));
  const LegSummary s = leg->summary();
  const QVector<QPair<QString, QString>> items = {
      {QStringLiteral("%1 NM").arg(s.distanceNm, 0, 'f', 1), tr("Distance")},
      {hoursText(s.hours) + " h", tr("Duration")},
      {QStringLiteral("%1 kn").arg(s.avgSog, 0, 'f', 1), tr("Average speed")},
      {QStringLiteral("%1 kn").arg(s.maxSog, 0, 'f', 1), tr("Top speed")},
      {QStringLiteral("%1 %").arg(int(std::round(s.sailingShare * 100))),
       tr("Under sail")},
      {QStringLiteral("%1°").arg(s.maxHeel, 0, 'f', 0), tr("Max. heel")},
      {QStringLiteral("%1 / %2 kn")
           .arg(s.avgTws, 0, 'f', 0)
           .arg(s.maxTws, 0, 'f', 0),
       tr("Wind avg / max")},
      {hoursText(s.engineHours) + " h", tr("Engine")},
      {QStringLiteral("%1 l").arg(s.fuelL, 0, 'f', 1), tr("Diesel")},
      {QStringLiteral("%1 Ah").arg(s.energyWh / 12.8, 0, 'f', 0),
       tr("Consumed")},
      {QStringLiteral("%1 Ah").arg(s.chargedWh / 12.8, 0, 'f', 0),
       tr("Charged")},
      {QStringLiteral("%1–%2 °C")
           .arg(s.minTempC, 0, 'f', 0)
           .arg(s.maxTempC, 0, 'f', 0),
       tr("Outside")}};
  for (int i = 0; i < items.size(); ++i)
    m_tiles->addWidget(tile(items[i].first, items[i].second), i / 6, i % 6);
}

// --------------------------------------------------------------------------
// LogbookEditor

LogbookEditor::LogbookEditor(QWidget* parent) : QWidget(parent) {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(12);
  auto* head = new QHBoxLayout;
  m_title = label(QString());
  m_title->setStyleSheet("font-size: 22px; font-weight: 600;");
  head->addWidget(m_title, 1);
  m_saved = label(QString(), "dim");
  head->addWidget(m_saved);
  outer->addLayout(head);

  auto* factsBox = new QWidget;
  auto* fl = new QVBoxLayout(factsBox);
  fl->setContentsMargins(0, 0, 0, 0);
  fl->setSpacing(6);
  m_facts = label(QString());
  m_facts->setWordWrap(true);
  fl->addWidget(m_facts);
  m_legs = new QVBoxLayout;
  m_legs->setSpacing(4);
  fl->addLayout(m_legs);
  outer->addWidget(panel(factsBox, tr("RECORDED")));

  auto* form = new QWidget;
  auto* g = new QGridLayout(form);
  g->setContentsMargins(0, 0, 0, 0);
  g->setHorizontalSpacing(10);
  g->setVerticalSpacing(8);
  m_crew = new QLineEdit;
  m_crew->setPlaceholderText(tr("Skipper, crew"));
  g->addWidget(label(tr("Crew")), 0, 0);
  g->addWidget(m_crew, 0, 1, 1, 2);
  m_weather = new QLineEdit;
  m_weather->setPlaceholderText(tr("Wind, sea, sky, visibility"));
  g->addWidget(label(tr("Weather")), 1, 0);
  g->addWidget(m_weather, 1, 1);
  auto* fill = button(tr("From data"), "wind");
  fill->setToolTip(tr("Wind and temperature of the recorded legs"));
  connect(fill, &QPushButton::clicked, this, &LogbookEditor::insertWeather);
  g->addWidget(fill, 1, 2);
  g->setColumnStretch(1, 1);
  outer->addWidget(form);
  outer->addWidget(label(tr("REPORT"), "section"));
  m_report = new QPlainTextEdit;
  m_report->setPlaceholderText(
      tr("What happened today: departure, manoeuvres, sail changes, "
         "sightings, crew, harbour …"));
  m_report->setStyleSheet(
      QStringLiteral("QPlainTextEdit { background: %1; border-radius: 12px; "
                     "padding: 10px; "
                     "font-size: 15px; }")
          .arg(Theme::p().panelHover.name(QColor::HexArgb)));
  outer->addWidget(m_report, 1);

  m_save.setSingleShot(true);
  m_save.setInterval(800);
  connect(&m_save, &QTimer::timeout, this, &LogbookEditor::flush);
  const auto edited = [this] {
    if (m_loading) return;
    m_saved->setText(tr("Editing…"));
    m_save.start();
  };
  connect(m_crew, &QLineEdit::textEdited, this, edited);
  connect(m_weather, &QLineEdit::textEdited, this, edited);
  connect(m_report, &QPlainTextEdit::textChanged, this, edited);
}

void LogbookEditor::flush() {
  m_save.stop();
  if (!m_date.isValid()) return;
  LogEntry e = Logbook::instance().entry(m_date);
  if (e.crew == m_crew->text() && e.weather == m_weather->text() &&
      e.report == m_report->toPlainText())
    return;
  e.crew = m_crew->text();
  e.weather = m_weather->text();
  e.report = m_report->toPlainText();
  Logbook::instance().save(e);
  m_saved->setText(tr("Saved %1").arg(QTime::currentTime().toString("HH:mm")));
}

void LogbookEditor::insertWeather() {
  const DayFacts f = DayFacts::of(m_date);
  if (f.legs == 0) return;
  QString w = tr("%1 %2 kn, max. %3 kn")
                  .arg(f.windDir)
                  .arg(f.avgTws, 0, 'f', 0)
                  .arg(f.maxTws, 0, 'f', 0);
  if (f.maxTempC != 0 || f.minTempC != 0)
    w += tr(", %1–%2 °C").arg(f.minTempC, 0, 'f', 0).arg(f.maxTempC, 0, 'f', 0);
  m_weather->setText(m_weather->text().trimmed().isEmpty()
                         ? w
                         : m_weather->text().trimmed() + "; " + w);
  m_save.start();
}

void LogbookEditor::showDay(const QDate& date) {
  flush();
  m_loading = true;
  m_date = date;
  m_title->setText(QLocale().toString(date, QLocale::LongFormat));
  const DayFacts f = DayFacts::of(date);
  m_facts->setText(f.text());
  clearLayout(m_legs);
  for (const Leg* l : LegStore::instance().legsOn(date)) {
    auto* b =
        button(QStringLiteral("%1  ·  %2–%3")
                   .arg(l->name, l->start().toLocalTime().toString("HH:mm"),
                        l->end().toLocalTime().toString("HH:mm")),
               "route", "ghost");
    b->setObjectName("dayLeg");
    b->setStyleSheet("text-align: left;");
    connect(b, &QPushButton::clicked, this,
            [this, id = l->id] { emit legRequested(id); });
    m_legs->addWidget(b);
  }
  const LogEntry e = Logbook::instance().entry(date);
  m_crew->setText(e.crew);
  m_weather->setText(e.weather);
  m_report->setPlainText(e.report);
  m_saved->setText(
      e.updated.isValid()
          ? tr("Saved %1").arg(e.updated.toLocalTime().toString("dd.MM. HH:mm"))
          : QString());
  m_loading = false;
}

// --------------------------------------------------------------------------
// LogView

LogView::LogView(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent);
  auto* l = new QVBoxLayout(this);
  l->setContentsMargins(16, 16, 16, 16);
  l->setSpacing(12);
  auto* head = new QHBoxLayout;
  auto* title = label(tr("Log"));
  title->setStyleSheet("font-size: 26px; font-weight: 600;");
  head->addWidget(title);
  head->addSpacing(20);
  const QString names[2] = {tr("Legs"), tr("Logbook")};
  const char* icons[2] = {"route", "book"};
  for (int i = 0; i < 2; ++i) {
    m_tabs[i] = new QPushButton(names[i]);
    m_tabs[i]->setProperty("role", "segment");
    m_tabs[i]->setCheckable(true);
    m_tabs[i]->setCursor(Qt::PointingHandCursor);
    m_tabs[i]->setMinimumSize(130, 40);
    m_tabs[i]->setIcon(Icons::icon(icons[i], Theme::p().text, 18));
    head->addWidget(m_tabs[i]);
  }
  connect(m_tabs[0], &QPushButton::clicked, this, &LogView::showLegs);
  connect(m_tabs[1], &QPushButton::clicked, this, [this] { showLogbook(); });
  head->addStretch(1);
  auto* exportMd = button(tr("Export logbook"), "export");
  connect(exportMd, &QPushButton::clicked, this, [this] {
    m_editor->flush();
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export logbook"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
            "/logbook.md",
        tr("Markdown (*.md)"));
    if (path.isEmpty()) return;
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
      f.write(Logbook::instance().markdown().toUtf8());
      f.commit();
    }
  });
  head->addWidget(exportMd);
  l->addLayout(head);

  m_stack = new QStackedWidget;
  l->addWidget(m_stack, 1);

  // Legs: overview left, details right.
  auto* legs = new QWidget;
  auto* ll = new QHBoxLayout(legs);
  ll->setContentsMargins(0, 0, 0, 0);
  ll->setSpacing(16);
  auto* overview = new QWidget;
  overview->setFixedWidth(360);
  auto* ol = new QVBoxLayout(overview);
  ol->setContentsMargins(0, 0, 0, 0);
  ol->setSpacing(10);
  m_totals = label(QString());
  m_totals->setWordWrap(true);
  ol->addWidget(panel(m_totals, tr("ALL LEGS")));
  auto* listContent = new QWidget;
  m_legList = new QVBoxLayout(listContent);
  m_legList->setContentsMargins(0, 0, 8, 0);
  m_legList->setSpacing(8);
  ol->addWidget(scrollFor(listContent), 1);
  ll->addWidget(overview);
  m_detail = new LegDetail;
  ll->addWidget(m_detail, 1);
  m_stack->addWidget(legs);

  // Logbook: days left, editor right.
  auto* book = new QWidget;
  auto* bl = new QHBoxLayout(book);
  bl->setContentsMargins(0, 0, 0, 0);
  bl->setSpacing(16);
  auto* dayContent = new QWidget;
  m_dayList = new QVBoxLayout(dayContent);
  m_dayList->setContentsMargins(0, 0, 8, 0);
  m_dayList->setSpacing(8);
  auto* dayScroll = scrollFor(dayContent);
  dayScroll->setFixedWidth(360);
  bl->addWidget(dayScroll);
  m_editor = new LogbookEditor;
  bl->addWidget(panel(m_editor), 1);
  m_stack->addWidget(book);

  connect(m_detail, &LegDetail::logbookRequested, this,
          [this](const QDate& d) { showLogbook(d); });
  connect(m_editor, &LogbookEditor::legRequested, this, &LogView::openLeg);
  connect(&LegStore::instance(), &LegStore::changed, this, [this] {
    rebuildLegs();
    rebuildDays();
  });
  connect(&Logbook::instance(), &Logbook::changed, this, &LogView::rebuildDays);
  connect(&Theme::instance(), &Theme::changed, this, [this] {
    rebuildLegs();
    rebuildDays();
    update();
  });
  rebuildLegs();
  rebuildDays();
  showLegs();
  const auto& all = LegStore::instance().legs();
  if (!all.isEmpty()) openLeg(all.first().id);
}

void LogView::setLand(std::shared_ptr<const LandData> land) {
  m_detail->setLand(std::move(land));
}

void LogView::setLeftInset(int px) {
  layout()->setContentsMargins(16 + px, 16, 16, 16);
}

bool LogView::logbookShown() const { return m_stack->currentIndex() == 1; }

void LogView::showLegs() {
  m_editor->flush();
  m_stack->setCurrentIndex(0);
  m_tabs[0]->setChecked(true);
  m_tabs[1]->setChecked(false);
}

void LogView::showLogbook(const QDate& date) {
  m_stack->setCurrentIndex(1);
  m_tabs[0]->setChecked(false);
  m_tabs[1]->setChecked(true);
  QDate d = date;
  if (!d.isValid()) d = m_editor->date();
  if (!d.isValid()) d = QDate::currentDate();
  m_editor->showDay(d);
  rebuildDays();
}

void LogView::openLeg(const QString& id) {
  showLegs();
  m_detail->showLeg(id);
  rebuildLegs();
}

void LogView::rebuildLegs() {
  clearLayout(m_legList);
  const Palette& pal = Theme::p();
  const auto& legs = LegStore::instance().legs();
  double nm = 0, hours = 0, sail = 0, fuel = 0;
  for (const Leg& leg : legs) {
    const LegSummary s = leg.summary();
    nm += s.distanceNm;
    hours += s.hours;
    sail += s.sailingShare * s.hours;
    fuel += s.fuelL;
    auto* row = new ListRow;
    row->setObjectName("leg-" + leg.id);
    row->setMinimumHeight(74);
    if (leg.id == m_detail->legId()) row->setHighlight(pal.accentBright);
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(16, 8, 12, 8);
    auto* col = new QVBoxLayout;
    col->setSpacing(2);
    auto* name = label(leg.name, "value");
    col->addWidget(name);
    const QDateTime a = leg.start().toLocalTime();
    col->addWidget(label(QLocale().toString(a.date(), QLocale::ShortFormat) +
                             "  " + a.toString("HH:mm"),
                         "dim"));
    col->addWidget(label(tr("%1 NM · %2 h · %3 kn · %4 % sail")
                             .arg(s.distanceNm, 0, 'f', 1)
                             .arg(hoursText(s.hours))
                             .arg(s.avgSog, 0, 'f', 1)
                             .arg(int(std::round(s.sailingShare * 100))),
                         "dim"));
    rl->addLayout(col, 1);
    connect(row, &ListRow::clicked, this, [this, id = leg.id] { openLeg(id); });
    m_legList->addWidget(row);
  }
  if (legs.isEmpty()) {
    auto* empty = label(tr("No legs yet. A leg is recorded automatically "
                           "while the boat moves."),
                        "dim");
    empty->setWordWrap(true);
    m_legList->addWidget(empty);
  }
  m_legList->addStretch(1);
  m_totals->setText(
      tr("%1 legs · %2 NM · %3 h underway\n%4 % under sail · %5 l diesel")
          .arg(legs.size())
          .arg(nm, 0, 'f', 1)
          .arg(hoursText(hours))
          .arg(hours > 0 ? int(std::round(sail / hours * 100)) : 0)
          .arg(fuel, 0, 'f', 1));
  if (LegStore::instance().recording())
    m_totals->setText(m_totals->text() + "\n" + tr("● Recording a leg"));
}

void LogView::rebuildDays() {
  clearLayout(m_dayList);
  const Palette& pal = Theme::p();
  auto& book = Logbook::instance();
  QVector<QDate> days = book.days();
  const QDate today = QDate::currentDate();
  if (!days.contains(today)) days.prepend(today);
  for (const QDate& d : days) {
    auto* row = new ListRow;
    row->setObjectName("day-" + d.toString(Qt::ISODate));
    row->setMinimumHeight(64);
    if (d == m_editor->date() && logbookShown())
      row->setHighlight(pal.accentBright);
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(16, 8, 12, 8);
    auto* col = new QVBoxLayout;
    col->setSpacing(2);
    col->addWidget(label(QLocale().toString(d, "ddd, d. MMM yyyy"), "value"));
    const DayFacts f = DayFacts::of(d);
    auto* facts = label(f.legs > 0 ? QStringLiteral("%1 · %2 NM")
                                         .arg(f.places.join(" – "))
                                         .arg(f.distanceNm, 0, 'f', 1)
                                   : tr("No leg"),
                        "dim");
    facts->setWordWrap(true);
    col->addWidget(facts);
    rl->addLayout(col, 1);
    const bool written = !book.entry(d).isEmpty();
    auto* mark = new QLabel;
    mark->setPixmap(Icons::pixmap(written ? "edit" : "plus",
                                  written ? pal.accentBright : pal.textFaint,
                                  18));
    mark->setToolTip(written ? tr("Report written") : tr("Write a report"));
    rl->addWidget(mark);
    connect(row, &ListRow::clicked, this, [this, d] { showLogbook(d); });
    m_dayList->addWidget(row);
  }
  m_dayList->addStretch(1);
}

void LogView::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  QPainter p(this);
  QLinearGradient g(0, 0, 0, height());
  g.setColorAt(0, pal.dark ? QColor(14, 17, 23) : QColor(244, 246, 249));
  g.setColorAt(1, pal.dark ? QColor(8, 10, 14) : QColor(228, 232, 238));
  p.fillRect(rect(), g);
}
