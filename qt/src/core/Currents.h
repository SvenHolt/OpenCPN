// Tides and currents for passage planning, at sea and inland.
//
// Currents (set = direction the water flows to, drift in kn) come from, in
// this order:
//  * rivers and canals: the flow direction of the OpenStreetMap waterway
//    (ways are drawn downstream) and a typical speed of the river, which
//    the skipper can change; canals have no current; tidal reaches are
//    left to the sources below,
//  * tidal current stations of the harmonic data (flood/ebb directions),
//  * the Open-Meteo marine forecast (tidal and wind driven currents, about
//    8 km grid), stored with the voyage for use offline,
//  * a manual set and drift.
// Water levels come from the tide stations (height above chart datum,
// high and low waters), the marine forecast (sea level against mean sea
// level) and, inland in Germany, the PEGELONLINE gauges.
#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointF>
#include <QVector>
#include <cmath>
#include <functional>
#include <limits>

#include "core/PassagePlanner.h"
#include "core/Tides.h"

struct OsmElement;

/** Hourly marine forecast at sample points along a route. */
struct MarineForecast {
  QVector<QPointF> points;  // lon, lat
  QVector<qint64> times;    // UTC seconds
  // [point][time]; NaN where the point lies on land
  QVector<QVector<float>> driftKn, setDeg, levelM;
  QDateTime fetched;

  bool isValid() const { return !points.isEmpty() && times.size() >= 2; }
  /** Current interpolated in time; the nearest sea point within 10 NM. */
  bool current(double lat, double lon, const QDateTime& t, double& setDeg,
               double& driftKn) const;
  /** Sea level against mean sea level (m). */
  bool level(double lat, double lon, const QDateTime& t, double& m) const;
  QJsonObject toJson() const;
  static MarineForecast fromJson(const QJsonObject& o);
  /** Parses an Open-Meteo marine answer for `points`. */
  static MarineForecast parse(const QByteArray& json,
                              const QVector<QPointF>& points,
                              QString* error = nullptr);

private:
  int nearest(double lat, double lon, double maxNm) const;
};

/** Rivers and canals along a route with their flow direction. */
struct Waterway {
  QString name, kind;       // kind: river, canal, tidal (tidal reach)
  QVector<QPointF> points;  // lon, lat, downstream
  double speedKn = 0;       // typical flow speed
};

class RiverNetwork {
public:
  const QVector<Waterway>& ways() const { return m_ways; }
  bool isEmpty() const { return m_ways.isEmpty(); }
  /** River current at a position (within 0.15 NM of a river or canal).
   *  Tidal reaches return false (the tide decides). */
  bool current(double lat, double lon, double& setDeg, double& driftKn,
               QString* name = nullptr) const;
  /** Names of the rivers and canals with their speed (for the panel). */
  QVector<QPair<QString, double>> summary() const;
  /** Sets the speed of all ways with this name. */
  void setSpeed(const QString& name, double kn);
  static RiverNetwork fromOsm(const QVector<OsmElement>& elements);
  /** Typical flow (kn) by river name; canals 0; unknown rivers 1 kn. */
  static double typicalSpeedKn(const QString& name, const QString& kind);
  /** Overpass query for the waterways in a box. */
  static QString query(double south, double west, double north, double east);
  QJsonObject toJson() const;
  static RiverNetwork fromJson(const QJsonObject& o);

private:
  QVector<Waterway> m_ways;
};

/** Tidal current stations (harmonic data with flood/ebb directions). */
namespace TidalCurrents {
/** Current from the stations within `maxNm` (inverse distance). */
bool current(double lat, double lon, const QDateTime& t, double& setDeg,
             double& driftKn, double maxNm = 6);
}  // namespace TidalCurrents

/** Water level at a place for the planner. */
struct LevelInfo {
  QString source;  // station name, "marine forecast"
  double distanceNm = -1;
  double levelM = std::numeric_limits<double>::quiet_NaN();
  bool chartDatum = true;         // false: against mean sea level
  QVector<TideExtreme> extremes;  // high and low waters around the time
  bool valid() const { return !std::isnan(levelM); }
};

/** All current and level sources of a passage. */
struct CurrentModel {
  enum class Mode { Auto, Manual, Off };
  Mode mode = Mode::Auto;
  double manualSetDeg = 0, manualDriftKn = 0;
  MarineForecast marine;
  RiverNetwork rivers;
  bool useStations = true;

  /** Current at a place and time; `source` names where it came from. */
  bool current(double lat, double lon, const QDateTime& t, double& setDeg,
               double& driftKn, QString* source = nullptr) const;
  CurrentSource source() const;
  /** Water level: the nearest tide station within 15 NM, else the marine
   *  forecast. */
  LevelInfo level(double lat, double lon, const QDateTime& t) const;
};

/** A PEGELONLINE gauge (German federal waterways). */
struct Gauge {
  QString name, water;
  double km = std::numeric_limits<double>::quiet_NaN();
  double lat = 0, lon = 0;
  double valueCm = std::numeric_limits<double>::quiet_NaN();
  QDateTime time;
  double mnwCm = std::numeric_limits<double>::quiet_NaN();  // mean low
  double mwCm = std::numeric_limits<double>::quiet_NaN();   // mean
  double mhwCm = std::numeric_limits<double>::quiet_NaN();  // mean high
  /** "784 cm (MW 520 cm)". */
  QString text() const;
  QJsonObject toJson() const;
  static Gauge fromJson(const QJsonObject& o);
};

/** Online services: the Open-Meteo marine forecast and PEGELONLINE. */
class TideServices : public QObject {
  Q_OBJECT
public:
  static TideServices& instance();
  using MarineCallback =
      std::function<void(const MarineForecast&, const QString& error)>;
  using GaugeCallback =
      std::function<void(const QVector<Gauge>&, const QString& error)>;
  void fetchMarine(const QVector<QPointF>& points, MarineCallback done);
  /** Gauges within `radiusKm` of a position, nearest first. */
  void fetchGauges(double lat, double lon, double radiusKm, GaugeCallback done);
  static QVector<Gauge> parseGauges(const QByteArray& json, double lat,
                                    double lon);

private:
  TideServices() = default;
  QNetworkAccessManager m_net;
};
