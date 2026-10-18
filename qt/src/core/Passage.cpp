#include "core/Passage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <algorithm>
#include <cmath>

#include "core/BoatConfig.h"
#include "core/GeoMath.h"
#include "core/Grib.h"
#include "core/NavStore.h"
#include "core/TrackRecorder.h"

namespace {
QString U(const std::string& s) { return QString::fromStdString(s); }
std::string S(const QString& s) { return s.toStdString(); }

QString compass(double deg) {
  static const char* const names[] = {"N",  "NNE", "NE", "ENE", "E",  "ESE",
                                      "SE", "SSE", "S",  "SSW", "SW", "WSW",
                                      "W",  "WNW", "NW", "NNW"};
  return names[int(std::floor(std::fmod(deg + 11.25 + 360, 360) / 22.5)) % 16];
}

QString timeText(time_t t) {
  if (t <= 0) return QStringLiteral("–");
  return QDateTime::fromSecsSinceEpoch(t).toString("ddd dd.MM. HH:mm");
}
}  // namespace

PassageService& PassageService::instance() {
  static PassageService s;
  return s;
}

PassageService::PassageService() {
  m_timer.setInterval(60 * 1000);
  connect(&m_timer, &QTimer::timeout, this, &PassageService::onTimer);
  loadSettings();
}

QString PassageService::defaultDatabasePath() {
#ifdef Q_OS_LINUX
  // Same rule as wxStandardPaths and tools/seachart: the XDG documents
  // folder if one is configured, else the home directory.
  const QString home = QDir::homePath();
  QFile f(home + "/.config/user-dirs.dirs");
  QString docs = home;
  if (f.open(QIODevice::ReadOnly)) {
    QTextStream in(&f);
    while (!in.atEnd()) {
      const QString line = in.readLine();
      if (line.startsWith("XDG_DOCUMENTS_DIR=")) {
        docs = line.section('=', 1).remove('"');
        docs.replace("$HOME", home);
      }
    }
  }
#else
  const QString docs =
      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
#endif
  return docs + "/seachart/seachart.db";
}

void PassageService::loadSettings() {
  QSettings s;
  s.beginGroup("passage");
  seachart::BoatModel d;
  m_boat.hull_speed_kn = s.value("hullSpeed", d.hull_speed_kn).toDouble();
  m_boat.design_wind_kn = s.value("designWind", d.design_wind_kn).toDouble();
  m_boat.no_go_deg = s.value("noGo", d.no_go_deg).toDouble();
  m_boat.draft_m = s.value("draft", d.draft_m).toDouble();
  m_boat.motor = s.value("motor", d.motor).toBool();
  m_boat.motor_speed_kn = s.value("motorSpeed", d.motor_speed_kn).toDouble();
  m_boat.motor_below_kn = s.value("motorBelow", d.motor_below_kn).toDouble();
  m_boat.max_wind_kn = s.value("maxWind", d.max_wind_kn).toDouble();
  m_boat.max_gust_kn = s.value("maxGust", d.max_gust_kn).toDouble();
  const QString path = s.value("database", defaultDatabasePath()).toString();
  s.endGroup();
  m_path = path;
  // created on the first save, not just by starting the app
  if (QFileInfo::exists(path)) openDatabase(path);
}

QString PassageService::databasePath() const { return m_path; }

bool PassageService::ensureOpen() {
  return m_db.IsOpen() || openDatabase(m_path);
}

bool PassageService::openDatabase(const QString& path, QString* error) {
  m_path = path;
  std::string err;
  const bool ok = m_db.Open(S(path), &err);
  if (!ok && error) *error = U(err);
  m_logbook.Open(seachart::LogbookPathFor(S(path)));
  QSettings s;
  s.setValue("passage/database", path);
  m_dbWeather = m_db.LoadWeather();
  reload();
  // a passage left under way when the app was closed is continued
  if (!m_trip) {
    m_trip = m_logbook.ActiveTrip();
    if (m_trip) {
      seachart::Trip t;
      m_logbook.GetTrip(m_trip, t);
      m_distance = t.distance_nm;
      m_maxSog = t.max_sog;
      m_havePos = false;
      addLogEvent(tr("Logbook continued after a restart"));
      m_timer.start();
    }
  }
  emit databaseChanged();
  emit passageChanged();
  return ok;
}

