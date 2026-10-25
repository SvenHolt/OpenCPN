#include "ui/Overlays.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "core/AppSettings.h"
#include "core/Connections.h"
#include "core/GeoMath.h"
#include "core/NavStore.h"
#include "ui/ChartView.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

namespace {
QString degText(double v) {
  if (std::isnan(v)) return QStringLiteral("–––°");
  return QStringLiteral("%1°").arg(int(std::lround(v)) % 360, 3, 10,
                                   QChar('0'));
}

/** Small caps label above a value, used throughout the cards. */
void drawStat(QPainter& p, const QRectF& r, const QString& label,
              const QString& value, const QColor& valueColor,
              int valueSize = 20) {
  const Palette& pal = Theme::p();
  p.setFont(Theme::font(10, QFont::DemiBold));
  p.setPen(pal.textFaint);
  p.drawText(QRectF(r.left(), r.top(), r.width(), 14),
             Qt::AlignLeft | Qt::AlignVCenter, label.toUpper());
  p.setFont(Theme::numberFont(valueSize, QFont::Normal));
  p.setPen(valueColor);
  p.drawText(QRectF(r.left(), r.top() + 15, r.width(), valueSize + 8),
             Qt::AlignLeft | Qt::AlignVCenter, value);
}
}  // namespace

QString aisShipTypeName(int t) {
  if (t == 30) return QObject::tr("Fishing");
  if (t == 31 || t == 32) return QObject::tr("Towing");
  if (t == 36) return QObject::tr("Sailing");
  if (t == 37) return QObject::tr("Pleasure craft");
  if (t >= 40 && t < 50) return QObject::tr("High speed craft");
  if (t == 50) return QObject::tr("Pilot");
  if (t == 51) return QObject::tr("Search and rescue");
  if (t == 52) return QObject::tr("Tug");
  if (t >= 60 && t < 70) return QObject::tr("Passenger");
  if (t >= 70 && t < 80) return QObject::tr("Cargo");
  if (t >= 80 && t < 90) return QObject::tr("Tanker");
  return QObject::tr("Vessel");
}

// --------------------------------------------------------------------------

InstrumentCard::InstrumentCard(QWidget* parent) : GlassPanel(parent, 26) {
  connect(&NavStore::instance(), &NavStore::ownShipChanged, this,
          qOverload<>(&QWidget::update));
  connect(&AppSettings::instance(), &AppSettings::changed, this,
          qOverload<>(&QWidget::update));
}

void InstrumentCard::paintEvent(QPaintEvent* e) {
  GlassPanel::paintEvent(e);
  const Palette& pal = Theme::p();
  const auto& own = NavStore::instance().ownShip();
  const auto& cfg = AppSettings::instance();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF r = panelRect().adjusted(24, 18, -24, -18);

  // Fix status
  const bool fresh = own.valid && own.lastFix.isValid() &&
                     own.lastFix.secsTo(QDateTime::currentDateTimeUtc()) < 10;
  p.setPen(Qt::NoPen);
  p.setBrush(fresh ? pal.success : pal.danger);
  p.drawEllipse(QPointF(r.left() + 4, r.top() + 8), 4, 4);
  p.setFont(Theme::font(11, QFont::DemiBold));
  p.setPen(pal.textDim);
  QString status = fresh ? tr("GPS FIX") : tr("NO FIX");
  if (fresh && own.satellites > 0)
    status += QStringLiteral(" · %1 SAT").arg(own.satellites);
  p.drawText(QRectF(r.left() + 14, r.top(), 200, 16),
             Qt::AlignLeft | Qt::AlignVCenter, status);

  // Speed, very large and light like an automotive speedometer.
  const auto sp = cfg.speed(own.valid ? own.sog : kNaN);
  const QFont big = Theme::numberFont(76, QFont::Light);
  p.setFont(big);
  p.setPen(pal.text);
  const QFontMetricsF fm(big);
  const QRectF speedRect(r.left() - 3, r.top() + 18, r.width(), 84);
  p.drawText(speedRect, Qt::AlignLeft | Qt::AlignVCenter, sp.first);
  const double w = fm.horizontalAdvance(sp.first);
  p.setFont(Theme::font(17, QFont::DemiBold));
  p.setPen(pal.textDim);
  p.drawText(QRectF(r.left() + w + 8, speedRect.top() + 46, 80, 26),
             Qt::AlignLeft | Qt::AlignVCenter,
             sp.second.isEmpty() ? cfg.speed(0).second : sp.second);

  // COG / HDG row
  const double colW = r.width() / 2;
  drawStat(p, QRectF(r.left(), r.top() + 108, colW, 40), tr("COG"),
           degText(own.valid ? own.cog : kNaN), pal.text);
  drawStat(p, QRectF(r.left() + colW, r.top() + 108, colW, 40), tr("HDG"),
           degText(own.hdg), pal.text);

  // Position
  p.setFont(Theme::numberFont(12, QFont::Medium));
  p.setPen(pal.textDim);
  const QString pos =
      own.valid ? geo::formatLat(own.lat) + "   " + geo::formatLon(own.lon)
                : tr("Waiting for position…");
  p.drawText(QRectF(r.left(), r.bottom() - 16, r.width(), 16),
             Qt::AlignLeft | Qt::AlignVCenter, pos);
}

