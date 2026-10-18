#include "ui/SupplyPages.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <cmath>

#include "core/NavStore.h"
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

QLabel* iconLabel(const QString& icon, const QColor& color, int size = 22) {
  auto* l = new QLabel;
  l->setFixedSize(size + 4, size + 4);
  l->setAlignment(Qt::AlignCenter);
  l->setPixmap(Icons::pixmap(icon, color, size));
  return l;
}

QScrollArea* scrollFor(QWidget* content) {
  auto* scroll = new QScrollArea;
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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

QWidget* card(QWidget* content, const QString& name = "supplyCard") {
  auto* w = new QWidget;
  w->setObjectName(name);
  w->setAttribute(Qt::WA_StyledBackground);
  w->setStyleSheet(
      QStringLiteral("#%1 { background: %2; border-radius: 14px; }")
          .arg(name, Theme::p().panelHover.name(QColor::HexArgb)));
  auto* l = new QVBoxLayout(w);
  l->setContentsMargins(12, 10, 12, 10);
  l->addWidget(content);
  return w;
}

QString daysText(double d) {
  if (std::isinf(d)) return QObject::tr("> 30 days");
  if (d < 1) return QObject::tr("%1 h").arg(int(std::round(d * 24)));
  return QObject::tr("%1 days").arg(d, 0, 'f', d < 10 ? 1 : 0);
}

QString number(double q) {
  return QString::number(q, 'f', q == std::round(q) ? 0 : 1);
}

QColor resourceColor(RangeResource r) {
  switch (r) {
    case RangeResource::FreshWater:
      return QColor(70, 150, 255);
    case RangeResource::WasteWater:
      return QColor(180, 130, 80);
    case RangeResource::Energy:
      return QColor(60, 200, 110);
    case RangeResource::Diesel:
      return QColor(255, 176, 50);
  }
  return Qt::white;
}

QString resourceIcon(RangeResource r) {
  switch (r) {
    case RangeResource::FreshWater:
      return "drop";
    case RangeResource::WasteWater:
      return "drop";
    case RangeResource::Energy:
      return "battery";
    case RangeResource::Diesel:
      return "fuel";
  }
  return {};
}

/** The range for a mode from the log, the boat state and the forecast. */
RangeResult computeRange(BoatMode mode, RangeWindow w, RangeState* stateOut,
                         UsageRates* rates = nullptr) {
  auto& log = ConsumptionLog::instance();
  const QDateTime now = QDateTime::currentDateTimeUtc();
  const auto win = range::window(log.samples(), w, now);
  const QVector<ConsumptionSample> samples =
      win.first.isValid() ? log.between(win.first, win.second)
                          : QVector<ConsumptionSample>();
  const UsageRates r = range::learn(samples, mode);
  if (rates) *rates = r;
  const RangeState state = RangeState::fromBoat();
  if (stateOut) *stateOut = state;
  const QDate today = QDate::currentDate();
  return range::project(r, state,
                        TemperatureForecast::instance().upcoming(today), today);
}

/** Asks for a forecast at the boat position (if known). */
void requestForecast() {
  const OwnShip& own = NavStore::instance().ownShip();
  if (own.valid) TemperatureForecast::instance().update(own.lat, own.lon);
}
}  // namespace

// --------------------------------------------------------------------------
// RangeChart

RangeChart::RangeChart(QWidget* parent) : QWidget(parent) {
  setMinimumHeight(210);
}

void RangeChart::setResult(const RangeResult& r, const RangeState& s) {
  m_result = r;
  m_state = s;
  update();
}

