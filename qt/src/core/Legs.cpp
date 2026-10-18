#include "core/Legs.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>
#include <cmath>

#include "core/BoatConfig.h"
#include "core/BoatSystems.h"
#include "core/GeoMath.h"
#include "core/NavStore.h"
#include "core/Polar.h"

namespace {
QString tr(const char* s) { return QCoreApplication::translate("Legs", s); }

// Compact JSON: short keys, NaN left out.
const struct {
  const char* key;
  double LegSample::*field;
} kFields[] = {{"sog", &LegSample::sog},        {"stw", &LegSample::stw},
               {"cog", &LegSample::cog},        {"hdg", &LegSample::hdg},
               {"heel", &LegSample::heel},      {"tws", &LegSample::tws},
               {"twd", &LegSample::twd},        {"aws", &LegSample::aws},
               {"awa", &LegSample::awa},        {"depth", &LegSample::depth},
               {"t_c", &LegSample::tempC},      {"loads", &LegSample::loadsW},
               {"charge", &LegSample::chargeW}, {"soc", &LegSample::soc},
               {"fuel", &LegSample::fuelLph},   {"rpm", &LegSample::rpm}};

double round2(double v) { return std::round(v * 100) / 100; }
}  // namespace

QJsonObject LegSample::toJson() const {
  QJsonObject o{{"t", t},
                {"lat", std::round(lat * 1e6) / 1e6},
                {"lon", std::round(lon * 1e6) / 1e6}};
  for (const auto& f : kFields)
    if (!std::isnan(this->*f.field)) o.insert(f.key, round2(this->*f.field));
  return o;
}

LegSample LegSample::fromJson(const QJsonObject& o) {
  LegSample s;
  s.t = qint64(o["t"].toDouble());
  s.lat = o["lat"].toDouble();
  s.lon = o["lon"].toDouble();
  for (const auto& f : kFields)
    if (o.contains(f.key)) s.*f.field = o[f.key].toDouble();
  return s;
}

// --------------------------------------------------------------------------

QDateTime Leg::start() const {
  return samples.isEmpty()
             ? QDateTime()
             : QDateTime::fromSecsSinceEpoch(samples.first().t, Qt::UTC);
}

QDateTime Leg::end() const {
  return samples.isEmpty()
             ? QDateTime()
             : QDateTime::fromSecsSinceEpoch(samples.last().t, Qt::UTC);
}

int Leg::indexAt(qint64 t) const {
  if (samples.isEmpty()) return -1;
  auto it =
      std::lower_bound(samples.begin(), samples.end(), t,
                       [](const LegSample& s, qint64 v) { return s.t < v; });
  if (it == samples.end()) return int(samples.size()) - 1;
  const int i = int(it - samples.begin());
  if (i > 0 && t - samples[i - 1].t < samples[i].t - t) return i - 1;
  return i;
}

LegSummary Leg::summary() const {
  LegSummary s;
  if (samples.size() < 2) return s;
  double sogSum = 0, heelSum = 0, twsSum = 0, tw = 0, hw = 0, sw = 0;
  double sailing = 0;
  s.minTempC = 1e9;
  s.maxTempC = -1e9;
  for (int i = 1; i < samples.size(); ++i) {
    const LegSample& a = samples[i - 1];
    const LegSample& b = samples[i];
    const double h = std::max<qint64>(0, b.t - a.t) / 3600.0;
    if (h > 0.25) continue;  // a gap in the recording
    s.distanceNm += geo::distanceNm(a.lat, a.lon, b.lat, b.lon);
    s.hours += h;
    if (!std::isnan(b.sog)) {
      sogSum += b.sog * h;
      sw += h;
      s.maxSog = std::max(s.maxSog, b.sog);
    }
    if (!std::isnan(b.heel)) {
      heelSum += std::abs(b.heel) * h;
      hw += h;
      s.maxHeel = std::max(s.maxHeel, std::abs(b.heel));
    }
    if (!std::isnan(b.tws)) {
      twsSum += b.tws * h;
      tw += h;
      s.maxTws = std::max(s.maxTws, b.tws);
    }
    const bool engine = !std::isnan(b.rpm) && b.rpm > 300;
    if (engine)
      s.engineHours += h;
    else
      sailing += h;
    if (!std::isnan(b.fuelLph)) s.fuelL += b.fuelLph * h;
    if (!std::isnan(b.loadsW)) s.energyWh += b.loadsW * h;
    if (!std::isnan(b.chargeW)) s.chargedWh += b.chargeW * h;
    if (!std::isnan(b.tempC)) {
      s.minTempC = std::min(s.minTempC, b.tempC);
      s.maxTempC = std::max(s.maxTempC, b.tempC);
    }
  }
  if (sw > 0) s.avgSog = sogSum / sw;
  if (hw > 0) s.avgHeel = heelSum / hw;
  if (tw > 0) s.avgTws = twsSum / tw;
  if (s.hours > 0) s.sailingShare = sailing / s.hours;
  if (s.minTempC > s.maxTempC) s.minTempC = s.maxTempC = 0;
  return s;
}

