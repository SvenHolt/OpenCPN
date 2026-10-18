#include "core/NavStore.h"

#include <algorithm>
#include <QDir>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <cmath>

#include "core/AppSettings.h"
#include "core/GeoMath.h"
#include "core/GpxIO.h"

static const char* kGotoRouteName = "Go to";

double Route::lengthNm() const {
  double d = 0;
  for (int i = 1; i < points.size(); ++i)
    d += geo::distanceNm(points[i - 1].lat, points[i - 1].lon, points[i].lat,
                         points[i].lon);
  return d;
}

QString AisTarget::displayName() const {
  return name.isEmpty() ? QString::number(mmsi) : name;
}

bool Environment::hasWind() const {
  return windTime.isValid() &&
         windTime.secsTo(QDateTime::currentDateTimeUtc()) < 15 &&
         !std::isnan(aws);
}

bool EngineData::running() const {
  return lastUpdate.isValid() &&
         lastUpdate.secsTo(QDateTime::currentDateTimeUtc()) < 10 &&
         rpm == rpm && rpm > 100;
}

void trueWindFromApparent(double awa, double aws, double v, double& twa,
                          double& tws) {
  const double a = awa * M_PI / 180;
  const double x = aws * std::cos(a) - v;  // along the bow
  const double y = aws * std::sin(a);      // to starboard
  tws = std::hypot(x, y);
  twa = std::atan2(y, x) * 180 / M_PI;
}

NavStore& NavStore::instance() {
  static NavStore s;
  return s;
}

NavStore::NavStore() {
  connect(&AppSettings::instance(), &AppSettings::changed, this, [this] {
    for (auto& t : m_ais) updateAisDerived(t);
    emit aisChanged();
  });
}

QString NavStore::newId() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

const Route* NavStore::route(const QString& id) const {
  for (const auto& r : m_routes)
    if (r.id == id) return &r;
  return nullptr;
}

void NavStore::applyNavUpdate(const NavUpdate& u) {
  const auto now = QDateTime::currentDateTimeUtc();
  bool envChanged = false;
  if (u.stw) m_env.stw = *u.stw, envChanged = true;
  if (u.depth) m_env.depth = *u.depth, envChanged = true;
  if (u.heel) m_env.heel = *u.heel, envChanged = true;
  if (u.twa || u.tws || u.twd) {
    if (u.twa) m_env.twa = *u.twa;
    if (u.tws) m_env.tws = *u.tws;
    if (u.twd) m_env.twd = *u.twd;
    m_env.trueWindTime = now;
    m_env.windTime = now;
    envChanged = true;
  }
  if (u.awa && u.aws) {
    m_env.awa = *u.awa;
    m_env.aws = *u.aws;
    m_env.windTime = now;
    // Derive true wind unless a sensor delivers it.
    const bool haveTrue =
        m_env.trueWindTime.isValid() && m_env.trueWindTime.secsTo(now) < 5;
    if (!haveTrue) {
      const double v = m_env.stw == m_env.stw   ? m_env.stw
                       : m_own.sog == m_own.sog ? m_own.sog
                                                : 0.0;
      trueWindFromApparent(m_env.awa, m_env.aws, v, m_env.twa, m_env.tws);
    }
    envChanged = true;
  }
  if (envChanged) {
    const double hdg = m_own.hdg == m_own.hdg ? m_own.hdg : m_own.cog;
    if (hdg == hdg && m_env.twa == m_env.twa &&
        !(m_env.trueWindTime.isValid() && u.twd))
      m_env.twd = geo::normalizeDeg(hdg + m_env.twa);
    emit environmentChanged();
  }
  if (u.rpm || u.coolantC || u.oilBar || u.fuelPct || u.engineHours ||
      u.voltage || u.fuelRateLph) {
    if (u.rpm) m_engine.rpm = *u.rpm;
    if (u.coolantC) m_engine.coolantC = *u.coolantC;
    if (u.oilBar) m_engine.oilBar = *u.oilBar;
    if (u.fuelPct) m_engine.fuelPct = *u.fuelPct;
    if (u.engineHours) m_engine.hours = *u.engineHours;
    if (u.voltage) m_engine.voltage = *u.voltage;
    if (u.fuelRateLph) m_engine.fuelRateLph = *u.fuelRateLph;
    if (u.rpm) m_engine.lastUpdate = now;
    emit engineChanged();
  }
  const bool navData = u.lat || u.lon || u.sog || u.cog || u.hdg ||
                       u.satellites || u.time || u.fixLost;
  if (!navData) return;
  if (u.fixLost) {
    if (m_own.valid) {
      m_own.valid = false;
      emit ownShipChanged();
    }
    return;
  }
  if (u.lat && u.lon) {
    m_own.lat = *u.lat;
    m_own.lon = *u.lon;
    m_own.valid = true;
    m_own.lastFix = QDateTime::currentDateTimeUtc();
  }
  if (u.sog) m_own.sog = *u.sog;
  if (u.cog) m_own.cog = *u.cog;
  if (u.hdg) m_own.hdg = *u.hdg;
  if (u.satellites) m_own.satellites = *u.satellites;
  if (u.time) m_own.gpsTime = *u.time;
  emit ownShipChanged();
  if (u.lat && u.lon) {
    updateGuidance();
    for (auto& t : m_ais) updateAisDerived(t);
  }
}