void RangeChart::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const auto& days = m_result.projection;
  if (days.isEmpty()) return;
  // Show two days beyond the limit, at least a week, at most three.
  const double limit = std::isinf(m_result.days) ? 21 : m_result.days;
  const int n = std::clamp(int(std::ceil(limit)) + 2, 7,
                           std::min<int>(21, int(days.size())));
  const QRectF plot(28, 8, width() - 36, height() - 58);
  const auto x = [&](double d) { return plot.left() + plot.width() * d / n; };
  const auto y = [&](double pct) {
    return plot.bottom() - plot.height() * std::clamp(pct, 0.0, 100.0) / 100;
  };
  QFont small = Theme::font(10);
  p.setFont(small);
  // Grid.
  p.setPen(QPen(pal.panelBorder, 1));
  for (int pct : {0, 50, 100}) {
    p.drawLine(QPointF(plot.left(), y(pct)), QPointF(plot.right(), y(pct)));
    p.setPen(pal.textFaint);
    p.drawText(QRectF(0, y(pct) - 8, 24, 16), Qt::AlignRight | Qt::AlignVCenter,
               QString::number(pct));
    p.setPen(QPen(pal.panelBorder, 1));
  }
  // Lines: share of each tank (holding tank: free space) and battery.
  const double freshCap = std::max(1.0, m_state.freshCapacityL);
  const double wasteCap = std::max(1.0, m_state.wasteCapacityL);
  const double fuelCap = std::max(1.0, m_state.fuelCapacityL);
  for (int k = 0; k < 4; ++k) {
    const auto kind = RangeResource(k);
    const auto share = [&](const ProjectionDay* d) -> double {
      switch (kind) {
        case RangeResource::FreshWater:
          return 100 * (d ? d->freshL : m_state.freshL) / freshCap;
        case RangeResource::WasteWater:
          return 100 * (1 - (d ? d->wasteL : m_state.wasteL) / wasteCap);
        case RangeResource::Energy:
          return d ? d->batteryPct : 100 * m_state.soc;
        case RangeResource::Diesel:
          return 100 * (d ? d->fuelL : m_state.fuelL) / fuelCap;
      }
      return 0;
    };
    QPainterPath path;
    path.moveTo(x(0), y(share(nullptr)));
    for (int i = 0; i < n && i < days.size(); ++i)
      path.lineTo(x(i + 1), y(share(&days[i])));
    QColor c = resourceColor(kind);
    p.setPen(QPen(c, kind == m_result.limit ? 3 : 1.8));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
  }
  // The limit.
  if (!std::isinf(m_result.days) && m_result.days <= n) {
    const double lx = x(m_result.days);
    p.setPen(QPen(pal.danger, 1.5, Qt::DashLine));
    p.drawLine(QPointF(lx, plot.top()), QPointF(lx, plot.bottom()));
  }
  // Days and temperatures below.
  const double step = plot.width() / n;
  const int every = n > 14 ? 2 : 1;
  for (int i = 0; i < n && i < days.size(); i += every) {
    const ProjectionDay& d = days[i];
    const QRectF col(x(i), plot.bottom() + 4, step * every, 14);
    p.setPen(pal.textFaint);
    p.drawText(col, Qt::AlignCenter, d.date.toString("dd."));
    p.setPen(d.forecast ? pal.text : pal.textFaint);
    p.drawText(col.translated(0, 15), Qt::AlignCenter,
               QStringLiteral("%1°").arg(int(std::round(d.tempC))));
    // Heating need as a small orange bar.
    if (d.degreeDays > 0.2) {
      const double h = std::min(12.0, d.degreeDays * 1.2);
      p.setPen(Qt::NoPen);
      p.setBrush(QColor(255, 140, 50, 170));
      p.drawRoundedRect(QRectF(col.center().x() - 3, height() - h - 2, 6, h), 2,
                        2);
    }
  }
}

// --------------------------------------------------------------------------
// RangePage

RangePage::RangePage(QWidget* parent) : QWidget(parent) {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  auto* content = new QWidget;
  auto* l = new QVBoxLayout(content);
  l->setContentsMargins(0, 0, 8, 0);
  l->setSpacing(10);
  outer->addWidget(scrollFor(content));

  auto* modes = new QHBoxLayout;
  modes->setSpacing(6);
  const QString names[2] = {tr("At anchor"), tr("Underway")};
  const char* icons[2] = {"anchor", "sail"};
  for (int i = 0; i < 2; ++i) {
    m_modes[i] = new QPushButton(names[i]);
    m_modes[i]->setProperty("role", "segment");
    m_modes[i]->setCheckable(true);
    m_modes[i]->setIcon(Icons::icon(icons[i], Theme::p().text, 18));
    m_modes[i]->setCursor(Qt::PointingHandCursor);
    m_modes[i]->setMinimumHeight(40);
    connect(m_modes[i], &QPushButton::clicked, this, [this, i] {
      setMode(i == 0 ? BoatMode::Anchor : BoatMode::Underway);
    });
    modes->addWidget(m_modes[i]);
  }
  l->addLayout(modes);

  auto* wrow = new QHBoxLayout;
  wrow->addWidget(label(tr("Based on"), "dim"));
  m_windowBox = new QComboBox;
  m_windowBox->addItems({tr("Last 3 days"), tr("Last 7 days"),
                         tr("Last 14 days"), tr("Last trip")});
  m_windowBox->setCurrentIndex(int(m_window));
  connect(m_windowBox, &QComboBox::currentIndexChanged, this,
          [this](int i) { setWindow(RangeWindow(i)); });
  wrow->addWidget(m_windowBox, 1);
  l->addLayout(wrow);

  m_summary = label(QString());
  m_summary->setWordWrap(true);
  m_summary->setStyleSheet("font-size: 17px; font-weight: 600;");
  l->addWidget(m_summary);

  for (int k = 0; k < 4; ++k) {
    auto* w = new QWidget;
    w->setObjectName(QStringLiteral("range-%1").arg(k));
    auto* g = new QGridLayout(w);
    g->setContentsMargins(0, 0, 0, 0);
    g->setHorizontalSpacing(10);
    g->setVerticalSpacing(2);
    auto* icon = iconLabel(resourceIcon(RangeResource(k)),
                           resourceColor(RangeResource(k)));
    g->addWidget(icon, 0, 0, 2, 1, Qt::AlignTop);
    auto* name = label(range::resourceName(RangeResource(k)), "value");
    g->addWidget(name, 0, 1);
    auto* days = label(QString());
    days->setObjectName("days");
    days->setStyleSheet("font-size: 20px; font-weight: 600;");
    g->addWidget(days, 0, 2, Qt::AlignRight);
    auto* detail = label(QString(), "dim");
    detail->setObjectName("detail");
    detail->setWordWrap(true);
    g->addWidget(detail, 1, 1, 1, 2);
    g->setColumnStretch(1, 1);
    m_rows[k] = w;
    l->addWidget(card(w, QStringLiteral("rangeCard%1").arg(k)));
  }

  l->addWidget(label(tr("FORECAST"), "section"));
  m_chart = new RangeChart;
  l->addWidget(m_chart);
  auto* frow = new QHBoxLayout;
  m_weather = label(QString(), "dim");
  m_weather->setWordWrap(true);
  frow->addWidget(m_weather, 1);
  auto* reload = new IconButton("refresh", tr("Reload forecast"));
  connect(reload, &QAbstractButton::clicked, this, [] {
    const OwnShip& own = NavStore::instance().ownShip();
    if (own.valid) TemperatureForecast::instance().fetch(own.lat, own.lon);
  });
  frow->addWidget(reload);
  l->addLayout(frow);
  m_basis = label(QString(), "dim");
  m_basis->setWordWrap(true);
  l->addWidget(m_basis);
  l->addStretch(1);

  connect(&ConsumptionLog::instance(), &ConsumptionLog::changed, this,
          &RangePage::refresh);
  auto& fc = TemperatureForecast::instance();
  connect(&fc, &TemperatureForecast::updated, this, &RangePage::refresh);
  connect(&fc, &TemperatureForecast::failed, this, [this](const QString& msg) {
    m_weather->setText(tr("Forecast not available: %1").arg(msg));
  });
  // Tank levels and the battery change all the time.
  m_refresh.setInterval(15000);
  connect(&m_refresh, &QTimer::timeout, this, [this] {
    if (isVisible()) refresh();
  });
  m_refresh.start();
  setMode(BoatMode::Anchor);
}

