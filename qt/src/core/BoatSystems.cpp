#include "core/BoatSystems.h"

#include <QCoreApplication>
#include <QRandomGenerator>
#include <algorithm>

#include "core/BoatConfig.h"

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

  // Lights: evening on the start.
  applyScene("evening");
  m_energy.soc = 0.82;
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

void BoatSystems::setShoreConnected(bool on) {
  m_energy.shoreConnected = on;
  emit changed();
}

QVector<LightScene> BoatSystems::scenes() {
  return {{"night", tr("Night sailing"), "moon"},
          {"evening", tr("Evening"), "sun"},
          {"bright", tr("Bright"), "sun"},
          {"anchor", tr("At anchor"), "anchor"},
          {"off", tr("All off"), "close"}};
}

void BoatSystems::applyScene(const QString& id) {
  // Per light: on, brightness, color.
  struct Setting {
    bool on;
    double level;
    QColor color;
  };
  const QColor red(255, 28, 0), warm(255, 176, 110), white(255, 240, 222);
  const auto setting = [&](const BoatLight& l) -> Setting {
    if (id == "night") {
      // Red and dim so the eyes stay adapted to the dark; only where the
      // crew moves at night.
      const bool used = l.id == "saloon_ceiling" || l.id == "chart_table" ||
                        l.id == "courtesy" || l.id == "galley_spot" ||
                        l.id == "heads_ceiling";
      return {used, l.id == "chart_table" ? 0.12 : 0.08, red};
    }
    if (id == "evening") {
      const bool used =
          l.zone == "saloon" || l.zone == "galley" || l.id == "heads_ceiling";
      return {used && l.id != "chart_table", 0.5, warm};
    }
    if (id == "bright") return {true, 1.0, white};
    if (id == "anchor") {
      const bool used = l.id == "saloon_indirect" || l.id == "courtesy";
      return {used, 0.25, warm};
    }
    return {false, l.brightness, l.color};
  };
  for (BoatLight& l : m_lights) {
    if (l.navigation) continue;
    const Setting s = setting(l);
    l.on = s.on;
    l.brightness = s.level;
    l.color = s.color;
  }
  // At anchor: the anchor light, not the running lights.
  if (id == "anchor") {
    for (BoatLight& l : m_lights) {
      if (l.id == "anchor_light") l.on = true;
      if (l.id == "nav_lights" || l.id == "steaming") l.on = false;
    }
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

  // Heater state machine (Webasto Air Top).
  Heater& h = m_heater;
  h.stateSecs += secs;
  const BoatZone* room = zone(h.zone);
  const double roomC = room ? room->temperatureC : 18;
  switch (h.state) {
    case Heater::State::Starting:
      h.outputPct = 20;
      if (h.stateSecs > 60) {
        h.state = Heater::State::Heating;
        h.stateSecs = 0;
      }
      break;
    case Heater::State::Heating: {
      const double diff = h.setpointC - roomC;
      h.outputPct = std::clamp(30 + 35 * diff, 15.0, 100.0);
      if (roomC > h.setpointC + 1.0) {
        h.state = Heater::State::Ventilation;
        h.stateSecs = 0;
      }
      break;
    }
    case Heater::State::Ventilation:
      h.outputPct = 0;
      if (roomC < h.setpointC - 0.5) {
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

  // Rooms: heat loss to the outside, heater air through the ducts, people
  // and the galley add a little.
  for (auto& z : m_zones) {
    if (z.id == "outside") continue;
    const double loss = (outside->temperatureC - z.temperatureC) / 3600 * 0.35;
    const double gain = heatW * heaterShare(z.id) / 2000 / 3600 * 9;
    z.temperatureC += (loss + gain) * secs + noise(0.003);
    z.humidityPct = std::clamp(
        z.humidityPct + (burning ? -0.002 : 0.001) * secs + noise(0.02), 35.0,
        90.0);
  }

  // Tanks: water use while the pump is on, the holding tank fills slowly.
  for (auto& c : m_consumers)
    if (c.id == "water_pump" && c.on)
      for (auto& t : m_tanks) {
        if (t.kind == BoatTank::Kind::FreshWater)
          t.level = std::max(0.0, t.level - 0.4 / t.capacityL * secs / 600);
        if (t.kind == BoatTank::Kind::WasteWater)
          t.level = std::min(1.0, t.level + 0.15 / t.capacityL * secs / 600);
      }
  for (auto& t : m_tanks)
    if (t.kind == BoatTank::Kind::Fuel)
      t.level = std::max(0.0, t.level - h.fuelLph * secs / 3600 / t.capacityL);

  // Energy (Victron): loads, solar, shore power, battery.
  EnergySystem& e = m_energy;
  double loads = heaterElW;
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
    e.shoreW = e.chargerW / 0.92;
    e.shoreA = e.shoreW / e.shoreV;
  } else {
    e.shoreV = e.shoreA = e.shoreW = e.chargerW = 0;
    e.chargerState = "Off";
  }

  const double netW = e.solarW + e.chargerW - e.loadsW;
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