void PassageService::reload() {
  m_harbours.clear();
  for (auto& h : m_db.Harbours()) m_harbours.push_back(h);
  emit harboursChanged();
}

const seachart::Harbour* PassageService::harbour(qint64 id) const {
  for (const auto& h : m_harbours)
    if (h.id == id) return &h;
  return nullptr;
}

const seachart::Harbour* PassageService::nearest(double lat, double lon,
                                                 double maxNm) const {
  const seachart::Harbour* best = nullptr;
  double bd = maxNm;
  for (const auto& h : m_harbours) {
    const double d = geo::distanceNm(lat, lon, h.lat, h.lon);
    if (d <= bd) {
      bd = d;
      best = &h;
    }
  }
  return best;
}

bool PassageService::saveHarbour(seachart::Harbour& h) {
  if (!ensureOpen()) return false;
  const bool ok = m_db.SaveHarbour(h);
  reload();
  return ok;
}

bool PassageService::removeHarbour(qint64 id) {
  const bool ok = m_db.DeleteHarbour(id);
  reload();
  return ok;
}

bool PassageService::addPhoto(qint64 id, const QString& file,
                              const QString& caption, QString* error) {
  std::string err;
  const bool ok = m_db.AddPhoto(id, S(file), S(caption), &err);
  if (!ok && error) *error = U(err);
  reload();
  return ok;
}

bool PassageService::removePhoto(qint64 photoId) {
  const bool ok = m_db.DeletePhoto(photoId);
  reload();
  return ok;
}

// ---------------------------------------------------------------- weather

seachart::Weather PassageService::fromGrib() {
  seachart::Weather w;
  auto file = GribManager::instance().file();
  if (!file || file->steps().isEmpty()) return w;
  const auto& steps = file->steps();
  const GribField* ref = steps.front().windU;
  if (!ref || !ref->isValid()) return w;
  // at most ~60 x 60 grid points; the router interpolates in between
  const int si = std::max(1, ref->ni / 60), sj = std::max(1, ref->nj / 60);
  const time_t t0 = steps.front().time.toSecsSinceEpoch();
  const time_t t1 = steps.back().time.toSecsSinceEpoch();
  const int hours = int((t1 - t0) / 3600) + 1;
  for (int j = 0; j < ref->nj; j += sj) {
    for (int i = 0; i < ref->ni; i += si) {
      const double lat = ref->lat1 + j * ref->dlat;
      const double lon = ref->lon1 + i * ref->dlon;
      seachart::Weather::Series s;
      s.lat = lat;
      s.lon = lon;
      s.t0 = t0;
      s.hours.resize(hours);
      // values at the GRIB steps, then linear in time (wind as a vector)
      int k = 0;
      for (int h = 0; h < hours; ++h) {
        const time_t t = t0 + time_t(h) * 3600;
        while (k + 1 < steps.size() &&
               steps[k + 1].time.toSecsSinceEpoch() <= t)
          ++k;
        const int k2 = std::min<int>(k + 1, int(steps.size()) - 1);
        const double ta = steps[k].time.toSecsSinceEpoch();
        const double tb = steps[k2].time.toSecsSinceEpoch();
        const double f = tb > ta ? (t - ta) / (tb - ta) : 0;
        auto uv = [&](const GribTimeStep& st, double& u, double& v) {
          u = st.windU ? st.windU->sample(lat, lon) : NAN;
          v = st.windV ? st.windV->sample(lat, lon) : NAN;
        };
        double ua, va, ub, vb;
        uv(steps[k], ua, va);
        uv(steps[k2], ub, vb);
        if (std::isnan(ua) || std::isnan(va)) continue;
        if (std::isnan(ub) || std::isnan(vb)) {
          ub = ua;
          vb = va;
        }
        const double u = ua + f * (ub - ua), v = va + f * (vb - va);
        seachart::WeatherSample& smp = s.hours[h];
        smp.valid = true;
        smp.wind_kn = std::hypot(u, v) * 1.943844;  // m/s -> kn
        smp.wind_dir = std::fmod(std::atan2(-u, -v) * 180 / M_PI + 360, 360);
        const GribField* gust = steps[k].gust;
        const double g = gust ? gust->sample(lat, lon) : NAN;
        smp.gust_kn = std::isnan(g) ? smp.wind_kn * 1.3 : g * 1.943844;
        const GribField* waves = steps[k].waves;
        const double wv = waves ? waves->sample(lat, lon) : NAN;
        smp.wave_m = std::isnan(wv) ? -1 : wv;
      }
      w.Add(std::move(s));
    }
  }
  w.SetFetched(steps.front().time.toSecsSinceEpoch());
  return w;
}