void NavStore::setOwnShip(const OwnShip& s) {
  m_own = s;
  emit ownShipChanged();
  updateGuidance();
}

QString NavStore::addWaypoint(Waypoint wp) {
  if (wp.id.isEmpty()) wp.id = newId();
  if (wp.name.isEmpty())
    wp.name =
        QStringLiteral("WP %1").arg(m_waypoints.size() + 1, 3, 10, QChar('0'));
  m_waypoints.push_back(wp);
  emit waypointsChanged();
  saveToDisk();
  return wp.id;
}

void NavStore::removeWaypoint(const QString& id) {
  m_waypoints.erase(
      std::remove_if(m_waypoints.begin(), m_waypoints.end(),
                     [&](const Waypoint& w) { return w.id == id; }),
      m_waypoints.end());
  emit waypointsChanged();
  saveToDisk();
}

void NavStore::setWaypoints(const QVector<Waypoint>& wps) {
  m_waypoints = wps;
  emit waypointsChanged();
}

QString NavStore::addRoute(Route r) {
  if (r.id.isEmpty()) r.id = newId();
  if (r.name.isEmpty()) r.name = tr("Route %1").arg(m_routes.size() + 1);
  for (auto& p : r.points)
    if (p.id.isEmpty()) p.id = newId();
  m_routes.push_back(r);
  emit routesChanged();
  saveToDisk();
  return r.id;
}

void NavStore::updateRoute(const Route& r) {
  for (auto& x : m_routes)
    if (x.id == r.id) x = r;
  emit routesChanged();
  if (r.id == m_activeRouteId) updateGuidance();
  saveToDisk();
}

void NavStore::removeRoute(const QString& id) {
  if (id == m_activeRouteId) stopNavigation();
  m_routes.erase(std::remove_if(m_routes.begin(), m_routes.end(),
                                [&](const Route& r) { return r.id == id; }),
                 m_routes.end());
  emit routesChanged();
  saveToDisk();
}

void NavStore::setRoutes(const QVector<Route>& routes) {
  m_routes = routes;
  emit routesChanged();
}

