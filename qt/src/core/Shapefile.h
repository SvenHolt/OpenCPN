// Reader for ESRI polygon shapefiles (the OpenCPN basemap). Polygons are
// returned already projected to normalized Web-Mercator coordinates.
#pragma once

#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QVector>

struct MercatorPolygon {
  QPainterPath path;  // all rings of one record, odd-even filled
  int pointCount = 0;
  QRectF bounds;
};

namespace Shapefile {
/** Loads all polygon records of a .shp file. Returns false on error. */
bool loadPolygons(const QString& path, QVector<MercatorPolygon>& out,
                  QString* error = nullptr);
}  // namespace Shapefile
