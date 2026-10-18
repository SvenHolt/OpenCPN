#include "core/WeatherModels.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>

#include "core/GeoMath.h"

namespace {
constexpr float kNaNf = std::numeric_limits<float>::quiet_NaN();

QJsonArray floats(const QVector<float>& v) {
  QJsonArray a;
  for (float f : v)
    a << (std::isnan(f) ? QJsonValue() : QJsonValue(std::round(f * 10) / 10));
  return a;
}

QVector<float> floats(const QJsonArray& a) {
  QVector<float> v;
  v.reserve(a.size());
  for (const auto& x : a) v << (x.isNull() ? kNaNf : float(x.toDouble()));
  return v;
}
}  // namespace

QDateTime ModelForecast::firstTime() const {
  return times.isEmpty()
             ? QDateTime()
             : QDateTime::fromSecsSinceEpoch(times.first(), Qt::UTC);
}

QDateTime ModelForecast::lastTime() const {
  return times.isEmpty() ? QDateTime()
                         : QDateTime::fromSecsSinceEpoch(times.last(), Qt::UTC);
}

bool ModelForecast::wind(double lat, double lon, const QDateTime& t, double& kn,
                         double& fromDeg, double* gust) const {
  if (!isValid()) return false;
  const qint64 s = t.toSecsSinceEpoch();
  if (s < times.first() || s > times.last()) return false;
  int ti =
      int(std::upper_bound(times.begin(), times.end(), s) - times.begin()) - 1;
  ti = std::clamp(ti, 0, int(times.size()) - 2);
  const double ft =
      double(s - times[ti]) / std::max<qint64>(1, times[ti + 1] - times[ti]);

  // The two nearest sample points, weighted by inverse distance.
  int best[2] = {-1, -1};
  double dist[2] = {1e300, 1e300};
  for (int i = 0; i < points.size(); ++i) {
    const double d = geo::distanceNm(lat, lon, points[i].y(), points[i].x());
    if (d < dist[0]) {
      best[1] = best[0];
      dist[1] = dist[0];
      best[0] = i;
      dist[0] = d;
    } else if (d < dist[1]) {
      best[1] = i;
      dist[1] = d;
    }
  }
  double u = 0, v = 0, g = 0, wsum = 0;
  for (int k = 0; k < 2; ++k) {
    const int p = best[k];
    if (p < 0) continue;
    const double w = 1.0 / std::max(0.5, dist[k]);
    for (int j = 0; j < 2; ++j) {
      const double sp = speedKn[p][ti + j], dr = dirFrom[p][ti + j];
      if (std::isnan(sp) || std::isnan(dr)) return false;
      const double wt = w * (j ? ft : 1 - ft);
      u += wt * -sp * std::sin(dr * M_PI / 180);
      v += wt * -sp * std::cos(dr * M_PI / 180);
      const double gs = gustKn.value(p).value(ti + j, kNaNf);
      g += wt * (std::isnan(gs) ? sp : gs);
      wsum += wt;
    }
  }
  if (wsum <= 0) return false;
  u /= wsum;
  v /= wsum;
  kn = std::hypot(u, v);
  fromDeg = std::fmod(std::atan2(-u, -v) * 180 / M_PI + 360, 360);
  if (gust) *gust = g / wsum;
  return true;
}

QJsonObject ModelForecast::toJson() const {
  QJsonArray pts, t, sp, dr, gs;
  for (const QPointF& p : points) pts << QJsonArray{p.x(), p.y()};
  for (qint64 x : times) t << double(x);
  for (int i = 0; i < points.size(); ++i) {
    sp << floats(speedKn.value(i));
    dr << floats(dirFrom.value(i));
    gs << floats(gustKn.value(i));
  }
  return QJsonObject{
      {"model", model}, {"fetched", fetched.toString(Qt::ISODate)},
      {"points", pts},  {"times", t},
      {"speed", sp},    {"dir", dr},
      {"gust", gs}};
}

ModelForecast ModelForecast::fromJson(const QJsonObject& o) {
  ModelForecast f;
  f.model = o.value("model").toString();
  f.fetched = QDateTime::fromString(o.value("fetched").toString(), Qt::ISODate);
  for (const auto& p : o.value("points").toArray()) {
    const QJsonArray a = p.toArray();
    f.points << QPointF(a.at(0).toDouble(), a.at(1).toDouble());
  }
  for (const auto& x : o.value("times").toArray())
    f.times << qint64(x.toDouble());
  for (const auto& a : o.value("speed").toArray())
    f.speedKn << floats(a.toArray());
  for (const auto& a : o.value("dir").toArray())
    f.dirFrom << floats(a.toArray());
  for (const auto& a : o.value("gust").toArray())
    f.gustKn << floats(a.toArray());
  return f;
}

// --------------------------------------------------------------------------

WeatherModels& WeatherModels::instance() {
  static WeatherModels s;
  return s;
}