// --------------------------------------------------------------------------

GuidanceCard::GuidanceCard(QWidget* parent) : GlassPanel(parent, 26) {
  m_stop = new IconButton("close", tr("Stop navigation"), this);
  connect(m_stop, &QAbstractButton::clicked, this,
          [] { NavStore::instance().stopNavigation(); });
  auto refresh = [this] {
    setVisible(NavStore::instance().guidance().active);
    update();
  };
  connect(&NavStore::instance(), &NavStore::guidanceChanged, this, refresh);
  connect(&AppSettings::instance(), &AppSettings::changed, this, refresh);
  hide();
}

void GuidanceCard::resizeEvent(QResizeEvent*) {
  const QRectF r = panelRect();
  m_stop->move(int(r.right() - m_stop->width() - 12), int(r.top() + 12));
}

void GuidanceCard::paintEvent(QPaintEvent* e) {
  GlassPanel::paintEvent(e);
  const Palette& pal = Theme::p();
  const auto& g = NavStore::instance().guidance();
  const auto& own = NavStore::instance().ownShip();
  const auto& cfg = AppSettings::instance();
  if (!g.active) return;
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF r = panelRect().adjusted(18, 18, -18, -18);

  // Maneuver tile: arrow pointing in the direction to steer.
  const QRectF tile(r.left(), r.top(), 88, 88);
  p.setPen(Qt::NoPen);
  p.setBrush(pal.accent);
  p.drawRoundedRect(tile, 20, 20);
  const double heading = own.cog == own.cog ? own.cog : own.hdg;
  const double rel = heading == heading ? geo::angleDiff(heading, g.btwDeg) : 0;
  p.save();
  p.translate(tile.center());
  p.rotate(qBound(-150.0, rel, 150.0));
  Icons::paint(&p, "arrow-up", QRectF(-26, -26, 52, 52), Qt::white);
  p.restore();

  const double x = tile.right() + 20;
  const double w = r.right() - x - 40;
  const auto dist = cfg.distance(g.dtwNm);
  p.setFont(Theme::numberFont(40, QFont::Normal));
  p.setPen(pal.text);
  const QFontMetricsF fm(p.font());
  p.drawText(QRectF(x, r.top() - 4, w, 48), Qt::AlignLeft | Qt::AlignVCenter,
             dist.first);
  p.setFont(Theme::font(17, QFont::DemiBold));
  p.setPen(pal.textDim);
  p.drawText(
      QRectF(x + fm.horizontalAdvance(dist.first) + 6, r.top() + 12, 60, 24),
      Qt::AlignLeft | Qt::AlignVCenter, dist.second);
  p.setFont(Theme::font(15, QFont::DemiBold));
  p.setPen(pal.text);
  p.drawText(QRectF(x, r.top() + 42, w, 22), Qt::AlignLeft | Qt::AlignVCenter,
             p.fontMetrics().elidedText(
                 tr("to %1 · %2").arg(g.waypointName, degText(g.btwDeg)),
                 Qt::ElideRight, int(w)));

  // Bottom stats
  const double y = r.top() + 70;
  const double cw = (r.right() - x) / 3;
  const QString eta = g.eta.isValid() ? g.eta.toString("HH:mm") : "––:––";
  QString xte = cfg.distanceText(std::fabs(g.xteNm));
  if (!std::isnan(g.xteNm) && std::fabs(g.xteNm) > 0.005)
    xte += g.xteNm > 0 ? " R" : " L";
  const bool xteWarn = std::fabs(g.xteNm) > 0.1;
  p.setFont(Theme::font(13, QFont::Medium));
  auto stat = [&](int col, const QString& label, const QString& value,
                  const QColor& c) {
    const QRectF cr(x + col * cw, y, cw, 20);
    p.setPen(pal.textFaint);
    p.setFont(Theme::font(10, QFont::DemiBold));
    const double lw = QFontMetricsF(p.font()).horizontalAdvance(label) + 6;
    p.drawText(cr, Qt::AlignLeft | Qt::AlignVCenter, label);
    p.setPen(c);
    p.setFont(Theme::numberFont(14, QFont::DemiBold));
    p.drawText(cr.adjusted(lw, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
               value);
  };
  stat(0, tr("ETA"), eta, pal.text);
  stat(1, tr("LEFT"), cfg.distanceText(g.remainingNm), pal.text);
  stat(2, tr("XTE"), xte, xteWarn ? pal.warning : pal.text);
  if (g.legCount > 1) {
    p.setFont(Theme::font(10, QFont::DemiBold));
    p.setPen(pal.textFaint);
    p.drawText(QRectF(r.right() - 120, r.top() + 46, 120, 18),
               Qt::AlignRight | Qt::AlignVCenter,
               tr("LEG %1/%2").arg(g.legIndex).arg(g.legCount));
  }
}

// --------------------------------------------------------------------------

MapControls::MapControls(ChartView* chart, QWidget* parent)
    : QWidget(parent), m_chart(chart) {
  auto* l = new QVBoxLayout(this);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(10);
  auto* zoomIn = new RoundButton("plus", this, 56);
  auto* zoomOut = new RoundButton("minus", this, 56);
  m_compass = new RoundButton("north", this, 56);
  m_follow = new RoundButton("locate", this, 56);
  zoomIn->setToolTip(tr("Zoom in"));
  zoomOut->setToolTip(tr("Zoom out"));
  m_compass->setToolTip(tr("North up / course up"));
  m_follow->setToolTip(tr("Follow own ship"));
  m_follow->setCheckable(true);
  m_follow->setChecked(chart->follow());
  m_compass->setCheckable(true);
  m_compass->setChecked(chart->courseUp());
  l->addWidget(zoomIn);
  l->addWidget(zoomOut);
  l->addSpacing(14);
  l->addWidget(m_compass);
  l->addWidget(m_follow);

  connect(zoomIn, &QAbstractButton::clicked, chart,
          [chart] { chart->zoomBy(2.0); });
  connect(zoomOut, &QAbstractButton::clicked, chart,
          [chart] { chart->zoomBy(0.5); });
  connect(m_follow, &QAbstractButton::clicked, chart,
          [this](bool on) { m_chart->setFollow(on); });
  connect(chart, &ChartView::followChanged, m_follow,
          &QAbstractButton::setChecked);
  connect(m_compass, &QAbstractButton::clicked, this, [this](bool on) {
    auto& s = AppSettings::instance();
    s.courseUp = on;
    s.notifyChanged();
  });
  connect(chart, &ChartView::viewChanged, this, [this] {
    m_compass->setIconRotation(-m_chart->rotation());
    m_compass->setChecked(m_chart->courseUp());
  });
}

// --------------------------------------------------------------------------

BottomDock::BottomDock(QWidget* parent) : QWidget(parent) {
  setFixedHeight(kHeight);
  auto* l = new QHBoxLayout(this);
  l->setContentsMargins(14, 8, 18, 8);
  l->setSpacing(6);

  auto* mob = new DockButton("lifebuoy", tr("MOB"), this);
  mob->setDanger(true);
  mob->setToolTip(tr("Man overboard"));
  connect(mob, &QAbstractButton::clicked, this, &BottomDock::mobRequested);
  l->addWidget(mob);
  l->addSpacing(10);
  const char* viewIcons[kViews] = {"gauge", "chart", "boat",
                                   "book",  "radar", "sonar"};
  const QString viewNames[kViews] = {tr("Cockpit"), tr("Chart"), tr("Boat"),
                                     tr("Log"),     tr("Radar"), tr("Sonar")};
  for (int i = 0; i < kViews; ++i) {
    m_views[i] = new DockButton(viewIcons[i], viewNames[i], this);
    m_views[i]->setCheckable(true);
    m_views[i]->setMinimumWidth(72);
    connect(m_views[i], &QAbstractButton::clicked, this,
            [this, i] { emit viewRequested(i); });
    l->addWidget(m_views[i]);
  }
  l->addStretch(1);

  m_sectionLayout = new QHBoxLayout;
  m_sectionLayout->setSpacing(4);
  l->addLayout(m_sectionLayout);
  m_more = new DockButton("more", tr("More"), this);
  m_more->setCheckable(true);
  m_more->setMinimumWidth(60);
  m_more->hide();
  connect(m_more, &QAbstractButton::clicked, this, [this] {
    m_more->setChecked(m_active >= 0 && !m_buttons[m_active]->isVisible());
    QMenu menu(this);
    for (int i = 0; i < m_buttons.size(); ++i) {
      if (m_buttons[i]->isVisible()) continue;
      QAction* a = menu.addAction(Icons::icon(m_icons[i], Theme::p().text, 18),
                                  m_labels[i]);
      connect(a, &QAction::triggered, this,
              [this, i] { emit sectionRequested(m_pages[i]); });
    }
    const QSize hint = menu.sizeHint();
    menu.exec(m_more->mapToGlobal(QPoint(0, -hint.height())));
  });
  l->addWidget(m_more);
  l->addStretch(1);

  m_status = new QLabel(this);
  m_status->setProperty("role", "dim");
  l->addWidget(m_status);
  l->addSpacing(12);
  m_theme = new DockButton("moon", tr("Night"), this);
  connect(m_theme, &QAbstractButton::clicked, this, [] {
    auto& s = AppSettings::instance();
    s.theme = Theme::instance().isDark() ? AppSettings::ThemeMode::Day
                                         : AppSettings::ThemeMode::Night;
    s.notifyChanged();
  });
  l->addWidget(m_theme);
  m_clock = new QLabel(this);
  m_clock->setFont(Theme::numberFont(22, QFont::Normal));
  m_clock->setMinimumWidth(78);
  m_clock->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  l->addWidget(m_clock);

  m_clockTimer.setInterval(1000);
  connect(&m_clockTimer, &QTimer::timeout, this, &BottomDock::updateStatus);
  m_clockTimer.start();
  connect(&Theme::instance(), &Theme::changed, this, [this] {
    updateStatus();
    update();
  });
  connect(&ConnectionManager::instance(), &ConnectionManager::statusChanged,
          this, &BottomDock::updateStatus);
  updateStatus();
}

void BottomDock::addSection(const QString& icon, const QString& label,
                            int page) {
  auto* b = new DockButton(icon, label, this);
  b->setCheckable(true);
  b->setMinimumWidth(b->sizeHint().width() - 12);
  connect(b, &QAbstractButton::clicked, this,
          [this, page] { emit sectionRequested(page); });
  m_sectionLayout->addWidget(b);
  m_buttons << b;
  m_icons << icon;
  m_labels << label;
  m_pages << page;
  fitSections();
}

void BottomDock::setActiveView(int view) {
  for (int i = 0; i < kViews; ++i) m_views[i]->setChecked(i == view);
}

void BottomDock::setActiveSection(int page) {
  const int s = int(m_pages.indexOf(page));
  m_active = s;
  for (int i = 0; i < m_buttons.size(); ++i) m_buttons[i]->setChecked(i == s);
  m_more->setChecked(s >= 0 && s < m_buttons.size() &&
                     !m_buttons[s]->isVisible());
}

void BottomDock::resizeEvent(QResizeEvent*) {
  // The source summary is the first thing to go on narrow screens.
  m_status->setVisible(width() >= 1700);
  fitSections();
}

void BottomDock::fitSections() {
  // Fixed parts: margins, MOB, the view buttons, night mode, clock.
  int fixed = 14 + 18 + 82 * 2 + 78 + 10 + 40;
  for (auto* v : m_views) fixed += v->minimumWidth() + 6;
  if (!m_status->isHidden()) fixed += m_status->sizeHint().width() + 18;
  const int available = width() - fixed;
  const int n = int(m_buttons.size());
  const auto widthOf = [this](int i) {
    return m_buttons[i]->minimumWidth() + 4;
  };
  int total = 0;
  for (int i = 0; i < n; ++i) total += widthOf(i);
  int shown = n;
  if (total > available) {
    // Room for the "More" button, then as many sections as fit.
    int used = m_more->minimumWidth() + 4;
    shown = 0;
    while (shown < n && used + widthOf(shown) <= available)
      used += widthOf(shown++);
  }
  for (int i = 0; i < n; ++i) m_buttons[i]->setVisible(i < shown);
  m_more->setVisible(shown < n);
  setActiveSection(m_active >= 0 ? m_pages[m_active] : -1);
}

void BottomDock::updateStatus() {
  const Palette& pal = Theme::p();
  m_clock->setText(QTime::currentTime().toString("HH:mm"));
  const bool dark = Theme::instance().isDark();
  m_theme->setIconName(dark ? "moon" : "sun");
  m_theme->setText(dark ? tr("Night") : tr("Day"));
  int connected = 0, total = 0;
  for (auto* c : ConnectionManager::instance().connections()) {
    if (!c->config().enabled) continue;
    ++total;
    if (c->status() == Connection::Status::Connected) ++connected;
  }
  const QColor dot = connected > 0 ? pal.success : pal.danger;
  m_status->setText(QStringLiteral("<span style='color:%1'>●</span>&nbsp; %2")
                        .arg(dot.name())
                        .arg(tr("%1/%2 sources").arg(connected).arg(total)));
}

void BottomDock::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  QPainter p(this);
  p.fillRect(rect(), pal.dock);
  p.setPen(QPen(pal.panelBorder, 1));
  p.drawLine(0, 0, width(), 0);
}

