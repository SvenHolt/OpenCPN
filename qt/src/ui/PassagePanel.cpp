#include "ui/PassagePanel.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QJsonArray>
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
#include "core/Tides.h"
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
  m_windDir->setObjectName("windDir");
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

  // Tides and currents: rivers, tidal streams, marine forecast, levels.
  auto* tideHead = new QHBoxLayout;
  tideHead->addWidget(label(tr("TIDES & CURRENTS"), "section"), 1);
  auto* loadTides = new QPushButton(tr("Load"));
  loadTides->setProperty("role", "ghost");
  loadTides->setIcon(Icons::icon("refresh", Theme::p().textDim, 16));
  loadTides->setToolTip(
      tr("Load the current and sea level forecast (Open-Meteo), the rivers "
         "and canals (OpenStreetMap) and the gauges (PEGELONLINE)"));
  loadTides->setCursor(Qt::PointingHandCursor);
  connect(loadTides, &QPushButton::clicked, this, &PassagePanel::fetchCurrents);
  tideHead->addWidget(loadTides);
  l->addLayout(tideHead);
  auto* curRow = new QHBoxLayout;
  curRow->setSpacing(6);
  curRow->addWidget(label(tr("Current"), "dim"));
  m_currentMode = new QComboBox;
  m_currentMode->setObjectName("currentMode");
  m_currentMode->addItem(tr("Automatic"));
  m_currentMode->addItem(tr("Manual"));
  m_currentMode->addItem(tr("Off"));
  m_currentMode->setCurrentIndex(set.value("currentMode", 0).toInt());
  curRow->addWidget(m_currentMode, 1);
  m_manualCurrent = new QWidget;
  auto* man = new QHBoxLayout(m_manualCurrent);
  man->setContentsMargins(0, 0, 0, 0);
  man->setSpacing(6);
  man->addWidget(label(tr("to"), "dim"));
  m_currentSet = new QSpinBox;
  m_currentSet->setObjectName("currentSet");
  m_currentSet->setRange(0, 359);
  m_currentSet->setWrapping(true);
  m_currentSet->setSuffix(QStringLiteral("°"));
  m_currentSet->setValue(set.value("currentSet", 90).toInt());
  m_currentSet->setToolTip(tr("Set: the direction the current flows to"));
  man->addWidget(m_currentSet, 1);
  m_currentDrift = new QDoubleSpinBox;
  m_currentDrift->setObjectName("currentDrift");
  m_currentDrift->setRange(0, 10);
  m_currentDrift->setDecimals(1);
  m_currentDrift->setSingleStep(0.1);
  m_currentDrift->setSuffix(tr(" kn"));
  m_currentDrift->setValue(set.value("currentDrift", 1.0).toDouble());
  man->addWidget(m_currentDrift, 1);
  curRow->addWidget(m_manualCurrent, 2);
  l->addLayout(curRow);
  m_currentInfo = label(QString(), "dim");
  m_currentInfo->setObjectName("currentInfo");
  m_currentInfo->setWordWrap(true);
  l->addWidget(m_currentInfo);
  m_riverList = new QVBoxLayout;
  m_riverList->setSpacing(4);
  l->addLayout(m_riverList);
  auto* levels = new QGridLayout;
  levels->setHorizontalSpacing(8);
  const auto levelSpin = [&](const char* key) {
    auto* sp = new QDoubleSpinBox;
    sp->setRange(-1, 15);
    sp->setDecimals(1);
    sp->setSingleStep(0.1);
    sp->setSuffix(tr(" m"));
    sp->setSpecialValueText(tr("any"));
    sp->setValue(set.value(key, -1.0).toDouble());
    sp->setToolTip(
        tr("Minimum water level (above chart datum) for a tidal harbour, a "
           "bar or a bridge; the departure window avoids lower water"));
    return sp;
  };
  levels->addWidget(label(tr("Min. level"), "dim"), 0, 0);
  m_minDepLevel = levelSpin("minDepLevel");
  m_minDepLevel->setObjectName("minDepLevel");
  levels->addWidget(m_minDepLevel, 0, 1);
  levels->addWidget(label(tr("at departure"), "dim"), 0, 2);
  m_minArrLevel = levelSpin("minArrLevel");
  m_minArrLevel->setObjectName("minArrLevel");
  levels->addWidget(m_minArrLevel, 1, 1);
  levels->addWidget(label(tr("at arrival"), "dim"), 1, 2);
  levels->setColumnStretch(2, 1);
  l->addLayout(levels);
  m_levelInfo = label(QString(), "dim");
  m_levelInfo->setObjectName("levelInfo");
  m_levelInfo->setTextFormat(Qt::RichText);
  m_levelInfo->setWordWrap(true);
  l->addWidget(m_levelInfo);

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
  connect(m_currentMode, &QComboBox::currentIndexChanged, this, changed);
  connect(m_currentSet, &QSpinBox::valueChanged, this, changed);
  for (auto* s : {m_currentDrift, m_minDepLevel, m_minArrLevel})
    connect(s, &QDoubleSpinBox::valueChanged, this, changed);
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
    if (sp.currentUsed && b.currentDriftKn >= 0.05) {
      auto* cur =
          label(tr("Current to %1° %2 kn · over ground %3° %4 kn")
                    .arg(int(std::round(b.currentSetDeg)) % 360)
                    .arg(b.currentDriftKn, 0, 'f', 1)
                    .arg(int(std::round(b.cogDeg)) % 360, 3, 10, QChar('0'))
                    .arg(b.sogKn, 0, 'f', 1),
                "dim");
      cur->setObjectName("boardCurrent");
      cur->setAttribute(Qt::WA_TransparentForMouseEvents);
      rl->addWidget(cur);
    }
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
  const CurrentModel model = m_currents;
  s.levelAt = [model](double lat, double lon, const QDateTime& t) {
    return model.level(lat, lon, t).levelM;
  };
  const double nan = std::numeric_limits<double>::quiet_NaN();
  s.minDepartureLevelM = m_minDepLevel->value() > m_minDepLevel->minimum()
                             ? m_minDepLevel->value()
                             : nan;
  s.minArrivalLevelM = m_minArrLevel->value() > m_minArrLevel->minimum()
                           ? m_minArrLevel->value()
                           : nan;
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
  set.setValue("currentMode", m_currentMode->currentIndex());
  set.setValue("currentSet", m_currentSet->value());
  set.setValue("currentDrift", m_currentDrift->value());
  set.setValue("minDepLevel", m_minDepLevel->value());
  set.setValue("minArrLevel", m_minArrLevel->value());
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
    m_currents.marine = {};
    m_currents.rivers = {};
    m_gaugesFrom.clear();
    m_gaugesTo.clear();
    if (OnlineOsm::instance().isEnabled() && OnlineOsm::instance().isOnline()) {
      fetchForecasts();
      fetchCurrents();
    }
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

  m_currents.mode = CurrentModel::Mode(m_currentMode->currentIndex());
  m_currents.manualSetDeg = m_currentSet->value();
  m_currents.manualDriftKn = m_currentDrift->value();
  m_manualCurrent->setVisible(m_currents.mode == CurrentModel::Mode::Manual);
  const CurrentSource current = m_currents.source();
  const PassageSettings s = settings();
  QVector<SourceResult> rows;
  QVector<DepartureOption> selectedOptions;
  for (const QString& id : ids) {
    SourceResult r;
    r.id = id;
    r.name = sourceName(id);
    r.best = PassagePlanner::planWindow(m_route, s, windFor(id), &r.options,
                                        current);
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
    m_plan = PassagePlanner::plan(m_route, one, windFor(m_sourceId), current);
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
    const PassagePlan p =
        PassagePlanner::plan(m_route, one, windFor(r.id), current);
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
                             SailInventory::load(), laneNm(), m_water, current);
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
  showCurrents();
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
      {"chosen", m_chosen.toUTC().toString(Qt::ISODate)},
      {"currents", [this] {
         QJsonObject c{{"mode", m_currentMode->currentIndex()},
                       {"set", m_currentSet->value()},
                       {"drift", m_currentDrift->value()},
                       {"minDep", m_minDepLevel->value()},
                       {"minArr", m_minArrLevel->value()}};
         if (m_currents.marine.isValid())
           c["marine"] = m_currents.marine.toJson();
         if (!m_currents.rivers.isEmpty())
           c["rivers"] = m_currents.rivers.toJson();
         QJsonArray from, to;
         for (const Gauge& g : m_gaugesFrom) from.append(g.toJson());
         for (const Gauge& g : m_gaugesTo) to.append(g.toJson());
         c["gaugesFrom"] = from;
         c["gaugesTo"] = to;
         return c;
       }()}};
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
  const QJsonObject c = o.value("currents").toObject();
  if (!c.isEmpty()) {
    m_currentMode->setCurrentIndex(c.value("mode").toInt(0));
    m_currentSet->setValue(c.value("set").toInt(m_currentSet->value()));
    m_currentDrift->setValue(
        c.value("drift").toDouble(m_currentDrift->value()));
    m_minDepLevel->setValue(c.value("minDep").toDouble(-1));
    m_minArrLevel->setValue(c.value("minArr").toDouble(-1));
    m_currents.marine = MarineForecast::fromJson(c.value("marine").toObject());
    m_currents.rivers = RiverNetwork::fromJson(c.value("rivers").toObject());
    m_gaugesFrom.clear();
    m_gaugesTo.clear();
    for (const auto& v : c.value("gaugesFrom").toArray())
      m_gaugesFrom << Gauge::fromJson(v.toObject());
    for (const auto& v : c.value("gaugesTo").toArray())
      m_gaugesTo << Gauge::fromJson(v.toObject());
  }
  m_debounce.start();
}