void RangePage::showEvent(QShowEvent* e) {
  QWidget::showEvent(e);
  requestForecast();
  refresh();
}

void RangePage::setMode(BoatMode m) {
  m_mode = m == BoatMode::Underway ? BoatMode::Underway : BoatMode::Anchor;
  m_modes[0]->setChecked(m_mode == BoatMode::Anchor);
  m_modes[1]->setChecked(m_mode == BoatMode::Underway);
  refresh();
}

void RangePage::setWindow(RangeWindow w) {
  m_window = w;
  const QSignalBlocker block(m_windowBox);
  m_windowBox->setCurrentIndex(int(w));
  refresh();
}

void RangePage::refresh() {
  RangeState state;
  m_result = computeRange(m_mode, m_window, &state);
  const UsageRates& r = m_result.rates;
  const QString where =
      m_mode == BoatMode::Anchor ? tr("At anchor") : tr("Underway");
  if (std::isinf(m_result.days))
    m_summary->setText(
        tr("%1 the supplies last more than 30 days.").arg(where));
  else
    m_summary->setText(tr("%1: %2 – %3 is the limit.")
                           .arg(where, daysText(m_result.days),
                                range::resourceName(m_result.limit)));

  const double whPerAh = state.voltage;
  for (int k = 0; k < 4; ++k) {
    const ResourceRange& rr = m_result.resources[k];
    auto* days = m_rows[k]->findChild<QLabel*>("days");
    auto* detail = m_rows[k]->findChild<QLabel*>("detail");
    days->setText(daysText(rr.days));
    const bool critical = rr.kind == m_result.limit && rr.days < 3;
    days->setStyleSheet(
        QStringLiteral("font-size: 20px; font-weight: 600; color: %1;")
            .arg(critical ? Theme::p().danger.name() : Theme::p().text.name()));
    QString text;
    switch (rr.kind) {
      case RangeResource::FreshWater:
        text = tr("%1 l left · %2 l/day")
                   .arg(number(std::round(rr.left)), number(r.freshLpd));
        break;
      case RangeResource::WasteWater:
        text = tr("%1 l free · %2 l/day")
                   .arg(number(std::round(rr.left)), number(r.wasteLpd));
        break;
      case RangeResource::Energy:
        text = tr("%1 Ah usable down to %2 % · use %3 Ah/day, solar %4 "
                  "Ah/day")
                   .arg(int(rr.left / whPerAh))
                   .arg(int(state.minSoc * 100))
                   .arg(int(std::round(
                       (r.baseWhPerDay + r.heaterWhPerDd * r.degreeDaysPerDay) /
                       whPerAh)))
                   .arg(int(std::round(r.solarWhPerDay / whPerAh)));
        if (r.alternatorWhPerDay > 1)
          text += tr(", alternator %1 Ah/day")
                      .arg(int(std::round(r.alternatorWhPerDay / whPerAh)));
        break;
      case RangeResource::Diesel: {
        const double heater = m_result.projection.isEmpty()
                                  ? 0
                                  : m_result.projection.first().heaterFuelL;
        text =
            tr("%1 l above the reserve · heater %2 l/day at %3")
                .arg(number(std::round(rr.left)), number(heater))
                .arg(m_result.projection.isEmpty()
                         ? QString()
                         : QStringLiteral("%1 °C").arg(
                               m_result.projection.first().tempC, 0, 'f', 0));
        if (r.engineFuelLpd > 0.05)
          text += tr(" · engine %1 l/day (%2 h)")
                      .arg(number(r.engineFuelLpd), number(r.engineHpd));
        break;
      }
    }
    detail->setText(text);
  }
  m_chart->setResult(m_result, state);

  auto& log = ConsumptionLog::instance();
  const auto win =
      range::window(log.samples(), m_window, QDateTime::currentDateTimeUtc());
  QString basis;
  if (!r.learned)
    basis = tr("Too little logged %1 – typical values for two people are "
               "used.")
                .arg(where.toLower());
  else
    basis = tr("Learned from %1 days %2 (%3 – %4), mean %5 °C.")
                .arg(r.days, 0, 'f', 1)
                .arg(where.toLower())
                .arg(win.first.toLocalTime().toString("dd.MM. HH:mm"),
                     win.second.toLocalTime().toString("dd.MM. HH:mm"))
                .arg(r.meanTempC, 0, 'f', 1);
  basis += " " + tr("Heating: %1 l diesel per degree day (base 18 °C).")
                     .arg(r.heaterFuelPerDd, 0, 'f', 2);
  if (log.simulatedHistory())
    basis += " " + tr("The log starts with simulated days.");
  m_basis->setText(basis);
  updateForecast();
}