// --------------------------------------------------------------------------

Toast::Toast(QWidget* parent) : GlassPanel(parent, 20) {
  m_hide.setSingleShot(true);
  m_hide.setInterval(7000);
  connect(&m_hide, &QTimer::timeout, this, &QWidget::hide);
  hide();
}

void Toast::showMessage(const QString& text, bool danger) {
  m_text = text;
  m_danger = danger;
  const QFontMetrics fm(Theme::font(15, QFont::DemiBold));
  resize(fm.horizontalAdvance(text) + 96 + 2 * kShadow, 60 + 2 * kShadow);
  show();
  raise();
  update();
  m_hide.start();
}

void Toast::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF r = panelRect();
  paintGlass(p, r, m_radius);
  const QColor c = m_danger ? pal.danger : pal.accent;
  p.setPen(Qt::NoPen);
  p.setBrush(c);
  p.drawEllipse(QRectF(r.left() + 14, r.center().y() - 16, 32, 32));
  Icons::paint(&p, "bell", QRectF(r.left() + 21, r.center().y() - 9, 18, 18),
               Qt::white);
  p.setFont(Theme::font(15, QFont::DemiBold));
  p.setPen(pal.text);
  p.drawText(r.adjusted(60, 0, -18, 0), Qt::AlignLeft | Qt::AlignVCenter,
             m_text);
}

