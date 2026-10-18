#include "ui/TidesPage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <cmath>

#include "core/AppSettings.h"
#include "core/GeoMath.h"
#include "core/NavStore.h"
#include "ui/ChartView.h"
#include "ui/GlassPanel.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

namespace {
QString valueText(double v, const QString& unit) {
  if (std::isnan(v)) return QStringLiteral("–");
  return QStringLiteral("%1 %2").arg(v, 0, 'f', unit == "kn" ? 1 : 2).arg(unit);
}

/** Reference position: own ship, else the chart center. */
void referencePos(const ChartView* chart, double& lat, double& lon) {
  const auto& own = NavStore::instance().ownShip();
  if (own.valid) {
    lat = own.lat;
    lon = own.lon;
  } else {
    chart->center(lat, lon);
  }
}
}  // namespace

// --------------------------------------------------------------------------

TideLayer::TideLayer(QObject* parent) : MapLayer(parent) {
  connect(&TideDatabase::instance(), &TideDatabase::loaded, this,
          &MapLayer::changed);
  m_refresh.setInterval(60 * 1000);
  connect(&m_refresh, &QTimer::timeout, this, &MapLayer::changed);
  m_refresh.start();
}

void TideLayer::paint(QPainter& p, const ChartView& view) {
  auto& db = TideDatabase::instance();
  if (!db.isLoaded() || view.displayScale() > 1.5e6) return;
  const Palette& pal = Theme::p();
  const QDateTime now = QDateTime::currentDateTimeUtc();
  const QRectF area = QRectF(view.rect()).adjusted(-40, -20, 40, 20);
  p.setRenderHint(QPainter::Antialiasing);
  const QFont font = Theme::font(11, QFont::DemiBold);
  p.setFont(font);
  const QFontMetricsF fm(font);
  int drawn = 0;
  QVector<QRectF> used;
  for (const auto& s : db.stations()) {
    const QPointF sp = view.screenPos(s.lat, s.lon);
    if (!area.contains(sp)) continue;
    const double v = db.value(s, now);
    if (std::isnan(v)) continue;
    const bool rising = db.value(s, now.addSecs(600)) > v;
    const bool current = s.type == TideStation::Type::Current;
    const QString text =
        current ? QStringLiteral("%1 kn").arg(std::fabs(v), 0, 'f', 1)
                : QStringLiteral("%1 m").arg(v, 0, 'f', 1);
    const QRectF r(sp.x() - 8, sp.y() - 12, fm.horizontalAdvance(text) + 32,
                   24);
    bool overlaps = false;
    for (const QRectF& u : used) overlaps |= u.intersects(r);
    if (overlaps) continue;
    used << r;
    paintGlass(p, r, 12, false);
    p.setPen(Qt::NoPen);
    p.setBrush(current ? pal.warning : pal.accent);
    p.drawEllipse(QPointF(r.left() + 12, r.center().y()), 7, 7);
    p.save();
    p.translate(r.left() + 12, r.center().y());
    if (current)
      p.rotate(v >= 0 ? 0 : 180);  // flood / ebb
    else
      p.rotate(rising ? 0 : 180);
    Icons::paint(&p, "arrow-up", QRectF(-5, -5, 10, 10), Qt::white);
    p.restore();
    p.setPen(pal.text);
    p.drawText(r.adjusted(24, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
               text);
    if (++drawn > 60) break;
  }
}

// --------------------------------------------------------------------------

TideCurve::TideCurve(QWidget* parent) : QWidget(parent) {
  setMinimumHeight(230);
}

void TideCurve::setStation(const TideStation* s) {
  m_station = s;
  update();
}

void TideCurve::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF card = QRectF(rect()).adjusted(0, 0, -8, 0);
  p.setPen(Qt::NoPen);
  p.setBrush(pal.panelHover);
  p.drawRoundedRect(card, 18, 18);
  auto& db = TideDatabase::instance();
  if (!m_station || !db.isLoaded()) {
    p.setPen(pal.textDim);
    p.setFont(Theme::font(13));
    p.drawText(card, Qt::AlignCenter,
               db.isLoaded() ? tr("Select a station") : tr("Loading tides…"));
    return;
  }
  const TideStation& s = *m_station;
  const QString unit = db.unit(s);
  const QDateTime now = QDateTime::currentDateTimeUtc();
  const QDateTime from = now.addSecs(-6 * 3600), to = now.addSecs(18 * 3600);

  // Header
  const QRectF in = card.adjusted(18, 14, -18, -14);
  p.setFont(Theme::font(15, QFont::DemiBold));
  p.setPen(pal.text);
  p.drawText(QRectF(in.left(), in.top(), in.width(), 20),
             Qt::AlignLeft | Qt::AlignVCenter,
             QFontMetrics(p.font()).elidedText(s.name, Qt::ElideRight,
                                               int(in.width())));
  const double v = db.value(s, now);
  p.setFont(Theme::numberFont(30, QFont::Light));
  p.drawText(QRectF(in.left(), in.top() + 22, in.width() / 2, 38),
             Qt::AlignLeft | Qt::AlignVCenter, valueText(v, unit));
  const auto ext = db.extremes(s, now, now.addSecs(26 * 3600));
  if (!ext.isEmpty()) {
    const TideExtreme& e = ext.first();
    const QString what = s.type == TideStation::Type::Current
                             ? (e.high ? tr("Max flood") : tr("Max ebb"))
                             : (e.high ? tr("High water") : tr("Low water"));
    p.setFont(Theme::font(12, QFont::DemiBold));
    p.setPen(pal.textDim);
    p.drawText(
        QRectF(in.left() + in.width() / 2, in.top() + 24, in.width() / 2, 16),
        Qt::AlignRight | Qt::AlignVCenter, what.toUpper());
    p.setPen(pal.text);
    p.setFont(Theme::numberFont(15, QFont::DemiBold));
    p.drawText(
        QRectF(in.left() + in.width() / 2, in.top() + 40, in.width() / 2, 20),
        Qt::AlignRight | Qt::AlignVCenter,
        e.time.toLocalTime().toString("HH:mm") + "  " +
            valueText(e.value, unit));
  }

  // Curve
  const QRectF plot(in.left(), in.top() + 70, in.width(), in.height() - 90);
  QVector<QPointF> samples;
  double lo = 1e9, hi = -1e9;
  for (qint64 t = 0; t <= 24 * 3600; t += 600) {
    const double y = db.value(s, from.addSecs(t));
    samples << QPointF(t, y);
    lo = std::min(lo, y);
    hi = std::max(hi, y);
  }
  if (hi - lo < 0.1) hi = lo + 0.1;
  auto map = [&](const QPointF& sp) {
    return QPointF(plot.left() + sp.x() / (24 * 3600) * plot.width(),
                   plot.bottom() - (sp.y() - lo) / (hi - lo) * plot.height());
  };
  QPainterPath line, fill;
  for (int i = 0; i < samples.size(); ++i) {
    const QPointF m = map(samples[i]);
    if (i == 0) {
      line.moveTo(m);
      fill.moveTo(m.x(), plot.bottom());
    }
    line.lineTo(m);
    fill.lineTo(m);
  }
  fill.lineTo(plot.right(), plot.bottom());
  fill.closeSubpath();
  QLinearGradient g(plot.topLeft(), plot.bottomLeft());
  QColor a = pal.accent;
  a.setAlpha(110);
  g.setColorAt(0, a);
  a.setAlpha(0);
  g.setColorAt(1, a);
  p.setPen(Qt::NoPen);
  p.setBrush(g);
  p.drawPath(fill);
  p.setPen(QPen(pal.accentBright, 2.5));
  p.setBrush(Qt::NoBrush);
  p.drawPath(line);

  // Time axis labels every 6 hours.
  p.setFont(Theme::font(10, QFont::DemiBold));
  p.setPen(pal.textFaint);
  for (int h = 0; h <= 24; h += 6) {
    const double x = plot.left() + h / 24.0 * plot.width();
    p.drawText(QRectF(x - 30, plot.bottom() + 4, 60, 14), Qt::AlignCenter,
               from.addSecs(h * 3600).toLocalTime().toString("HH:mm"));
  }
  // Now marker
  const QPointF nowPt = map(QPointF(6 * 3600, v));
  p.setPen(QPen(pal.text, 1, Qt::DashLine));
  p.drawLine(QPointF(nowPt.x(), plot.top()), QPointF(nowPt.x(), plot.bottom()));
  p.setPen(QPen(Qt::white, 2));
  p.setBrush(pal.accent);
  p.drawEllipse(nowPt, 5, 5);
}