void NavStore::activateRoute(const QString& id, int startLeg) {
  const Route* r = route(id);
  if (!r || r->points.size() < 2) return;
  m_activeRouteId = id;
  if (startLeg < 1) {
    // Start with the leg whose end point is closest ahead of us.
    startLeg = 1;
    if (m_own.valid) {
      double best = 1e12;
      for (int i = 0; i < r->points.size(); ++i) {
        const double d = geo::distanceNm(m_own.lat, m_own.lon, r->points[i].lat,
                                         r->points[i].lon);
        if (d < best) {
          best = d;
          startLeg = i;
        }
      }
      startLeg = qBound(1, startLeg, int(r->points.size()) - 1);
    }
  }
  m_activeLeg = startLeg;
  m_legArmed = false;
  updateGuidance();
}

void NavStore::goTo(const QString& name, double lat, double lon) {
  // Replace a previous temporary route.
  for (const auto& r : m_routes)
    if (r.name == kGotoRouteName) {
      removeRoute(r.id);
      break;
    }
  if (!m_own.valid) return;
  Route r;
  r.name = kGotoRouteName;
  Waypoint from;
  from.name = tr("Start");
  from.lat = m_own.lat;
  from.lon = m_own.lon;
  Waypoint to;
  to.name = name;
  to.lat = lat;
  to.lon = lon;
  r.points = {from, to};
  activateRoute(addRoute(r), 1);
}

void NavStore::stopNavigation() {
  m_activeRouteId.clear();
  m_activeLeg = 0;
  updateGuidance();
}

void NavStore::advanceLeg() {
  const Route* r = route(m_activeRouteId);
  if (!r) return;
  if (m_activeLeg + 1 >= r->points.size()) {
    emit alarm(tr("Arrived at %1").arg(r->points.last().name));
    stopNavigation();
    return;
  }
  ++m_activeLeg;
  m_legArmed = false;
  updateGuidance();
}

void NavStore::manOverboard() {
  if (!m_own.valid) return;
  Waypoint wp;
  wp.name = tr("MOB %1").arg(QDateTime::currentDateTime().toString("HH:mm"));
  wp.symbol = "mob";
  wp.lat = m_own.lat;
  wp.lon = m_own.lon;
  addWaypoint(wp);
  goTo(wp.name, wp.lat, wp.lon);
  emit alarm(tr("Man overboard! Navigating back to MOB position."));
}

void NavStore::updateGuidance() {
  Guidance g;
  const Route* r = route(m_activeRouteId);
  if (r && m_own.valid && m_activeLeg > 0 && m_activeLeg < r->points.size()) {
    const Waypoint& from = r->points[m_activeLeg - 1];
    const Waypoint& to = r->points[m_activeLeg];
    g.active = true;
    g.routeName = r->name;
    g.waypointName = to.name;
    g.legIndex = m_activeLeg;
    g.legCount = r->points.size() - 1;
    g.dtwNm = geo::distanceNm(m_own.lat, m_own.lon, to.lat, to.lon);
    g.btwDeg = geo::bearingDeg(m_own.lat, m_own.lon, to.lat, to.lon);
    g.xteNm = geo::crossTrackNm(from.lat, from.lon, to.lat, to.lon, m_own.lat,
                                m_own.lon);
    g.remainingNm = g.dtwNm;
    for (int i = m_activeLeg + 1; i < r->points.size(); ++i)
      g.remainingNm +=
          geo::distanceNm(r->points[i - 1].lat, r->points[i - 1].lon,
                          r->points[i].lat, r->points[i].lon);
    if (m_activeLeg + 1 < r->points.size()) {
      const Waypoint& next = r->points[m_activeLeg + 1];
      g.turnDeg =
          geo::angleDiff(geo::bearingDeg(from.lat, from.lon, to.lat, to.lon),
                         geo::bearingDeg(to.lat, to.lon, next.lat, next.lon));
    }
    if (m_own.sog == m_own.sog && m_own.sog > 0.2)
      g.eta = QDateTime::currentDateTime().addSecs(
          qint64(g.remainingNm / m_own.sog * 3600));

    const double radius = AppSettings::instance().arrivalRadiusNm;
    if (g.dtwNm > radius) m_legArmed = true;
    if (m_legArmed && g.dtwNm < radius) {
      m_guidance = g;
      advanceLeg();
      return;
    }
  }
  m_guidance = g;
  emit guidanceChanged();
}

