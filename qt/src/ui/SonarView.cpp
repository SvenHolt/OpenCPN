#include "ui/SonarView.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <cmath>

#include "core/AppSettings.h"
#include "core/NavStore.h"
#include "ui/Theme.h"

namespace {
QLabel* label(const QString& text, const char* role = nullptr) {
  auto* l = new QLabel(text);
  if (role) l->setProperty("role", role);
  return l;
}

QWidget* card(QWidget* content, const QString& title) {
  auto* w = new QWidget;
  w->setObjectName("sonarCard");
  w->setAttribute(Qt::WA_StyledBackground);
  const Palette& pal = Theme::p();
  w->setStyleSheet(QStringLiteral("#sonarCard { background: %1; border: 1px "
                                  "solid %2; border-radius: 18px; }")
                       .arg(pal.panel.name(QColor::HexArgb),
                            pal.panelBorder.name(QColor::HexArgb)));
  auto* l = new QVBoxLayout(w);
  l->setContentsMargins(16, 12, 16, 14);
  l->setSpacing(8);
  l->addWidget(label(title, "section"));
  l->addWidget(content);
  return w;
}

/** A round depth range above `m`: 5, 10, 15, 20, 30, 50, 75, 100 … */
double niceRange(double m) {
  for (double r : {5.0, 10.0, 15.0, 20.0, 30.0, 50.0, 75.0, 100.0, 150.0, 200.0,
                   300.0, 500.0, 1000.0})
    if (m <= r) return r;
  return std::ceil(m / 500) * 500;
}
}  // namespace

// --------------------------------------------------------------------------

Echogram::Echogram(QWidget* parent) : QWidget(parent) {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  connect(&SonarService::instance(), &SonarService::pinged, this,
          &Echogram::rebuild);
}

void Echogram::setSpanSecs(int secs) {
  m_span = std::max(30, secs);
  rebuild();
}

void Echogram::setRangeM(double m) {
  m_fixedRange = m;
  rebuild();
}

void Echogram::setColors(Colors c) {
  m_colors = c;
  rebuild();
}

void Echogram::resizeEvent(QResizeEvent*) { rebuild(); }

QRectF Echogram::plotRect() const {
  return QRectF(54, 10, std::max(10, width() - 54 - 70),
                std::max(10, height() - 10 - 30));
}

int Echogram::echoAt(const SonarPing& p, double d) {
  if (p.hasEcho()) {
    if (d < 0 || d >= p.rangeM) return 0;
    const int i = int(d / p.rangeM * p.samples.size());
    return uchar(p.samples[std::min<int>(i, int(p.samples.size()) - 1)]);
  }
  if (std::isnan(p.depthM) || p.depthM <= 0) return 0;
  // From the depth: a faint surface band, the bottom echo (stronger and
  // thicker in deeper water) and the fading return of the ground below.
  const double bottom = p.depthM;
  const double band = 0.25 + 0.04 * bottom;
  if (d < 0.6) return int(70 * (1 - d / 0.6));
  if (d < bottom) return 0;
  if (d < bottom + band) return int(255 - 40 * (d - bottom) / band);
  const double below = (d - bottom - band) / (0.35 * bottom + 1);
  return below < 1 ? int(170 * (1 - below) * (1 - below)) + 20 : 0;
}

QRgb Echogram::color(int v, Colors c) {
  if (c == Colors::Gray) {
    const int g = 12 + v * 243 / 255;
    return qRgb(g, g, g);
  }
  // Echo sounder colors: deep blue water, blue - green - yellow - red.
  struct Stop {
    int v, r, g, b;
  };
  static const Stop stops[] = {{0, 6, 18, 52},      {40, 20, 60, 160},
                               {90, 30, 170, 220},  {140, 60, 200, 80},
                               {190, 240, 220, 50}, {230, 240, 90, 30},
                               {255, 150, 20, 20}};
  int i = 0;
  while (i < 5 && v > stops[i + 1].v) ++i;
  const Stop& a = stops[i];
  const Stop& b = stops[i + 1];
  const double t = double(v - a.v) / std::max(1, b.v - a.v);
  return qRgb(int(a.r + (b.r - a.r) * t), int(a.g + (b.g - a.g) * t),
              int(a.b + (b.b - a.b) * t));
}

