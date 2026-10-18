// Central navigation model: own ship, waypoints, routes, AIS targets and the
// active route guidance. UI widgets observe it through signals.
#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>
#include <limits>

#include "core/Nmea.h"

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

struct OwnShip {
  bool valid = false;
  double lat = 0, lon = 0;
  double sog = kNaN, cog = kNaN, hdg = kNaN;
  int satellites = -1;
  QDateTime lastFix;
  QDateTime gpsTime;
};

/** Wind, water speed and depth. NaN = not available. */
struct Environment {
  double awa = kNaN, aws = kNaN;              // apparent, deg (+ = stbd), kn
  double twa = kNaN, tws = kNaN, twd = kNaN;  // true
  double stw = kNaN, depth = kNaN;
  QDateTime windTime, trueWindTime;
  bool hasWind() const;
};

struct EngineData {
  double rpm = kNaN, coolantC = kNaN, oilBar = kNaN, fuelPct = kNaN;
  double hours = kNaN, voltage = kNaN, fuelRateLph = kNaN;
  QDateTime lastUpdate;
  /** True when RPM data is recent and above idle threshold. */
  bool running() const;
};

/** True wind from apparent wind and boat speed (angles in degrees). */
void trueWindFromApparent(double awa, double aws, double boatSpeed, double& twa,
                          double& tws);

struct Waypoint {
  QString id;
  QString name;
  double lat = 0, lon = 0;
  QString symbol = QStringLiteral("circle");
  QString description;
  QDateTime time;  // track points
};

struct Route {
  QString id;
  QString name;
  QVector<Waypoint> points;
  bool visible = true;
  double lengthNm() const;
};

struct AisTarget {
  quint32 mmsi = 0;
  QString name, callsign;
  int shipType = 0;
  int navStatus = 15;
  double lat = kNaN, lon = kNaN;
  double sog = kNaN, cog = kNaN, hdg = kNaN;
  QDateTime lastSeen;
  double cpaNm = kNaN, tcpaMin = kNaN;
  double rangeNm = kNaN, bearing = kNaN;
  bool dangerous = false;
  bool hasPosition() const { return lat == lat && lon == lon; }
  QString displayName() const;
};

struct Guidance {
  bool active = false;
  QString routeName;
  QString waypointName;
  int legIndex = 0;  // index of the waypoint being steered to
  int legCount = 0;
  double dtwNm = kNaN;   // distance to waypoint
  double btwDeg = kNaN;  // bearing to waypoint
  double xteNm = kNaN;   // cross-track error, + = right of track
  double remainingNm = kNaN;
  double turnDeg = kNaN;  // course change required at the next waypoint
  QDateTime eta;
};

class NavStore : public QObject {
  Q_OBJECT
public:
  static NavStore& instance();

  const OwnShip& ownShip() const { return m_own; }
  const Environment& environment() const { return m_env; }
  const EngineData& engine() const { return m_engine; }
  const QVector<Waypoint>& waypoints() const { return m_waypoints; }
  const QVector<Route>& routes() const { return m_routes; }
  const QHash<quint32, AisTarget>& aisTargets() const { return m_ais; }
  const Guidance& guidance() const { return m_guidance; }
  QString activeRouteId() const { return m_activeRouteId; }
  const Route* route(const QString& id) const;

  // Own ship
  void applyNavUpdate(const NavUpdate& u);
  void setOwnShip(const OwnShip& s);

  // Waypoints
  QString addWaypoint(Waypoint wp);
  void removeWaypoint(const QString& id);
  void setWaypoints(const QVector<Waypoint>& wps);

  // Routes
  QString addRoute(Route r);
  void updateRoute(const Route& r);
  void removeRoute(const QString& id);
  void setRoutes(const QVector<Route>& routes);

  // Navigation
  void activateRoute(const QString& id, int startLeg = -1);
  /** Creates a temporary "Go to" route from own ship to the position. */
  void goTo(const QString& name, double lat, double lon);
  void stopNavigation();
  void advanceLeg();
  /** Man overboard: drops a mark at own ship and navigates back to it. */
  void manOverboard();

  // AIS
  void applyAis(const AisMessage& m);
  void upsertAisTarget(const AisTarget& t);
  void expireAis(int maxAgeSecs = 600);

  // Persistence (GPX in the application data directory)
  QString storagePath() const;
  void loadFromDisk();
  void saveToDisk() const;

  static QString newId();

signals:
  void ownShipChanged();
  void environmentChanged();
  void engineChanged();
  void waypointsChanged();
  void routesChanged();
  void aisChanged();
  void guidanceChanged();
  void alarm(const QString& message);

private:
  NavStore();
  void updateGuidance();
  void updateAisDerived(AisTarget& t);

  OwnShip m_own;
  Environment m_env;
  EngineData m_engine;
  QVector<Waypoint> m_waypoints;
  QVector<Route> m_routes;
  QHash<quint32, AisTarget> m_ais;
  QString m_activeRouteId;
  int m_activeLeg = 0;
  // Arrival only counts after the boat has been outside the arrival radius
  // on the current leg (important for MOB, which starts at the target).
  bool m_legArmed = false;
  Guidance m_guidance;
  QSet<quint32> m_alarmed;
};
