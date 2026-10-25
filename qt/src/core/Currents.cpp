#include "core/Currents.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>

#include "core/GeoMath.h"
#include "core/osm/Overpass.h"

namespace {
QString tr(const char* s) { return QCoreApplication::translate("Currents", s); }
constexpr double kDeg = M_PI / 180;
constexpr float kNaNf = std::numeric_limits<float>::quiet_NaN();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

QJsonArray floats(const QVector<float>& v, int decimals = 2) {
  const double f = std::pow(10, decimals);
  QJsonArray a;
  for (float x : v)
    a << (std::isnan(x) ? QJsonValue() : QJsonValue(std::round(x * f) / f));
  return a;
}

QVector<float> floats(const QJsonArray& a) {
  QVector<float> v;
  v.reserve(a.size());
  for (const auto& x : a) v << (x.isDouble() ? float(x.toDouble()) : kNaNf);
  return v;
}

/** Vector sum of set/drift pairs: returns false without any. */
bool vectorMean(const QVector<std::pair<double, double>>& sd,
                const QVector<double>& w, double& setDeg, double& driftKn) {
  double u = 0, v = 0, ws = 0;
  for (int i = 0; i < sd.size(); ++i) {
    u += w[i] * sd[i].second * std::sin(sd[i].first * kDeg);
    v += w[i] * sd[i].second * std::cos(sd[i].first * kDeg);
    ws += w[i];
  }
  if (ws <= 0) return false;
  u /= ws;
  v /= ws;
  driftKn = std::hypot(u, v);
  setDeg = std::fmod(std::atan2(u, v) / kDeg + 360, 360);
  return true;
}

/** Distance from p to the segment a-b in NM (flat earth). */
double segmentNm(const QPointF& p, const QPointF& a, const QPointF& b,
                 double* along = nullptr) {
  const double k = std::cos(p.y() * kDeg) * 60;
  const QPointF pa((a.x() - p.x()) * k, (a.y() - p.y()) * 60);
  const QPointF pb((b.x() - p.x()) * k, (b.y() - p.y()) * 60);
  const QPointF ab = pb - pa;
  const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
  double t = len2 > 0 ? -(pa.x() * ab.x() + pa.y() * ab.y()) / len2 : 0;
  t = std::clamp(t, 0.0, 1.0);
  if (along) *along = t;
  const QPointF c = pa + ab * t;
  return std::hypot(c.x(), c.y());
}
}  // namespace

// --------------------------------------------------------------------------
// Marine forecast

int MarineForecast::nearest(double lat, double lon, double maxNm) const {
  int best = -1;
  double bestD = maxNm;
  for (int i = 0; i < points.size(); ++i) {
    const double d = geo::distanceNm(lat, lon, points[i].y(), points[i].x());
    if (d < bestD) {
      bestD = d;
      best = i;
    }
  }
  return best;
}

bool MarineForecast::current(double lat, double lon, const QDateTime& t,
                             double& set, double& drift) const {
  if (!isValid()) return false;
  const qint64 s = t.toSecsSinceEpoch();
  if (s < times.first() || s > times.last()) return false;
  // The nearest point that has sea data.
  int p = -1;
  double bestD = 10;
  for (int i = 0; i < points.size(); ++i) {
    if (driftKn.value(i).isEmpty() || std::isnan(driftKn[i].value(0))) continue;
    const double d = geo::distanceNm(lat, lon, points[i].y(), points[i].x());
    if (d < bestD) {
      bestD = d;
      p = i;
    }
  }
  if (p < 0) return false;
  int ti =
      int(std::upper_bound(times.begin(), times.end(), s) - times.begin()) - 1;
  ti = std::clamp(ti, 0, int(times.size()) - 2);
  const double f =
      double(s - times[ti]) / std::max<qint64>(1, times[ti + 1] - times[ti]);
  QVector<std::pair<double, double>> sd;
  for (int j = 0; j < 2; ++j) {
    const float d = driftKn[p].value(ti + j, kNaNf);
    const float a = setDeg[p].value(ti + j, kNaNf);
    if (std::isnan(d) || std::isnan(a)) return false;
    sd.push_back({a, d});
  }
  return vectorMean(sd, {1 - f, f}, set, drift);
}