QJsonObject Leg::toJson() const {
  QJsonArray arr;
  for (const auto& s : samples) arr.append(s.toJson());
  return {{"id", id}, {"name", name},           {"from", from},
          {"to", to}, {"simulated", simulated}, {"samples", arr}};
}

Leg Leg::fromJson(const QJsonObject& o) {
  Leg l;
  l.id = o["id"].toString();
  l.name = o["name"].toString();
  l.from = o["from"].toString();
  l.to = o["to"].toString();
  l.simulated = o["simulated"].toBool();
  for (const auto& v : o["samples"].toArray())
    l.samples << LegSample::fromJson(v.toObject());
  return l;
}

// --------------------------------------------------------------------------

LegStore& LegStore::instance() {
  static LegStore s;
  return s;
}

LegStore::LegStore() {
  load();
  if (m_legs.isEmpty() && BoatSystems::instance().simulated())
    seedSimulated(QDateTime::currentDateTimeUtc());
  m_timer.setInterval(kIntervalSecs * 1000);
  connect(&m_timer, &QTimer::timeout, this, [this] {
    const OwnShip& own = NavStore::instance().ownShip();
    if (!own.valid) return;
    addSample(sampleNow(), BoatSystems::instance().engineRunning());
  });
  m_timer.start();
}

QString LegStore::dir() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
         "/legs";
}

void LegStore::load() {
  m_legs.clear();
  const QDir d(dir());
  for (const QString& f : d.entryList({"*.json"}, QDir::Files)) {
    QFile file(d.filePath(f));
    if (!file.open(QIODevice::ReadOnly)) continue;
    Leg l = Leg::fromJson(QJsonDocument::fromJson(file.readAll()).object());
    if (!l.id.isEmpty() && l.samples.size() >= 2) m_legs << l;
  }
  std::sort(m_legs.begin(), m_legs.end(),
            [](const Leg& a, const Leg& b) { return a.start() > b.start(); });
}

void LegStore::reload() {
  load();
  emit changed();
}

void LegStore::store(const Leg& leg) const {
  QDir().mkpath(dir());
  QSaveFile f(dir() + "/" + leg.id + ".json");
  if (!f.open(QIODevice::WriteOnly)) return;
  f.write(QJsonDocument(leg.toJson()).toJson(QJsonDocument::Compact));
  f.commit();
}

const Leg* LegStore::leg(const QString& id) const {
  for (const auto& l : m_legs)
    if (l.id == id) return &l;
  return nullptr;
}

QVector<const Leg*> LegStore::legsOn(const QDate& date) const {
  QVector<const Leg*> out;
  for (const auto& l : m_legs) {
    const QDate a = l.start().toLocalTime().date();
    const QDate b = l.end().toLocalTime().date();
    if (a <= date && date <= b) out << &l;
  }
  std::reverse(out.begin(), out.end());  // in the order sailed
  return out;
}

void LegStore::save(const Leg& leg) {
  Leg l = leg;
  if (l.id.isEmpty())
    l.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  if (l.name.isEmpty())
    l.name = tr("Leg %1").arg(l.start().toLocalTime().toString("dd.MM.yyyy"));
  auto it = std::find_if(m_legs.begin(), m_legs.end(),
                         [&](const Leg& x) { return x.id == l.id; });
  if (it != m_legs.end())
    *it = l;
  else
    m_legs << l;
  std::sort(m_legs.begin(), m_legs.end(),
            [](const Leg& a, const Leg& b) { return a.start() > b.start(); });
  store(l);
  emit changed();
}

void LegStore::remove(const QString& id) {
  if (m_legs.removeIf([&](const Leg& l) { return l.id == id; }) == 0) return;
  QFile::remove(dir() + "/" + id + ".json");
  emit changed();
}

void LegStore::rename(const QString& id, const QString& name,
                      const QString& from, const QString& to) {
  for (auto& l : m_legs)
    if (l.id == id) {
      l.name = name;
      l.from = from;
      l.to = to;
      store(l);
      emit changed();
      return;
    }
}

