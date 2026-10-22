#include "ui/PassagePanel.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>

#include "core/AppSettings.h"
#include "core/GeoMath.h"
#include "core/BoatConfig.h"
#include "core/Grib.h"
#include "core/NavStore.h"
#include "core/Polar.h"
#include "core/osm/OnlineOsm.h"
#include "ui/DrawerPages.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

namespace {
QLabel* label(const QString& text, const char* role = nullptr) {
  auto* l = new QLabel(text);
  if (role) l->setProperty("role", role);
  return l;
}

QString hoursText(double h) {
  const int mins = int(std::round(h * 60));
  return QStringLiteral("%1:%2 h").arg(mins / 60).arg(mins % 60, 2, 10,
                                                      QChar('0'));
}

QString timeText(const QDateTime& t) {
  const QDateTime local = t.toLocalTime();
  const QDate today = QDate::currentDate();
  QString day;
  if (local.date() == today)
    day = QObject::tr("today");
  else if (local.date() == today.addDays(1))
    day = QObject::tr("tomorrow");
  else
    day = QLocale().toString(local.date(), "ddd d.M.");
  return day + " " + local.toString("HH:mm");
}

QString compass(double deg) {
  const QString names[] = {
      QObject::tr("N"), QObject::tr("NE"), QObject::tr("E"), QObject::tr("SE"),
      QObject::tr("S"), QObject::tr("SW"), QObject::tr("W"), QObject::tr("NW")};
  return names[int(std::round(deg / 45.0)) % 8];
}

QColor modeColor(PassageLeg::Mode m) {
  const Palette& pal = Theme::p();
  switch (m) {
    case PassageLeg::Mode::Beat:
      return pal.warning;
    case PassageLeg::Mode::Run:
      return pal.success;
    case PassageLeg::Mode::Motor:
      return pal.textDim;
    default:
      return pal.accentBright;
  }
}

/** One result tile: big value, small caption. */
QWidget* tile(const QString& value, const QString& caption,
              const QColor& color = QColor()) {
  auto* w = new QWidget;
  w->setObjectName("tile");
  w->setAttribute(Qt::WA_StyledBackground);
  w->setStyleSheet(
      QStringLiteral("#tile { background: %1; border-radius: 12px; }")
          .arg(Theme::p().panelHover.name(QColor::HexArgb)));
  auto* l = new QVBoxLayout(w);
  l->setContentsMargins(12, 8, 12, 8);
  l->setSpacing(0);
  auto* v = new QLabel(value);
  v->setFont(Theme::numberFont(20, QFont::Normal));
  if (color.isValid())
    v->setStyleSheet(QStringLiteral("color: %1;").arg(color.name()));
  l->addWidget(v);
  l->addWidget(label(caption, "dim"));
  return w;
}
}  // namespace

// --------------------------------------------------------------------------

DepartureTimeline::DepartureTimeline(QWidget* parent) : QWidget(parent) {
  setMinimumHeight(86);
  setCursor(Qt::PointingHandCursor);
}

void DepartureTimeline::setOptions(const QVector<DepartureOption>& options,
                                   const QDateTime& selected) {
  m_options = options;
  m_selected = selected;
  setVisible(options.size() > 1);
  update();
}

QRectF DepartureTimeline::barRect(int i) const {
  const int n = int(m_options.size());
  const double w = double(width()) / std::max(1, n);
  double maxH = 0;
  for (const auto& o : m_options) maxH = std::max(maxH, o.hours);
  const double h = maxH > 0 ? m_options[i].hours / maxH : 0;
  const double top = 4 + (height() - 26) * (1 - h);
  return QRectF(i * w + 1, top, std::max(2.0, w - 2), height() - 22 - top);
}

void DepartureTimeline::paintEvent(QPaintEvent*) {
  if (m_options.isEmpty()) return;
  const Palette& pal = Theme::p();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  for (int i = 0; i < m_options.size(); ++i) {
    const auto& o = m_options[i];
    // Blue: much sailing; grey: mostly motor.
    QColor c = pal.textFaint;
    c = QColor::fromRgbF(
        c.redF() + (pal.accentBright.redF() - c.redF()) * o.sailShare,
        c.greenF() + (pal.accentBright.greenF() - c.greenF()) * o.sailShare,
        c.blueF() + (pal.accentBright.blueF() - c.blueF()) * o.sailShare);
    const bool selected = o.departure == m_selected;
    if (!selected) c.setAlphaF(0.75);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(barRect(i), 2, 2);
    // The share of the passage in darkness, from the bottom.
    if (o.hours > 0 && o.nightHours > 0.01) {
      QRectF night = barRect(i);
      night.setTop(night.bottom() -
                   night.height() * std::min(1.0, o.nightHours / o.hours));
      p.setBrush(QColor(0, 0, 0, 120));
      p.drawRect(night);
    }
    if (o.best || selected) {
      p.setPen(QPen(selected ? pal.text : pal.success, 2));
      p.setBrush(Qt::NoBrush);
      p.drawRoundedRect(barRect(i).adjusted(-1, -1, 1, 1), 3, 3);
    }
  }
  // Time labels at the start, the middle and the end.
  p.setPen(pal.textDim);
  p.setFont(Theme::font(10));
  const int n = int(m_options.size());
  for (int i : {0, n / 2, n - 1}) {
    const QRectF r = barRect(i);
    const QString t =
        m_options[i].departure.toLocalTime().toString("ddd HH:mm");
    const Qt::Alignment a = i == 0       ? Qt::AlignLeft
                            : i == n - 1 ? Qt::AlignRight
                                         : Qt::AlignHCenter;
    QRectF tr(r.center().x() - 60, height() - 16, 120, 16);
    if (i == 0) tr.moveLeft(0);
    if (i == n - 1) tr.moveRight(width());
    p.drawText(tr, a | Qt::AlignVCenter, t);
  }
}