bool MarineForecast::level(double lat, double lon, const QDateTime& t,
                           double& m) const {
  if (!isValid()) return false;
  const int p = nearest(lat, lon, 10);
  if (p < 0 || levelM.value(p).isEmpty()) return false;
  const qint64 s = t.toSecsSinceEpoch();
  if (s < times.first() || s > times.last()) return false;
  int ti =
      int(std::upper_bound(times.begin(), times.end(), s) - times.begin()) - 1;
  ti = std::clamp(ti, 0, int(times.size()) - 2);
  const double f =
      double(s - times[ti]) / std::max<qint64>(1, times[ti + 1] - times[ti]);
  const float a = levelM[p].value(ti, kNaNf),
              b = levelM[p].value(ti + 1, kNaNf);
  if (std::isnan(a) || std::isnan(b)) return false;
  m = a + (b - a) * f;
  return true;
}

QJsonObject MarineForecast::toJson() const {
  QJsonArray pts, t, d, s, l;
  for (const QPointF& p : points) pts << QJsonArray{p.x(), p.y()};
  for (qint64 x : times) t << double(x);
  for (int i = 0; i < points.size(); ++i) {
    d << floats(driftKn.value(i));
    s << floats(setDeg.value(i), 0);
    l << floats(levelM.value(i));
  }
  return {{"points", pts}, {"times", t},
          {"drift", d},    {"set", s},
          {"level", l},    {"fetched", fetched.toString(Qt::ISODate)}};
}

MarineForecast MarineForecast::fromJson(const QJsonObject& o) {
  MarineForecast m;
  for (const auto& p : o["points"].toArray()) {
    const QJsonArray a = p.toArray();
    m.points << QPointF(a.at(0).toDouble(), a.at(1).toDouble());
  }
  for (const auto& x : o["times"].toArray()) m.times << qint64(x.toDouble());
  for (const auto& x : o["drift"].toArray()) m.driftKn << floats(x.toArray());
  for (const auto& x : o["set"].toArray()) m.setDeg << floats(x.toArray());
  for (const auto& x : o["level"].toArray()) m.levelM << floats(x.toArray());
  m.fetched = QDateTime::fromString(o["fetched"].toString(), Qt::ISODate);
  return m;
}

MarineForecast MarineForecast::parse(const QByteArray& json,
                                     const QVector<QPointF>& points,
                                     QString* error) {
  MarineForecast m;
  const QJsonDocument doc = QJsonDocument::fromJson(json);
  QJsonArray locations;
  if (doc.isArray())
    locations = doc.array();
  else if (doc.isObject())
    locations << doc.object();
  if (locations.size() != points.size()) {
    if (error) *error = tr("Unexpected marine data.");
    return m;
  }
  m.points = points;
  for (int i = 0; i < locations.size(); ++i) {
    const QJsonObject loc = locations[i].toObject();
    const QJsonObject h = loc["hourly"].toObject();
    if (i == 0)
      for (const auto& x : h["time"].toArray()) m.times << qint64(x.toDouble());
    // The grid cell may be a sea cell far away from a point on land.
    const double cellNm = geo::distanceNm(points[i].y(), points[i].x(),
                                          loc["latitude"].toDouble(),
                                          loc["longitude"].toDouble());
    const bool usable = cellNm < 6;
    QVector<float> d = floats(h["ocean_current_velocity"].toArray());
    for (float& x : d) x = usable ? x / 1.852f : kNaNf;  // km/h -> kn
    QVector<float> s = floats(h["ocean_current_direction"].toArray());
    QVector<float> l = floats(h["sea_level_height_msl"].toArray());
    if (!usable) {
      s.fill(kNaNf);
      l.fill(kNaNf);
    }
    m.driftKn << d;
    m.setDeg << s;
    m.levelM << l;
  }
  m.fetched = QDateTime::currentDateTimeUtc();
  if (m.times.size() < 2 && error) *error = tr("No marine forecast.");
  return m;
}

// --------------------------------------------------------------------------
// Rivers

double RiverNetwork::typicalSpeedKn(const QString& name, const QString& kind) {
  if (kind == "canal") return 0;
  static const QVector<QPair<QString, double>> table = {
      {"rhein", 3.0},  {"rhine", 3.0},  {"rhin", 3.0},   {"waal", 2.5},
      {"donau", 2.5},  {"danube", 2.5}, {"rhône", 3.0},  {"rhone", 3.0},
      {"inn", 3.0},    {"elbe", 1.6},   {"labe", 1.6},   {"weser", 1.2},
      {"oder", 1.2},   {"odra", 1.2},   {"ems", 1.0},    {"ijssel", 1.5},
      {"maas", 1.0},   {"meuse", 1.0},  {"loire", 1.5},  {"seine", 1.0},
      {"saale", 0.8},  {"main", 0.6},   {"mosel", 0.6},  {"moselle", 0.6},
      {"neckar", 0.6}, {"saar", 0.6},   {"lahn", 0.5},   {"havel", 0.3},
      {"spree", 0.3},  {"peene", 0.2},  {"warnow", 0.2}, {"trave", 0.2}};
  const QString n = name.toLower();
  for (const auto& [key, kn] : table)
    if (n == key || n.startsWith(key + " ") || n.endsWith(key) ||
        n.contains(key + "-"))
      return kn;
  return 1.0;
}