void Echogram::rebuild() {
  const QRectF r = plotRect();
  const auto& pings = SonarService::instance().pings();
  // Depth range: fixed, or from the deepest reading shown.
  double maxD = 0;
  const QDateTime now =
      pings.isEmpty() ? QDateTime::currentDateTimeUtc() : pings.last().time;
  const QDateTime from = now.addSecs(-m_span);
  for (int i = int(pings.size()) - 1; i >= 0 && pings[i].time >= from; --i) {
    if (!std::isnan(pings[i].depthM)) maxD = std::max(maxD, pings[i].depthM);
    if (pings[i].hasEcho())
      maxD = std::max(maxD, double(pings[i].rangeM) / 1.3);
  }
  m_usedRange =
      m_fixedRange > 0 ? m_fixedRange : niceRange(std::max(3.0, maxD * 1.3));
  const int w = int(r.width()), h = int(r.height());
  m_image = QImage(w, h, QImage::Format_RGB32);
  m_image.fill(color(0, m_colors));
  if (pings.isEmpty() || w <= 0 || h <= 0) {
    update();
    return;
  }
  // One column per pixel: the ping at that time (gaps stay empty).
  int pi = int(pings.size()) - 1;
  for (int x = w - 1; x >= 0; --x) {
    const QDateTime t =
        now.addMSecs(-qint64(double(w - 1 - x) / w * m_span * 1000));
    while (pi > 0 && pings[pi].time > t) --pi;
    const SonarPing& p = pings[pi];
    if (std::abs(p.time.msecsTo(t)) > 3000) continue;
    for (int y = 0; y < h; ++y) {
      const double d = (y + 0.5) / h * m_usedRange;
      const int v = echoAt(p, d);
      if (v > 0)
        reinterpret_cast<QRgb*>(m_image.scanLine(y))[x] = color(v, m_colors);
    }
  }
  update();
}

void Echogram::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF r = plotRect();
  p.drawImage(r.topLeft(), m_image);
  p.setPen(QPen(pal.panelBorder, 1));
  p.setBrush(Qt::NoBrush);
  p.drawRect(r);

  // Depth scale.
  p.setFont(Theme::font(11));
  const double step = m_usedRange <= 10    ? 1
                      : m_usedRange <= 30  ? 5
                      : m_usedRange <= 100 ? 10
                      : m_usedRange <= 300 ? 50
                                           : 100;
  for (double d = 0; d <= m_usedRange + 1e-6; d += step) {
    const double y = r.top() + d / m_usedRange * r.height();
    p.setPen(QPen(QColor(255, 255, 255, 30), 1));
    p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    p.setPen(pal.textDim);
    p.drawText(QRectF(0, y - 8, r.left() - 6, 16),
               Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("%1 m").arg(d, 0, 'f', 0));
  }
  // Time axis.
  const auto& pings = SonarService::instance().pings();
  const QDateTime now =
      pings.isEmpty() ? QDateTime::currentDateTimeUtc() : pings.last().time;
  const int tickSecs = m_span <= 120    ? 30
                       : m_span <= 600  ? 60
                       : m_span <= 1800 ? 300
                                        : 600;
  const qint64 nowS = now.toSecsSinceEpoch();
  for (qint64 t = nowS - nowS % tickSecs; t > nowS - m_span; t -= tickSecs) {
    const double x = r.right() - double(nowS - t) / m_span * r.width();
    p.setPen(QPen(QColor(255, 255, 255, 25), 1));
    p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    p.setPen(pal.textDim);
    p.drawText(
        QRectF(x - 30, r.bottom() + 4, 60, 18), Qt::AlignCenter,
        QDateTime::fromSecsSinceEpoch(t).toLocalTime().toString("HH:mm"));
  }

  // Safety depth.
  const double safety = AppSettings::instance().safetyDepth;
  if (safety > 0 && safety < m_usedRange) {
    const double y = r.top() + safety / m_usedRange * r.height();
    p.setPen(QPen(pal.warning, 1.2, Qt::DashLine));
    p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    p.setPen(pal.warning);
    p.drawText(QRectF(r.right() + 4, y - 8, 66, 16),
               Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("%1 m").arg(safety, 0, 'f', 1));
  }

  // Bottom line from the depth readings.
  QPainterPath line;
  bool open = false;
  for (int i = int(pings.size()) - 1; i >= 0; --i) {
    const qint64 age = pings[i].time.msecsTo(now);
    if (age > qint64(m_span) * 1000) break;
    const double d = pings[i].depthM;
    if (std::isnan(d)) {
      open = false;
      continue;
    }
    const QPointF pt(
        r.right() - double(age) / (m_span * 1000.0) * r.width(),
        r.top() + std::min(d, m_usedRange) / m_usedRange * r.height());
    if (open)
      line.lineTo(pt);
    else
      line.moveTo(pt);
    open = true;
  }
  p.setPen(QPen(QColor(255, 255, 255, 200), 1.4));
  p.drawPath(line);
  if (!pings.isEmpty() && !std::isnan(pings.last().depthM)) {
    const double d = pings.last().depthM;
    const double y =
        r.top() + std::min(d, m_usedRange) / m_usedRange * r.height();
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawEllipse(QPointF(r.right(), y), 3.5, 3.5);
    p.setPen(pal.text);
    p.setFont(Theme::font(13, QFont::DemiBold));
    p.drawText(QRectF(r.right() + 6, y - 10, 64, 20),
               Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("%1 m").arg(d, 0, 'f', 1));
  }
  if (pings.isEmpty()) {
    p.setPen(pal.textDim);
    p.setFont(Theme::font(15));
    p.drawText(r, Qt::AlignCenter,
               tr("No depth data – connect an echo sounder via Signal K or "
                  "NMEA 0183"));
  }
}