void DepartureTimeline::mouseReleaseEvent(QMouseEvent* e) {
  for (int i = 0; i < m_options.size(); ++i) {
    const QRectF r = barRect(i);
    if (e->position().x() >= r.left() - 1 &&
        e->position().x() <= r.right() + 1) {
      m_selected = m_options[i].departure;
      update();
      emit departureChosen(m_selected);
      return;
    }
  }
}

// --------------------------------------------------------------------------

bool PassagePanel::gribWind(double lat, double lon, const QDateTime& t,
                            double& kn, double& fromDeg) {
  const auto file = GribManager::instance().file();
  if (!file) return false;
  const auto& steps = file->steps();
  if (steps.isEmpty()) return false;
  const auto sample = [&](int i, double& u, double& v) {
    double s, d;
    if (!file->wind(i, lat, lon, s, d) || std::isnan(s)) return false;
    // Vector pointing where the wind goes to.
    u = -s * std::sin(d * M_PI / 180);
    v = -s * std::cos(d * M_PI / 180);
    return true;
  };
  const QDateTime ut = t.toUTC();
  int i = 0;
  while (i + 1 < steps.size() && steps[i + 1].time <= ut) ++i;
  // Only a short extrapolation beyond the forecast range.
  if (ut < steps.first().time.addSecs(-5400) ||
      ut > steps.last().time.addSecs(5400))
    return false;
  double u0, v0;
  if (!sample(i, u0, v0)) return false;
  double u = u0, v = v0;
  if (i + 1 < steps.size() && ut > steps[i].time) {
    double u1, v1;
    if (sample(i + 1, u1, v1)) {
      const double f =
          double(steps[i].time.secsTo(ut)) /
          std::max<qint64>(1, steps[i].time.secsTo(steps[i + 1].time));
      u = u0 + (u1 - u0) * f;
      v = v0 + (v1 - v0) * f;
    }
  }
  kn = std::hypot(u, v);
  fromDeg = std::fmod(std::atan2(-u, -v) * 180 / M_PI + 360, 360);
  return true;
}

QString PassagePanel::modeName(PassageLeg::Mode m) {
  switch (m) {
    case PassageLeg::Mode::Beat:
      return tr("Beating");
    case PassageLeg::Mode::Run:
      return tr("Running");
    case PassageLeg::Mode::Motor:
      return tr("Motor");
    default:
      return tr("Sailing");
  }
}