QString RiverNetwork::query(double s, double w, double n, double e) {
  return QStringLiteral(
             "[out:json][timeout:60];"
             "way[\"waterway\"~\"^(river|canal|tidal_channel)$\"]"
             "(%1,%2,%3,%4);out geom;")
      .arg(s, 0, 'f', 4)
      .arg(w, 0, 'f', 4)
      .arg(n, 0, 'f', 4)
      .arg(e, 0, 'f', 4);
}

RiverNetwork RiverNetwork::fromOsm(const QVector<OsmElement>& elements) {
  RiverNetwork net;
  for (const OsmElement& e : elements) {
    if (e.type != "way" || e.geometry.size() < 2) continue;
    const QString ww = e.tags.value("waterway");
    Waterway w;
    w.name = e.name();
    if (w.name.isEmpty()) w.name = ww == "canal" ? tr("Canal") : tr("River");
    w.kind = ww == "canal" ? "canal" : "river";
    if (ww == "tidal_channel" || e.tags.value("tidal") == "yes")
      w.kind = "tidal";
    w.points = e.geometry;
    w.speedKn = typicalSpeedKn(w.name, w.kind);
    net.m_ways << w;
  }
  return net;
}

bool RiverNetwork::current(double lat, double lon, double& set, double& drift,
                           QString* name) const {
  const QPointF p(lon, lat);
  const Waterway* best = nullptr;
  int bestSeg = -1;
  double bestD = 0.15;
  for (const Waterway& w : m_ways) {
    for (int i = 1; i < w.points.size(); ++i) {
      const double d = segmentNm(p, w.points[i - 1], w.points[i]);
      if (d < bestD) {
        bestD = d;
        best = &w;
        bestSeg = i;
      }
    }
  }
  if (!best || best->kind == "tidal") return false;
  const QPointF a = best->points[bestSeg - 1], b = best->points[bestSeg];
  set = geo::bearingDeg(a.y(), a.x(), b.y(), b.x());
  drift = best->speedKn;
  if (name) *name = best->name;
  return true;
}

QVector<QPair<QString, double>> RiverNetwork::summary() const {
  QVector<QPair<QString, double>> out;
  for (const Waterway& w : m_ways) {
    if (w.kind == "tidal") continue;
    if (std::none_of(out.begin(), out.end(),
                     [&](const auto& x) { return x.first == w.name; }))
      out << qMakePair(w.name, w.speedKn);
  }
  std::sort(out.begin(), out.end());
  return out;
}

void RiverNetwork::setSpeed(const QString& name, double kn) {
  for (Waterway& w : m_ways)
    if (w.name == name && w.kind != "tidal") w.speedKn = kn;
}

QJsonObject RiverNetwork::toJson() const {
  QJsonArray ways;
  for (const Waterway& w : m_ways) {
    QJsonArray pts;
    for (const QPointF& p : w.points)
      pts << std::round(p.x() * 1e5) / 1e5 << std::round(p.y() * 1e5) / 1e5;
    ways << QJsonObject{
        {"name", w.name}, {"kind", w.kind}, {"kn", w.speedKn}, {"pts", pts}};
  }
  return {{"ways", ways}};
}

RiverNetwork RiverNetwork::fromJson(const QJsonObject& o) {
  RiverNetwork net;
  for (const auto& v : o["ways"].toArray()) {
    const QJsonObject wo = v.toObject();
    Waterway w;
    w.name = wo["name"].toString();
    w.kind = wo["kind"].toString("river");
    w.speedKn = wo["kn"].toDouble();
    const QJsonArray pts = wo["pts"].toArray();
    for (int i = 0; i + 1 < pts.size(); i += 2)
      w.points << QPointF(pts[i].toDouble(), pts[i + 1].toDouble());
    if (w.points.size() >= 2) net.m_ways << w;
  }
  return net;
}

