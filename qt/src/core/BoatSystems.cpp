#include "core/BoatSystems.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>

#include "core/BoatConfig.h"
#include "core/NavStore.h"

namespace {
QString tr(const char* s) {
  return QCoreApplication::translate("BoatSystems", s);
}

// How much of the heater's air goes to each room (ducts).
double heaterShare(const QString& zone) {
  if (zone == "saloon") return 1.0;
  if (zone == "galley") return 0.7;
  if (zone == "forward") return 0.6;
  if (zone == "aft") return 0.5;
  if (zone == "heads") return 0.4;
  return 0;
}

double noise(double amplitude) {
  return (QRandomGenerator::global()->generateDouble() * 2 - 1) * amplitude;
}
}  // namespace

QString Heater::stateName(State s) {
  switch (s) {
    case State::Starting:
      return tr("Starting");
    case State::Heating:
      return tr("Heating");
    case State::Ventilation:
      return tr("Ventilation");
    case State::CoolDown:
      return tr("Cooling down");
    case State::Fault:
      return tr("Fault");
    default:
      return tr("Off");
  }
}

double Heater::setpointFor(const QString& z) const {
  if (z == zone) return setpointC;
  return zoneSetpointC.value(z, std::numeric_limits<double>::quiet_NaN());
}

BoatSystems& BoatSystems::instance() {
  static BoatSystems s;
  return s;
}

BoatSystems::BoatSystems() {
  setup();
  m_lastStep = QDateTime::currentDateTimeUtc();
  m_timer.setInterval(1000);
  connect(&m_timer, &QTimer::timeout, this, [this] {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    simulate(std::clamp(m_lastStep.msecsTo(now) / 1000.0, 0.0, 10.0));
    m_lastStep = now;
  });
  m_timer.start();
}

