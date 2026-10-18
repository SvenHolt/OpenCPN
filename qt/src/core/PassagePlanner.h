// Passage planning for a route: departure time window, sailing and motor
// legs, tacks and gybes, duration, arrival, fuel and daylight.
//
// The boat speed comes from a simple polar model scaled by the boat's
// hull speed and the set sail area. Wind comes from the loaded GRIB file
// (interpolated in space and time) or from manual values.
#pragma once

#include <QDateTime>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

struct PassageSettings {
  QDateTime departure;
  /** Latest departure; departures from `departure` up to this time are
   *  evaluated every `windowStepMin` minutes. Invalid = no window. */
  QDateTime latestDeparture;
  int windowStepMin = 60;
  enum class Optimize { Duration, Sailing, Daylight };
  Optimize optimize = Optimize::Duration;

  enum class Strategy {
    Sail,       // sail whenever possible, motor only in calms / too slow
    MotorSail,  // whichever is faster (motor sailing counts as motor)
    Motor       // motor only
  };
  Strategy strategy = Strategy::Sail;

  double hullSpeedKn = 7.0;     // best sailing speed of the boat
  double motorSpeedKn = 6.0;    // at cruising RPM
  double fuelLph = 3.0;         // consumption at cruising RPM
  double sailAreaFactor = 1.0;  // SailConfig::areaFactor()
  bool downwindSail = false;    // gennaker/spinnaker set
  double noGoDeg = 45;          // closest true wind angle when beating
  double runDeg = 160;          // deepest angle before gybing downwind
  double minSailWindKn = 5;     // less wind: motor
  double minSailSpeedKn = 2.5;  // slower made good: motor
  double boardNm = 1.5;         // length of a board when tacking/gybing

  // Wind when no GRIB data covers the time and place.
  double manualWindFromDeg = 270, manualWindKn = 12;
};

/** Wind at a place and time: speed (kn) and direction (from, deg true).
 *  Returns false if not available. */
using WindSource = std::function<bool(
    double lat, double lon, const QDateTime& t, double& kn, double& fromDeg)>;

struct PassageLeg {
  enum class Mode { Sail, Beat, Run, Motor };
  int index = 0;  // 0 = first leg
  double distanceNm = 0, courseDeg = 0;
  double windKn = 0, windFromDeg = 0, twaDeg = 0;  // at the leg's middle
  bool gribWind = false;
  Mode mode = Mode::Sail;
  double sailedNm = 0;  // incl. the extra distance of tacking
  double speedKn = 0;   // through the water on the boards / under engine
  double hours = 0;
  int tacks = 0, gybes = 0;
  double motorHours = 0;
  QDateTime start, end;
};

struct PassagePlan {
  QVector<PassageLeg> legs;
  QDateTime departure, arrival;
  double distanceNm = 0, sailedNm = 0, hours = 0;
  double motorHours = 0, sailHours = 0;
  int tacks = 0, gybes = 0;
  double fuelLiters = 0;
  double nightHours = 0;
  double maxWindKn = 0;
  bool gribUsed = false, manualUsed = false;
  QDateTime sunrise, sunset;  // at the destination on the arrival day
  QStringList warnings;
  double sailShare() const { return hours > 0 ? sailHours / hours : 0; }
};

/** One evaluated departure of a time window. */
struct DepartureOption {
  QDateTime departure;
  double hours = 0, sailShare = 0, nightHours = 0;
  int tacks = 0;
  bool best = false;
};

namespace PassagePlanner {
/** Plans a single departure. `route` is x = lon, y = lat. */
PassagePlan plan(const QVector<QPointF>& route, const PassageSettings& s,
                 const WindSource& wind);
/** Evaluates the departure window and returns the best plan; `options`
 *  receives every evaluated departure (for a timeline). */
PassagePlan planWindow(const QVector<QPointF>& route, const PassageSettings& s,
                       const WindSource& wind,
                       QVector<DepartureOption>* options = nullptr);

/** Boat speed through the water (kn) for true wind and true wind angle. */
double boatSpeed(const PassageSettings& s, double windKn, double twaDeg);

/** Sunrise and sunset (UTC) at a position on a date; invalid in polar
 *  day/night. NOAA's simplified algorithm, about ±2 minutes. */
void sunTimes(double lat, double lon, const QDate& date, QDateTime& sunrise,
              QDateTime& sunset);
}  // namespace PassagePlanner
