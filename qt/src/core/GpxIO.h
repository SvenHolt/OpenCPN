// GPX 1.1 import/export. Compatible with OpenCPN's navobj.xml.
#pragma once

#include <QString>
#include <QVector>

#include "core/NavStore.h"

struct GpxData {
  QVector<Waypoint> waypoints;
  QVector<Route> routes;
  QVector<Route> tracks;  // track segments flattened into point lists
};

namespace GpxIO {
bool read(const QString& path, GpxData& out, QString* error = nullptr);
bool parse(const QByteArray& xml, GpxData& out, QString* error = nullptr);
bool write(const QString& path, const GpxData& data);
QByteArray serialize(const GpxData& data);
}  // namespace GpxIO