void BoatSystems::setup() {
  // Rooms of a Dufour 2800 style 28 ft cruiser.
  const struct {
    const char* id;
    QString name;
    double t;
  } zones[] = {
      {"outside", tr("Outside"), 14}, {"forward", tr("Forward cabin"), 16.5},
      {"saloon", tr("Saloon"), 17.5}, {"galley", tr("Galley"), 17.8},
      {"heads", tr("Heads"), 16.0},   {"aft", tr("Aft cabin"), 16.2}};
  for (const auto& z : zones) {
    BoatZone zone;
    zone.id = z.id;
    zone.name = z.name;
    zone.temperatureC = z.t;
    zone.humidityPct = zone.id == "outside" ? 78 : 62;
    zone.signalKPath = zone.id == "outside"
                           ? "environment.outside.temperature"
                           : "environment.inside." + zone.id + ".temperature";
    m_zones << zone;
  }

  m_tanks = {{"fresh", tr("Fresh water"), BoatTank::Kind::FreshWater, 160, 0.72,
              "tanks.freshWater.0.currentLevel"},
             {"waste", tr("Holding tank"), BoatTank::Kind::WasteWater, 40, 0.18,
              "tanks.wasteWater.0.currentLevel"},
             {"fuel", tr("Diesel"), BoatTank::Kind::Fuel,
              BoatConfig::instance().tankLiters,
              BoatConfig::instance().manualFuelPct / 100,
              "tanks.fuel.0.currentLevel"}};

  const auto addLight = [this](const QString& id, const QString& name,
                               const QString& zone, double w,
                               bool nav = false) {
    BoatLight l;
    l.id = id;
    l.name = name;
    l.zone = zone;
    l.maxPowerW = w;
    l.navigation = nav;
    l.signalKPath = "electrical.switches." + id + ".state";
    m_lights << l;
  };
  addLight("fwd_ceiling", tr("Ceiling"), "forward", 4);
  addLight("fwd_read_p", tr("Reading light port"), "forward", 2);
  addLight("fwd_read_s", tr("Reading light stbd"), "forward", 2);
  addLight("saloon_ceiling", tr("Ceiling"), "saloon", 6);
  addLight("saloon_indirect", tr("Indirect light"), "saloon", 5);
  addLight("chart_table", tr("Chart table"), "saloon", 2);
  addLight("galley_spot", tr("Galley spots"), "galley", 4);
  addLight("heads_ceiling", tr("Ceiling"), "heads", 3);
  addLight("aft_ceiling", tr("Ceiling"), "aft", 4);
  addLight("aft_read", tr("Reading light"), "aft", 2);
  addLight("courtesy", tr("Floor lights"), "saloon", 1.5);
  addLight("nav_lights", tr("Running lights"), "outside", 8, true);
  addLight("steaming", tr("Steaming light"), "outside", 3, true);
  addLight("anchor_light", tr("Anchor light"), "outside", 2, true);
  addLight("deck_light", tr("Deck light"), "outside", 15, true);

  const auto addConsumer = [this](const QString& id, const QString& name,
                                  const QString& zone, double w, bool on,
                                  bool switchable = true) {
    BoatConsumer c;
    c.id = id;
    c.name = name;
    c.zone = zone;
    c.powerW = w;
    c.on = on;
    c.switchable = switchable;
    c.signalKPath = "electrical.switches." + id + ".state";
    m_consumers << c;
  };
  addConsumer("fridge", tr("Fridge"), "galley", 18, true);  // average
  addConsumer("water_pump", tr("Water pump"), "saloon", 3, true);
  addConsumer("instruments", tr("Instruments"), "saloon", 8, true);
  addConsumer("chartplotter", tr("Chart plotter"), "saloon", 12, true);
  addConsumer("vhf", tr("VHF radio"), "saloon", 6, true);
  addConsumer("autopilot", tr("Autopilot"), "aft", 30, false);
  addConsumer("usb", tr("USB / 12 V sockets"), "saloon", 5, true);
  addConsumer("stereo", tr("Stereo"), "saloon", 15, false);
  addConsumer("inverter", tr("230 V inverter"), "aft", 25, false);
  addConsumer("bilge", tr("Bilge pump (auto)"), "saloon", 0, true, false);

  loadScenes();
  // Lights: evening on the start.
  applyScene("evening");
  m_energy.soc = 0.82;
}

const BoatTank* BoatSystems::tank(BoatTank::Kind kind) const {
  for (const auto& t : m_tanks)
    if (t.kind == kind) return &t;
  return nullptr;
}

bool BoatSystems::engineRunning() const {
  const EngineData& e = NavStore::instance().engine();
  return !std::isnan(e.rpm) && e.rpm > 300 && e.lastUpdate.isValid() &&
         e.lastUpdate.secsTo(QDateTime::currentDateTimeUtc()) < 60;
}

const BoatZone* BoatSystems::zone(const QString& id) const {
  for (const auto& z : m_zones)
    if (z.id == id) return &z;
  return nullptr;
}

double BoatSystems::outsideC() const {
  const BoatZone* z = zone("outside");
  return z ? z->temperatureC : 15;
}

BoatLight* BoatSystems::light(const QString& id) {
  for (auto& l : m_lights)
    if (l.id == id) return &l;
  return nullptr;
}

void BoatSystems::setLight(const QString& id, bool on) {
  if (BoatLight* l = light(id)) {
    l->on = on;
    if (on && l->brightness <= 0.01) l->brightness = 1;
    m_scene.clear();
    emit changed();
  }
}

void BoatSystems::setLightLevel(const QString& id, double brightness,
                                const QColor& c) {
  if (BoatLight* l = light(id)) {
    l->brightness = std::clamp(brightness, 0.0, 1.0);
    if (c.isValid()) l->color = c;
    l->on = l->brightness > 0.01;
    m_scene.clear();
    emit changed();
  }
}

void BoatSystems::setConsumer(const QString& id, bool on) {
  for (auto& c : m_consumers)
    if (c.id == id && c.switchable) {
      c.on = on;
      emit changed();
    }
}