void NavStore::updateAisDerived(AisTarget& t) {
  if (!m_own.valid || !t.hasPosition()) return;
  const auto& cfg = AppSettings::instance();
  t.rangeNm = geo::distanceNm(m_own.lat, m_own.lon, t.lat, t.lon);
  t.bearing = geo::bearingDeg(m_own.lat, m_own.lon, t.lat, t.lon);
  const auto cpa = geo::computeCpa(m_own.lat, m_own.lon, m_own.sog, m_own.cog,
                                   t.lat, t.lon, t.sog, t.cog);
  t.cpaNm = cpa.valid ? cpa.cpaNm : kNaN;
  t.tcpaMin = cpa.valid ? cpa.tcpaMin : kNaN;
  const bool danger = cpa.valid && cpa.cpaNm < cfg.cpaAlarmNm &&
                      cpa.tcpaMin > 0 && cpa.tcpaMin < cfg.tcpaAlarmMin;
  if (danger && !t.dangerous && !m_alarmed.contains(t.mmsi)) {
    m_alarmed.insert(t.mmsi);
    emit alarm(tr("Collision risk: %1 – CPA %2 in %3 min")
                   .arg(t.displayName(),
                        AppSettings::instance().distanceText(cpa.cpaNm))
                   .arg(cpa.tcpaMin, 0, 'f', 0));
  }
  if (!danger) m_alarmed.remove(t.mmsi);
  t.dangerous = danger;
}

void NavStore::applyAis(const AisMessage& m) {
  if (m.own || m.mmsi == 0) return;
  AisTarget& t = m_ais[m.mmsi];
  t.mmsi = m.mmsi;
  if (m.lat) t.lat = *m.lat;
  if (m.lon) t.lon = *m.lon;
  if (m.sog) t.sog = *m.sog;
  if (m.cog) t.cog = *m.cog;
  if (m.hdg) t.hdg = *m.hdg;
  if (m.navStatus) t.navStatus = *m.navStatus;
  if (m.shipType) t.shipType = *m.shipType;
  if (m.name && !m.name->isEmpty()) t.name = *m.name;
  if (m.callsign && !m.callsign->isEmpty()) t.callsign = *m.callsign;
  t.lastSeen = QDateTime::currentDateTimeUtc();
  updateAisDerived(t);
  emit aisChanged();
}

void NavStore::upsertAisTarget(const AisTarget& in) {
  AisTarget& t = m_ais[in.mmsi];
  t = in;
  t.lastSeen = QDateTime::currentDateTimeUtc();
  updateAisDerived(t);
  emit aisChanged();
}

void NavStore::expireAis(int maxAgeSecs) {
  const auto now = QDateTime::currentDateTimeUtc();
  bool changed = false;
  for (auto it = m_ais.begin(); it != m_ais.end();) {
    if (it->lastSeen.secsTo(now) > maxAgeSecs) {
      it = m_ais.erase(it);
      changed = true;
    } else {
      ++it;
    }
  }
  if (changed) emit aisChanged();
}

QString NavStore::storagePath() const {
  const QString dir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dir);
  return dir + "/navobj.gpx";
}

void NavStore::loadFromDisk() {
  GpxData d;
  if (GpxIO::read(storagePath(), d)) {
    m_waypoints = d.waypoints;
    m_routes.clear();
    for (const auto& r : d.routes)
      if (r.name != kGotoRouteName) m_routes.push_back(r);
    emit waypointsChanged();
    emit routesChanged();
  }
}

void NavStore::saveToDisk() const {
  GpxData d;
  d.waypoints = m_waypoints;
  for (const auto& r : m_routes)
    if (r.name != kGotoRouteName) d.routes.push_back(r);
  GpxIO::write(storagePath(), d);
}