// --------------------------------------------------------------------------

RouteEditBar::RouteEditBar(QWidget* parent) : GlassPanel(parent, 24) {
  auto* l = new QHBoxLayout(this);
  l->setContentsMargins(kShadow + 20, kShadow + 10, kShadow + 10, kShadow + 10);
  l->setSpacing(8);
  auto* icon = new QLabel(this);
  icon->setPixmap(Icons::pixmap("route", Theme::p().accentBright, 22));
  connect(&Theme::instance(), &Theme::changed, icon, [icon] {
    icon->setPixmap(Icons::pixmap("route", Theme::p().accentBright, 22));
  });
  l->addWidget(icon);
  m_label = new QLabel(this);
  m_label->setProperty("role", "value");
  m_label->setMinimumWidth(220);
  l->addWidget(m_label, 1);
  auto* undo = new QPushButton(tr("Undo"), this);
  auto* cancel = new QPushButton(tr("Cancel"), this);
  m_done = new QPushButton(tr("Save route"), this);
  m_done->setProperty("role", "primary");
  for (auto* b : {undo, cancel, m_done}) {
    b->setCursor(Qt::PointingHandCursor);
    l->addWidget(b);
  }
  connect(undo, &QPushButton::clicked, this, &RouteEditBar::undo);
  connect(cancel, &QPushButton::clicked, this, &RouteEditBar::cancel);
  connect(m_done, &QPushButton::clicked, this, &RouteEditBar::done);
  setPointCount(0);
  hide();
}