void BoatSystems::setHeaterOn(bool on) {
  Heater& h = m_heater;
  const bool running = h.state == Heater::State::Starting ||
                       h.state == Heater::State::Heating ||
                       h.state == Heater::State::Ventilation;
  if (on && !running) {
    h.state = Heater::State::Starting;
    h.stateSecs = 0;
    h.faultCode = 0;
  } else if (!on && running) {
    // The burner always cools down before it stops.
    h.state = Heater::State::CoolDown;
    h.stateSecs = 0;
    h.outputPct = 0;
    h.fuelLph = 0;
  }
  emit changed();
}

void BoatSystems::setHeaterSetpoint(double c) {
  m_heater.setpointC = std::clamp(c, 8.0, 30.0);
  emit changed();
}

void BoatSystems::setZoneSetpoint(const QString& z, double c) {
  if (z == m_heater.zone) {
    if (!std::isnan(c)) setHeaterSetpoint(c);
    return;
  }
  if (std::isnan(c))
    m_heater.zoneSetpointC.remove(z);
  else
    m_heater.zoneSetpointC.insert(z, std::clamp(c, 8.0, 30.0));
  emit changed();
}

void BoatSystems::setWaterHeaterOn(bool on) {
  m_water.on = on;
  m_water.heating = on && m_water.tempC < m_water.targetC - 1;
  emit changed();
}

void BoatSystems::setShoreConnected(bool on) {
  m_energy.shoreConnected = on;
  emit changed();
}

// --------------------------------------------------------------------------
// Light scenes

const SceneLight* LightScene::find(const QString& lightId) const {
  for (const auto& l : lights)
    if (l.lightId == lightId) return &l;
  return nullptr;
}

QJsonObject LightScene::toJson() const {
  QJsonArray arr;
  for (const auto& l : lights)
    arr.append(QJsonObject{{"light", l.lightId},
                           {"on", l.on},
                           {"brightness", l.brightness},
                           {"color", l.color.name()}});
  return {{"id", id},
          {"name", name},
          {"icon", icon},
          {"builtIn", builtIn},
          {"lights", arr}};
}

LightScene LightScene::fromJson(const QJsonObject& o) {
  LightScene s;
  s.id = o["id"].toString();
  s.name = o["name"].toString();
  s.icon = o["icon"].toString("bulb");
  s.builtIn = o["builtIn"].toBool();
  for (const auto& v : o["lights"].toArray()) {
    const QJsonObject lo = v.toObject();
    SceneLight l;
    l.lightId = lo["light"].toString();
    l.on = lo["on"].toBool();
    l.brightness = std::clamp(lo["brightness"].toDouble(1), 0.0, 1.0);
    l.color = QColor(lo["color"].toString());
    if (!l.color.isValid()) l.color = QColor(255, 236, 200);
    if (!l.lightId.isEmpty()) s.lights << l;
  }
  return s;
}