void PassagePanel::setCurrents(const CurrentModel& m) {
  m_currents.marine = m.marine;
  m_currents.rivers = m.rivers;
  m_currents.useStations = m.useStations;
  m_currentMode->setCurrentIndex(int(m.mode));
  m_currentSet->setValue(int(std::round(m.manualSetDeg)) % 360);
  m_currentDrift->setValue(m.manualDriftKn);
  m_debounce.start();
}

void PassagePanel::fetchCurrents() {
  if (m_route.size() < 2) return;
  m_currentInfo->setText(tr("Loading currents, rivers and gauges…"));
  QPointer<PassagePanel> self(this);
  const QVector<QPointF> route = m_route;
  TideServices::instance().fetchMarine(
      WeatherModels::samplePoints(route, 4),
      [self, route](const MarineForecast& m, const QString& err) {
        if (!self || self->m_route != route) return;
        if (!m.isValid() && !err.isEmpty())
          qInfo("Marine forecast not available: %s", qPrintable(err));
        self->m_currents.marine = m;
        self->m_debounce.start();
      });
  // Rivers and canals along the route (only for passages of moderate size;
  // at sea the box holds no waterways anyway).
  GeoBox box{1e9, 1e9, -1e9, -1e9};
  for (const QPointF& p : route) {
    box.south = std::min(box.south, p.y());
    box.north = std::max(box.north, p.y());
    box.west = std::min(box.west, p.x());
    box.east = std::max(box.east, p.x());
  }
  box.south -= 0.03;
  box.north += 0.03;
  box.west -= 0.05;
  box.east += 0.05;
  if (box.areaDeg2() <= 2.0) {
    OnlineOsm::instance().queryArea(
        box, RiverNetwork::query(box.south, box.west, box.north, box.east),
        [self, route](const QByteArray& json, const QString& err) {
          if (!self || self->m_route != route || !err.isEmpty()) return;
          // Keep the speeds the skipper set for rivers of the same name.
          RiverNetwork net =
              RiverNetwork::fromOsm(OverpassService::parseJson(json));
          for (const auto& [name, kn] : self->m_currents.rivers.summary())
            net.setSpeed(name, kn);
          self->m_currents.rivers = net;
          self->m_debounce.start();
        });
  }
  // Inland gauges near departure and destination.
  for (const bool from : {true, false}) {
    const QPointF p = from ? route.first() : route.last();
    TideServices::instance().fetchGauges(
        p.y(), p.x(), 25,
        [self, route, from](const QVector<Gauge>& g, const QString&) {
          if (!self || self->m_route != route) return;
          (from ? self->m_gaugesFrom : self->m_gaugesTo) = g.mid(0, 2);
          self->m_debounce.start();
        });
  }
}

