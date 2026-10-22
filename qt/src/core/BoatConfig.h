// Boat setup entered by the skipper: sail plan, propulsion and fuel tank.
#pragma once

#include <QObject>
#include <QString>

struct SailConfig {
  enum class Propulsion { Sailing, MotorSailing, Motor };
  enum class Main { Down, Full, Reef1, Reef2, Reef3 };
  enum class Head { None, Genoa, Jib, Storm };
  enum class Downwind { None, Gennaker, Spinnaker, Code0 };

  Propulsion propulsion = Propulsion::Sailing;
  Main main = Main::Full;
  Head head = Head::Genoa;
  int headFurlPct = 100;  // unfurled share of the headsail
  Downwind downwind = Downwind::None;

  bool motoring() const { return propulsion != Propulsion::Sailing; }
  bool anySail() const {
    return main != Main::Down || head != Head::None ||
           downwind != Downwind::None;
  }
  /** Relative sail area 0..1.4 (full main + genoa = 1). */
  double areaFactor() const;
  /** Short human readable summary, e.g. "Main 1st reef · Genoa 75%". */
  QString summary() const;

  static QString name(Main m);
  static QString name(Head h);
  static QString name(Downwind d);
  static QString name(Propulsion p);
};

class BoatConfig : public QObject {
  Q_OBJECT
public:
  static BoatConfig& instance();

  const SailConfig& sails() const { return m_sails; }
  void setSails(const SailConfig& s);

  // Engine / tank
  double tankLiters = 120;
  double consumptionLph = 3.0;  // at cruising RPM, used without a flow sensor
  double cruiseRpm = 2200;
  double manualFuelPct = 80;  // used when no fuel sensor is present
  double maxRpm = 3600;
  void setEngineSetup(double tank, double consumption, double cruise,
                      double manualFuel);

  void load();
  void save() const;
  /** Tells the listeners about other boat settings (sail inventory). */
  void notifyChanged() { emit changed(); }

signals:
  void changed();

private:
  BoatConfig() = default;
  SailConfig m_sails;
};