PassagePanel::PassagePanel(QWidget* parent) : QWidget(parent) {
  QSettings set;
  set.beginGroup("Passage");
  auto* l = new QVBoxLayout(this);
  l->setContentsMargins(0, 4, 0, 0);
  l->setSpacing(8);
  l->addWidget(label(tr("PASSAGE"), "section"));

  // Departure and time window
  auto* dep = new QGridLayout;
  dep->setHorizontalSpacing(8);
  dep->setVerticalSpacing(6);
  dep->addWidget(label(tr("Departure"), "dim"), 0, 0);
  m_departure = new QDateTimeEdit;
  m_departure->setDisplayFormat("ddd dd.MM. HH:mm");
  m_departure->setCalendarPopup(true);
  QDateTime now = QDateTime::currentDateTime();
  now.setTime(QTime(now.time().hour(), now.time().minute() < 30 ? 30 : 0));
  if (now < QDateTime::currentDateTime()) now = now.addSecs(3600);
  m_departure->setDateTime(now);
  dep->addWidget(m_departure, 0, 1, 1, 2);
  dep->addWidget(label(tr("Window"), "dim"), 1, 0);
  m_window = new QComboBox;
  m_window->addItem(tr("Only this time"), 0);
  for (int h : {6, 12, 24, 48, 72})
    m_window->addItem(tr("Next %1 h").arg(h), h);
  m_window->setCurrentIndex(set.value("window", 0).toInt());
  dep->addWidget(m_window, 1, 1);
  m_optimize = new QComboBox;
  m_optimize->addItem(tr("Fastest"));
  m_optimize->addItem(tr("Most sailing"));
  m_optimize->addItem(tr("Daylight"));
  m_optimize->setCurrentIndex(set.value("optimize", 0).toInt());
  dep->addWidget(m_optimize, 1, 2);
  dep->setColumnStretch(1, 1);
  dep->setColumnStretch(2, 1);
  l->addLayout(dep);

  // Propulsion
  auto* prop = new QHBoxLayout;
  prop->setSpacing(6);
  m_strategy = new QButtonGroup(this);
  const QString names[] = {tr("Sail"), tr("Motor sail"), tr("Motor")};
  int defStrategy = 0;
  switch (BoatConfig::instance().sails().propulsion) {
    case SailConfig::Propulsion::MotorSailing:
      defStrategy = 1;
      break;
    case SailConfig::Propulsion::Motor:
      defStrategy = 2;
      break;
    default:
      break;
  }
  for (int i = 0; i < 3; ++i) {
    auto* b = new QPushButton(names[i]);
    b->setProperty("role", "segment");
    b->setCheckable(true);
    b->setCursor(Qt::PointingHandCursor);
    m_strategy->addButton(b, i);
    prop->addWidget(b);
  }
  m_strategy->button(set.value("strategy", defStrategy).toInt())
      ->setChecked(true);
  l->addLayout(prop);

  // Boat speeds
  auto* speeds = new QGridLayout;
  speeds->setHorizontalSpacing(8);
  speeds->addWidget(label(tr("Engine"), "dim"), 0, 0);
  m_motorSpeed = new QDoubleSpinBox;
  m_motorSpeed->setRange(1, 30);
  m_motorSpeed->setDecimals(1);
  m_motorSpeed->setSuffix(tr(" kn"));
  m_motorSpeed->setValue(set.value("motorSpeed", 6.0).toDouble());
  speeds->addWidget(m_motorSpeed, 0, 1);
  m_polarInfo = label(QString(), "dim");
  m_polarInfo->setWordWrap(true);
  speeds->addWidget(m_polarInfo, 0, 2);
  speeds->setColumnStretch(2, 1);
  speeds->addWidget(label(tr("Lane"), "dim"), 1, 0);
  m_lane = new QDoubleSpinBox;
  m_lane->setRange(0.2, 10);
  m_lane->setDecimals(1);
  m_lane->setSingleStep(0.1);
  m_lane->setPrefix(QStringLiteral("± "));
  m_lane->setSuffix(tr(" NM"));
  m_lane->setValue(set.value("laneNm", 1.0).toDouble());
  m_lane->setToolTip(
      tr("How far the boards may lead off the rhumb line "
         "when tacking or gybing"));
  speeds->addWidget(m_lane, 1, 1);
  speeds->addWidget(label(tr("tacking lane around the route"), "dim"), 1, 2);
  l->addLayout(speeds);

  // Wind
  m_windInfo = label(QString(), "dim");
  m_windInfo->setWordWrap(true);
  l->addWidget(m_windInfo);
  auto* windRow = new QHBoxLayout;
  windRow->addWidget(label(tr("Wind from"), "dim"));
  m_windDir = new QSpinBox;
  m_windDir->setRange(0, 359);
  m_windDir->setWrapping(true);
  m_windDir->setSuffix(QStringLiteral("°"));
  m_windKn = new QDoubleSpinBox;
  m_windKn->setRange(0, 60);
  m_windKn->setDecimals(0);
  m_windKn->setSuffix(tr(" kn"));
  // Current true wind as a start value, if instruments deliver it.
  const auto& env = NavStore::instance().environment();
  const bool live = !std::isnan(env.twd) && !std::isnan(env.tws);
  m_windDir->setValue(
      int(live ? env.twd : set.value("windFrom", 270).toDouble()) % 360);
  m_windKn->setValue(live ? env.tws : set.value("windKn", 12).toDouble());
  windRow->addWidget(m_windDir, 1);
  windRow->addWidget(m_windKn, 1);
  l->addLayout(windRow);

  // Weather models: every source over every departure of the window.
  auto* modelHead = new QHBoxLayout;
  modelHead->addWidget(label(tr("WEATHER MODELS"), "section"), 1);
  auto* load = new QPushButton(tr("Load"));
  load->setProperty("role", "ghost");
  load->setIcon(Icons::icon("refresh", Theme::p().textDim, 16));
  load->setToolTip(
      tr("Load the forecasts of ICON, GFS, ECMWF and "
         "Météo-France for this route (Open-Meteo)"));
  load->setCursor(Qt::PointingHandCursor);
  connect(load, &QPushButton::clicked, this, &PassagePanel::fetchForecasts);
  modelHead->addWidget(load);
  l->addLayout(modelHead);
  m_modelInfo = label(QString(), "dim");
  m_modelInfo->setWordWrap(true);
  l->addWidget(m_modelInfo);
  m_matrix = new ModelMatrix;
  l->addWidget(m_matrix);
  connect(m_matrix, &ModelMatrix::picked, this,
          [this](const QString& source, const QDateTime& d) {
            m_sourceId = source;
            m_chosen = d;
            recalculate();
          });

  // Results
  m_results = new QWidget;
  auto* rl = new QVBoxLayout(m_results);
  rl->setContentsMargins(0, 4, 0, 0);
  rl->setSpacing(8);
  auto* tiles = new QWidget;
  m_tiles = new QGridLayout(tiles);
  m_tiles->setContentsMargins(0, 0, 0, 0);
  m_tiles->setSpacing(6);
  rl->addWidget(tiles);
  m_warnings = label(QString());
  m_warnings->setWordWrap(true);
  rl->addWidget(m_warnings);
  m_bestInfo = label(QString(), "dim");
  m_bestInfo->setWordWrap(true);
  rl->addWidget(m_bestInfo);
  m_timeline = new DepartureTimeline;
  rl->addWidget(m_timeline);
  rl->addWidget(label(tr("LEGS"), "section"));
  m_legs = new QVBoxLayout;
  m_legs->setSpacing(4);
  rl->addLayout(m_legs);

  // Sail plan: boards with sails and trim, tacks, gybes, sail changes.
  auto* sailHead = new QHBoxLayout;
  sailHead->addWidget(label(tr("SAIL PLAN"), "section"), 1);
  m_applySails = new QPushButton(tr("Set first sails"));
  m_applySails->setProperty("role", "ghost");
  m_applySails->setCursor(Qt::PointingHandCursor);
  m_applySails->setToolTip(
      tr("Takes the sails of the first board into the sail configuration"));
  connect(m_applySails, &QPushButton::clicked, this, [this] {
    for (const SailBoard& b : m_sail.boards)
      if (!b.motor) {
        SailConfig c = b.advice.sails;
        c.propulsion = BoatConfig::instance().sails().propulsion;
        BoatConfig::instance().setSails(c);
        return;
      }
  });
  sailHead->addWidget(m_applySails);
  rl->addLayout(sailHead);
  m_sailSummary = label(QString(), "dim");
  m_sailSummary->setWordWrap(true);
  rl->addWidget(m_sailSummary);
  m_sailList = new QVBoxLayout;
  m_sailList->setSpacing(4);
  rl->addLayout(m_sailList);
  l->addWidget(m_results);

  m_debounce.setSingleShot(true);
  m_debounce.setInterval(150);
  connect(&m_debounce, &QTimer::timeout, this, &PassagePanel::recalculate);
  const auto changed = [this] {
    m_chosen = {};
    m_debounce.start();
  };
  connect(m_departure, &QDateTimeEdit::dateTimeChanged, this, changed);
  connect(m_window, &QComboBox::currentIndexChanged, this, changed);
  connect(m_optimize, &QComboBox::currentIndexChanged, this, changed);
  connect(m_strategy, &QButtonGroup::idClicked, this, changed);
  connect(&PolarStore::instance(), &PolarStore::changed, this, changed);
  for (auto* s : {m_motorSpeed, m_windKn})
    connect(s, &QDoubleSpinBox::valueChanged, this, changed);
  connect(m_windDir, &QSpinBox::valueChanged, this, changed);
  connect(m_lane, &QDoubleSpinBox::valueChanged, this, changed);
  connect(m_timeline, &DepartureTimeline::departureChosen, this,
          [this](const QDateTime& d) {
            m_chosen = d;
            recalculate();
          });
  connect(&GribManager::instance(), &GribManager::fileChanged, this, changed);
  connect(&BoatConfig::instance(), &BoatConfig::changed, this, changed);
  hide();
}