// --------------------------------------------------------------------------

SonarView::SonarView(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent);
  QSettings set;
  set.beginGroup("Sonar");
  auto* l = new QVBoxLayout(this);
  l->setContentsMargins(16, 16, 16, 16);
  l->setSpacing(12);
  auto* head = new QHBoxLayout;
  auto* title = label(tr("Sonar"));
  title->setStyleSheet("font-size: 26px; font-weight: 600;");
  head->addWidget(title);
  head->addSpacing(20);
  m_status = label(QString(), "dim");
  m_status->setObjectName("sonarStatus");
  head->addWidget(m_status, 1);
  l->addLayout(head);

  auto* body = new QHBoxLayout;
  body->setSpacing(16);
  m_echo = new Echogram;
  body->addWidget(m_echo, 1);
  auto* side = new QWidget;
  side->setFixedWidth(330);
  auto* sl = new QVBoxLayout(side);
  sl->setContentsMargins(0, 0, 0, 0);
  sl->setSpacing(12);

  auto* readout = new QWidget;
  auto* rl = new QVBoxLayout(readout);
  rl->setContentsMargins(0, 0, 0, 0);
  m_depth = new QLabel;
  m_depth->setObjectName("sonarDepth");
  m_depth->setFont(Theme::numberFont(56));
  rl->addWidget(m_depth);
  m_details = label(QString(), "dim");
  m_details->setObjectName("sonarDetails");
  m_details->setWordWrap(true);
  rl->addWidget(m_details);
  sl->addWidget(card(readout, tr("DEPTH")));

  auto* disp = new QWidget;
  auto* dl = new QVBoxLayout(disp);
  dl->setContentsMargins(0, 0, 0, 0);
  dl->setSpacing(8);
  auto* spans = new QHBoxLayout;
  const int secs[4] = {120, 300, 900, 3600};
  const QString names[4] = {tr("2 min"), tr("5 min"), tr("15 min"), tr("1 h")};
  const int span = set.value("span", 300).toInt();
  for (int i = 0; i < 4; ++i) {
    m_span[i] = new QPushButton(names[i]);
    m_span[i]->setProperty("role", "segment");
    m_span[i]->setCheckable(true);
    m_span[i]->setChecked(secs[i] == span);
    m_span[i]->setCursor(Qt::PointingHandCursor);
    spans->addWidget(m_span[i]);
    connect(m_span[i], &QPushButton::clicked, this, [this, i, secs] {
      for (int k = 0; k < 4; ++k) m_span[k]->setChecked(k == i);
      m_echo->setSpanSecs(secs[i]);
      QSettings().setValue("Sonar/span", secs[i]);
      updateReadout();
    });
  }
  dl->addLayout(spans);
  auto* rangeRow = new QHBoxLayout;
  rangeRow->addWidget(label(tr("Range"), "dim"));
  m_range = new QComboBox;
  m_range->setObjectName("sonarRange");
  m_range->addItem(tr("Automatic"), 0.0);
  for (double m : {5.0, 10.0, 20.0, 30.0, 50.0, 100.0, 200.0})
    m_range->addItem(QStringLiteral("%1 m").arg(m), m);
  m_range->setCurrentIndex(
      std::max(0, m_range->findData(set.value("range", 0.0).toDouble())));
  rangeRow->addWidget(m_range, 1);
  dl->addLayout(rangeRow);
  auto* colorRow = new QHBoxLayout;
  colorRow->addWidget(label(tr("Colors"), "dim"));
  m_colors = new QComboBox;
  m_colors->addItem(tr("Color"));
  m_colors->addItem(tr("Grayscale"));
  m_colors->setCurrentIndex(set.value("colors", 0).toInt());
  colorRow->addWidget(m_colors, 1);
  dl->addLayout(colorRow);
  sl->addWidget(card(disp, tr("DISPLAY")));

  auto* sk = new QWidget;
  auto* kl = new QVBoxLayout(sk);
  kl->setContentsMargins(0, 0, 0, 0);
  kl->addWidget(label(tr("Echo samples (Signal K path, optional):"), "dim"));
  m_path = new QLineEdit(SonarService::instance().echoPath());
  m_path->setObjectName("sonarPath");
  kl->addWidget(m_path);
  auto* note = label(
      tr("Signal K has no standard path for sonar returns. A plugin can send "
         "{\"range\": m, \"samples\": [...]} here; otherwise the echogram is "
         "drawn from the depth (environment.depth.*, NMEA DPT/DBT) and the "
         "water temperature."),
      "dim");
  note->setWordWrap(true);
  kl->addWidget(note);
  sl->addWidget(card(sk, tr("DATA")));
  sl->addStretch(1);
  body->addWidget(side);
  l->addLayout(body, 1);

  m_echo->setSpanSecs(span);
  m_echo->setRangeM(m_range->currentData().toDouble());
  m_echo->setColors(Echogram::Colors(m_colors->currentIndex()));
  connect(m_range, &QComboBox::currentIndexChanged, this, [this] {
    m_echo->setRangeM(m_range->currentData().toDouble());
    QSettings().setValue("Sonar/range", m_range->currentData());
  });
  connect(m_colors, &QComboBox::currentIndexChanged, this, [this](int i) {
    m_echo->setColors(Echogram::Colors(i));
    QSettings().setValue("Sonar/colors", i);
  });
  connect(m_path, &QLineEdit::editingFinished, this,
          [this] { SonarService::instance().setEchoPath(m_path->text()); });
  connect(&SonarService::instance(), &SonarService::pinged, this,
          &SonarView::updateReadout);
  updateReadout();
}