void RangePage::updateForecast() {
  auto& fc = TemperatureForecast::instance();
  const auto days = fc.upcoming(QDate::currentDate());
  if (fc.busy())
    m_weather->setText(tr("Loading the temperature forecast…"));
  else if (days.isEmpty())
    m_weather->setText(
        tr("No temperature forecast (offline): the logged "
           "temperatures are used."));
  else
    m_weather->setText(
        tr("Open-Meteo forecast for %1 days from %2 (%3, %4). Beyond it the "
           "mean temperature is used.")
            .arg(days.size())
            .arg(fc.fetched().toLocalTime().toString("dd.MM. HH:mm"))
            .arg(fc.position().y(), 0, 'f', 2)
            .arg(fc.position().x(), 0, 'f', 2));
}

// --------------------------------------------------------------------------
// RangeCard

RangeCard::RangeCard(QWidget* parent) : QWidget(parent) {
  setMinimumHeight(118);
  setCursor(Qt::PointingHandCursor);
  m_timer.setInterval(30000);
  connect(&m_timer, &QTimer::timeout, this, [this] {
    if (isVisible()) refresh();
  });
  m_timer.start();
  connect(&ConsumptionLog::instance(), &ConsumptionLog::changed, this,
          &RangeCard::refresh);
  connect(&TemperatureForecast::instance(), &TemperatureForecast::updated, this,
          &RangeCard::refresh);
  refresh();
}

void RangeCard::refresh() {
  m_anchor = computeRange(BoatMode::Anchor, RangeWindow::Days7, nullptr);
  m_underway = computeRange(BoatMode::Underway, RangeWindow::Days7, nullptr);
  update();
}

void RangeCard::paintEvent(QPaintEvent*) {
  const Palette& pal = Theme::p();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const double half = width() / 2.0;
  const RangeResult* rs[2] = {&m_anchor, &m_underway};
  const QString names[2] = {tr("At anchor"), tr("Underway")};
  for (int i = 0; i < 2; ++i) {
    const RangeResult& r = *rs[i];
    const QRectF col(i * half, 0, half - 8, height());
    Icons::paint(&p, i == 0 ? "anchor" : "sail", QRectF(col.left(), 2, 18, 18),
                 pal.textDim);
    p.setFont(Theme::font(12, QFont::DemiBold));
    p.setPen(pal.textDim);
    p.drawText(QRectF(col.left() + 24, 0, col.width() - 24, 22),
               Qt::AlignLeft | Qt::AlignVCenter, names[i]);
    p.setFont(Theme::numberFont(28, QFont::DemiBold));
    p.setPen(!std::isinf(r.days) && r.days < 3 ? pal.danger : pal.text);
    p.drawText(QRectF(col.left(), 24, col.width(), 38),
               Qt::AlignLeft | Qt::AlignVCenter, daysText(r.days));
    p.setFont(Theme::font(12));
    p.setPen(pal.textDim);
    if (!std::isinf(r.days))
      p.drawText(QRectF(col.left(), 62, col.width(), 18),
                 Qt::AlignLeft | Qt::AlignVCenter,
                 tr("limit: %1").arg(range::resourceName(r.limit)));
    // Four small bars: days per resource relative to 14 days.
    for (int k = 0; k < 4; ++k) {
      const double d = r.resources[k].days;
      const double f = std::isinf(d) ? 1.0 : std::clamp(d / 14, 0.02, 1.0);
      const QRectF bar(col.left(), 86 + k * 7, col.width() - 10, 4);
      p.setPen(Qt::NoPen);
      p.setBrush(pal.panelPressed);
      p.drawRoundedRect(bar, 2, 2);
      p.setBrush(resourceColor(RangeResource(k)));
      p.drawRoundedRect(QRectF(bar.left(), bar.top(), bar.width() * f, 4), 2,
                        2);
    }
  }
}