double PassagePanel::laneNm() const { return m_lane->value(); }

void PassagePanel::showSailPlan() {
  const Palette& pal = Theme::p();
  const auto& cfg = AppSettings::instance();
  const SailingPlan& sp = m_sail;
  while (QLayoutItem* it = m_sailList->takeAt(0)) {
    if (QWidget* w = it->widget()) {
      w->hide();
      w->deleteLater();
    }
    delete it;
  }
  QStringList sum;
  sum << tr("%n board(s)", nullptr, int(sp.boards.size()));
  if (sp.tacks) sum << tr("%n tack(s)", nullptr, sp.tacks);
  if (sp.gybes) sum << tr("%n gybe(s)", nullptr, sp.gybes);
  if (sp.sailChanges) sum << tr("%n sail change(s)", nullptr, sp.sailChanges);
  sum << cfg.distanceText(sp.sailedNm) + " · " + hoursText(sp.hours);
  QString text = sum.join(" · ");
  if (!sp.warnings.isEmpty()) text += "\n⚠ " + sp.warnings.join("\n⚠ ");
  m_sailSummary->setText(text);
  m_applySails->setEnabled(
      std::any_of(sp.boards.begin(), sp.boards.end(),
                  [](const SailBoard& b) { return !b.motor; }));

  // Boards in order, with the tacks/gybes and sail changes before them.
  constexpr int kMaxRows = 60;
  int rows = 0, ev = 0;
  const auto eventRow = [&](const SailEvent& e) {
    const bool turn =
        e.kind == SailEvent::Kind::Tack || e.kind == SailEvent::Kind::Gybe;
    const QColor c = turn                                    ? pal.warning
                     : e.kind == SailEvent::Kind::SailChange ? pal.accentBright
                                                             : pal.textDim;
    auto* l = new QLabel(
        QStringLiteral("<span style='color:%1'>%2 <b>%3</b> %4</span>"
                       "<span style='color:%5'> · %6 %7%8</span>")
            .arg(
                c.name(), turn ? QStringLiteral("↻") : QStringLiteral("⛵"),
                SailEvent::kindName(e.kind),
                e.time.toLocalTime().toString("HH:mm"), pal.textDim.name(),
                geo::formatLat(e.pos.y()), geo::formatLon(e.pos.x()),
                e.text.isEmpty() ? QString() : " · " + e.text.toHtmlEscaped()));
    l->setObjectName(turn ? "sailTurn" : "sailEvent");
    l->setTextFormat(Qt::RichText);
    l->setWordWrap(true);
    m_sailList->addWidget(l);
  };
  for (const SailBoard& b : sp.boards) {
    // Events up to the start of this board.
    while (ev < sp.events.size() && sp.events[ev].time <= b.start) {
      const SailEvent& e = sp.events[ev++];
      if (e.kind == SailEvent::Kind::Tack || e.kind == SailEvent::Kind::Gybe ||
          e.kind == SailEvent::Kind::SailChange)
        eventRow(e);
    }
    if (++rows > kMaxRows) {
      m_sailList->addWidget(label(
          tr("… %n more board(s)", nullptr, int(sp.boards.size()) - kMaxRows),
          "dim"));
      break;
    }
    auto* row = new ListRow;
    row->setObjectName("sailBoard");
    row->setMinimumHeight(50);
    if (!b.motor) row->setHighlight(b.twa > 0 ? pal.success : pal.danger);
    auto* rl = new QVBoxLayout(row);
    rl->setContentsMargins(10, 6, 10, 6);
    rl->setSpacing(2);
    const QString tack =
        b.motor ? tr("engine")
                : (b.twa > 0 ? tr("starboard tack") : tr("port tack"));
    auto* head = new QLabel(
        QStringLiteral("<b>%1</b> &nbsp;%2° · %3 · %4 · %5 kn<br>"
                       "<span style='color:%6'>%7 · TWA %8° · %9 kn true · "
                       "%10</span>")
            .arg(b.start.toLocalTime().toString("HH:mm"))
            .arg(int(std::round(b.headingDeg)) % 360, 3, 10, QChar('0'))
            .arg(tack, cfg.distanceText(b.distanceNm))
            .arg(b.speedKn, 0, 'f', 1)
            .arg(pal.textDim.name(),
                 b.motor ? tr("Under engine") : b.advice.trim.pointOfSail)
            .arg(int(std::round(std::abs(b.twa))))
            .arg(int(std::round(b.tws)))
            .arg(b.motor ? QString() : b.advice.summary().toHtmlEscaped()));
    head->setTextFormat(Qt::RichText);
    head->setWordWrap(true);
    head->setAttribute(Qt::WA_TransparentForMouseEvents);
    rl->addWidget(head);
    auto* trim = label(QString(), "dim");
    trim->setObjectName("trim");
    trim->setWordWrap(true);
    trim->setAttribute(Qt::WA_TransparentForMouseEvents);
    if (!b.motor) {
      QStringList lines;
      lines << tr("Boom about %1° off the centreline")
                   .arg(int(std::round(b.advice.trim.boomDeg)));
      lines += b.advice.trim.lines();
      if (!b.advice.note.isEmpty()) lines.prepend(b.advice.note);
      trim->setText("• " + lines.join("\n• "));
    }
    trim->hide();
    rl->addWidget(trim);
    if (!b.motor)
      connect(row, &ListRow::clicked, trim,
              [trim] { trim->setVisible(trim->isHidden()); });
    m_sailList->addWidget(row);
  }
}