const seachart::Weather& PassageService::weatherFor(const QDateTime& time) {
  const time_t t = time.toSecsSinceEpoch();
  m_gribWeather = fromGrib();
  if (!m_gribWeather.Empty() && m_gribWeather.First() <= t + 3600 &&
      m_gribWeather.Last() >= t) {
    m_weatherSource = tr("GRIB file, %1 to %2")
                          .arg(timeText(m_gribWeather.First()),
                               timeText(m_gribWeather.Last()));
    return m_gribWeather;
  }
  if (!m_dbWeather.Empty()) {
    m_weatherSource =
        tr("Offline forecast from %1, until %2")
            .arg(timeText(m_dbWeather.Fetched()), timeText(m_dbWeather.Last()));
    return m_dbWeather;
  }
  m_weatherSource =
      tr("No forecast: load a GRIB file or run "
         "'seachart weather'. Calculated with the default "
         "speed.");
  return m_noWeather;
}

// ------------------------------------------------------------- planning

void PassageService::setBoat(const seachart::BoatModel& b) {
  m_boat = b;
  QSettings s;
  s.beginGroup("passage");
  s.setValue("hullSpeed", b.hull_speed_kn);
  s.setValue("designWind", b.design_wind_kn);
  s.setValue("noGo", b.no_go_deg);
  s.setValue("draft", b.draft_m);
  s.setValue("motor", b.motor);
  s.setValue("motorSpeed", b.motor_speed_kn);
  s.setValue("motorBelow", b.motor_below_kn);
  s.setValue("maxWind", b.max_wind_kn);
  s.setValue("maxGust", b.max_gust_kn);
  s.endGroup();
}

seachart::BoatModel PassageService::effectiveBoat() const {
  seachart::BoatModel b = m_boat;
  const SailConfig& sails = BoatConfig::instance().sails();
  if (sails.propulsion == SailConfig::Propulsion::Motor || !sails.anySail()) {
    b.hull_speed_kn = 0;  // engine only
    b.motor = true;
  } else {
    // less sail needs more wind for the same speed
    b.design_wind_kn /= std::max(0.3, std::min(1.4, sails.areaFactor()));
    if (sails.propulsion == SailConfig::Propulsion::MotorSailing) {
      b.motor = true;
      b.motor_below_kn = std::max(b.motor_below_kn, b.motor_speed_kn);
    }
  }
  b.default_speed_kn = b.motor ? b.motor_speed_kn : 0.7 * m_boat.hull_speed_kn;
  return b;
}

seachart::PassagePlan PassageService::plan(qint64 fromId, qint64 toId,
                                           const QDateTime& departure) {
  seachart::PassagePlan p;
  const seachart::Harbour* a = harbour(fromId);
  const seachart::Harbour* b = harbour(toId);
  if (!a || !b || fromId == toId) {
    p.error = S(tr("Please choose two different harbours."));
  } else {
    const seachart::NavGrid grid =
        m_db.LoadNavGrid(a->lat, a->lon, b->lat, b->lon);
    seachart::PassageRouter router(grid, weatherFor(departure),
                                   effectiveBoat());
    p = router.Plan(a->lat, a->lon, b->lat, b->lon,
                    departure.toSecsSinceEpoch());
  }
  m_plan = p;
  m_planFrom = fromId;
  m_planTo = toId;
  m_routeId.clear();
  emit planChanged();
  return p;
}

void PassageService::clearPlan() {
  m_plan = seachart::PassagePlan();
  m_routeId.clear();
  emit planChanged();
}

