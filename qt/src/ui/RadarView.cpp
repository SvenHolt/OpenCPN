#include "ui/RadarView.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <cmath>

#include "core/GeoMath.h"
#include "core/NavStore.h"
#include "ui/ChartView.h"
#include "ui/GlassPanel.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

namespace {
constexpr double kM = 1852.0;

QLabel* label(const QString& text, const char* role = nullptr) {
  auto* l = new QLabel(text);
  if (role) l->setProperty("role", role);
  return l;
}

QWidget* card(QWidget* content, const QString& title) {
  auto* w = new QWidget;
  w->setObjectName("radarCard");
  w->setAttribute(Qt::WA_StyledBackground);
  const Palette& pal = Theme::p();
  w->setStyleSheet(QStringLiteral("#radarCard { background: %1; border: 1px "
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

QWidget* toggleRow(const QString& text, ToggleSwitch* sw) {
  auto* w = new QWidget;
  auto* l = new QHBoxLayout(w);
  l->setContentsMargins(0, 0, 0, 0);
  l->addWidget(label(text), 1);
  l->addWidget(sw);
  return w;
}

/** Heading for north up: from the radar, the compass or the course. */
double shipHeading() {
  const double radar = RadarService::instance().image().headingDeg();
  if (!std::isnan(radar)) return radar;
  const OwnShip& own = NavStore::instance().ownShip();
  if (!std::isnan(own.hdg)) return own.hdg;
  return own.cog;
}
}  // namespace

// --------------------------------------------------------------------------

RadarDisplay RadarDisplay::load() {
  QSettings s;
  s.beginGroup("Radar");
  RadarDisplay d;
  d.rangeNm = s.value("rangeNm", d.rangeNm).toDouble();
  d.northUp = s.value("northUp", d.northUp).toBool();
  d.gain = s.value("gain", d.gain).toDouble();
  d.threshold = s.value("threshold", d.threshold).toInt();
  d.rings = s.value("rings", d.rings).toBool();
  d.ais = s.value("ais", d.ais).toBool();
  d.overlay = s.value("overlay", d.overlay).toBool();
  return d;
}

void RadarDisplay::save() const {
  QSettings s;
  s.beginGroup("Radar");
  s.setValue("rangeNm", rangeNm);
  s.setValue("northUp", northUp);
  s.setValue("gain", gain);
  s.setValue("threshold", threshold);
  s.setValue("rings", rings);
  s.setValue("ais", ais);
  s.setValue("overlay", overlay);
}

RadarImage::Style RadarDisplay::style() const {
  RadarImage::Style st;
  st.legend = RadarService::instance().legend();
  st.gain = gain;
  st.threshold = threshold;
  return st;
}

// --------------------------------------------------------------------------

RadarScope::RadarScope(QWidget* parent) : QWidget(parent) {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setCursor(Qt::CrossCursor);
  connect(&RadarService::instance(), &RadarService::updated, this,
          &RadarScope::refresh);
  connect(&NavStore::instance(), &NavStore::aisChanged, this,
          qOverload<>(&QWidget::update));
}

void RadarScope::setDisplay(const RadarDisplay& d) {
  m_display = d;
  refresh();
}

double RadarScope::heading() const { return shipHeading(); }

void RadarScope::setMarker(double brgRel, double nm) {
  m_markBrg = brgRel;
  m_markNm = nm;
  update();
  emit markerChanged();
}

QRectF RadarScope::scopeRect() const {
  const double side = std::min(width(), height()) - 56;
  return QRectF((width() - side) / 2, (height() - side) / 2, side, side);
}

void RadarScope::resizeEvent(QResizeEvent*) { refresh(); }

void RadarScope::refresh() {
  if (!isVisible()) return;
  const QRectF r = scopeRect();
  const double hdg = heading();
  const bool north = m_display.northUp && !std::isnan(hdg);
  const qreal dpr = devicePixelRatioF();
  m_image = RadarService::instance().image().render(
      int(r.width() * dpr), north ? hdg : 0, m_display.rangeNm * kM,
      m_display.style());
  m_image.setDevicePixelRatio(dpr);
  update();
}

void RadarScope::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF r = scopeRect();
  const QPointF c = r.center();
  const double rad = r.width() / 2;
  const double hdg = heading();
  const bool north = m_display.northUp && !std::isnan(hdg);
  // Screen angle (deg clockwise from up) of the bow.
  const double bow = north ? hdg : 0;
  const auto at = [&](double screenDeg, double frac) {
    const double a = screenDeg * M_PI / 180;
    return c + QPointF(std::sin(a), -std::cos(a)) * rad * frac;
  };

  // Scope background: dark in day and night.
  QRadialGradient bg(c, rad);
  bg.setColorAt(0, QColor(8, 26, 30));
  bg.setColorAt(1, QColor(4, 14, 18));
  p.setPen(QPen(pal.panelBorder, 2));
  p.setBrush(bg);
  p.drawEllipse(c, rad, rad);
  p.drawImage(r.topLeft(), m_image);

  const QColor ring(110, 200, 190, 90);
  // Bearing scale: true bearings north up, relative to the bow head up.
  p.setFont(Theme::font(11));
  for (int deg = 0; deg < 360; deg += 5) {
    const double screen = deg;
    const bool major = deg % 30 == 0;
    p.setPen(QPen(major ? QColor(170, 230, 220) : ring, major ? 1.6 : 1));
    p.drawLine(at(screen, 1.0), at(screen, major ? 0.955 : 0.975));
    if (major) {
      p.setPen(pal.textDim);
      const QPointF t = at(screen, 1.0) + (at(screen, 1.0) - c) / rad * 14;
      p.drawText(QRectF(t.x() - 20, t.y() - 9, 40, 18), Qt::AlignCenter,
                 QStringLiteral("%1°").arg(deg, 3, 10, QChar('0')));
    }
  }
  // Range rings with their distance.
  if (m_display.rings) {
    p.setBrush(Qt::NoBrush);
    for (int i = 1; i <= 3; ++i) {
      p.setPen(QPen(ring, 1, Qt::DashLine));
      p.drawEllipse(c, rad * i / 4, rad * i / 4);
      p.setPen(QColor(150, 210, 200));
      const QPointF t = at(135, i / 4.0);
      p.drawText(QRectF(t.x() + 3, t.y() - 2, 70, 16), Qt::AlignLeft,
                 RadarView::rangeText(m_display.rangeNm * i / 4));
    }
  }
  // Heading line.
  p.setPen(QPen(QColor(240, 240, 240, 200), 1.4));
  p.drawLine(c, at(bow, 1.0));
  if (north) {  // north marker on the ring
    p.setPen(Qt::NoPen);
    p.setBrush(pal.accentBright);
    const QPointF n = at(0, 1.0);
    p.drawPolygon(QPolygonF(
        {n + QPointF(0, -2), n + QPointF(-6, -12), n + QPointF(6, -12)}));
  }

  // AIS targets relative to the own ship.
  const OwnShip& own = NavStore::instance().ownShip();
  if (m_display.ais && own.valid) {
    const double refHdg = north ? 0 : hdg;
    if (!std::isnan(refHdg)) {
      p.setFont(Theme::font(10));
      for (const AisTarget& t : NavStore::instance().aisTargets()) {
        if (!t.hasPosition()) continue;
        const double nm = geo::distanceNm(own.lat, own.lon, t.lat, t.lon);
        if (nm > m_display.rangeNm) continue;
        const double brg = geo::bearingDeg(own.lat, own.lon, t.lat, t.lon);
        const QPointF pos = at(brg - refHdg, nm / m_display.rangeNm);
        const double cog = std::isnan(t.cog) ? brg : t.cog;
        p.save();
        p.translate(pos);
        p.rotate(cog - refHdg);
        p.setPen(QPen(t.dangerous ? pal.danger : pal.ais, 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(
            QPolygonF({QPointF(0, -9), QPointF(5.5, 6), QPointF(-5.5, 6)}));
        p.restore();
        p.setPen(QColor(220, 230, 235));
        p.drawText(pos + QPointF(9, 4), t.displayName());
      }
    }
  }

  // Own ship.
  p.save();
  p.translate(c);
  p.rotate(bow);
  p.setPen(QPen(Qt::white, 1.4));
  p.setBrush(pal.ownShip);
  QPainterPath boat;
  boat.moveTo(0, -11);
  boat.cubicTo(5, -6, 5, 4, 4, 9);
  boat.lineTo(-4, 9);
  boat.cubicTo(-5, 4, -5, -6, 0, -11);
  p.drawPath(boat);
  p.restore();

  // EBL / VRM.
  if (!std::isnan(m_markBrg) && !std::isnan(m_markNm)) {
    const QColor m(255, 196, 64);
    p.setPen(QPen(m, 1.4, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawLine(c, at(bow + m_markBrg, 1.0));
    const double f = m_markNm / m_display.rangeNm;
    if (f <= 1) p.drawEllipse(c, rad * f, rad * f);
    p.setPen(Qt::NoPen);
    p.setBrush(m);
    if (f <= 1) p.drawEllipse(at(bow + m_markBrg, f), 3.5, 3.5);
  }

  // Corner texts: range and orientation.
  p.setPen(pal.text);
  p.setFont(Theme::font(15, QFont::DemiBold));
  p.drawText(QRectF(8, 4, 200, 24), Qt::AlignLeft | Qt::AlignVCenter,
             RadarView::rangeText(m_display.rangeNm));
  p.setFont(Theme::font(11));
  p.setPen(pal.textDim);
  QString orient = north ? tr("NORTH UP") : tr("HEAD UP");
  if (m_display.northUp && !north) orient += " · " + tr("no heading");
  p.drawText(QRectF(8, 26, 260, 18), Qt::AlignLeft | Qt::AlignVCenter, orient);
  if (!std::isnan(hdg))
    p.drawText(
        QRectF(width() - 208, 4, 200, 24), Qt::AlignRight | Qt::AlignVCenter,
        tr("HDG %1°").arg(int(std::round(hdg)) % 360, 3, 10, QChar('0')));
}

void RadarScope::mouseReleaseEvent(QMouseEvent* e) {
  const QRectF r = scopeRect();
  const QPointF d = e->position() - r.center();
  const double rad = r.width() / 2;
  const double dist = std::hypot(d.x(), d.y());
  if (e->button() == Qt::RightButton || dist > rad) {
    setMarker(std::nan(""), std::nan(""));
    return;
  }
  const double hdg = heading();
  const bool north = m_display.northUp && !std::isnan(hdg);
  const double screen = std::atan2(d.x(), -d.y()) * 180 / M_PI;
  setMarker(std::fmod(screen - (north ? hdg : 0) + 720, 360),
            dist / rad * m_display.rangeNm);
}

void RadarScope::wheelEvent(QWheelEvent* e) {
  emit zoomRequested(e->angleDelta().y() > 0 ? -1 : 1);
}

// --------------------------------------------------------------------------

const QVector<double>& RadarView::ranges() {
  static const QVector<double> r = {0.125, 0.25, 0.5, 0.75, 1,  1.5, 2,
                                    3,     4,    6,   8,    12, 16,  24};
  return r;
}

QString RadarView::rangeText(double nm) {
  if (nm < 0.2) return QStringLiteral("%1 m").arg(int(std::round(nm * kM)));
  return QStringLiteral("%1 NM").arg(QLocale().toString(nm, 'g', 3));
}

RadarView::RadarView(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent);
  m_display = RadarDisplay::load();
  auto* l = new QVBoxLayout(this);
  l->setContentsMargins(16, 16, 16, 16);
  l->setSpacing(12);
  auto* head = new QHBoxLayout;
  auto* title = label(tr("Radar"));
  title->setStyleSheet("font-size: 26px; font-weight: 600;");
  head->addWidget(title);
  head->addSpacing(20);
  m_source = new QComboBox;
  m_source->setObjectName("radarSource");
  m_source->setMinimumWidth(220);
  head->addWidget(m_source);
  m_status = label(QString(), "dim");
  m_status->setObjectName("radarStatus");
  head->addWidget(m_status, 1);
  l->addLayout(head);

  auto* body = new QHBoxLayout;
  body->setSpacing(16);
  m_scope = new RadarScope;
  body->addWidget(m_scope, 1);
  auto* side = new QWidget;
  side->setFixedWidth(330);
  auto* sl = new QVBoxLayout(side);
  sl->setContentsMargins(0, 0, 0, 0);
  sl->setSpacing(12);

  // Display: range, orientation, gain, clutter.
  auto* disp = new QWidget;
  auto* dl = new QVBoxLayout(disp);
  dl->setContentsMargins(0, 0, 0, 0);
  dl->setSpacing(8);
  auto* rangeRow = new QHBoxLayout;
  auto* minus = new QPushButton;
  minus->setIcon(Icons::icon("minus", Theme::p().text, 18));
  minus->setToolTip(tr("Shorter range"));
  auto* plus = new QPushButton;
  plus->setIcon(Icons::icon("plus", Theme::p().text, 18));
  plus->setToolTip(tr("Longer range"));
  m_range = new QComboBox;
  m_range->setObjectName("radarRange");
  for (double nm : ranges()) m_range->addItem(rangeText(nm), nm);
  rangeRow->addWidget(minus);
  rangeRow->addWidget(m_range, 1);
  rangeRow->addWidget(plus);
  dl->addLayout(rangeRow);
  auto* orient = new QHBoxLayout;
  const QString names[2] = {tr("Head up"), tr("North up")};
  for (int i = 0; i < 2; ++i) {
    m_orient[i] = new QPushButton(names[i]);
    m_orient[i]->setProperty("role", "segment");
    m_orient[i]->setCheckable(true);
    m_orient[i]->setCursor(Qt::PointingHandCursor);
    orient->addWidget(m_orient[i]);
    connect(m_orient[i], &QPushButton::clicked, this, [this, i] {
      m_display.northUp = i == 1;
      apply();
    });
  }
  dl->addLayout(orient);
  dl->addWidget(label(tr("Gain (display)"), "dim"));
  m_gain = new QSlider(Qt::Horizontal);
  m_gain->setObjectName("radarGain");
  m_gain->setRange(30, 300);
  dl->addWidget(m_gain);
  dl->addWidget(label(tr("Suppress weak echoes (sea clutter)"), "dim"));
  m_threshold = new QSlider(Qt::Horizontal);
  m_threshold->setRange(1, 10);
  dl->addWidget(m_threshold);
  sl->addWidget(card(disp, tr("DISPLAY")));

  auto* show = new QWidget;
  auto* shl = new QVBoxLayout(show);
  shl->setContentsMargins(0, 0, 0, 0);
  m_rings = new ToggleSwitch;
  m_ais = new ToggleSwitch;
  m_overlay = new ToggleSwitch;
  m_overlay->setObjectName("radarOverlay");
  shl->addWidget(toggleRow(tr("Range rings"), m_rings));
  shl->addWidget(toggleRow(tr("AIS targets"), m_ais));
  shl->addWidget(toggleRow(tr("Radar on the chart"), m_overlay));
  sl->addWidget(card(show, tr("SHOW")));

  auto* infoBox = new QWidget;
  auto* il = new QVBoxLayout(infoBox);
  il->setContentsMargins(0, 0, 0, 0);
  m_info = label(QString(), "dim");
  m_info->setObjectName("radarInfo");
  m_info->setWordWrap(true);
  il->addWidget(m_info);
  m_marker = label(tr("Tap the scope for bearing and distance (EBL/VRM)."));
  m_marker->setObjectName("radarMarker");
  m_marker->setWordWrap(true);
  il->addWidget(m_marker);
  sl->addWidget(card(infoBox, tr("RADAR")));
  m_hint = label(
      tr("Radars come from a Signal K server (Settings → Data connections), "
         "e.g. with the Mayara radar server for Navico, Raymarine, Furuno "
         "and Garmin radars. Without one the simulator shows echoes of the "
         "coastline and the AIS targets."),
      "dim");
  m_hint->setWordWrap(true);
  sl->addWidget(m_hint);
  sl->addStretch(1);
  body->addWidget(side);
  l->addLayout(body, 1);

  // State to the widgets.
  m_range->setCurrentIndex(
      std::max<int>(0, int(ranges().indexOf(m_display.rangeNm))));
  if (m_range->currentData().toDouble() != m_display.rangeNm)
    m_display.rangeNm = m_range->currentData().toDouble();
  m_gain->setValue(int(std::round(m_display.gain * 100)));
  m_threshold->setValue(m_display.threshold);
  m_rings->setChecked(m_display.rings);
  m_ais->setChecked(m_display.ais);
  m_overlay->setChecked(m_display.overlay);

  connect(m_range, &QComboBox::currentIndexChanged, this, [this] {
    m_display.rangeNm = m_range->currentData().toDouble();
    apply();
  });
  const auto step = [this](int d) {
    m_range->setCurrentIndex(
        std::clamp(m_range->currentIndex() + d, 0, m_range->count() - 1));
  };
  connect(minus, &QPushButton::clicked, this, [step] { step(-1); });
  connect(plus, &QPushButton::clicked, this, [step] { step(1); });
  connect(m_scope, &RadarScope::zoomRequested, this, step);
  connect(m_gain, &QSlider::valueChanged, this, [this](int v) {
    m_display.gain = v / 100.0;
    apply();
  });
  connect(m_threshold, &QSlider::valueChanged, this, [this](int v) {
    m_display.threshold = v;
    apply();
  });
  connect(m_rings, &QAbstractButton::toggled, this, [this](bool on) {
    m_display.rings = on;
    apply();
  });
  connect(m_ais, &QAbstractButton::toggled, this, [this](bool on) {
    m_display.ais = on;
    apply();
  });
  connect(m_overlay, &QAbstractButton::toggled, this, [this](bool on) {
    m_display.overlay = on;
    apply();
  });
  connect(m_source, &QComboBox::activated, this, [this] {
    RadarService::instance().select(m_source->currentData().toString());
    apply();
  });
  connect(&RadarService::instance(), &RadarService::sourcesChanged, this,
          &RadarView::rebuildSources);
  connect(&RadarService::instance(), &RadarService::statusChanged, this,
          &RadarView::updateInfo);
  connect(&RadarService::instance(), &RadarService::updated, this, [this] {
    static int n = 0;
    if (++n % 10 == 0) updateInfo();
  });
  connect(m_scope, &RadarScope::markerChanged, this, &RadarView::updateInfo);
  rebuildSources();
  apply();
}

void RadarView::setLeftInset(int px) {
  layout()->setContentsMargins(16 + px, 16, 16, 16);
}

void RadarView::setRangeNm(double nm) {
  const int i = int(ranges().indexOf(nm));
  if (i >= 0) m_range->setCurrentIndex(i);
}

void RadarView::rebuildSources() {
  QSignalBlocker block(m_source);
  m_source->clear();
  auto& svc = RadarService::instance();
  for (const auto& s : svc.sources()) m_source->addItem(s.name, s.id);
  QString cur = svc.current();
  if (cur.isEmpty()) cur = svc.sources().first().id;  // the automatic choice
  m_source->setCurrentIndex(std::max(0, m_source->findData(cur)));
  m_hint->setVisible(svc.sources().size() == 1);
  updateInfo();
}

void RadarView::apply() {
  m_orient[0]->setChecked(!m_display.northUp);
  m_orient[1]->setChecked(m_display.northUp);
  m_display.save();
  auto& svc = RadarService::instance();
  // The simulator follows the chosen range; real radars are set at the
  // radar (or the Mayara web interface), the view zooms.
  svc.setSimulatorRange(int(std::round(m_display.rangeNm * kM)));
  m_scope->setDisplay(m_display);
  updateInfo();
  emit displayChanged();
}

void RadarView::updateInfo() {
  auto& svc = RadarService::instance();
  m_status->setText(svc.status());
  const RadarImage& img = svc.image();
  QStringList lines;
  if (img.rangeM() > 0)
    lines << tr("Radar range %1 · %2 spokes × %3 samples")
                 .arg(rangeText(img.rangeM() / kM))
                 .arg(img.spokes())
                 .arg(img.maxLen());
  const double hdg = m_scope->heading();
  lines << (!std::isnan(img.headingDeg()) ? tr("Heading from the radar")
            : !std::isnan(NavStore::instance().ownShip().hdg)
                ? tr("Heading from the compass")
            : !std::isnan(hdg) ? tr("No heading – course over ground used")
                               : tr("No heading – head up only"));
  m_info->setText(lines.join("\n"));
  const double brg = m_scope->markerBearingRel();
  if (std::isnan(brg)) {
    m_marker->setText(tr("Tap the scope for bearing and distance (EBL/VRM)."));
  } else {
    QString t = tr("EBL %1° relative").arg(brg, 0, 'f', 0);
    if (!std::isnan(hdg))
      t += tr(" · %1° true").arg(std::fmod(brg + hdg, 360), 0, 'f', 0);
    t += "\n" + tr("VRM %1").arg(rangeText(m_scope->markerRangeNm()));
    m_marker->setText(t);
  }
}

void RadarView::showEvent(QShowEvent*) {
  RadarService::instance().want(this, true);
  rebuildSources();
  m_scope->setDisplay(m_display);
}

void RadarView::hideEvent(QHideEvent*) {
  RadarService::instance().want(this, false);
}

void RadarView::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  QPainter p(this);
  QLinearGradient g(0, 0, 0, height());
  g.setColorAt(0, pal.dark ? QColor(14, 17, 23) : QColor(244, 246, 249));
  g.setColorAt(1, pal.dark ? QColor(8, 10, 14) : QColor(228, 232, 238));
  p.fillRect(rect(), g);
}

// --------------------------------------------------------------------------

RadarLayer::RadarLayer(QObject* parent) : MapLayer(parent) {
  connect(&RadarService::instance(), &RadarService::updated, this, [this] {
    m_dirty = true;
    if (m_display.overlay) emit changed();
  });
  setDisplay(RadarDisplay::load());
}

void RadarLayer::setDisplay(const RadarDisplay& d) {
  m_display = d;
  m_dirty = true;
  RadarService::instance().want(this, d.overlay);
  emit changed();
}

void RadarLayer::paint(QPainter& p, const ChartView& view) {
  if (!m_display.overlay) return;
  const OwnShip& own = NavStore::instance().ownShip();
  const RadarImage& img = RadarService::instance().image();
  const double hdg = shipHeading();
  if (!own.valid || img.rangeM() <= 0 || std::isnan(hdg)) return;
  const double rangeM = img.rangeM();
  // Screen radius and the direction of north at the own ship.
  const QPointF c = view.screenPos(own.lat, own.lon);
  double lat2, lon2;
  geo::destination(own.lat, own.lon, 0, rangeM / kM, lat2, lon2);
  const QPointF n = view.screenPos(lat2, lon2);
  const double radius = std::hypot(n.x() - c.x(), n.y() - c.y());
  if (radius < 8) return;
  const double northDeg =
      std::atan2(n.x() - c.x(), -(n.y() - c.y())) * 180 / M_PI;
  const int size = int(std::min(1600.0, 2 * radius));
  if (m_dirty || size != m_cacheSize || std::abs(hdg - m_cacheHdg) > 0.5) {
    RadarImage::Style st = m_display.style();
    m_cache = img.render(size, hdg, rangeM, st);
    m_cacheSize = size;
    m_cacheHdg = hdg;
    m_dirty = false;
  }
  p.save();
  p.setOpacity(0.75);
  p.translate(c);
  p.rotate(northDeg);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  p.drawImage(QRectF(-radius, -radius, 2 * radius, 2 * radius), m_cache);
  p.setOpacity(0.5);
  p.setPen(QPen(QColor(110, 200, 190), 1, Qt::DashLine));
  p.setBrush(Qt::NoBrush);
  p.drawEllipse(QPointF(0, 0), radius, radius);
  p.restore();
}
