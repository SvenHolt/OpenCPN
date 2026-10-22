// Sail planning along a route: the best sail configuration and trim for
// each board, and where to tack and gybe.
//
// SailAdvisor picks sails (reefs, headsail and furling, downwind sails)
// from the apparent wind and the boat's sail inventory, and describes the
// trim (boom angle, traveller, sheets, cars, outhaul, backstay, vang,
// preventer, pole). planSailing() turns a route into boards: legs that
// cannot be sailed directly are beaten (or gybed downwind) in a lane
// around the rhumb line, each tack and gybe gets a position and a time,
// and sail changes are placed where the wind requires them.
#pragma once

#include <QDateTime>
#include <QPointF>
#include <QStringList>
#include <QVector>
#include <functional>

#include "core/BoatConfig.h"
#include "core/PassagePlanner.h"

/** The sails on board and when to reef (apparent wind, kn). */
struct SailInventory {
  bool genoa = true, jib = false, storm = false;
  bool gennaker = true, spinnaker = false, code0 = false;
  int reefs = 3;
  double reef1Aws = 18, reef2Aws = 23, reef3Aws = 28;
  double furlStartAws = 15;    // the genoa is furled from here on
  double downwindMaxTws = 15;  // gennaker/spinnaker up to this true wind

  static SailInventory load();  // QSettings "SailInventory/…"
  void save() const;
};

struct SailTrim {
  QString pointOfSail;        // close-hauled, beam reach, …
  bool starboardTack = true;  // wind from starboard, boom to port
  double boomDeg = 0;         // boom angle off the centreline
  QString mainsheet, traveller, vang, outhaul, backstay, cunningham;
  QString headsail, car;
  QString extra;  // preventer, pole, wing-on-wing, gennaker tack line …
  /** All instructions, one per line. */
  QStringList lines() const;
};

struct SailAdvice {
  SailConfig sails;  // propulsion = Sailing
  SailTrim trim;
  double tws = 0, twa = 0, aws = 0, awa = 0;  // twa/awa 0..180
  QString note;                               // e.g. "Too little wind to sail"
  /** "Main 1st reef · Genoa 70% · Gennaker". */
  QString summary() const;
  /** Same sails (genoa furling compared in 10 % steps). */
  bool sameSails(const SailAdvice& o) const;
};

namespace SailAdvisor {
/** Apparent wind (kn, angle 0..180) from true wind and boat speed. */
void apparent(double tws, double twa, double boatKn, double& aws, double& awa);
/** Best sails and trim; `twaSigned` > 0 = wind from starboard. */
SailAdvice advise(double tws, double twaSigned, double boatKn,
                  const SailInventory& inv);
QString pointOfSail(double twa);
}  // namespace SailAdvisor

struct SailBoard {
  int leg = 0;       // route leg (0 = first)
  QPointF from, to;  // lon, lat
  QDateTime start, end;
  double headingDeg = 0, distanceNm = 0, speedKn = 0;
  double tws = 0, twd = 0;
  double twa = 0;  // signed, + = wind from starboard
  bool motor = false;
  PassageLeg::Mode mode = PassageLeg::Mode::Sail;
  SailAdvice advice;
  double hours() const { return start.secsTo(end) / 3600.0; }
};

struct SailEvent {
  enum class Kind {
    Start,
    Tack,
    Gybe,
    SailChange,
    MotorOn,
    MotorOff,
    Waypoint,
    Arrival
  };
  Kind kind = Kind::Start;
  QPointF pos;  // lon, lat
  QDateTime time;
  QString text;
  static QString kindName(Kind k);
};

struct SailingPlan {
  QVector<SailBoard> boards;
  QVector<SailEvent> events;
  QVector<QPointF> track;  // lon, lat: start, board ends
  int tacks = 0, gybes = 0, sailChanges = 0;
  double sailedNm = 0, hours = 0, motorHours = 0;
  QDateTime departure, arrival;
  QStringList warnings;
  /** Events of one kind (tests, map). */
  QVector<SailEvent> eventsOf(SailEvent::Kind k) const;
};

/** True if the position is navigable water (land check of boards). */
using WaterTest = std::function<bool(double lat, double lon)>;

namespace SailPlanner {
/** Boards, tacks, gybes and sail changes for a route (x = lon, y = lat).
 *  Boards stay within `corridorNm` of the rhumb line of each leg and,
 *  with a water test, off land. */
SailingPlan plan(const QVector<QPointF>& route, const PassageSettings& s,
                 const WindSource& wind, const SailInventory& inv,
                 double corridorNm = 1.0, const WaterTest& water = {});
}  // namespace SailPlanner