LegSample LegStore::sampleNow() {
  const NavStore& nav = NavStore::instance();
  const OwnShip& own = nav.ownShip();
  const Environment& env = nav.environment();
  const auto& sys = BoatSystems::instance();
  LegSample s;
  s.t = QDateTime::currentSecsSinceEpoch();
  s.lat = own.lat;
  s.lon = own.lon;
  s.sog = own.sog;
  s.cog = own.cog;
  s.hdg = own.hdg;
  s.stw = env.stw;
  s.heel = env.heel;
  s.tws = env.tws;
  s.twd = env.twd;
  s.aws = env.aws;
  s.awa = env.awa;
  s.depth = env.depth;
  s.tempC = sys.outsideC();
  const EnergySystem& e = sys.energy();
  s.loadsW = e.loadsW;
  s.chargeW = e.solarW + e.alternatorW + e.chargerW;
  s.soc = e.soc;
  const EngineData& eng = nav.engine();
  const bool running = sys.engineRunning();
  s.rpm = running ? eng.rpm : 0;
  const double engineLph =
      running
          ? (std::isnan(eng.fuelRateLph) ? BoatConfig::instance().consumptionLph
                                         : eng.fuelRateLph)
          : 0;
  s.fuelLph = engineLph + sys.heater().fuelLph;
  return s;
}

void LegStore::addSample(const LegSample& s, bool engineRunning) {
  const bool moving =
      engineRunning || (!std::isnan(s.sog) && s.sog > kMovingKn);
  if (moving) m_lastMoving = s.t;
  if (!recording()) {
    if (!moving) return;
    m_current = Leg();
    m_current.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  }
  m_current.samples << s;
  if (!moving && s.t - m_lastMoving > kRestMinutes * 60) {
    finishCurrent();
    return;
  }
  // Keep a copy on disk while recording (every five minutes).
  if (m_current.samples.size() % 10 == 0) store(m_current);
  emit currentChanged();
}

void LegStore::finishCurrent() {
  if (!recording()) return;
  Leg l = m_current;
  m_current = Leg();
  // Drop the time at rest at the end.
  while (l.samples.size() > 2 && l.samples.last().t > m_lastMoving)
    l.samples.removeLast();
  const LegSummary sum = l.summary();
  if (l.samples.size() < 3 || sum.distanceNm < 0.2 || sum.hours < 0.1) {
    QFile::remove(dir() + "/" + l.id + ".json");
    emit currentChanged();
    return;
  }
  // Start and end next to a saved mark take its name.
  const auto nearestMark = [](const LegSample& p) {
    QString best;
    double bestD = 1.0;  // NM
    for (const Waypoint& w : NavStore::instance().waypoints()) {
      const double d = geo::distanceNm(p.lat, p.lon, w.lat, w.lon);
      if (d < bestD) {
        bestD = d;
        best = w.name;
      }
    }
    return best;
  };
  l.from = nearestMark(l.samples.first());
  l.to = nearestMark(l.samples.last());
  if (!l.from.isEmpty() && !l.to.isEmpty()) l.name = l.from + " – " + l.to;
  save(l);
  emit currentChanged();
}

// --------------------------------------------------------------------------
// Simulated history: four legs around the islands south of Funen.