QVector<LightScene> BoatSystems::defaultScenes() const {
  const QColor red(255, 28, 0), warm(255, 176, 110), white(255, 240, 222),
      neutral(255, 248, 238);
  const auto make = [this](const QString& id, const QString& name,
                           const QString& icon, auto setting) {
    LightScene s;
    s.id = id;
    s.name = name;
    s.icon = icon;
    s.builtIn = true;
    for (const BoatLight& l : m_lights) {
      SceneLight sl;
      sl.lightId = l.id;
      if (setting(l, sl)) s.lights << sl;
    }
    return s;
  };
  QVector<LightScene> out;
  // Red and dim so the eyes stay adapted to the dark; only where the crew
  // moves at night.
  out << make("night", tr("Night sailing"), "moon",
              [&](const BoatLight& l, SceneLight& sl) {
                if (l.navigation) return false;
                sl.on = l.id == "saloon_ceiling" || l.id == "chart_table" ||
                        l.id == "courtesy" || l.id == "galley_spot" ||
                        l.id == "heads_ceiling";
                sl.brightness = l.id == "chart_table" ? 0.12 : 0.08;
                sl.color = red;
                return true;
              });
  out << make("evening", tr("Evening"), "sun",
              [&](const BoatLight& l, SceneLight& sl) {
                if (l.navigation) return false;
                sl.on = (l.zone == "saloon" || l.zone == "galley" ||
                         l.id == "heads_ceiling") &&
                        l.id != "chart_table";
                sl.brightness = 0.5;
                sl.color = warm;
                return true;
              });
  out << make("bright", tr("Bright"), "sun",
              [&](const BoatLight& l, SceneLight& sl) {
                if (l.navigation) return false;
                sl.on = true;
                sl.brightness = 1.0;
                sl.color = white;
                return true;
              });
  // At anchor: the anchor light, not the running lights.
  out << make("anchor", tr("At anchor"), "anchor",
              [&](const BoatLight& l, SceneLight& sl) {
                if (l.navigation) {
                  if (l.id == "deck_light") return false;
                  sl.on = l.id == "anchor_light";
                  sl.color = white;
                  return true;
                }
                sl.on = l.id == "saloon_indirect" || l.id == "courtesy";
                sl.brightness = 0.25;
                sl.color = warm;
                return true;
              });
  // Showering: bright neutral light in the heads, a soft path through the
  // saloon.
  out << make("shower", tr("Shower"), "drop",
              [&](const BoatLight& l, SceneLight& sl) {
                if (l.navigation) return false;
                sl.on = l.id == "heads_ceiling" || l.id == "saloon_indirect" ||
                        l.id == "courtesy";
                sl.brightness = l.id == "heads_ceiling" ? 1.0 : 0.3;
                sl.color = l.id == "heads_ceiling" ? neutral : warm;
                return true;
              });
  out << make("off", tr("All off"), "close",
              [&](const BoatLight& l, SceneLight& sl) {
                if (l.navigation) return false;
                sl.on = false;
                sl.brightness = l.brightness;
                sl.color = l.color;
                return true;
              });
  return out;
}

QString BoatSystems::scenesFile() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
         "/light-scenes.json";
}

void BoatSystems::loadScenes() {
  m_scenes = defaultScenes();
  QFile f(scenesFile());
  if (!f.open(QIODevice::ReadOnly)) return;
  const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
  for (const auto& v : arr) {
    LightScene s = LightScene::fromJson(v.toObject());
    if (s.id.isEmpty()) continue;
    auto it = std::find_if(m_scenes.begin(), m_scenes.end(),
                           [&](const LightScene& x) { return x.id == s.id; });
    if (it != m_scenes.end()) {
      s.builtIn = it->builtIn;
      *it = s;
    } else {
      s.builtIn = false;
      m_scenes << s;
    }
  }
}

void BoatSystems::storeScenes() const {
  QJsonArray arr;
  for (const auto& s : m_scenes) arr.append(s.toJson());
  const QString path = scenesFile();
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return;
  f.write(QJsonDocument(arr).toJson());
  f.commit();
}

const LightScene* BoatSystems::scene(const QString& id) const {
  for (const auto& s : m_scenes)
    if (s.id == id) return &s;
  return nullptr;
}

void BoatSystems::saveScene(const LightScene& scene) {
  LightScene s = scene;
  if (s.id.isEmpty())
    s.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  auto it = std::find_if(m_scenes.begin(), m_scenes.end(),
                         [&](const LightScene& x) { return x.id == s.id; });
  if (it != m_scenes.end()) {
    s.builtIn = it->builtIn;
    *it = s;
  } else {
    s.builtIn = false;
    m_scenes << s;
  }
  storeScenes();
  emit scenesChanged();
}

void BoatSystems::removeScene(const QString& id) {
  for (int i = 0; i < m_scenes.size(); ++i) {
    if (m_scenes[i].id != id) continue;
    if (m_scenes[i].builtIn) {
      for (const auto& d : defaultScenes())
        if (d.id == id) m_scenes[i] = d;
    } else {
      m_scenes.removeAt(i);
    }
    if (m_scene == id) m_scene.clear();
    storeScenes();
    emit scenesChanged();
    return;
  }
}

