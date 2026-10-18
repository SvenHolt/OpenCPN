#include "core/Consumption.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

#include "core/BoatConfig.h"
#include "core/NavStore.h"

namespace {
QString tr(const char* s) {
  return QCoreApplication::translate("Consumption", s);
}
const char* kModes[] = {"anchor", "underway", "marina"};
}  // namespace

QString boatModeName(BoatMode m) {
  switch (m) {
    case BoatMode::Underway:
      return tr("Underway");
    case BoatMode::Marina:
      return tr("Shore power");
    default:
      return tr("At anchor");
  }
}

QJsonObject ConsumptionSample::toJson() const {
  return {{"end", end.toString(Qt::ISODate)},
          {"h", hours},
          {"mode", kModes[int(mode)]},
          {"t", outsideC},
          {"fresh", freshL},
          {"waste", wasteL},
          {"heaterFuel", heaterFuelL},
          {"engineFuel", engineFuelL},
          {"loads", loadsWh},
          {"heaterWh", heaterWh},
          {"solar", solarWh},
          {"shore", shoreWh},
          {"alt", alternatorWh},
          {"engineH", engineH},
          {"freshLevel", freshLevelL},
          {"wasteLevel", wasteLevelL},
          {"fuelLevel", fuelLevelL},
          {"soc", soc}};
}

ConsumptionSample ConsumptionSample::fromJson(const QJsonObject& o) {
  ConsumptionSample s;
  s.end = QDateTime::fromString(o["end"].toString(), Qt::ISODate);
  s.hours = o["h"].toDouble(0.5);
  const QString m = o["mode"].toString();
  for (int i = 0; i < 3; ++i)
    if (m == kModes[i]) s.mode = BoatMode(i);
  s.outsideC = o["t"].toDouble(15);
  s.freshL = o["fresh"].toDouble();
  s.wasteL = o["waste"].toDouble();
  s.heaterFuelL = o["heaterFuel"].toDouble();
  s.engineFuelL = o["engineFuel"].toDouble();
  s.loadsWh = o["loads"].toDouble();
  s.heaterWh = o["heaterWh"].toDouble();
  s.solarWh = o["solar"].toDouble();
  s.shoreWh = o["shore"].toDouble();
  s.alternatorWh = o["alt"].toDouble();
  s.engineH = o["engineH"].toDouble();
  s.freshLevelL = o["freshLevel"].toDouble();
  s.wasteLevelL = o["wasteLevel"].toDouble();
  s.fuelLevelL = o["fuelLevel"].toDouble();
  s.soc = o["soc"].toDouble();
  return s;
}

// --------------------------------------------------------------------------

ConsumptionLog& ConsumptionLog::instance() {
  static ConsumptionLog s;
  return s;
}

ConsumptionLog::ConsumptionLog() {
  load();
  // The simulator has no past; start with generated days so that the range
  // analysis has something to learn from.
  if (m_samples.isEmpty() && BoatSystems::instance().simulated())
    seedSimulated(QDateTime::currentDateTimeUtc(), 10);
  m_last = BoatSystems::instance().counters();
  m_lastTime = QDateTime::currentDateTimeUtc();
  m_timer.setInterval(kIntervalMin * 60 * 1000);
  connect(&m_timer, &QTimer::timeout, this, &ConsumptionLog::sampleNow);
  m_timer.start();
}

QString ConsumptionLog::file() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
         "/consumption.json";
}

void ConsumptionLog::load() {
  m_samples.clear();
  m_simulated = false;
  QFile f(file());
  if (!f.open(QIODevice::ReadOnly)) return;
  const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
  m_simulated = root["simulated"].toBool();
  for (const auto& v : root["samples"].toArray()) {
    const ConsumptionSample s = ConsumptionSample::fromJson(v.toObject());
    if (s.end.isValid()) m_samples << s;
  }
  std::sort(m_samples.begin(), m_samples.end(),
            [](const auto& a, const auto& b) { return a.end < b.end; });
}

void ConsumptionLog::reload() {
  load();
  emit changed();
}