QString PassageService::createRoute() {
  if (!m_plan.ok) return {};
  auto& nav = NavStore::instance();
  if (!m_routeId.isEmpty() && nav.route(m_routeId)) return m_routeId;
  const seachart::Harbour* a = harbour(m_planFrom);
  const seachart::Harbour* b = harbour(m_planTo);
  Route r;
  r.name = QStringLiteral("%1 – %2").arg(a ? U(a->name) : QString(),
                                         b ? U(b->name) : QString());
  const int n = int(m_plan.points.size());
  for (int i = 0; i < n; ++i) {
    Waypoint w;
    w.lat = m_plan.points[i].first;
    w.lon = m_plan.points[i].second;
    if (i == 0 && a)
      w.name = U(a->name);
    else if (i == n - 1 && b)
      w.name = U(b->name);
    else
      w.name = QStringLiteral("WP%1").arg(i, 2, 10, QChar('0'));
    if (i > 0) {
      const auto& leg = m_plan.legs[i - 1];
      w.time = QDateTime::fromSecsSinceEpoch(leg.end);
      w.description = tr("ETA %1 · %2 kn")
                          .arg(timeText(leg.end))
                          .arg(leg.speed_kn, 0, 'f', 1);
    } else {
      w.time = QDateTime::fromSecsSinceEpoch(m_plan.departure);
    }
    r.points.push_back(w);
  }
  m_routeId = nav.addRoute(r);
  emit planChanged();
  return m_routeId;
}

// ------------------------------------------------------------- logbook

seachart::LogEntry PassageService::entryNow(const QString& event) {
  seachart::LogEntry e;
  const auto& own = NavStore::instance().ownShip();
  const auto& env = NavStore::instance().environment();
  e.time = QDateTime::currentSecsSinceEpoch();
  e.lat = own.lat;
  e.lon = own.lon;
  e.sog = std::isnan(own.sog) ? 0 : own.sog;
  e.cog = std::isnan(own.cog) ? 0 : own.cog;
  QString ev = event;
  if (!own.valid) ev += ev.isEmpty() ? "[no GPS]" : " [no GPS]";
  if (!std::isnan(env.tws) && !std::isnan(env.twd)) {
    e.wind_kn = env.tws;  // measured on board
    e.wind_dir = env.twd;
  } else if (own.valid) {
    const seachart::WeatherSample w =
        weatherFor(QDateTime::currentDateTime()).At(own.lat, own.lon, e.time);
    if (w.valid) {
      e.wind_kn = w.wind_kn;
      e.wind_dir = w.wind_dir;
    }
  }
  e.event = S(ev);
  return e;
}

void PassageService::addLogEvent(const QString& text) {
  if (!m_trip) return;
  m_logbook.AddEntry(m_trip, entryNow(text));
  m_lastEntry = QDateTime::currentDateTime();
}

bool PassageService::startPassage() {
  if (!m_plan.ok || m_trip || !ensureOpen()) return false;
  const QString routeId = createRoute();
  NavStore::instance().activateRoute(routeId);
  auto& rec = TrackRecorder::instance();
  if (!rec.isRecording()) rec.start();
  const seachart::Harbour* a = harbour(m_planFrom);
  const seachart::Harbour* b = harbour(m_planTo);
  seachart::Trip t;
  t.from = a ? a->name : std::string();
  t.to = b ? b->name : std::string();
  t.name = t.from + " – " + t.to;
  if (a) {
    t.from_lat = a->lat;
    t.from_lon = a->lon;
  }
  if (b) {
    t.to_lat = b->lat;
    t.to_lon = b->lon;
  }
  t.planned_departure = m_plan.departure;
  t.planned_arrival = m_plan.arrival;
  t.planned_nm = m_plan.distance_nm;
  t.route_guid = S(routeId);
  t.track_guid = S(rec.activeTrack().id);
  QString plan;
  for (const auto& l : m_plan.legs)
    plan += QStringLiteral("%1  %2 NM  %3°  %4 kn  wind %5 kn %6\n")
                .arg(timeText(l.start))
                .arg(l.distance_nm, 5, 'f', 1)
                .arg(int(std::lround(l.course_deg)) % 360, 3, 10, QChar('0'))
                .arg(l.speed_kn, 4, 'f', 1)
                .arg(l.avg_wind_kn, 2, 'f', 0)
                .arg(compass(l.wind_dir));
  t.plan = S(plan);
  t.weather = S(m_weatherSource);
  t.started = QDateTime::currentSecsSinceEpoch();
  if (!m_logbook.AddTrip(t)) return false;
  m_trip = t.id;
  m_distance = 0;
  m_maxSog = 0;
  m_havePos = false;
  addLogEvent(tr("Departure from %1").arg(U(t.from)));
  m_timer.start();
  emit passageChanged();
  return true;
}