PassageSettings PassagePanel::settings() const {
  PassageSettings s;
  s.departure = m_departure->dateTime();
  const int windowH = m_window->currentData().toInt();
  if (windowH > 0) s.latestDeparture = s.departure.addSecs(windowH * 3600);
  s.windowStepMin = windowH > 24 ? 120 : windowH > 12 ? 60 : 30;
  s.optimize = PassageSettings::Optimize(m_optimize->currentIndex());
  s.strategy = PassageSettings::Strategy(m_strategy->checkedId());
  s.motorSpeedKn = m_motorSpeed->value();
  const Polar& polar = PolarStore::instance().polar();
  s.polar = std::make_shared<Polar>(polar);
  s.hullSpeedKn = polar.maxSpeed();
  const auto& boat = BoatConfig::instance();
  s.fuelLph = boat.consumptionLph;
  s.sailAreaFactor = boat.sails().anySail() ? boat.sails().areaFactor() : 1.0;
  s.downwindSail = boat.sails().downwind != SailConfig::Downwind::None;
  s.manualWindFromDeg = m_windDir->value();
  s.manualWindKn = m_windKn->value();
  return s;
}

void PassagePanel::saveSettings() const {
  QSettings set;
  set.beginGroup("Passage");
  set.setValue("window", m_window->currentIndex());
  set.setValue("optimize", m_optimize->currentIndex());
  set.setValue("strategy", m_strategy->checkedId());
  set.setValue("motorSpeed", m_motorSpeed->value());
  set.setValue("laneNm", m_lane->value());
  set.setValue("windFrom", m_windDir->value());
  set.setValue("windKn", m_windKn->value());
}

void PassagePanel::setRoute(const QVector<QPointF>& route) {
  const bool same = route == m_route;
  m_route = route;
  m_chosen = {};
  setVisible(route.size() >= 2);
  if (route.size() < 2) return;
  if (!same) {
    // New route: forecasts of the old one no longer apply. Load them for
    // the initial planning while online.
    m_forecasts.clear();
    if (OnlineOsm::instance().isEnabled() && OnlineOsm::instance().isOnline())
      fetchForecasts();
  }
  recalculate();
}

QString PassagePanel::sourceName(const QString& id) const {
  if (id == "manual") return tr("Manual");
  if (id == "grib") return tr("GRIB file");
  return WeatherModels::displayName(id);
}

WindSource PassagePanel::windFor(const QString& id) const {
  if (id == "grib") return gribWind;
  const auto it = m_forecasts.constFind(id);
  if (it == m_forecasts.constEnd()) return {};
  const ModelForecast f = *it;
  return [f](double lat, double lon, const QDateTime& t, double& kn,
             double& from) { return f.wind(lat, lon, t, kn, from); };
}