QVector<WeatherModelInfo> WeatherModels::available() {
  return {{"icon_seamless", "ICON (DWD)"},
          {"gfs_seamless", "GFS (NOAA)"},
          {"ecmwf_ifs025", "ECMWF IFS"},
          {"meteofrance_seamless", "Météo-France"}};
}

QString WeatherModels::displayName(const QString& id) {
  for (const auto& m : available())
    if (m.id == id) return m.name;
  return id;
}

QVector<QPointF> WeatherModels::samplePoints(const QVector<QPointF>& route,
                                             double spacingNm) {
  QVector<QPointF> out;
  if (route.isEmpty()) return out;
  double total = 0;
  for (int i = 1; i < route.size(); ++i)
    total += geo::distanceNm(route[i - 1].y(), route[i - 1].x(), route[i].y(),
                             route[i].x());
  spacingNm = std::max(spacingNm, total / 39);
  out << route.first();
  double next = spacingNm, cum = 0;
  for (int i = 1; i < route.size(); ++i) {
    const QPointF a = route[i - 1], b = route[i];
    const double d = geo::distanceNm(a.y(), a.x(), b.y(), b.x());
    while (d > 0 && next < cum + d) {
      out << a + (b - a) * ((next - cum) / d);
      next += spacingNm;
    }
    cum += d;
  }
  if (geo::distanceNm(out.last().y(), out.last().x(), route.last().y(),
                      route.last().x()) > 0.5)
    out << route.last();
  // Open-Meteo works on a grid of a few km: round to 0.01°.
  for (QPointF& p : out)
    p = QPointF(std::round(p.x() * 100) / 100, std::round(p.y() * 100) / 100);
  return out;
}

void WeatherModels::fetch(const QVector<QPointF>& points,
                          const QStringList& models, Callback done) {
  QStringList lats, lons;
  for (const QPointF& p : points) {
    lats << QString::number(p.y(), 'f', 2);
    lons << QString::number(p.x(), 'f', 2);
  }
  QUrl url("https://api.open-meteo.com/v1/forecast");
  QUrlQuery q;
  q.addQueryItem("latitude", lats.join(','));
  q.addQueryItem("longitude", lons.join(','));
  q.addQueryItem("hourly", "wind_speed_10m,wind_direction_10m,wind_gusts_10m");
  q.addQueryItem("models", models.join(','));
  q.addQueryItem("wind_speed_unit", "kn");
  q.addQueryItem("forecast_days", "7");
  q.addQueryItem("timeformat", "unixtime");
  url.setQuery(q);
  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::UserAgentHeader,
                "OpenCPN-Qt/0.1 (passage planning)");
  req.setTransferTimeout(30000);
  QNetworkReply* r = m_net.get(req);
  connect(r, &QNetworkReply::finished, this, [r, points, models, done] {
    r->deleteLater();
    const QByteArray body = r->readAll();
    if (r->error() != QNetworkReply::NoError) {
      // Open-Meteo explains bad requests in {"reason": …}.
      const QString reason =
          QJsonDocument::fromJson(body).object().value("reason").toString();
      done({}, reason.isEmpty() ? r->errorString() : reason);
      return;
    }
    QString err;
    const auto result = parse(body, points, models, &err);
    done(result, err);
  });
}

QHash<QString, ModelForecast> WeatherModels::parse(
    const QByteArray& json, const QVector<QPointF>& points,
    const QStringList& models, QString* error) {
  QHash<QString, ModelForecast> out;
  const QJsonDocument doc = QJsonDocument::fromJson(json);
  QJsonArray locations;
  if (doc.isArray())
    locations = doc.array();
  else if (doc.isObject())
    locations << doc.object();
  if (locations.size() != points.size()) {
    if (error)
      *error = QCoreApplication::translate("WeatherModels",
                                           "Unexpected weather data.");
    return out;
  }
  const QDateTime now = QDateTime::currentDateTimeUtc();
  for (const QString& m : models) {
    ModelForecast f;
    f.model = m;
    f.points = points;
    f.fetched = now;
    bool any = false;
    for (int i = 0; i < locations.size(); ++i) {
      const QJsonObject hourly =
          locations[i].toObject().value("hourly").toObject();
      if (i == 0)
        for (const auto& t : hourly.value("time").toArray())
          f.times << qint64(t.toDouble());
      const auto key = [&](const char* var) {
        // With one model the keys carry no model suffix.
        const QString k = QString::fromLatin1(var) + '_' + m;
        return hourly.contains(k) ? k : QString::fromLatin1(var);
      };
      f.speedKn << floats(hourly.value(key("wind_speed_10m")).toArray());
      f.dirFrom << floats(hourly.value(key("wind_direction_10m")).toArray());
      f.gustKn << floats(hourly.value(key("wind_gusts_10m")).toArray());
      for (float v : f.speedKn.last()) any |= !std::isnan(v);
    }
    // A model that does not cover the area answers with nulls only.
    if (any && f.isValid()) out.insert(m, f);
  }
  if (out.isEmpty() && error)
    *error = QCoreApplication::translate("WeatherModels",
                                         "No model covers this area.");
  return out;
}
