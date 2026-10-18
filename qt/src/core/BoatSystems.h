// On-board systems: cabin temperatures, tanks, Victron energy system
// (solar, battery bank, shore power), lights with scenes, consumers and a
// Webasto diesel heater.
//
// Every value carries its Signal K path. Until the Signal K connection and
// the digital switching hardware exist, a simulator provides plausible,
// slowly changing values and executes the commands (switching, scenes,
// heater) locally.
#pragma once

#include <QColor>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>
#include <cmath>
#include <limits>

/** A room of the boat (Dufour 2800: forward cabin, saloon, galley, heads,
 *  aft cabin) or the outside. */
struct BoatZone {
  QString id, name;
  double temperatureC = std::numeric_limits<double>::quiet_NaN();
  double humidityPct = std::numeric_limits<double>::quiet_NaN();
  QString signalKPath;  // environment.inside.<id>.temperature
};

struct BoatTank {
  QString id, name;
  enum class Kind { FreshWater, WasteWater, Fuel } kind = Kind::FreshWater;
  double capacityL = 0;
  double level = 0;     // 0..1
  QString signalKPath;  // tanks.<type>.<n>.currentLevel
};

struct BoatLight {
  QString id, name, zone;
  bool on = false;
  double brightness = 1.0;  // 0..1
  QColor color = QColor(255, 236, 200);
  double maxPowerW = 3;
  bool navigation = false;  // running/anchor lights, not part of scenes
  QString signalKPath;      // electrical.switches.<id>.state
  double powerW() const { return on ? maxPowerW * brightness : 0; }
};

struct BoatConsumer {
  QString id, name, zone;
  bool on = false;
  double powerW = 0;  // while on (average for cycling loads)
  bool switchable = true;
  QString signalKPath;
};

/** Victron: SmartShunt/BMV, SmartSolar MPPT and MultiPlus (shore power). */
struct EnergySystem {
  // Battery bank
  double capacityAh = 200;
  double soc = 0.8;       // 0..1
  double voltage = 12.8;  // V
  double current = 0;     // A, + = charging
  double batteryTempC = 18;
  double consumedAh = 0;
  double timeToGoH = std::numeric_limits<double>::quiet_NaN();
  // Solar
  double solarPeakW = 200;
  double solarW = 0, solarV = 0;
  double solarYieldTodayWh = 0;
  QString solarState = "Off";  // Off, Bulk, Absorption, Float
  // Shore power / charger
  bool shoreConnected = false;
  double shoreV = 0, shoreA = 0, shoreW = 0;
  double shoreLimitA = 16;
  double chargerW = 0;
  QString chargerState = "Off";
  // Loads
  double loadsW = 0;
};

/** Webasto Air Top (diesel air heater). */
struct Heater {
  enum class State { Off, Starting, Heating, Ventilation, CoolDown, Fault };
  State state = State::Off;
  double setpointC = 20;
  double outputPct = 0;  // heating power 0..100
  double outletC = 18;   // air outlet temperature
  double supplyV = 12.8;
  double fuelLph = 0;
  double operatingHours = 312;
  double stateSecs = 0;  // time in the current state
  int faultCode = 0;
  QString zone = "saloon";  // room sensor
  static QString stateName(State s);
};

struct LightScene {
  QString id, name, icon;
};

class BoatSystems : public QObject {
  Q_OBJECT
public:
  static BoatSystems& instance();

  const QVector<BoatZone>& zones() const { return m_zones; }
  const QVector<BoatTank>& tanks() const { return m_tanks; }
  const QVector<BoatLight>& lights() const { return m_lights; }
  const QVector<BoatConsumer>& consumers() const { return m_consumers; }
  const EnergySystem& energy() const { return m_energy; }
  const Heater& heater() const { return m_heater; }
  const BoatZone* zone(const QString& id) const;
  double outsideC() const;

  /** True while the values come from the simulator (no Signal K yet). */
  bool simulated() const { return true; }

  // --- Commands (later sent via Signal K / digital switching) -------------
  void setLight(const QString& id, bool on);
  void setLightLevel(const QString& id, double brightness, const QColor& c);
  void setConsumer(const QString& id, bool on);
  void setHeaterOn(bool on);
  void setHeaterSetpoint(double c);
  void setShoreConnected(bool on);  // simulator only

  // --- Light scenes --------------------------------------------------------
  static QVector<LightScene> scenes();
  /** Sets brightness and color of every cabin light; navigation lights are
   *  left alone. */
  void applyScene(const QString& id);
  QString activeScene() const { return m_scene; }

  /** Advances the simulation by `secs` (also driven by a timer). */
  void simulate(double secs);

signals:
  void changed();

private:
  BoatSystems();
  void setup();
  BoatLight* light(const QString& id);

  QVector<BoatZone> m_zones;
  QVector<BoatTank> m_tanks;
  QVector<BoatLight> m_lights;
  QVector<BoatConsumer> m_consumers;
  EnergySystem m_energy;
  Heater m_heater;
  QString m_scene = "off";
  QTimer m_timer;
  QDateTime m_lastStep;
  int m_day = -1;
};