void PassagePanel::recalculate() {
  if (m_route.size() < 2) return;
  saveSettings();
  const Polar& polar = PolarStore::instance().polar();
  m_polarInfo->setText(tr("Polar: %1 (max. %2 kn)")
                           .arg(polar.name)
                           .arg(polar.maxSpeed(), 0, 'f', 1));

  // All wind sources over the departure window.
  QStringList ids;
  for (const auto& m : WeatherModels::available())
    if (m_forecasts.contains(m.id)) ids << m.id;
  if (GribManager::instance().file()) ids << "grib";
  ids << "manual";
  if (!ids.contains(m_sourceId)) m_sourceId = ids.first();

  const PassageSettings s = settings();
  QVector<SourceResult> rows;
  QVector<DepartureOption> selectedOptions;
  for (const QString& id : ids) {
    SourceResult r;
    r.id = id;
    r.name = sourceName(id);
    r.best = PassagePlanner::planWindow(m_route, s, windFor(id), &r.options);
    // A source that leaves gaps (manual wind used) is shown dimmed.
    r.usable = id == "manual" || !r.best.manualUsed;
    if (id == m_sourceId) {
      m_plan = r.best;
      selectedOptions = r.options;
    }
    rows << r;
  }
  if (m_chosen.isValid()) {
    PassageSettings one = s;
    one.departure = m_chosen;
    one.latestDeparture = {};
    m_plan = PassagePlanner::plan(m_route, one, windFor(m_sourceId));
  }
  m_matrix->setResults(rows, m_sourceId, m_plan.departure);

  // Agreement of the models on the selected departure.
  double minH = 1e9, maxH = 0;
  int models = 0;
  for (const SourceResult& r : rows) {
    if (r.id == "manual" || r.id == "grib" || !r.usable) continue;
    PassageSettings one = s;
    one.departure = m_plan.departure;
    one.latestDeparture = {};
    const PassagePlan p = PassagePlanner::plan(m_route, one, windFor(r.id));
    minH = std::min(minH, p.hours);
    maxH = std::max(maxH, p.hours);
    ++models;
  }
  QString info;
  if (m_forecasts.isEmpty()) {
    info = tr("No model forecasts loaded yet.");
  } else {
    QDateTime fetched;
    for (const auto& f : m_forecasts)
      if (!fetched.isValid() || f.fetched < fetched) fetched = f.fetched;
    info = tr("%n model(s), loaded %1%2", nullptr, int(m_forecasts.size()))
               .arg(QLocale().toString(fetched.toLocalTime(),
                                       QLocale::ShortFormat),
                    m_forecastsOffline ? tr(" (stored offline)") : QString());
    if (models >= 2)
      info += tr(" · for this departure the models differ by %1")
                  .arg(hoursText(maxH - minH));
  }
  m_modelInfo->setText(info);

  m_windInfo->setText(
      m_sourceId == "manual"
          ? tr("Constant wind as entered below.")
          : tr("Wind from %1; the values below are used outside of it.")
                .arg(sourceName(m_sourceId)));
  // Boards, tacks and sails for the shown departure and wind source.
  PassageSettings one = s;
  one.departure = m_plan.departure;
  one.latestDeparture = {};
  m_sail = SailPlanner::plan(m_route, one, windFor(m_sourceId),
                             SailInventory::load(), laneNm(), m_water);
  // The tacks and gybes of the sail plan are the ones to count.
  m_plan.tacks = m_plan.gybes = 0;
  for (PassageLeg& leg : m_plan.legs) leg.tacks = leg.gybes = 0;
  for (int i = 1; i < m_sail.boards.size(); ++i) {
    const SailBoard& a = m_sail.boards[i - 1];
    const SailBoard& b = m_sail.boards[i];
    if (a.leg != b.leg || a.motor || b.motor || a.mode != b.mode ||
        (a.twa > 0) == (b.twa > 0) || b.leg >= m_plan.legs.size())
      continue;
    if (b.mode == PassageLeg::Mode::Beat) {
      ++m_plan.legs[b.leg].tacks;
      ++m_plan.tacks;
    } else if (b.mode == PassageLeg::Mode::Run) {
      ++m_plan.legs[b.leg].gybes;
      ++m_plan.gybes;
    }
  }
  showResults(selectedOptions);
  showSailPlan();
  // The matrix shows the departures of all sources; the bar timeline is
  // only needed without models.
  m_timeline->setVisible(rows.size() == 1 && selectedOptions.size() > 1);
  emit planChanged();
}

void PassagePanel::setForecasts(const QHash<QString, ModelForecast>& f,
                                bool offline) {
  m_forecasts = f;
  m_forecastsOffline = offline;
  if (!f.isEmpty() && (m_sourceId == "manual" || !f.contains(m_sourceId)))
    m_sourceId =
        f.contains("icon_seamless") ? "icon_seamless" : f.begin().key();
  m_debounce.start();
}

void PassagePanel::fetchForecasts() {
  if (m_route.size() < 2) return;
  m_modelInfo->setText(tr("Loading forecasts of the weather models…"));
  QStringList models;
  for (const auto& m : WeatherModels::available()) models << m.id;
  QPointer<PassagePanel> self(this);
  const QVector<QPointF> route = m_route;
  WeatherModels::instance().fetch(
      WeatherModels::samplePoints(route), models,
      [self, route](const QHash<QString, ModelForecast>& f,
                    const QString& err) {
        if (!self || self->m_route != route) return;
        if (!err.isEmpty() && f.isEmpty()) {
          self->m_modelInfo->setText(
              tr("Weather models not available: %1").arg(err));
          return;
        }
        self->setForecasts(f, false);
      });
}