void SonarView::setLeftInset(int px) {
  layout()->setContentsMargins(16 + px, 16, 16, 16);
}

void SonarView::showEvent(QShowEvent*) { updateReadout(); }

void SonarView::updateReadout() {
  if (!isVisible()) return;
  auto& svc = SonarService::instance();
  const auto& pings = svc.pings();
  const SonarPing* last = pings.isEmpty() ? nullptr : &pings.last();
  const bool fresh =
      last && last->time.secsTo(QDateTime::currentDateTimeUtc()) < 10;
  const double d = fresh ? last->depthM : std::nan("");
  m_depth->setText(std::isnan(d) ? QStringLiteral("– m")
                                 : QStringLiteral("%1 m").arg(d, 0, 'f', 1));
  const double safety = AppSettings::instance().safetyDepth;
  m_depth->setStyleSheet(
      QStringLiteral("color: %1;")
          .arg((!std::isnan(d) && d < safety ? Theme::p().warning
                                             : Theme::p().text)
                   .name()));
  QStringList lines;
  const double keel = svc.belowKeelM();
  if (!std::isnan(keel)) lines << tr("Below keel %1 m").arg(keel, 0, 'f', 1);
  if (fresh && !std::isnan(last->waterTempC))
    lines << tr("Water %1 °C").arg(last->waterTempC, 0, 'f', 1);
  double lo, hi;
  svc.depthRange(m_echo->spanSecs(), lo, hi);
  if (!std::isnan(lo))
    lines << tr("Shown: %1 – %2 m").arg(lo, 0, 'f', 1).arg(hi, 0, 'f', 1);
  m_details->setText(lines.join("\n"));
  const QString src = svc.source();
  m_status->setText(
      src.isEmpty() ? tr("No echo sounder data")
      : svc.receivingEcho()
          ? tr("Echo samples from %1").arg(src)
          : tr("Depth from %1 · echogram drawn from the depth").arg(src));
}

void SonarView::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  QPainter p(this);
  QLinearGradient g(0, 0, 0, height());
  g.setColorAt(0, pal.dark ? QColor(14, 17, 23) : QColor(244, 246, 249));
  g.setColorAt(1, pal.dark ? QColor(8, 10, 14) : QColor(228, 232, 238));
  p.fillRect(rect(), g);
}