void RouteEditBar::setPointCount(int n) {
  m_label->setText(n == 0 ? tr("Tap the chart to add route points")
                          : tr("New route · %n point(s)", nullptr, n));
  m_done->setEnabled(n >= 2);
}

// --------------------------------------------------------------------------

TargetCard::TargetCard(QWidget* parent) : GlassPanel(parent, 24) {
  m_close = new IconButton("close", tr("Close"), this);
  connect(m_close, &QAbstractButton::clicked, this, [this] {
    hide();
    emit closed();
  });
  connect(&NavStore::instance(), &NavStore::aisChanged, this,
          qOverload<>(&QWidget::update));
  hide();
}

void TargetCard::setTarget(quint32 mmsi) {
  m_mmsi = mmsi;
  setVisible(mmsi != 0);
  update();
}

void TargetCard::resizeEvent(QResizeEvent*) {
  const QRectF r = panelRect();
  m_close->move(int(r.right() - m_close->width() - 10), int(r.top() + 10));
}

void TargetCard::paintEvent(QPaintEvent* e) {
  GlassPanel::paintEvent(e);
  const auto& targets = NavStore::instance().aisTargets();
  auto it = targets.find(m_mmsi);
  if (it == targets.end()) return;
  const AisTarget& t = *it;
  const Palette& pal = Theme::p();
  const auto& cfg = AppSettings::instance();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF r = panelRect().adjusted(22, 18, -22, -18);

  p.setFont(Theme::font(20, QFont::DemiBold));
  p.setPen(pal.text);
  p.drawText(QRectF(r.left(), r.top(), r.width() - 40, 28),
             Qt::AlignLeft | Qt::AlignVCenter,
             p.fontMetrics().elidedText(t.displayName(), Qt::ElideRight,
                                        int(r.width() - 40)));
  p.setFont(Theme::font(12, QFont::Medium));
  p.setPen(pal.textDim);
  QString sub =
      aisShipTypeName(t.shipType) + " · MMSI " + QString::number(t.mmsi);
  if (!t.callsign.isEmpty()) sub += " · " + t.callsign;
  p.drawText(QRectF(r.left(), r.top() + 28, r.width(), 18),
             Qt::AlignLeft | Qt::AlignVCenter, sub);

  if (t.dangerous) {
    const QRectF badge(r.left(), r.top() + 54, r.width(), 28);
    p.setPen(Qt::NoPen);
    p.setBrush(pal.danger);
    p.drawRoundedRect(badge, 10, 10);
    p.setFont(Theme::font(12, QFont::Bold));
    p.setPen(Qt::white);
    p.drawText(badge, Qt::AlignCenter, tr("COLLISION RISK"));
  }
  const double y0 = r.top() + (t.dangerous ? 94 : 62);
  const double cw = r.width() / 2;
  drawStat(p, QRectF(r.left(), y0, cw, 40), tr("SOG"), cfg.speedText(t.sog),
           pal.text, 18);
  drawStat(p, QRectF(r.left() + cw, y0, cw, 40), tr("COG"), degText(t.cog),
           pal.text, 18);
  drawStat(p, QRectF(r.left(), y0 + 48, cw, 40), tr("Range"),
           cfg.distanceText(t.rangeNm) + "  " + degText(t.bearing), pal.text,
           18);
  QString cpa = QStringLiteral("–");
  if (!std::isnan(t.cpaNm))
    cpa = t.tcpaMin > 0.5
              ? cfg.distanceText(t.cpaNm) +
                    QStringLiteral(" / %1′").arg(t.tcpaMin, 0, 'f', 0)
              : tr("Opening");
  drawStat(p, QRectF(r.left() + cw, y0 + 48, cw, 40), tr("CPA / TCPA"), cpa,
           t.dangerous ? pal.danger : pal.text, 18);
}
