// Voyages stored for offline use: the route, the passage settings, wind
// forecasts of several models, a copy of the GRIB file and the OSM data
// (harbours, seamarks, coastline) along the route. Everything stays
// available without internet on longer passages.
#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QVector>

#include "core/WeatherModels.h"
#include "core/osm/Overpass.h"

struct Voyage {
  QString id, name;
  QDateTime created, updated;
  QString fromName, toName;
  QVector<QPointF> route;  // x = lon, y = lat
  QJsonObject settings;    // passage planner inputs
  QHash<QString, ModelForecast> forecasts;
  QString gribFile;     // file name inside the voyage folder
  int tiles = 0;        // harbour tiles stored for offline use
  QStringList regions;  // names of offline OSM regions
  QString status;       // last download result

  double lengthNm() const;
  QJsonObject toJson() const;
  static Voyage fromJson(const QJsonObject& o);
};

class VoyageStore : public QObject {
  Q_OBJECT
public:
  static VoyageStore& instance();
  const QVector<Voyage>& voyages() const { return m_voyages; }
  const Voyage* voyage(const QString& id) const;
  /** Stores a voyage (assigns id and dates); returns its id. */
  QString save(Voyage v);
  void remove(const QString& id);
  QString folder(const QString& id) const;

  /** Downloads everything for offline use: model forecasts, GRIB copy,
   *  harbour tiles along the route (kept without expiry) and offline OSM
   *  regions for the local Overpass instance. `tileQuery` is the chart
   *  layer's Overpass query with "%1" for the bounding box. */
  void prepareOffline(const QString& id, const QString& tileQuery);
  bool isPreparing(const QString& id) const { return m_preparing == id; }

  /** Boxes covering the route with a margin, at most `maxDeg2` each. */
  static QVector<GeoBox> corridor(const QVector<QPointF>& route,
                                  double marginNm, double maxDeg2);

signals:
  void changed();
  void progress(const QString& id, const QString& text, int done, int total);
  void prepared(const QString& id, bool ok, const QString& summary);

private:
  VoyageStore();
  void load();
  void write(const Voyage& v) const;
  void step();
  Voyage* find(const QString& id);

  QVector<Voyage> m_voyages;
  // Running offline preparation
  QString m_preparing;
  QString m_tileQuery;
  int m_stage = 0;
  QStringList m_tileKeys;
  int m_tilesDone = 0;
  QVector<QPair<QString, GeoBox>> m_regionQueue;
  QStringList m_notes;
};