// --------------------------------------------------------------------------
// ProvisionEditor

ProvisionEditor::ProvisionEditor(QWidget* parent) : QWidget(parent) {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(10);
  auto* content = new QWidget;
  auto* l = new QVBoxLayout(content);
  l->setContentsMargins(0, 0, 8, 0);
  l->setSpacing(8);
  outer->addWidget(scrollFor(content), 1);

  const auto row = [&](const QString& text, QWidget* w) {
    auto* r = new QHBoxLayout;
    auto* lab = label(text);
    lab->setMinimumWidth(110);
    r->addWidget(lab);
    r->addWidget(w, 1);
    l->addLayout(r);
  };
  m_name = new QLineEdit;
  m_name->setPlaceholderText(tr("e.g. Apples"));
  row(tr("Name"), m_name);
  m_category = new QComboBox;
  for (auto c : ProvisionStore::categories())
    m_category->addItem(
        Icons::icon(ProvisionStore::categoryIcon(c), Theme::p().text, 18),
        ProvisionStore::categoryName(c), int(c));
  row(tr("Category"), m_category);
  auto* qrow = new QWidget;
  auto* ql = new QHBoxLayout(qrow);
  ql->setContentsMargins(0, 0, 0, 0);
  m_quantity = new QDoubleSpinBox;
  m_quantity->setRange(0, 9999);
  m_quantity->setDecimals(1);
  m_quantity->setAlignment(Qt::AlignRight);
  ql->addWidget(m_quantity, 1);
  m_unit = new QComboBox;
  m_unit->addItems({"pcs", "kg", "g", "l", "pack", "can", "jar"});
  ql->addWidget(m_unit);
  row(tr("Quantity"), qrow);
  auto* drow = new QWidget;
  auto* dl = new QHBoxLayout(drow);
  dl->setContentsMargins(0, 0, 0, 0);
  m_hasDate = new QCheckBox;
  m_hasDate->setToolTip(
      tr("Without a date the typical shelf life of the category is used"));
  dl->addWidget(m_hasDate);
  m_bestBefore = new QDateEdit;
  m_bestBefore->setCalendarPopup(true);
  m_bestBefore->setDisplayFormat("dd.MM.yyyy");
  dl->addWidget(m_bestBefore, 1);
  connect(m_hasDate, &QCheckBox::toggled, m_bestBefore, &QWidget::setEnabled);
  row(tr("Best before"), drow);
  m_planned = new QDoubleSpinBox;
  m_planned->setRange(0, 99);
  m_planned->setDecimals(2);
  m_planned->setSingleStep(0.1);
  m_planned->setAlignment(Qt::AlignRight);
  m_planned->setToolTip(
      tr("Planned use per person and day; the log of "
         "what was taken wins once there is one"));
  row(tr("Per person/day"), m_planned);
  m_min = new QDoubleSpinBox;
  m_min->setRange(0, 999);
  m_min->setDecimals(1);
  m_min->setAlignment(Qt::AlignRight);
  row(tr("Minimum stock"), m_min);

  m_countBox = new QWidget;
  auto* cl = new QVBoxLayout(m_countBox);
  cl->setContentsMargins(0, 0, 0, 0);
  cl->addWidget(label(tr("STOCK COUNT"), "section"));
  auto* crow = new QHBoxLayout;
  m_counted = new QDoubleSpinBox;
  m_counted->setRange(0, 9999);
  m_counted->setDecimals(1);
  m_counted->setAlignment(Qt::AlignRight);
  crow->addWidget(m_counted, 1);
  auto* countBtn = button(tr("Counted"), "check");
  countBtn->setToolTip(tr("Less than in stock is logged as used"));
  connect(countBtn, &QPushButton::clicked, this, [this] {
    if (m_item.id.isEmpty()) return;
    auto& store = ProvisionStore::instance();
    store.count(m_item.id, m_counted->value(), QDate::currentDate());
    if (const ProvisionItem* i = store.find(m_item.id)) edit(*i);
  });
  crow->addWidget(countBtn);
  cl->addLayout(crow);
  m_usage = label(QString(), "dim");
  m_usage->setWordWrap(true);
  cl->addWidget(m_usage);
  l->addWidget(m_countBox);
  l->addStretch(1);

  auto* buttons = new QHBoxLayout;
  m_delete = button(tr("Delete"), "trash", "ghost");
  connect(m_delete, &QPushButton::clicked, this, [this] {
    if (!m_item.id.isEmpty()) ProvisionStore::instance().remove(m_item.id);
    emit closed();
  });
  buttons->addWidget(m_delete);
  buttons->addStretch(1);
  auto* cancel = button(tr("Cancel"), QString());
  connect(cancel, &QPushButton::clicked, this, &ProvisionEditor::closed);
  buttons->addWidget(cancel);
  auto* save = button(tr("Save"), "check", "primary");
  connect(save, &QPushButton::clicked, this, [this] {
    ProvisionItem i = item();
    if (i.name.isEmpty()) i.name = tr("Item");
    ProvisionStore::instance().save(i);
    emit closed();
  });
  buttons->addWidget(save);
  outer->addLayout(buttons);
}

