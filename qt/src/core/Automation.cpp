#include "core/Automation.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>
#include <iterator>

#include "core/BoatSystems.h"

namespace {
QString tr(const char* s) {
  return QCoreApplication::translate("Automation", s);
}

QString zoneName(const QString& id) {
  const BoatZone* z = BoatSystems::instance().zone(id);
  return z ? z->name : id;
}

QString lightName(const QString& id) {
  for (const auto& l : BoatSystems::instance().lights())
    if (l.id == id)
      return l.navigation ? l.name : zoneName(l.zone) + " · " + l.name;
  return id;
}

QString consumerName(const QString& id) {
  for (const auto& c : BoatSystems::instance().consumers())
    if (c.id == id) return c.name;
  return id;
}

const char* kTypes[] = {"heater", "zoneSetpoint", "waterHeater",
                        "scene",  "light",        "consumer"};
}  // namespace

// --------------------------------------------------------------------------

QString AutomationAction::typeName(Type t) {
  switch (t) {
    case Type::Heater:
      return tr("Heater");
    case Type::ZoneSetpoint:
      return tr("Room temperature");
    case Type::WaterHeater:
      return tr("Hot water");
    case Type::Scene:
      return tr("Light scene");
    case Type::Light:
      return tr("Light");
    case Type::Consumer:
      return tr("Consumer");
  }
  return {};
}

QString AutomationAction::describe() const {
  const QString onOff = on ? tr("on") : tr("off");
  switch (type) {
    case Type::Heater:
      return tr("Heater %1").arg(onOff);
    case Type::ZoneSetpoint:
      return tr("%1 %2 °C").arg(zoneName(target)).arg(value, 0, 'f', 1);
    case Type::WaterHeater:
      return tr("Hot water %1").arg(onOff);
    case Type::Scene: {
      const LightScene* s = BoatSystems::instance().scene(target);
      return tr("Scene “%1”").arg(s ? s->name : target);
    }
    case Type::Light:
      return QStringLiteral("%1 %2").arg(lightName(target), onOff);
    case Type::Consumer:
      return QStringLiteral("%1 %2").arg(consumerName(target), onOff);
  }
  return {};
}

QJsonObject AutomationAction::toJson() const {
  return {{"type", kTypes[int(type)]},
          {"target", target},
          {"value", value},
          {"on", on}};
}

AutomationAction AutomationAction::fromJson(const QJsonObject& o) {
  AutomationAction a;
  const QString t = o["type"].toString();
  for (int i = 0; i < int(std::size(kTypes)); ++i)
    if (t == kTypes[i]) a.type = Type(i);
  a.target = o["target"].toString();
  a.value = o["value"].toDouble(20);
  a.on = o["on"].toBool(true);
  return a;
}

// --------------------------------------------------------------------------

bool Automation::allowedOn(const QDate& d) const {
  return (weekdays & (1 << (d.dayOfWeek() - 1))) &&
         (months & (1 << (d.month() - 1)));
}

QString Automation::schedule() const {
  QStringList parts;
  if (trigger == Trigger::Manual) {
    parts << tr("Manual");
  } else {
    const QString t = time.toString("HH:mm");
    if (weekdays == 0x7f)
      parts << tr("Daily %1").arg(t);
    else if (weekdays == 0x1f)
      parts << tr("Mon–Fri %1").arg(t);
    else if (weekdays == 0x60)
      parts << tr("Weekends %1").arg(t);
    else {
      QStringList days;
      for (int i = 0; i < 7; ++i)
        if (weekdays & (1 << i))
          days << QLocale().dayName(i + 1, QLocale::ShortFormat);
      parts << days.join(", ") + " " + t;
    }
  }
  if (months != 0xfff) {
    if (months == kWinter)
      parts << tr("Winter (Nov–Mar)");
    else {
      QStringList m;
      for (int i = 0; i < 12; ++i)
        if (months & (1 << i))
          m << QLocale().monthName(i + 1, QLocale::ShortFormat);
      parts << m.join(", ");
    }
  }
  return parts.join(" · ");
}

QJsonObject Automation::toJson() const {
  QJsonArray acts;
  for (const auto& a : actions) acts.append(a.toJson());
  return {{"id", id},
          {"name", name},
          {"icon", icon},
          {"enabled", enabled},
          {"trigger", trigger == Trigger::Time ? "time" : "manual"},
          {"time", time.toString("HH:mm")},
          {"weekdays", weekdays},
          {"months", months},
          {"lastRun", lastRun.toString(Qt::ISODate)},
          {"actions", acts}};
}

Automation Automation::fromJson(const QJsonObject& o) {
  Automation a;
  a.id = o["id"].toString();
  a.name = o["name"].toString();
  a.icon = o["icon"].toString("auto");
  a.enabled = o["enabled"].toBool(true);
  a.trigger =
      o["trigger"].toString() == "time" ? Trigger::Time : Trigger::Manual;
  a.time = QTime::fromString(o["time"].toString("07:00"), "HH:mm");
  if (!a.time.isValid()) a.time = QTime(7, 0);
  a.weekdays = o["weekdays"].toInt(0x7f) & 0x7f;
  a.months = o["months"].toInt(0xfff) & 0xfff;
  a.lastRun = QDateTime::fromString(o["lastRun"].toString(), Qt::ISODate);
  for (const auto& v : o["actions"].toArray())
    a.actions << AutomationAction::fromJson(v.toObject());
  return a;
}

// --------------------------------------------------------------------------