void PassageService::onTimer() {
  if (!m_trip) return;
  const auto& own = NavStore::instance().ownShip();
  if (own.valid) {
    if (m_havePos) {
      const double d = geo::distanceNm(m_lastLat, m_lastLon, own.lat, own.lon);
      if (d < 0.5) m_distance += d;  // ignore position jumps
    }
    m_lastLat = own.lat;
    m_lastLon = own.lon;
    m_havePos = true;
    if (!std::isnan(own.sog)) m_maxSog = std::max(m_maxSog, own.sog);
  }
  if (!m_lastEntry.isValid() ||
      m_lastEntry.secsTo(QDateTime::currentDateTime()) >= 15 * 60) {
    addLogEvent(QString());
    seachart::Trip t;
    if (m_logbook.GetTrip(m_trip, t)) {
      t.distance_nm = m_distance;
      t.max_sog = m_maxSog;
      m_logbook.UpdateTrip(t);
    }
  }
}

qint64 PassageService::stopPassage() {
  if (!m_trip) return 0;
  const qint64 id = m_trip;
  seachart::Trip t;
  m_logbook.GetTrip(id, t);
  addLogEvent(tr("Arrival at %1").arg(U(t.to)));
  m_timer.stop();
  m_trip = 0;
  auto& rec = TrackRecorder::instance();
  if (rec.isRecording()) {
    const QString trackId = rec.activeTrack().id;
    rec.stop();
    if (!trackId.isEmpty()) t.track_guid = S(trackId);
  }
  if (NavStore::instance().activeRouteId() == U(t.route_guid))
    NavStore::instance().stopNavigation();
  t.ended = QDateTime::currentSecsSinceEpoch();
  t.distance_nm = m_distance;
  t.max_sog = m_maxSog;
  m_logbook.UpdateTrip(t);
  emit passageChanged();
  return id;
}

QString PassageService::durationText(double hours) {
  const long minutes = std::lround(std::max(0.0, hours) * 60);
  if (minutes >= 24 * 60)
    return QStringLiteral("%1 d %2 h")
        .arg(minutes / 1440)
        .arg(minutes / 60 % 24);
  if (minutes < 60) return tr("%1 min").arg(minutes);
  // same style as the track list: 2:10 h
  return QStringLiteral("%1:%2 h")
      .arg(minutes / 60)
      .arg(minutes % 60, 2, 10, QChar('0'));
}

QString PassageService::tripReport(qint64 tripId) {
  seachart::Trip t;
  if (!m_logbook.GetTrip(tripId, t)) return {};
  QString s;
  QTextStream o(&s);
  o << U(t.name) << "\n";
  o << tr("Departure") << ": " << timeText(t.started) << "   " << tr("Arrival")
    << ": " << (t.ended ? timeText(t.ended) : tr("under way")) << "\n";
  o << tr("Distance %1 NM in %2, max. %3 kn")
           .arg(t.distance_nm, 0, 'f', 1)
           .arg(durationText(t.Hours()))
           .arg(t.max_sog, 0, 'f', 1)
    << "\n";
  o << tr("Planned: %1 NM, departure %2, arrival %3")
           .arg(t.planned_nm, 0, 'f', 1)
           .arg(timeText(t.planned_departure), timeText(t.planned_arrival))
    << "\n";
  if (!t.weather.empty()) o << tr("Weather") << ": " << U(t.weather) << "\n";
  if (!t.notes.empty()) o << "\n" << U(t.notes) << "\n";
  o << "\n" << tr("Entries") << ":\n";
  for (const auto& e : m_logbook.Entries(tripId)) {
    o << QStringLiteral("%1  %2 %3  %4 kn %5°")
             .arg(timeText(e.time), -16)
             .arg(e.lat, 8, 'f', 4)
             .arg(e.lon, 9, 'f', 4)
             .arg(e.sog, 4, 'f', 1)
             .arg(int(std::lround(e.cog)) % 360, 3, 10, QChar('0'));
    if (e.wind_kn >= 0)
      o << QStringLiteral("  wind %1 kn %2")
               .arg(e.wind_kn, 2, 'f', 0)
               .arg(compass(e.wind_dir));
    if (!e.event.empty()) o << "  " << U(e.event);
    o << "\n";
  }
  if (!t.plan.empty()) o << "\n" << tr("Plan") << ":\n" << U(t.plan);
  return s;
}