void ConsumptionLog::store() const {
  QJsonArray arr;
  for (const auto& s : m_samples) arr.append(s.toJson());
  const QString path = file();
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return;
  f.write(
      QJsonDocument(QJsonObject{{"simulated", m_simulated}, {"samples", arr}})
          .toJson(QJsonDocument::Compact));
  f.commit();
}

QVector<ConsumptionSample> ConsumptionLog::between(const QDateTime& from,
                                                   const QDateTime& to) const {
  QVector<ConsumptionSample> out;
  for (const auto& s : m_samples)
    if (s.end > from && s.start() < to) out << s;
  return out;
}

void ConsumptionLog::add(const ConsumptionSample& s) {
  auto it = std::upper_bound(
      m_samples.begin(), m_samples.end(), s,
      [](const auto& a, const auto& b) { return a.end < b.end; });
  m_samples.insert(it, s);
  const QDateTime limit = m_samples.last().end.addDays(-kKeepDays);
  m_samples.removeIf([&](const auto& x) { return x.end < limit; });
  store();
  emit changed();
}

void ConsumptionLog::clear() {
  m_samples.clear();
  m_simulated = false;
  store();
  emit changed();
}

BoatMode ConsumptionLog::currentMode() {
  const auto& sys = BoatSystems::instance();
  if (sys.energy().shoreConnected) return BoatMode::Marina;
  const OwnShip& own = NavStore::instance().ownShip();
  if (sys.engineRunning() || (own.valid && own.sog > 1.5))
    return BoatMode::Underway;
  return BoatMode::Anchor;
}

void ConsumptionLog::sampleNow() {
  const auto& sys = BoatSystems::instance();
  const QDateTime now = QDateTime::currentDateTimeUtc();
  const BoatCounters& c = sys.counters();
  ConsumptionSample s;
  s.end = now;
  s.hours = std::max(1.0 / 60, m_lastTime.secsTo(now) / 3600.0);
  s.mode = currentMode();
  s.outsideC = sys.outsideC();
  s.freshL = c.freshL - m_last.freshL;
  s.wasteL = c.wasteL - m_last.wasteL;
  s.heaterFuelL = c.heaterFuelL - m_last.heaterFuelL;
  s.engineFuelL = c.engineFuelL - m_last.engineFuelL;
  s.loadsWh = c.loadsWh - m_last.loadsWh;
  s.heaterWh = c.heaterWh - m_last.heaterWh;
  s.solarWh = c.solarWh - m_last.solarWh;
  s.shoreWh = c.shoreWh - m_last.shoreWh;
  s.alternatorWh = c.alternatorWh - m_last.alternatorWh;
  s.engineH = c.engineH - m_last.engineH;
  if (const BoatTank* t = sys.tank(BoatTank::Kind::FreshWater))
    s.freshLevelL = t->level * t->capacityL;
  if (const BoatTank* t = sys.tank(BoatTank::Kind::WasteWater))
    s.wasteLevelL = t->level * t->capacityL;
  if (const BoatTank* t = sys.tank(BoatTank::Kind::Fuel))
    s.fuelLevelL = t->level * t->capacityL;
  s.soc = sys.energy().soc;
  m_last = c;
  m_lastTime = now;
  add(s);
}