void ProvisionEditor::edit(const ProvisionItem& item) {
  m_item = item;
  m_name->setText(item.name);
  m_category->setCurrentIndex(m_category->findData(int(item.category)));
  m_quantity->setValue(item.quantity);
  const int unit = m_unit->findText(item.unit);
  m_unit->setCurrentIndex(unit >= 0 ? unit : 0);
  m_hasDate->setChecked(item.bestBefore.isValid());
  m_bestBefore->setEnabled(item.bestBefore.isValid());
  m_bestBefore->setDate(
      item.bestBefore.isValid()
          ? item.bestBefore
          : QDate::currentDate().addDays(
                ProvisionStore::shelfLifeDays(item.category)));
  m_planned->setValue(item.plannedPerPersonDay);
  m_min->setValue(item.minQuantity);
  m_counted->setValue(item.quantity);
  m_delete->setVisible(!item.id.isEmpty());
  m_countBox->setVisible(!item.id.isEmpty());
  QStringList uses;
  for (int k = int(item.usage.size()) - 1; k >= 0 && uses.size() < 8; --k)
    uses << QStringLiteral("%1: −%2 %3")
                .arg(item.usage[k].date.toString("dd.MM."),
                     number(item.usage[k].quantity), item.unit);
  const auto& store = ProvisionStore::instance();
  const double perDay = store.usePerDay(item, QDate::currentDate());
  QString text = uses.isEmpty() ? tr("Nothing taken yet.")
                                : tr("Taken: %1").arg(uses.join(" · "));
  if (perDay > 0)
    text += "\n" + tr("Use %1 %2/day → lasts %3 days.")
                       .arg(number(std::round(perDay * 100) / 100), item.unit)
                       .arg(item.quantity / perDay, 0, 'f', 1);
  m_usage->setText(text);
}

ProvisionItem ProvisionEditor::item() const {
  ProvisionItem i = m_item;
  i.name = m_name->text().trimmed();
  i.category = ProvisionCategory(m_category->currentData().toInt());
  i.quantity = m_quantity->value();
  i.unit = m_unit->currentText();
  i.bestBefore = m_hasDate->isChecked()
                     ? m_bestBefore->date()
                     : (i.added.isValid() ? i.added : QDate::currentDate())
                           .addDays(ProvisionStore::shelfLifeDays(i.category));
  i.plannedPerPersonDay = m_planned->value();
  i.minQuantity = m_min->value();
  return i;
}

// --------------------------------------------------------------------------
// ProvisionsPage

ProvisionsPage::ProvisionsPage(QWidget* parent) : QWidget(parent) {
  auto* l = new QVBoxLayout(this);
  l->setContentsMargins(0, 0, 0, 0);
  m_stack = new QStackedWidget;
  l->addWidget(m_stack);

  auto* listPage = new QWidget;
  auto* ll = new QVBoxLayout(listPage);
  ll->setContentsMargins(0, 0, 0, 0);
  ll->setSpacing(10);
  auto* chips = new QGridLayout;
  chips->setSpacing(6);
  const auto addChip = [&](int cat, const QString& text, const QString& icon,
                           int index) {
    auto* b = new QPushButton(text);
    b->setProperty("role", "segment");
    b->setCheckable(true);
    b->setCursor(Qt::PointingHandCursor);
    if (!icon.isEmpty()) b->setIcon(Icons::icon(icon, Theme::p().text, 16));
    b->setObjectName(QStringLiteral("chip%1").arg(cat));
    connect(b, &QPushButton::clicked, this, [this, cat] { setCategory(cat); });
    chips->addWidget(b, index / 4, index % 4);
    m_chips << b;
  };
  addChip(-1, tr("All"), "list", 0);
  int idx = 1;
  for (auto c : ProvisionStore::categories())
    addChip(int(c), ProvisionStore::categoryName(c),
            ProvisionStore::categoryIcon(c), idx++);
  ll->addLayout(chips);
  auto* actions = new QHBoxLayout;
  auto* add = button(tr("Add item"), "plus", "primary");
  connect(add, &QPushButton::clicked, this, [this] { editItem({}); });
  actions->addWidget(add);
  m_totals = label(QString(), "dim");
  actions->addWidget(m_totals, 1, Qt::AlignRight);
  ll->addLayout(actions);
  auto* content = new QWidget;
  m_list = new QVBoxLayout(content);
  m_list->setContentsMargins(0, 0, 8, 0);
  m_list->setSpacing(6);
  ll->addWidget(scrollFor(content), 1);
  m_stack->addWidget(listPage);

  m_editor = new ProvisionEditor;
  m_stack->addWidget(m_editor);
  connect(m_editor, &ProvisionEditor::closed, this,
          [this] { m_stack->setCurrentIndex(0); });
  connect(&ProvisionStore::instance(), &ProvisionStore::changed, this,
          &ProvisionsPage::rebuild);
  connect(&Theme::instance(), &Theme::changed, this, &ProvisionsPage::rebuild);
  setCategory(-1);
}