QJsonObject PassagePanel::saveState() const {
  return QJsonObject{
      {"departure", m_departure->dateTime().toUTC().toString(Qt::ISODate)},
      {"window", m_window->currentIndex()},
      {"optimize", m_optimize->currentIndex()},
      {"strategy", m_strategy->checkedId()},
      {"motorSpeed", m_motorSpeed->value()},
      {"windFrom", m_windDir->value()},
      {"windKn", m_windKn->value()},
      {"source", m_sourceId},
      {"chosen", m_chosen.toUTC().toString(Qt::ISODate)}};
}

void PassagePanel::restoreState(const QJsonObject& o) {
  const QDateTime dep =
      QDateTime::fromString(o.value("departure").toString(), Qt::ISODate);
  if (dep.isValid()) m_departure->setDateTime(dep.toLocalTime());
  m_window->setCurrentIndex(o.value("window").toInt(m_window->currentIndex()));
  m_optimize->setCurrentIndex(
      o.value("optimize").toInt(m_optimize->currentIndex()));
  if (auto* b = m_strategy->button(o.value("strategy").toInt(0)))
    b->setChecked(true);
  m_motorSpeed->setValue(o.value("motorSpeed").toDouble(m_motorSpeed->value()));
  m_windDir->setValue(o.value("windFrom").toInt(m_windDir->value()));
  m_windKn->setValue(o.value("windKn").toDouble(m_windKn->value()));
  m_sourceId = o.value("source").toString(m_sourceId);
  m_chosen = QDateTime::fromString(o.value("chosen").toString(), Qt::ISODate);
  m_debounce.start();
}

void PassagePanel::showResults(const QVector<DepartureOption>& options) {
  const Palette& pal = Theme::p();
  const auto& cfg = AppSettings::instance();
  const PassagePlan& p = m_plan;
  while (QLayoutItem* it = m_tiles->takeAt(0)) {
    delete it->widget();
    delete it;
  }
  const int sailPct = int(std::round(p.sailShare() * 100));
  m_tiles->addWidget(tile(hoursText(p.hours), tr("Duration")), 0, 0);
  m_tiles->addWidget(tile(timeText(p.arrival), tr("Arrival")), 0, 1);
  m_tiles->addWidget(tile(QStringLiteral("%1 %").arg(sailPct),
                          tr("Sail · %1 motor").arg(hoursText(p.motorHours)),
                          sailPct >= 50 ? pal.accentBright : QColor()),
                     1, 0);
  m_tiles->addWidget(tile(QStringLiteral("%1 / %2").arg(p.tacks).arg(p.gybes),
                          tr("Tacks / gybes")),
                     1, 1);
  m_tiles->addWidget(
      tile(cfg.distanceText(p.sailedNm),
           tr("Sailed (%1 direct)").arg(cfg.distanceText(p.distanceNm))),
      2, 0);
  m_tiles->addWidget(
      tile(QStringLiteral("%1 l").arg(p.fuelLiters, 0, 'f', 1),
           p.nightHours > 0.05
               ? tr("Diesel · %1 at night").arg(hoursText(p.nightHours))
               : tr("Diesel")),
      2, 1);

  QStringList warn = p.warnings;
  const auto& boat = BoatConfig::instance();
  const double fuelLeft = boat.tankLiters * boat.manualFuelPct / 100;
  if (p.fuelLiters > fuelLeft * 0.8)
    warn << tr("Fuel is getting short: %1 l needed, about %2 l on board")
                .arg(p.fuelLiters, 0, 'f', 0)
                .arg(fuelLeft, 0, 'f', 0);
  m_warnings->setVisible(!warn.isEmpty());
  m_warnings->setText("⚠ " + warn.join("\n⚠ "));
  m_warnings->setStyleSheet(
      QStringLiteral("color: %1;").arg(pal.warning.name()));

  QString info = tr("Departure %1").arg(timeText(p.departure));
  if (p.sunset.isValid())
    info += tr(" · sunset %1").arg(p.sunset.toLocalTime().toString("HH:mm"));
  if (options.size() > 1)
    info += m_chosen.isValid() ? tr(" · chosen in the window below")
                               : tr(" · best of %n departure(s) in the window",
                                    nullptr, int(options.size()));
  m_bestInfo->setText(info);
  m_timeline->setOptions(options, p.departure);

  while (QLayoutItem* it = m_legs->takeAt(0)) {
    delete it->widget();
    delete it;
  }
  for (const PassageLeg& leg : p.legs) {
    QString extra;
    if (leg.tacks) extra += tr(" · %n tack(s)", nullptr, leg.tacks);
    if (leg.gybes) extra += tr(" · %n gybe(s)", nullptr, leg.gybes);
    auto* row = new QLabel(
        QStringLiteral("<span style='color:%1'>●</span> <b>%2</b> &nbsp;%3° · "
                       "%4 · <span style='color:%1'>%5</span>%6<br>"
                       "<span style='color:%7'>%8 → %9 · %10 · wind %11 "
                       "%12 kn%13</span>")
            .arg(modeColor(leg.mode).name())
            .arg(leg.index + 1)
            .arg(int(std::round(leg.courseDeg)) % 360, 3, 10, QChar('0'))
            .arg(cfg.distanceText(leg.distanceNm), modeName(leg.mode), extra,
                 pal.textDim.name(), leg.start.toLocalTime().toString("HH:mm"),
                 leg.end.toLocalTime().toString("HH:mm"))
            .arg(hoursText(leg.hours), compass(leg.windFromDeg))
            .arg(int(std::round(leg.windKn)))
            .arg(
                QStringLiteral(" · TWA %1°").arg(int(std::round(leg.twaDeg)))));
    row->setTextFormat(Qt::RichText);
    row->setWordWrap(true);
    m_legs->addWidget(row);
  }
}