void ConsumptionLog::seedSimulated(const QDateTime& end, int days) {
  // A plausible cruise: two people, autumn weather, mostly at anchor with
  // some passages and a night in a marina.
  auto* rng = QRandomGenerator::global();
  const auto noise = [rng](double a) {
    return (rng->generateDouble() * 2 - 1) * a;
  };
  const auto& sys = BoatSystems::instance();
  const double solarPeak = sys.energy().solarPeakW;
  const double engineLph = BoatConfig::instance().consumptionLph;
  m_samples.clear();
  const int steps = days * 48;
  const QDateTime start = end.addSecs(-qint64(steps) * 1800);
  const double baseTemp = 12 + noise(2);
  double fresh = 150, waste = 6, fuel = 110, soc = 0.9;
  for (int i = 1; i <= steps; ++i) {
    ConsumptionSample s;
    s.end = start.addSecs(qint64(i) * 1800);
    s.hours = 0.5;
    const QDateTime local = s.end.toLocalTime();
    const double hour = local.time().hour() + local.time().minute() / 60.0;
    const int day = (i - 1) / 48;
    // Day pattern: anchor, passage day, anchor, marina every fifth day.
    const bool passageDay = day % 3 == 1;
    const bool marinaDay = day % 5 == 4;
    const bool sailing = passageDay && hour >= 9 && hour < 16;
    s.mode = marinaDay ? BoatMode::Marina
             : sailing ? BoatMode::Underway
                       : BoatMode::Anchor;
    // Weather: a cold spell in the middle, daily cycle.
    const double spell = -4 * std::exp(-std::pow((day - days / 2.0) / 2, 2));
    s.outsideC = baseTemp + spell + 4 * std::sin((hour - 9) / 24 * 2 * M_PI) +
                 noise(0.5);
    // Water: mornings and evenings.
    const int hr = int(hour);
    const double weight = hr < 7                ? 0.1
                          : hr < 9              ? 3.0
                          : hr == 12            ? 1.5
                          : hr >= 18 && hr < 21 ? 2.5
                                                : 0.8;
    s.freshL = std::max(0.0, 24.0 / 24 * weight * 0.5 * (1 + noise(0.3)));
    s.wasteL = s.freshL / 3;
    // Heater: evenings, nights and mornings when it is cold.
    const bool heaterTime = hour < 9 || hour >= 17;
    const double demand = std::max(0.0, 19 - s.outsideC);
    const double heaterLph =
        heaterTime && demand > 0 ? std::min(0.28, 0.02 * demand + 0.04) : 0;
    s.heaterFuelL = heaterLph * 0.5;
    s.heaterWh = heaterLph > 0 ? (12 + 22 * heaterLph / 0.28) * 0.5 : 0;
    // Engine: harbour manoeuvres and calm passages.
    const bool motoring =
        sailing && (hour < 10 || hour >= 15 || (day % 2 == 0 && hour < 12));
    s.engineH = motoring ? 0.5 : 0;
    s.engineFuelL = motoring ? engineLph * 0.5 * (1 + noise(0.1)) : 0;
    // Electricity: fridge, instruments, lights in the evening.
    double loadsW = 30 + (hour >= 18 && hour < 23 ? 25 : 0) +
                    (s.mode == BoatMode::Underway ? 55 : 0) + noise(4);
    s.loadsWh = loadsW * 0.5 + s.heaterWh;
    const double sun = std::max(0.0, std::sin((hour - 6.5) / 13.5 * M_PI));
    const double clouds =
        std::clamp(0.6 + 0.3 * std::sin(day * 1.7) + noise(0.1), 0.15, 1.0);
    s.solarWh = solarPeak * sun * clouds * 0.75 * 0.5;
    s.alternatorWh = motoring ? 600 * 0.5 : 0;
    s.shoreWh = s.mode == BoatMode::Marina
                    ? std::max(0.0, s.loadsWh - s.solarWh) + 60
                    : 0;
    // Levels: refills in the marina.
    if (marinaDay) {
      fresh = 160;
      waste = 0;
      fuel = BoatConfig::instance().tankLiters * 0.95;
    }
    fresh = std::max(0.0, fresh - s.freshL);
    waste = std::min(40.0, waste + s.wasteL);
    fuel = std::max(0.0, fuel - s.heaterFuelL - s.engineFuelL);
    soc =
        std::clamp(soc + (s.solarWh + s.alternatorWh + s.shoreWh - s.loadsWh) /
                             12.8 / sys.energy().capacityAh,
                   0.0, 1.0);
    s.freshLevelL = fresh;
    s.wasteLevelL = waste;
    s.fuelLevelL = fuel;
    s.soc = soc;
    m_samples << s;
  }
  m_simulated = true;
  store();
  emit changed();
}