AutomationStore& AutomationStore::instance() {
  static AutomationStore s;
  return s;
}

AutomationStore::AutomationStore() {
  load();
  m_timer.setInterval(20 * 1000);
  connect(&m_timer, &QTimer::timeout, this,
          [this] { check(QDateTime::currentDateTime()); });
  m_timer.start();
}

QString AutomationStore::file() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
         "/automations.json";
}

QVector<Automation> AutomationStore::defaults() {
  using T = AutomationAction::Type;
  const auto act = [](T type, const QString& target = {}, double value = 0,
                      bool on = true) {
    AutomationAction a;
    a.type = type;
    a.target = target;
    a.value = value;
    a.on = on;
    return a;
  };
  QVector<Automation> out;

  Automation shower;
  shower.id = "winter_shower";
  shower.name = tr("Winter shower");
  shower.icon = "drop";
  shower.months = Automation::kWinter;
  shower.actions = {act(T::Heater), act(T::ZoneSetpoint, "saloon", 21),
                    act(T::ZoneSetpoint, "heads", 23), act(T::WaterHeater),
                    act(T::Scene, "shower")};
  out << shower;

  Automation morning;
  morning.id = "morning_warmup";
  morning.name = tr("Warm morning");
  morning.icon = "sun";
  morning.trigger = Automation::Trigger::Time;
  morning.time = QTime(6, 45);
  morning.months = Automation::kWinter;
  morning.enabled = false;
  morning.actions = {act(T::Heater), act(T::ZoneSetpoint, "saloon", 20),
                     act(T::ZoneSetpoint, "forward", 18)};
  out << morning;

  Automation night;
  night.id = "night_passage";
  night.name = tr("Night passage");
  night.icon = "moon";
  night.actions = {act(T::Scene, "night"), act(T::Light, "nav_lights"),
                   act(T::Light, "anchor_light", 0, false),
                   act(T::Consumer, "instruments"),
                   act(T::Consumer, "autopilot")};
  out << night;

  Automation leave;
  leave.id = "leave_boat";
  leave.name = tr("Leave the boat");
  leave.icon = "close";
  leave.actions = {act(T::Heater, {}, 0, false),
                   act(T::WaterHeater, {}, 0, false),
                   act(T::Scene, "off"),
                   act(T::Consumer, "stereo", 0, false),
                   act(T::Consumer, "inverter", 0, false),
                   act(T::Consumer, "water_pump", 0, false)};
  out << leave;
  return out;
}

void AutomationStore::load() {
  QFile f(file());
  if (!f.open(QIODevice::ReadOnly)) {
    m_items = defaults();
    return;
  }
  m_items.clear();
  for (const auto& v : QJsonDocument::fromJson(f.readAll()).array()) {
    Automation a = Automation::fromJson(v.toObject());
    if (!a.id.isEmpty()) m_items << a;
  }
}

void AutomationStore::reload() {
  load();
  emit changed();
}

void AutomationStore::store() const {
  QJsonArray arr;
  for (const auto& a : m_items) arr.append(a.toJson());
  const QString path = file();
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return;
  f.write(QJsonDocument(arr).toJson());
  f.commit();
}

const Automation* AutomationStore::find(const QString& id) const {
  for (const auto& a : m_items)
    if (a.id == id) return &a;
  return nullptr;
}

QString AutomationStore::save(const Automation& automation) {
  Automation a = automation;
  if (a.id.isEmpty())
    a.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  auto it = std::find_if(m_items.begin(), m_items.end(),
                         [&](const Automation& x) { return x.id == a.id; });
  if (it != m_items.end())
    *it = a;
  else
    m_items << a;
  store();
  emit changed();
  return a.id;
}

void AutomationStore::remove(const QString& id) {
  const auto n =
      m_items.removeIf([&](const Automation& a) { return a.id == id; });
  if (n == 0) return;
  store();
  emit changed();
}

void AutomationStore::execute(const Automation& a) {
  auto& sys = BoatSystems::instance();
  using T = AutomationAction::Type;
  for (const auto& act : a.actions) {
    switch (act.type) {
      case T::Heater:
        sys.setHeaterOn(act.on);
        break;
      case T::ZoneSetpoint:
        sys.setZoneSetpoint(act.target, act.value);
        break;
      case T::WaterHeater:
        sys.setWaterHeaterOn(act.on);
        break;
      case T::Scene:
        sys.applyScene(act.target);
        break;
      case T::Light:
        sys.setLight(act.target, act.on);
        break;
      case T::Consumer:
        sys.setConsumer(act.target, act.on);
        break;
    }
  }
}

void AutomationStore::run(const QString& id) {
  runAt(id, QDateTime::currentDateTime());
}

void AutomationStore::runAt(const QString& id, const QDateTime& when) {
  for (auto& a : m_items) {
    if (a.id != id) continue;
    execute(a);
    a.lastRun = when;
    store();
    emit changed();
    emit ran(a.id, a.name);
    return;
  }
}

QStringList AutomationStore::check(const QDateTime& now) {
  QStringList ran;
  for (const auto& a : m_items) {
    if (!a.enabled || a.trigger != Automation::Trigger::Time) continue;
    if (!a.allowedOn(now.date())) continue;
    const QDateTime due(now.date(), a.time);
    const qint64 late = due.secsTo(now);
    if (late < 0 || late > 10 * 60) continue;
    if (a.lastRun.isValid() && a.lastRun >= due) continue;
    ran << a.id;
  }
  for (const QString& id : ran) runAt(id, now);
  return ran;
}
