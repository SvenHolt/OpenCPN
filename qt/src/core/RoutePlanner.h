// Automatic route planning over water (harbour to harbour).
//
// The area around start and destination is rasterized in Mercator space:
// basemap land polygons and OSM barriers (coastline, breakwaters) block
// cells. OSM coastlines have the land on their left side; enclosed areas
// on that side are blocked as well. A* finds the shortest path on the
// 8-connected grid, which is then straightened by line of sight.
#pragma once

#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QVector>

#include "core/Shapefile.h"

struct RoutePlanRequest {
  double fromLat = 0, fromLon = 0, toLat = 0, toLon = 0;
  /** Land polygons (normalized Mercator), e.g. the basemap. */
  const QVector<MercatorPolygon>* land = nullptr;
  /** Mercator rectangles in which `land` is ignored because detailed OSM
   *  barriers cover them (offline regions). */
  QVector<QRectF> landIgnored;
  /** Lines that must not be crossed, in normalized Mercator. */
  QVector<QPolygonF> barriers;
  /** Barriers with land on their left (OSM natural=coastline). */
  QVector<QPolygonF> coastlines;
  /** Distance kept from land and barriers. */
  double marginNm = 0.05;
  /** Grid size along the longer side. */
  int maxCells = 700;
};

struct RoutePlanResult {
  QString error;
  QVector<QPointF> points;  // x = lon, y = lat, including start and end
  double lengthNm = 0;
  double cellNm = 0;  // grid resolution used
  bool ok() const { return error.isEmpty() && points.size() >= 2; }
};

namespace RoutePlanner {
RoutePlanResult plan(const RoutePlanRequest& request);
/** Searched area (normalized Mercator) for start and destination. */
QRectF searchArea(double fromLat, double fromLon, double toLat, double toLon);
}  // namespace RoutePlanner