// --------------------------------------------------------------------------
// Tidal current stations

bool TidalCurrents::current(double lat, double lon, const QDateTime& t,
                            double& set, double& drift, double maxNm) {
  const auto& db = TideDatabase::instance();
  if (!db.isLoaded()) return false;
  QVector<std::pair<double, double>> sd;
  QVector<double> w;
  for (const TideStation* s :
       db.nearest(lat, lon, 3, TideStation::Type::Current)) {
    if (std::isnan(s->floodDir)) continue;
    const double d = geo::distanceNm(lat, lon, s->lat, s->lon);
    if (d > maxNm) continue;
    const double v = db.value(*s, t);
    if (std::isnan(v)) continue;
    sd.push_back({v >= 0 ? s->floodDir : s->ebbDir, std::abs(v)});
    w << 1.0 / std::max(0.2, d);
  }
  return !sd.isEmpty() && vectorMean(sd, w, set, drift);
}

// --------------------------------------------------------------------------
// Model

bool CurrentModel::current(double lat, double lon, const QDateTime& t,
                           double& set, double& drift, QString* src) const {
  switch (mode) {
    case Mode::Off:
      return false;
    case Mode::Manual:
      set = manualSetDeg;
      drift = manualDriftKn;
      if (src) *src = tr("manual");
      return drift > 0;
    default:
      break;
  }
  QString river;
  if (rivers.current(lat, lon, set, drift, &river)) {
    if (src) *src = river;
    return true;
  }
  if (useStations && TidalCurrents::current(lat, lon, t, set, drift)) {
    if (src) *src = tr("tidal stations");
    return true;
  }
  if (marine.current(lat, lon, t, set, drift)) {
    if (src) *src = tr("marine forecast");
    return true;
  }
  return false;
}

CurrentSource CurrentModel::source() const {
  if (mode == Mode::Off) return {};
  // A copy: the planner runs on its own.
  const CurrentModel copy = *this;
  return
      [copy](double lat, double lon, const QDateTime& t, double& set,
             double& drift) { return copy.current(lat, lon, t, set, drift); };
}

LevelInfo CurrentModel::level(double lat, double lon,
                              const QDateTime& t) const {
  LevelInfo info;
  const auto& db = TideDatabase::instance();
  if (db.isLoaded()) {
    const auto near = db.nearest(lat, lon, 1, TideStation::Type::Tide);
    if (!near.isEmpty()) {
      const TideStation* s = near.first();
      const double d = geo::distanceNm(lat, lon, s->lat, s->lon);
      if (d <= 15) {
        info.source = s->name;
        info.distanceNm = d;
        info.levelM = db.value(*s, t);
        info.extremes =
            db.extremes(*s, t.addSecs(-7 * 3600), t.addSecs(7 * 3600));
        return info;
      }
    }
  }
  double m;
  if (marine.level(lat, lon, t, m)) {
    info.source = tr("marine forecast");
    info.levelM = m;
    info.chartDatum = false;
  }
  return info;
}

// --------------------------------------------------------------------------
// Gauges and services

QString Gauge::text() const {
  QString s = std::isnan(valueCm)
                  ? QStringLiteral("–")
                  : QStringLiteral("%1 cm").arg(valueCm, 0, 'f', 0);
  if (!std::isnan(mwCm)) s += " " + tr("(MW %1 cm)").arg(mwCm, 0, 'f', 0);
  return s;
}

QJsonObject Gauge::toJson() const {
  const auto num = [](double v) { return std::isnan(v) ? QJsonValue() : v; };
  return {{"name", name},
          {"water", water},
          {"km", num(km)},
          {"lat", lat},
          {"lon", lon},
          {"cm", num(valueCm)},
          {"time", time.toString(Qt::ISODate)},
          {"mnw", num(mnwCm)},
          {"mw", num(mwCm)},
          {"mhw", num(mhwCm)}};
}

Gauge Gauge::fromJson(const QJsonObject& o) {
  const auto num = [&](const char* k) {
    return o[k].isDouble() ? o[k].toDouble() : kNaN;
  };
  Gauge g;
  g.name = o["name"].toString();
  g.water = o["water"].toString();
  g.km = num("km");
  g.lat = o["lat"].toDouble();
  g.lon = o["lon"].toDouble();
  g.valueCm = num("cm");
  g.time = QDateTime::fromString(o["time"].toString(), Qt::ISODate);
  g.mnwCm = num("mnw");
  g.mwCm = num("mw");
  g.mhwCm = num("mhw");
  return g;
}