void LegStore::seedSimulated(const QDateTime& now) {
  struct Plan {
    int daysAgo;
    QString from, to;
    QVector<QPointF> route;  // lon, lat
    double twd, tws;
  };
  const QVector<Plan> plans = {
      {12,
       "Kiel",
       "Marstal",
       {{10.163, 54.372},
        {10.205, 54.430},
        {10.275, 54.500},
        {10.400, 54.700},
        {10.522, 54.853}},
       235,
       14},
      {9,
       "Marstal",
       "Faaborg",
       {{10.522, 54.853},
        {10.420, 54.820},
        {10.250, 54.840},
        {10.120, 54.900},
        {10.130, 55.020},
        {10.240, 55.095}},
       260,
       11},
      {6,
       "Faaborg",
       "Sønderborg",
       {{10.240, 55.095},
        {10.120, 55.030},
        {9.950, 54.960},
        {9.850, 54.925},
        {9.792, 54.912}},
       300,
       17},
      {3,
       "Sønderborg",
       "Kiel",
       {{9.792, 54.912},
        {9.880, 54.860},
        {10.020, 54.700},
        {10.150, 54.520},
        {10.190, 54.440},
        {10.163, 54.372}},
       215,
       9},
  };
  // A fixed seed: the same plausible legs on every start.
  QRandomGenerator gen(2800);
  auto* rng = &gen;
  const auto noise = [rng](double a) {
    return (rng->generateDouble() * 2 - 1) * a;
  };
  const Polar polar = Polar::generic(6.8);
  const double engineLph = BoatConfig::instance().consumptionLph;
  double soc = 0.92;
  for (const Plan& p : plans) {
    Leg leg;
    leg.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    leg.name = p.from + " – " + p.to;
    leg.from = p.from;
    leg.to = p.to;
    leg.simulated = true;
    QDateTime t(now.toLocalTime().date().addDays(-p.daysAgo), QTime(9, 15));
    qint64 secs = t.toUTC().toSecsSinceEpoch();
    double twd = p.twd, tws = p.tws;
    bool motoring = false;
    for (int seg = 1; seg < p.route.size(); ++seg) {
      const QPointF a = p.route[seg - 1], b = p.route[seg];
      const double len = geo::distanceNm(a.y(), a.x(), b.y(), b.x());
      const double brg = geo::bearingDeg(a.y(), a.x(), b.y(), b.x());
      double done = 0;
      while (done < len) {
        // Wind veers and gusts slowly.
        twd += noise(1.5);
        tws = std::clamp(tws + noise(0.6), 3.0, 26.0);
        const double twa = std::abs(geo::normalizeDeg(twd - brg + 180) - 180);
        const bool harbour = seg == 1 && done < 1.0;
        const bool arriving = seg == p.route.size() - 1 && len - done < 1.0;
        double sail = twa < 40 ? polar.speedAt(tws, 45) *
                                     std::cos((45 - twa) * M_PI / 180) * 0.7
                               : polar.speedAt(tws, twa);
        // The engine goes on below 3 kn and off again above 4 kn.
        motoring = sail < (motoring ? 4.0 : 3.0);
        const bool motor = harbour || arriving || motoring;
        LegSample s;
        const double f = done / std::max(0.01, len);
        s.t = secs;
        s.lat = a.y() + (b.y() - a.y()) * f + noise(0.0002);
        s.lon = a.x() + (b.x() - a.x()) * f + noise(0.0003);
        s.sog = std::max(0.5, (motor ? std::max(5.4, sail) : sail) *
                                      (harbour || arriving ? 0.75 : 1.0) +
                                  noise(0.25));
        s.stw = s.sog * 0.97;
        s.cog = geo::normalizeDeg(brg + noise(4));
        s.hdg = geo::normalizeDeg(brg + noise(3));
        s.twd = geo::normalizeDeg(twd);
        s.tws = tws;
        // Apparent wind and heel.
        const double tr = (twd - s.hdg) * M_PI / 180;
        const double ax = tws * std::cos(tr) + s.sog, ay = tws * std::sin(tr);
        s.aws = std::hypot(ax, ay);
        s.awa = std::atan2(ay, ax) * 180 / M_PI;
        s.heel =
            motor ? noise(2)
                  : std::min(26.0, 0.09 * s.aws * s.aws *
                                       std::abs(std::sin(s.awa * M_PI / 180))) *
                            (s.awa > 0 ? -1 : 1) +
                        noise(2);
        s.depth = harbour || arriving
                      ? 4 + noise(1)
                      : 12 + 8 * std::sin(secs / 900.0) + noise(1);
        const QDateTime local =
            QDateTime::fromSecsSinceEpoch(secs).toLocalTime();
        const double hour = local.time().hour() + local.time().minute() / 60.0;
        s.tempC = 12 + 3 * std::sin((hour - 9) / 24 * 2 * M_PI) + noise(0.3);
        s.rpm = motor ? 2200 + noise(80) : 0;
        s.fuelLph = motor ? engineLph * (1 + noise(0.08)) : 0;
        s.loadsW = 75 + (motor ? 0 : 30) + noise(6);  // autopilot under sail
        const double sun = std::max(0.0, std::sin((hour - 6.5) / 13.5 * M_PI));
        s.chargeW = 200 * sun * 0.7 + (motor ? 600 : 0);
        soc = std::clamp(
            soc + (s.chargeW - s.loadsW) / 12.8 / 200 * kIntervalSecs / 3600,
            0.2, 1.0);
        s.soc = soc;
        leg.samples << s;
        secs += kIntervalSecs;
        done += s.sog * kIntervalSecs / 3600.0;
      }
    }
    m_legs << leg;
    store(leg);
  }
  std::sort(m_legs.begin(), m_legs.end(),
            [](const Leg& a, const Leg& b) { return a.start() > b.start(); });
  emit changed();
}
