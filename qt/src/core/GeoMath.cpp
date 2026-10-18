#include "core/GeoMath.h"

#include <QtMath>
#include <cmath>

namespace geo {

static double deg2rad(double d) { return d * M_PI / 180.0; }
static double rad2deg(double r) { return r * 180.0 / M_PI; }

QPointF toMercator(double lat, double lon) {
  lat = qBound(-kMaxMercatorLat, lat, kMaxMercatorLat);
  const double x = (lon + 180.0) / 360.0;
  const double s = std::sin(deg2rad(lat));
  const double y = 0.5 - std::log((1 + s) / (1 - s)) / (4 * M_PI);
  return {x, y};
}

void fromMercator(const QPointF& p, double& lat, double& lon) {
  lon = p.x() * 360.0 - 180.0;
  const double n = M_PI - 2.0 * M_PI * p.y();
  lat = rad2deg(std::atan(std::sinh(n)));
}

double nmPerMercatorUnit(double lat) {
  return 2 * M_PI * kEarthRadiusNm * std::cos(deg2rad(lat));
}

double distanceNm(double lat1, double lon1, double lat2, double lon2) {
  const double p1 = deg2rad(lat1), p2 = deg2rad(lat2);
  const double dp = p2 - p1, dl = deg2rad(lon2 - lon1);
  const double a =
      std::sin(dp / 2) * std::sin(dp / 2) +
      std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
  return 2 * kEarthRadiusNm * std::atan2(std::sqrt(a), std::sqrt(1 - a));
}

double bearingDeg(double lat1, double lon1, double lat2, double lon2) {
  const double p1 = deg2rad(lat1), p2 = deg2rad(lat2);
  const double dl = deg2rad(lon2 - lon1);
  const double y = std::sin(dl) * std::cos(p2);
  const double x =
      std::cos(p1) * std::sin(p2) - std::sin(p1) * std::cos(p2) * std::cos(dl);
  return normalizeDeg(rad2deg(std::atan2(y, x)));
}

void destination(double lat, double lon, double bearing, double distNm,
                 double& outLat, double& outLon) {
  const double d = distNm / kEarthRadiusNm, b = deg2rad(bearing);
  const double p1 = deg2rad(lat), l1 = deg2rad(lon);
  const double p2 = std::asin(std::sin(p1) * std::cos(d) +
                              std::cos(p1) * std::sin(d) * std::cos(b));
  const double l2 = l1 + std::atan2(std::sin(b) * std::sin(d) * std::cos(p1),
                                    std::cos(d) - std::sin(p1) * std::sin(p2));
  outLat = rad2deg(p2);
  outLon = rad2deg(l2);
  if (outLon > 180) outLon -= 360;
  if (outLon < -180) outLon += 360;
}

double crossTrackNm(double lat1, double lon1, double lat2, double lon2,
                    double lat, double lon) {
  const double d13 = distanceNm(lat1, lon1, lat, lon) / kEarthRadiusNm;
  const double t13 = deg2rad(bearingDeg(lat1, lon1, lat, lon));
  const double t12 = deg2rad(bearingDeg(lat1, lon1, lat2, lon2));
  return std::asin(std::sin(d13) * std::sin(t13 - t12)) * kEarthRadiusNm;
}

double normalizeDeg(double deg) {
  double r = std::fmod(deg, 360.0);
  if (r < 0) r += 360.0;
  return r;
}

double angleDiff(double a, double b) {
  double d = normalizeDeg(b - a);
  if (d > 180) d -= 360;
  return d;
}

static QString formatDM(double v, QChar pos, QChar neg, int degWidth) {
  const QChar hemi = v >= 0 ? pos : neg;
  v = std::fabs(v);
  int deg = int(v);
  double min = (v - deg) * 60.0;
  if (min >= 59.9995) {
    min = 0;
    ++deg;
  }
  return QStringLiteral("%1°%2′ %3")
      .arg(deg, degWidth, 10, QChar('0'))
      .arg(min, 6, 'f', 3, QChar('0'))
      .arg(hemi);
}

QString formatLat(double lat) { return formatDM(lat, 'N', 'S', 2); }
QString formatLon(double lon) { return formatDM(lon, 'E', 'W', 3); }

Cpa computeCpa(double ownLat, double ownLon, double ownSog, double ownCog,
               double tLat, double tLon, double tSog, double tCog) {
  Cpa r;
  if (std::isnan(ownSog) || std::isnan(ownCog)) ownSog = ownCog = 0;
  if (std::isnan(tSog) || std::isnan(tCog)) return r;
  // Target position relative to own ship in NM (x east, y north).
  const double k = std::cos(deg2rad(ownLat));
  const double px = (tLon - ownLon) * 60.0 * k;
  const double py = (tLat - ownLat) * 60.0;
  // Relative velocity in NM/h.
  const double vx =
      tSog * std::sin(deg2rad(tCog)) - ownSog * std::sin(deg2rad(ownCog));
  const double vy =
      tSog * std::cos(deg2rad(tCog)) - ownSog * std::cos(deg2rad(ownCog));
  const double v2 = vx * vx + vy * vy;
  double t = 0;
  if (v2 > 1e-9) t = -(px * vx + py * vy) / v2;
  if (t < 0) t = 0;
  const double cx = px + vx * t, cy = py + vy * t;
  r.valid = true;
  r.cpaNm = std::sqrt(cx * cx + cy * cy);
  r.tcpaMin = t * 60.0;
  return r;
}

}  // namespace geo