TideServices& TideServices::instance() {
  static TideServices s;
  return s;
}

void TideServices::fetchMarine(const QVector<QPointF>& points,
                               MarineCallback done) {
  QStringList lats, lons;
  for (const QPointF& p : points) {
    lats << QString::number(p.y(), 'f', 3);
    lons << QString::number(p.x(), 'f', 3);
  }
  QUrl url("https://marine-api.open-meteo.com/v1/marine");
  QUrlQuery q;
  q.addQueryItem("latitude", lats.join(','));
  q.addQueryItem("longitude", lons.join(','));
  q.addQueryItem("hourly",
                 "ocean_current_velocity,ocean_current_direction,"
                 "sea_level_height_msl");
  q.addQueryItem("forecast_days", "7");
  q.addQueryItem("timeformat", "unixtime");
  q.addQueryItem("cell_selection", "sea");
  url.setQuery(q);
  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::UserAgentHeader,
                "OpenCPN-Qt/0.1 (passage planning)");
  req.setTransferTimeout(30000);
  QNetworkReply* r = m_net.get(req);
  connect(r, &QNetworkReply::finished, this, [r, points, done] {
    r->deleteLater();
    const QByteArray body = r->readAll();
    if (r->error() != QNetworkReply::NoError) {
      const QString reason =
          QJsonDocument::fromJson(body).object().value("reason").toString();
      done({}, reason.isEmpty() ? r->errorString() : reason);
      return;
    }
    QString err;
    const MarineForecast m = MarineForecast::parse(body, points, &err);
    done(m, err);
  });
}

void TideServices::fetchGauges(double lat, double lon, double radiusKm,
                               GaugeCallback done) {
  QUrl url(
      "https://www.pegelonline.wsv.de/webservices/rest-api/v2/stations.json");
  QUrlQuery q;
  q.addQueryItem("latitude", QString::number(lat, 'f', 4));
  q.addQueryItem("longitude", QString::number(lon, 'f', 4));
  q.addQueryItem("radius", QString::number(radiusKm, 'f', 0));
  q.addQueryItem("includeTimeseries", "true");
  q.addQueryItem("includeCurrentMeasurement", "true");
  q.addQueryItem("includeCharacteristicValues", "true");
  url.setQuery(q);
  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::UserAgentHeader, "OpenCPN-Qt/0.1");
  req.setTransferTimeout(20000);
  QNetworkReply* r = m_net.get(req);
  connect(r, &QNetworkReply::finished, this, [r, lat, lon, done] {
    r->deleteLater();
    if (r->error() != QNetworkReply::NoError) {
      done({}, r->errorString());
      return;
    }
    done(parseGauges(r->readAll(), lat, lon), {});
  });
}

QVector<Gauge> TideServices::parseGauges(const QByteArray& json, double lat,
                                         double lon) {
  QVector<Gauge> out;
  for (const auto& v : QJsonDocument::fromJson(json).array()) {
    const QJsonObject o = v.toObject();
    Gauge g;
    g.name = o["longname"].toString(o["shortname"].toString());
    g.water = o["water"].toObject()["longname"].toString();
    if (o["km"].isDouble()) g.km = o["km"].toDouble();
    g.lat = o["latitude"].toDouble();
    g.lon = o["longitude"].toDouble();
    for (const auto& ts : o["timeseries"].toArray()) {
      const QJsonObject t = ts.toObject();
      if (t["shortname"].toString() != "W") continue;
      const QJsonObject cur = t["currentMeasurement"].toObject();
      if (cur["value"].isDouble()) g.valueCm = cur["value"].toDouble();
      g.time = QDateTime::fromString(cur["timestamp"].toString(), Qt::ISODate);
      for (const auto& cv : t["characteristicValues"].toArray()) {
        const QJsonObject c = cv.toObject();
        const QString n = c["shortname"].toString();
        const double val = c["value"].toDouble(kNaN);
        if (n == "MNW") g.mnwCm = val;
        if (n == "MW") g.mwCm = val;
        if (n == "MHW") g.mhwCm = val;
      }
    }
    if (o["latitude"].isDouble()) out << g;
  }
  std::sort(out.begin(), out.end(), [lat, lon](const Gauge& a, const Gauge& b) {
    return geo::distanceNm(lat, lon, a.lat, a.lon) <
           geo::distanceNm(lat, lon, b.lat, b.lon);
  });
  return out;
}