// --------------------------------------------------------------------------

TidesPage::TidesPage(ChartView* chart, QWidget* parent)
    : ListPage(parent), m_chart(chart) {
  m_curve = new TideCurve;
  actions()->addWidget(m_curve);
  auto* hint = new QLabel(tr("NEAREST STATIONS"));
  hint->setProperty("role", "section");
  actions()->addWidget(hint);
  setEmptyText(tr("Loading tide stations…"));
  connect(&TideDatabase::instance(), &TideDatabase::loaded, this,
          &TidesPage::rebuild);
  connect(&Theme::instance(), &Theme::changed, this, &TidesPage::rebuild);
  m_refresh.setInterval(60 * 1000);
  connect(&m_refresh, &QTimer::timeout, this, [this] {
    if (isVisible()) rebuild();
  });
  m_refresh.start();
  rebuild();
}

void TidesPage::rebuild() {
  clearRows();
  auto& db = TideDatabase::instance();
  if (!db.isLoaded()) return;
  const Palette& pal = Theme::p();
  double lat, lon;
  referencePos(m_chart, lat, lon);
  auto stations = db.nearest(lat, lon, 12, TideStation::Type::Tide);
  stations += db.nearest(lat, lon, 4, TideStation::Type::Current);
  const QDateTime now = QDateTime::currentDateTimeUtc();
  const TideStation* selected = nullptr;
  for (const TideStation* s : stations)
    if (s->name == m_selected) selected = s;
  if (!selected && !stations.isEmpty()) {
    selected = stations.first();
    m_selected = selected->name;
  }
  m_curve->setStation(selected);
  for (const TideStation* s : stations) {
    auto* row = new ListRow;
    if (s == selected) row->setHighlight(pal.accentBright);
    auto* l = new QHBoxLayout(row);
    l->setContentsMargins(14, 8, 14, 8);
    const double v = db.value(*s, now);
    const bool rising = db.value(*s, now.addSecs(600)) > v;
    const bool current = s->type == TideStation::Type::Current;
    auto* icon = new QLabel;
    icon->setPixmap(Icons::pixmap(current ? "navigate" : "arrow-up",
                                  current ? pal.warning : pal.accentBright,
                                  20));
    if (!current && !rising)
      icon->setPixmap(icon->pixmap().transformed(QTransform().rotate(180)));
    l->addWidget(icon);
    auto* col = new QVBoxLayout;
    col->setSpacing(2);
    auto* name = new QLabel(s->name);
    name->setProperty("role", "value");
    name->setWordWrap(true);
    auto* sub = new QLabel(AppSettings::instance().distanceText(
                               geo::distanceNm(lat, lon, s->lat, s->lon)) +
                           (s->reference ? QString() : tr(" · secondary")));
    sub->setProperty("role", "dim");
    col->addWidget(name);
    col->addWidget(sub);
    l->addLayout(col, 1);
    auto* val = new QLabel(valueText(v, db.unit(*s)));
    val->setProperty("role", "value");
    l->addWidget(val);
    const QString key = s->name;
    const double slat = s->lat, slon = s->lon;
    connect(row, &ListRow::clicked, this, [this, key, slat, slon] {
      m_selected = key;
      m_chart->centerOn(slat, slon);
      QTimer::singleShot(0, this, &TidesPage::rebuild);
    });
    addRow(row);
  }
}