LightScene BoatSystems::captureScene(const QString& name) const {
  LightScene s;
  s.name = name;
  s.icon = "bulb";
  for (const BoatLight& l : m_lights) {
    if (l.navigation) continue;
    s.lights << SceneLight{l.id, l.on, l.brightness, l.color};
  }
  return s;
}

void BoatSystems::applyScene(const QString& id) {
  const LightScene* s = scene(id);
  if (!s) return;
  for (const SceneLight& sl : s->lights) {
    BoatLight* l = light(sl.lightId);
    if (!l) continue;
    l->on = sl.on;
    l->brightness = sl.brightness;
    l->color = sl.color;
  }
  m_scene = id;
  emit changed();
}

// --------------------------------------------------------------------------
// Simulation

void BoatSystems::simulate(double secs) {
  if (secs <= 0) return;
  const QDateTime now = QDateTime::currentDateTime();
  const double hour = now.time().hour() + now.time().minute() / 60.0;
  if (m_day != now.date().dayOfYear()) {
    m_day = now.date().dayOfYear();
    m_energy.solarYieldTodayWh = 0;
  }

  // Outside: daily cycle.
  BoatZone* outside = nullptr;
  for (auto& z : m_zones)
    if (z.id == "outside") outside = &z;
  const double outTarget = 13 + 5 * std::sin((hour - 9) / 24 * 2 * M_PI);
  outside->temperatureC +=
      (outTarget - outside->temperatureC) * std::min(1.0, secs / 1800) +
      noise(0.01);

  // Heater state machine (Webasto Air Top). It regulates on the room with
  // the largest deficit; rooms with an own target close their duct flap
  // once they are warm enough.
  Heater& h = m_heater;
  h.stateSecs += secs;
  const BoatZone* room = zone(h.zone);
  const double roomC = room ? room->temperatureC : 18;
  double deficit = -1e9;  // target - temperature, largest over all rooms
  for (const auto& z : m_zones) {
    const double target = h.setpointFor(z.id);
    if (!std::isnan(target))
      deficit = std::max(deficit, target - z.temperatureC);
  }
  switch (h.state) {
    case Heater::State::Starting:
      h.outputPct = 20;
      if (h.stateSecs > 60) {
        h.state = Heater::State::Heating;
        h.stateSecs = 0;
      }
      break;
    case Heater::State::Heating:
      h.outputPct = std::clamp(50 + 60 * deficit, 15.0, 100.0);
      if (deficit < -0.5) {
        h.state = Heater::State::Ventilation;
        h.stateSecs = 0;
      }
      break;
    case Heater::State::Ventilation:
      h.outputPct = 0;
      if (deficit > 0.3) {
        h.state = Heater::State::Starting;
        h.stateSecs = 0;
      }
      break;
    case Heater::State::CoolDown:
      h.outputPct = 0;
      if (h.stateSecs > 120) {
        h.state = Heater::State::Off;
        h.stateSecs = 0;
      }
      break;
    default:
      h.outputPct = 0;
  }
  const bool burning =
      h.state == Heater::State::Heating || h.state == Heater::State::Starting;
  h.fuelLph = burning ? 0.11 + 0.17 * h.outputPct / 100 : 0;
  const double heatW = burning ? 2000 * h.outputPct / 100 : 0;
  h.outletC +=
      ((burning ? roomC + 15 + 35 * h.outputPct / 100 : roomC) - h.outletC) *
      std::min(1.0, secs / 60);
  if (h.state != Heater::State::Off) h.operatingHours += secs / 3600;
  h.supplyV = m_energy.voltage;
  // Electrical draw: glow plug while starting, fan and pump otherwise.
  double heaterElW = 0;
  if (h.state == Heater::State::Starting) heaterElW = 90;
  if (h.state == Heater::State::Heating)
    heaterElW = 12 + 22 * h.outputPct / 100;
  if (h.state == Heater::State::Ventilation ||
      h.state == Heater::State::CoolDown)
    heaterElW = 10;

  // Duct flaps: a room only gets a trickle of air once it is warm (its own
  // target, else the one of the sensor room); the air then goes to the
  // rooms that are still cold.
  QHash<QString, double> share;
  double shareSum = 0, openSum = 0, trickleSum = 0;
  QSet<QString> closed;
  for (const auto& z : m_zones) {
    const double sh = heaterShare(z.id);
    if (sh <= 0) continue;
    double target = h.setpointFor(z.id);
    if (std::isnan(target)) target = h.setpointC;
    shareSum += sh;
    if (z.temperatureC > target + 0.2) {
      closed.insert(z.id);
      trickleSum += sh * 0.05;
    } else {
      openSum += sh;
    }
  }
  for (const auto& z : m_zones) {
    const double sh = heaterShare(z.id);
    share.insert(z.id, closed.contains(z.id) || openSum <= 0
                           ? sh * 0.05
                           : sh * (shareSum - trickleSum) / openSum);
  }

  // Rooms: heat loss to the outside, heater air through the ducts, people
  // and the galley add a little.
  for (auto& z : m_zones) {
    if (z.id == "outside") continue;
    const double loss = (outside->temperatureC - z.temperatureC) / 3600 * 0.35;
    const double gain = heatW * share.value(z.id) / 2000 / 3600 * 20;
    z.temperatureC += (loss + gain) * secs + noise(0.003);
    z.humidityPct = std::clamp(
        z.humidityPct + (burning ? -0.002 : 0.001) * secs + noise(0.02), 35.0,
        90.0);
  }

  // Hot water boiler: thermostat with 5 K hysteresis, slow heat loss.
  WaterHeater& w = m_water;
  if (!w.on)
    w.heating = false;
  else if (w.tempC >= w.targetC)
    w.heating = false;
  else if (w.tempC < w.targetC - 5)
    w.heating = true;
  const double waterLossW = (w.tempC - roomC) * 1.2;
  w.tempC +=
      ((w.heating ? w.powerW : 0) - waterLossW) * secs / (4186.0 * w.capacityL);

  // Tanks: about 22 l of water a day for two, mostly mornings and
  // evenings, while the pump is on; a third ends in the holding tank.
  // Diesel for the heater and the engine.
  BoatCounters& cnt = m_counters;
  bool pump = false;
  for (const auto& c : m_consumers)
    if (c.id == "water_pump") pump = c.on;
  const int hr = now.time().hour();
  const double weight = hr < 7                ? 0.1
                        : hr < 9              ? 3.0
                        : hr == 12            ? 1.5
                        : hr >= 18 && hr < 21 ? 2.5
                                              : 0.8;
  const double waterL = pump ? 22.0 / 24 * weight * secs / 3600 : 0;
  const bool engine = engineRunning();
  const double engineLph =
      engine ? (std::isnan(NavStore::instance().engine().fuelRateLph)
                    ? BoatConfig::instance().consumptionLph
                    : NavStore::instance().engine().fuelRateLph)
             : 0;
  for (auto& t : m_tanks) {
    if (t.kind == BoatTank::Kind::FreshWater) {
      const double used = std::min(waterL, t.level * t.capacityL);
      t.level -= used / t.capacityL;
      cnt.freshL += used;
    }
    if (t.kind == BoatTank::Kind::WasteWater) {
      const double in = std::min(waterL / 3, (1 - t.level) * t.capacityL);
      t.level += in / t.capacityL;
      cnt.wasteL += in;
    }
    if (t.kind == BoatTank::Kind::Fuel) {
      const double used = std::min((h.fuelLph + engineLph) * secs / 3600,
                                   t.level * t.capacityL);
      t.level -= used / t.capacityL;
      cnt.heaterFuelL += h.fuelLph * secs / 3600;
      cnt.engineFuelL += engineLph * secs / 3600;
    }
  }
  if (engine) cnt.engineH += secs / 3600;

  // Energy (Victron): loads, solar, shore power, battery.
  EnergySystem& e = m_energy;
  double loads = heaterElW;
  // Without shore power the boiler runs through the inverter.
  const double boilerW = w.heating ? w.powerW : 0;
  if (!e.shoreConnected) loads += boilerW / 0.9;
  for (const auto& l : m_lights) loads += l.powerW();
  for (const auto& c : m_consumers)
    if (c.on) loads += c.powerW;
  e.loadsW = loads + noise(0.5);

  const double sun = std::max(0.0, std::sin((hour - 6.5) / 13.5 * M_PI));
  const double clouds = 0.75 + 0.2 * std::sin(hour * 2.3) + noise(0.03);
  double solarAvail = e.solarPeakW * sun * std::clamp(clouds, 0.3, 1.0);
  // The MPPT limits the charge when the bank is full.
  if (e.soc > 0.98) {
    e.solarState = solarAvail > 1 ? "Float" : "Off";
    solarAvail = std::min(solarAvail, e.loadsW + 5);
  } else if (e.soc > 0.9) {
    e.solarState = solarAvail > 1 ? "Absorption" : "Off";
  } else {
    e.solarState = solarAvail > 1 ? "Bulk" : "Off";
  }
  e.solarW = solarAvail;
  e.solarV = solarAvail > 1 ? 18 + 2 * sun : 0;
  e.solarYieldTodayWh += e.solarW * secs / 3600;

  if (e.shoreConnected) {
    e.shoreV = 230 + noise(1.5);
    const double maxChargeW = e.soc > 0.98 ? 20 : e.soc > 0.9 ? 180 : 420;
    e.chargerW =
        std::min(maxChargeW + e.loadsW, e.shoreLimitA * e.shoreV * 0.9);
    e.chargerState = e.soc > 0.98  ? "Float"
                     : e.soc > 0.9 ? "Absorption"
                                   : "Bulk";
    e.shoreW = e.chargerW / 0.92 + boilerW;
    e.shoreA = e.shoreW / e.shoreV;
  } else {
    e.shoreV = e.shoreA = e.shoreW = e.chargerW = 0;
    e.chargerState = "Off";
  }

  // The alternator charges while the engine runs.
  e.alternatorW = engine ? (e.soc > 0.98  ? std::min(e.loadsW, e.alternatorMaxW)
                            : e.soc > 0.9 ? e.alternatorMaxW * 0.4
                                          : e.alternatorMaxW)
                         : 0;
  const double netW = e.solarW + e.chargerW + e.alternatorW - e.loadsW;
  cnt.loadsWh += e.loadsW * secs / 3600;
  cnt.heaterWh += heaterElW * secs / 3600;
  cnt.solarWh += e.solarW * secs / 3600;
  cnt.shoreWh += e.chargerW * secs / 3600;
  cnt.alternatorWh += e.alternatorW * secs / 3600;
  e.current = netW / std::max(11.0, e.voltage);
  e.soc = std::clamp(e.soc + e.current * secs / 3600 / e.capacityAh, 0.0, 1.0);
  e.consumedAh = (1 - e.soc) * e.capacityAh;
  // LiFePO4-like curve plus the internal resistance.
  const double rest =
      12.9 + 0.5 * e.soc - (e.soc < 0.1 ? (0.1 - e.soc) * 8 : 0);
  e.voltage =
      rest + e.current * 0.004 + (e.current > 5 && e.soc > 0.9 ? 0.6 : 0);
  e.timeToGoH = e.current < -0.05 ? e.soc * e.capacityAh / -e.current
                                  : std::numeric_limits<double>::quiet_NaN();
  e.batteryTempC += (std::max(15.0, roomC) - e.batteryTempC) * secs / 3600;
  emit changed();
}