// --------------------------------------------------------------------------

namespace {
constexpr int kNameWidth = 96;
constexpr int kRowHeight = 28;
constexpr int kHeader = 18;
}  // namespace

ModelMatrix::ModelMatrix(QWidget* parent) : QWidget(parent) {
  setCursor(Qt::PointingHandCursor);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

QSize ModelMatrix::sizeHint() const {
  return {360, kHeader + int(m_rows.size()) * kRowHeight + 4};
}

int ModelMatrix::columns() const {
  int n = 0;
  for (const auto& r : m_rows) n = std::max(n, int(r.options.size()));
  return n;
}

QRectF ModelMatrix::cell(int row, int col) const {
  const double w = double(width() - kNameWidth) / std::max(1, columns());
  return QRectF(kNameWidth + col * w + 1, kHeader + row * kRowHeight + 2,
                std::max(2.0, w - 2), kRowHeight - 4);
}

void ModelMatrix::setResults(const QVector<SourceResult>& rows,
                             const QString& selected,
                             const QDateTime& departure) {
  m_rows = rows;
  m_selected = selected;
  m_departure = departure;
  setFixedHeight(sizeHint().height());
  update();
}

void ModelMatrix::paintEvent(QPaintEvent*) {
  if (m_rows.isEmpty()) return;
  const Palette& pal = Theme::p();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const int cols = columns();
  // Header: departure times at the first, middle and last column.
  p.setFont(Theme::font(10));
  p.setPen(pal.textDim);
  const auto& opts = m_rows.first().options;
  for (int c : {0, cols / 2, cols - 1}) {
    if (c < 0 || c >= opts.size()) continue;
    const QRectF r = cell(0, c);
    const Qt::Alignment a = cols == 1       ? Qt::AlignLeft
                            : c == 0        ? Qt::AlignLeft
                            : c == cols - 1 ? Qt::AlignRight
                                            : Qt::AlignHCenter;
    QRectF t(r.center().x() - 60, 0, 120, kHeader - 2);
    if (c == 0) t.moveLeft(r.left());
    if (c == cols - 1 && cols > 1) t.moveRight(r.right());
    p.drawText(t, a | Qt::AlignVCenter,
               opts[c].departure.toLocalTime().toString("ddd HH:mm"));
  }
  for (int row = 0; row < m_rows.size(); ++row) {
    const SourceResult& r = m_rows[row];
    const bool sel = r.id == m_selected;
    p.setFont(Theme::font(12, sel ? QFont::DemiBold : QFont::Normal));
    p.setPen(sel ? pal.text : pal.textDim);
    p.drawText(
        QRectF(0, kHeader + row * kRowHeight, kNameWidth - 6, kRowHeight),
        Qt::AlignLeft | Qt::AlignVCenter, r.name);
    for (int c = 0; c < r.options.size(); ++c) {
      const DepartureOption& o = r.options[c];
      const QRectF rc = cell(row, c);
      const QColor faint = pal.panelPressed, accent = pal.accent;
      const double f = o.sailShare;
      QColor col = QColor::fromRgbF(
          faint.redF() + (accent.redF() - faint.redF()) * f,
          faint.greenF() + (accent.greenF() - faint.greenF()) * f,
          faint.blueF() + (accent.blueF() - faint.blueF()) * f);
      col.setAlphaF(r.usable ? 1.0 : 0.35);
      p.setPen(Qt::NoPen);
      p.setBrush(col);
      p.drawRoundedRect(rc, 4, 4);
      if (o.nightHours > 0.01 && o.hours > 0) {
        QRectF n = rc;
        n.setTop(n.bottom() - 4);
        n.setWidth(rc.width() * std::min(1.0, o.nightHours / o.hours));
        p.setBrush(QColor(0, 0, 0, 140));
        p.drawRect(n);
      }
      const bool picked = sel && o.departure == m_departure;
      if (o.best || picked) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(picked ? pal.text : pal.success, picked ? 2.2 : 1.6));
        p.drawRoundedRect(rc.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
      }
      if (rc.width() >= 36) {
        const int mins = int(std::round(o.hours * 60));
        p.setPen(f > 0.5 ? QColor(Qt::white) : pal.text);
        p.setFont(Theme::font(10, QFont::DemiBold));
        p.drawText(rc, Qt::AlignCenter,
                   rc.width() >= 80
                       ? QStringLiteral("%1:%2 h · %3 %")
                             .arg(mins / 60)
                             .arg(mins % 60, 2, 10, QChar('0'))
                             .arg(int(std::round(f * 100)))
                       : QStringLiteral("%1:%2").arg(mins / 60).arg(
                             mins % 60, 2, 10, QChar('0')));
      }
    }
  }
}

void ModelMatrix::mouseReleaseEvent(QMouseEvent* e) {
  for (int row = 0; row < m_rows.size(); ++row)
    for (int c = 0; c < m_rows[row].options.size(); ++c)
      if (cell(row, c).adjusted(-1, -2, 1, 2).contains(e->position())) {
        m_selected = m_rows[row].id;
        m_departure = m_rows[row].options[c].departure;
        update();
        emit picked(m_selected, m_departure);
        return;
      }
}
