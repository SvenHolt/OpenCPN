// Geodesy helpers for the Qt6 frontend: Web-Mercator projection,
// great-circle navigation and CPA computation.
#pragma once

#include <QPointF>
#include <QString>

namespace geo {

constexpr double kEarthRadiusNm = 3440.065;
constexpr double kMaxMercatorLat = 85.05112878;

/** Normalized Web-Mercator coordinates, x and y in [0, 1], y grows south. */
QPointF toMercator(double lat, double lon);
void fromMercator(const QPointF& p, double& lat, double& lon);

/** Nautical miles per normalized mercator unit at the given latitude. */
double nmPerMercatorUnit(double lat);

double distanceNm(double lat1, double lon1, double lat2, double lon2);
/** Initial great-circle bearing from 1 to 2, degrees true [0, 360). */
double bearingDeg(double lat1, double lon1, double lat2, double lon2);
void destination(double lat, double lon, double bearing, double distNm,
                 double& outLat, double& outLon);
/** Signed cross-track error of point p relative to leg 1->2, NM, + = right. */
double crossTrackNm(double lat1, double lon1, double lat2, double lon2,
                    double lat, double lon);

double normalizeDeg(double deg);
/** Signed difference b - a in (-180, 180]. */
double angleDiff(double a, double b);

QString formatLat(double lat);
QString formatLon(double lon);

struct Cpa {
  bool valid = false;
  double cpaNm = 0;
  double tcpaMin = 0;
};
/** Closest point of approach using a local flat-earth approximation. */
Cpa computeCpa(double ownLat, double ownLon, double ownSog, double ownCog,
               double tLat, double tLon, double tSog, double tCog);

}  // namespace geo