QString PassagePanel::levelText(const QString& title, const QPointF& pos,
                                const QDateTime& t,
                                const QVector<Gauge>& gauges) const {
  const Palette& pal = Theme::p();
  QString out =
      QStringLiteral("<b>%1</b> %2").arg(title.toHtmlEscaped(), timeText(t));
  const LevelInfo li = m_currents.level(pos.y(), pos.x(), t);
  if (li.valid()) {
    out += " · " + (li.chartDatum ? tr("%1 m above chart datum")
                                  : tr("%1 m against mean sea level"))
                       .arg(li.levelM, 0, 'f', 1);
    out += QStringLiteral(" <span style='color:%1'>(%2)</span>")
               .arg(pal.textDim.name(), li.source.toHtmlEscaped());
    QStringList ex;
    for (const TideExtreme& e : li.extremes)
      ex << QStringLiteral("%1 %2 %3 m")
                .arg(e.high ? tr("HW") : tr("LW"),
                     e.time.toLocalTime().toString("HH:mm"))
                .arg(e.value, 0, 'f', 1);
    if (!ex.isEmpty()) out += "<br>" + ex.join(" · ");
  }
  for (const Gauge& g : gauges)
    out += QStringLiteral("<br>%1 %2: %3")
               .arg(tr("Gauge"), g.name.toHtmlEscaped(), g.text());
  if (!li.valid() && gauges.isEmpty()) out += " · " + tr("no water level data");
  return out;
}