void ProvisionsPage::setCategory(int category) {
  m_category = category;
  for (int i = 0; i < m_chips.size(); ++i)
    m_chips[i]->setChecked(i == category + 1);
  m_stack->setCurrentIndex(0);
  rebuild();
}

void ProvisionsPage::editItem(const QString& id) {
  const ProvisionItem* found = ProvisionStore::instance().find(id);
  ProvisionItem item;
  if (found) {
    item = *found;
  } else {
    item.category = ProvisionCategory(m_category >= 0 ? m_category : 0);
    item.quantity = 1;
  }
  m_editor->edit(item);
  m_stack->setCurrentIndex(1);
}

void ProvisionsPage::rebuild() {
  clearLayout(m_list);
  const auto& store = ProvisionStore::instance();
  const Palette& pal = Theme::p();
  const QDate today = QDate::currentDate();
  int shown = 0;
  for (auto c : ProvisionStore::categories()) {
    if (m_category >= 0 && int(c) != m_category) continue;
    const auto items = store.inCategory(c);
    if (m_category < 0 && items.isEmpty()) continue;
    m_list->addWidget(
        label(ProvisionStore::categoryName(c).toUpper(), "section"));
    for (const ProvisionItem& i : items) {
      ++shown;
      auto* row = new ListRow;
      row->setObjectName("provision-" + i.id);
      auto* rl = new QHBoxLayout(row);
      rl->setContentsMargins(12, 6, 8, 6);
      rl->setSpacing(10);
      rl->addWidget(
          iconLabel(ProvisionStore::categoryIcon(c), pal.textDim, 20));
      auto* col = new QVBoxLayout;
      col->setSpacing(1);
      col->addWidget(label(i.name, "value"));
      QStringList sub;
      sub << QStringLiteral("%1 %2").arg(number(i.quantity), i.unit);
      QString bb;
      QColor bbColor = pal.textDim;
      if (i.bestBefore.isValid()) {
        const qint64 d = today.daysTo(i.bestBefore);
        bb = tr("best before %1").arg(i.bestBefore.toString("dd.MM."));
        if (d < 0)
          bbColor = pal.danger;
        else if (d <= 3)
          bbColor = pal.warning;
      }
      const double left = store.daysLeft(i, today);
      if (left >= 0) sub << tr("%1 days").arg(left, 0, 'f', left < 10 ? 1 : 0);
      auto* subl = label(sub.join(" · "), "dim");
      auto* sl = new QHBoxLayout;
      sl->setSpacing(6);
      sl->addWidget(subl);
      if (!bb.isEmpty()) {
        auto* bbl = label("· " + bb, "dim");
        bbl->setStyleSheet(QStringLiteral("color: %1;").arg(bbColor.name()));
        sl->addWidget(bbl);
      }
      sl->addStretch(1);
      col->addLayout(sl);
      rl->addLayout(col, 1);
      auto* take = new IconButton("minus", tr("Take one"));
      take->setObjectName("take");
      take->setEnabled(i.quantity > 0);
      const double step = (i.unit == "kg" || i.unit == "l") ? 0.5
                          : i.unit == "g"                   ? 100
                                                            : 1;
      connect(take, &QAbstractButton::clicked, this, [id = i.id, step] {
        ProvisionStore::instance().take(id, step, QDate::currentDate());
      });
      rl->addWidget(take);
      connect(row, &ListRow::clicked, this,
              [this, id = i.id] { editItem(id); });
      m_list->addWidget(row);
    }
  }
  if (shown == 0) {
    auto* empty = label(tr("No provisions recorded. Add what is on board – "
                           "with best-before date and planned use the check "
                           "shows what runs out or spoils."),
                        "dim");
    empty->setWordWrap(true);
    m_list->addWidget(empty);
  }
  m_list->addStretch(1);
  m_totals->setText(tr("%1 items").arg(shown));
}

// --------------------------------------------------------------------------
// ProvisionCheckPage

ProvisionCheckPage::ProvisionCheckPage(QWidget* parent) : QWidget(parent) {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(10);
  auto& store = ProvisionStore::instance();
  auto* trip = new QHBoxLayout;
  trip->addWidget(label(tr("Crew")));
  m_crew = new QSpinBox;
  m_crew->setRange(1, 20);
  m_crew->setValue(store.crew());
  m_crew->setAlignment(Qt::AlignRight);
  trip->addWidget(m_crew);
  trip->addSpacing(12);
  trip->addWidget(label(tr("Trip")));
  m_days = new QSpinBox;
  m_days->setRange(1, 365);
  m_days->setSuffix(tr(" days"));
  m_days->setValue(store.tripDays());
  m_days->setAlignment(Qt::AlignRight);
  trip->addWidget(m_days, 1);
  outer->addLayout(trip);
  const auto apply = [this] {
    ProvisionStore::instance().setTrip(m_crew->value(), m_days->value());
  };
  connect(m_crew, &QSpinBox::valueChanged, this, apply);
  connect(m_days, &QSpinBox::valueChanged, this, apply);

  auto* content = new QWidget;
  m_content = new QVBoxLayout(content);
  m_content->setContentsMargins(0, 0, 8, 0);
  m_content->setSpacing(8);
  outer->addWidget(scrollFor(content), 1);
  connect(&store, &ProvisionStore::changed, this, &ProvisionCheckPage::rebuild);
  connect(&Theme::instance(), &Theme::changed, this,
          &ProvisionCheckPage::rebuild);
  rebuild();
}

void ProvisionCheckPage::rebuild() {
  clearLayout(m_content);
  auto& store = ProvisionStore::instance();
  {
    const QSignalBlocker b1(m_crew), b2(m_days);
    m_crew->setValue(store.crew());
    m_days->setValue(store.tripDays());
  }
  const Palette& pal = Theme::p();
  const QDate today = QDate::currentDate();

  // Categories: items, days covered, first best-before date.
  m_content->addWidget(label(tr("OVERVIEW"), "section"));
  auto* grid = new QGridLayout;
  grid->setSpacing(6);
  int idx = 0;
  for (auto c : ProvisionStore::categories()) {
    const CategorySummary s = store.summary(c, today);
    auto* w = new QWidget;
    auto* g = new QGridLayout(w);
    g->setContentsMargins(0, 0, 0, 0);
    g->setSpacing(2);
    g->addWidget(
        iconLabel(ProvisionStore::categoryIcon(c), pal.accentBright, 18), 0, 0);
    g->addWidget(label(ProvisionStore::categoryName(c), "value"), 0, 1);
    QString text = s.items == 1 ? tr("1 item") : tr("%1 items").arg(s.items);
    if (s.daysCovered >= 0)
      text += " · " + tr("%1 d").arg(s.daysCovered, 0, 'f', 1);
    auto* t = label(text, "dim");
    if (s.daysCovered >= 0 && s.daysCovered < store.tripDays())
      t->setStyleSheet(QStringLiteral("color: %1;").arg(pal.warning.name()));
    g->addWidget(t, 1, 0, 1, 2);
    g->setColumnStretch(1, 1);
    grid->addWidget(card(w, QStringLiteral("cat%1").arg(int(c))), idx / 2,
                    idx % 2);
    ++idx;
  }
  m_content->addLayout(grid);

  // Findings.
  const auto issues = store.check(today);
  m_content->addWidget(label(tr("CHECK"), "section"));
  if (issues.isEmpty())
    m_content->addWidget(label(tr("Everything lasts for the trip."), "dim"));
  for (const auto& is : issues) {
    using K = ProvisionIssue::Kind;
    auto* row = new ListRow;
    row->setObjectName("issue");
    row->setMinimumHeight(44);
    const QColor c = is.kind == K::Expired || is.kind == K::RunsOut
                         ? pal.danger
                         : pal.warning;
    row->setHighlight(c);
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(14, 6, 10, 6);
    const char* icon = is.kind == K::RunsOut    ? "bell"
                       : is.kind == K::BelowMin ? "basket"
                                                : "close";
    rl->addWidget(iconLabel(icon, c, 18));
    auto* t = label(is.text);
    t->setWordWrap(true);
    rl->addWidget(t, 1);
    connect(row, &ListRow::clicked, this,
            [this, id = is.itemId] { emit itemRequested(id); });
    m_content->addWidget(row);
  }

  // Shopping list for the trip.
  const auto shopping = store.shoppingList(today);
  m_content->addWidget(
      label(tr("SHOPPING LIST FOR %1 DAYS").arg(store.tripDays()), "section"));
  if (shopping.isEmpty())
    m_content->addWidget(label(tr("Nothing to buy."), "dim"));
  QStringList lines;
  for (const auto& e : shopping)
    lines
        << QStringLiteral("• %1 %2 %3").arg(number(e.quantity), e.unit, e.name);
  if (!lines.isEmpty()) {
    auto* list = label(lines.join("\n"));
    list->setObjectName("shopping");
    list->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_content->addWidget(card(list, "shoppingCard"));
  }
  m_content->addStretch(1);
}