void PassagePanel::showCurrents() {
  const Palette& pal = Theme::p();
  QStringList src;
  if (m_currents.mode == CurrentModel::Mode::Off) {
    src << tr("Currents are not taken into account.");
  } else if (m_currents.mode == CurrentModel::Mode::Manual) {
    src << tr("Constant current to %1° with %2 kn.")
               .arg(m_currentSet->value())
               .arg(m_currentDrift->value(), 0, 'f', 1);
  } else {
    int river = 0, canal = 0;
    for (const Waterway& w : m_currents.rivers.ways())
      (w.kind == "canal" ? canal : river) += w.kind == "tidal" ? 0 : 1;
    if (river || canal)
      src << tr("%1 river and %2 canal sections (OpenStreetMap)")
                 .arg(river)
                 .arg(canal);
    if (TideDatabase::instance().isLoaded())
      src << tr("tidal current stations");
    if (m_currents.marine.isValid())
      src << tr("marine forecast of %1")
                 .arg(
                     QLocale().toString(m_currents.marine.fetched.toLocalTime(),
                                        QLocale::ShortFormat));
    QString text = src.isEmpty() ? tr("No current data loaded yet.")
                                 : tr("Sources: %1").arg(src.join(", "));
    if (m_plan.currentUsed)
      text += "\n" + (m_plan.currentGainH >= 0
                          ? tr("The current saves %1 (max. %2 kn).")
                          : tr("The current costs %1 (max. %2 kn)."))
                         .arg(hoursText(std::abs(m_plan.currentGainH)))
                         .arg(m_plan.maxCurrentKn, 0, 'f', 1);
    src = {text};
  }
  m_currentInfo->setText(src.join(" "));

  // Rivers with their speed, changeable.
  while (QLayoutItem* it = m_riverList->takeAt(0)) {
    if (QWidget* w = it->widget()) {
      w->hide();
      w->deleteLater();
    }
    delete it;
  }
  if (m_currents.mode == CurrentModel::Mode::Auto) {
    int n = 0;
    for (const auto& [name, kn] : m_currents.rivers.summary()) {
      if (++n > 8) break;
      auto* row = new QWidget;
      row->setObjectName("riverRow");
      auto* rl = new QHBoxLayout(row);
      rl->setContentsMargins(0, 0, 0, 0);
      rl->addWidget(label("〰 " + name), 1);
      auto* sp = new QDoubleSpinBox;
      sp->setRange(0, 8);
      sp->setDecimals(1);
      sp->setSingleStep(0.1);
      sp->setSuffix(tr(" kn"));
      sp->setValue(kn);
      sp->setToolTip(tr("Flow speed of this river (downstream)"));
      const QString key = name;
      connect(sp, &QDoubleSpinBox::editingFinished, this, [this, sp, key] {
        m_currents.rivers.setSpeed(key, sp->value());
        m_debounce.start();
      });
      rl->addWidget(sp);
      m_riverList->addWidget(row);
    }
  }

  // Water levels at departure and destination.
  if (m_route.size() >= 2 && m_plan.departure.isValid()) {
    QString t =
        levelText(tr("Departure"), m_route.first(), m_plan.departure,
                  m_gaugesFrom) +
        "<br>" +
        levelText(tr("Arrival"), m_route.last(), m_plan.arrival, m_gaugesTo);
    if (!m_plan.levelOk)
      t += QStringLiteral("<br><span style='color:%1'>⚠ %2</span>")
               .arg(pal.warning.name(),
                    tr("The minimum water level is not reached."));
    m_levelInfo->setText(t);
  }
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
  if (p.currentUsed) {
    const int mins = int(std::round(p.currentGainH * 60));
    m_tiles->addWidget(
        tile(QStringLiteral("%1%2:%3 h")
                 .arg(mins >= 0 ? "+" : "−")
                 .arg(std::abs(mins) / 60)
                 .arg(std::abs(mins) % 60, 2, 10, QChar('0')),
             tr("Current · max. %1 kn").arg(p.maxCurrentKn, 0, 'f', 1),
             mins >= 0 ? pal.success : pal.warning),
        3, 0);
    if (!std::isnan(p.arrivalLevelM))
      m_tiles->addWidget(
          tile(QStringLiteral("%1 m").arg(p.arrivalLevelM, 0, 'f', 1),
               tr("Water level at arrival"),
               p.levelOk ? QColor() : pal.warning),
          3, 1);
  }

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
    QString cur;
    if (p.currentUsed && leg.currentDriftKn >= 0.05)
      cur = tr(" · current %1%2 kn · SOG %3 kn")
                .arg(leg.currentAlongKn >= 0 ? "+" : "−")
                .arg(std::abs(leg.currentAlongKn), 0, 'f', 1)
                .arg(leg.sogKn, 0, 'f', 1);
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
            .arg(QStringLiteral(" · TWA %1°").arg(int(std::round(leg.twaDeg))) +
                 cur));
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
