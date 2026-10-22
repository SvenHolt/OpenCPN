// Range analysis: how long fresh water, the holding tank, the battery and
// the diesel last at anchor and underway.
//
// The daily use is learned from the consumption log (the last days or the
// last trip, split by mode). Heating depends on the weather: diesel and
// heater electricity are learned per heating degree day (base 18 °C) and
// projected with the temperature forecast; solar yield follows the
// forecast radiation.
#pragma once

#include <QDate>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPair>
#include <QPointF>
#include <QVector>
#include <cmath>
#include <limits>

#include "core/Consumption.h"

/** One day of the temperature forecast. */
struct DayForecast {
  QDate date;
  double minC = 0, maxC = 0;
  double radiationMJ = std::numeric_limits<double>::quiet_NaN();  // per m²
  double meanC() const { return (minC + maxC) / 2; }
  /** Heating degree days (base 18 °C), from the daily min and max. */
  double degreeDays() const;
};

/** Daily use for one mode, learned from the log. */
struct UsageRates {
  BoatMode mode = BoatMode::Anchor;
  double days = 0;       // logged time in this mode
  bool learned = false;  // false: defaults (too little data)
  double meanTempC = 15;
  double degreeDaysPerDay = 0;
  double freshLpd = 0, wasteLpd = 0;
  double engineFuelLpd = 0, engineHpd = 0;
  double heaterFuelPerDd = 0;  // l per degree day
  double heaterWhPerDd = 0;    // Wh per degree day
  double baseWhPerDay = 0;     // loads without the heater
  double solarWhPerDay = 0, alternatorWhPerDay = 0;

  static UsageRates defaults(BoatMode mode);
};

enum class RangeResource { FreshWater, WasteWater, Energy, Diesel };

struct ResourceRange {
  RangeResource kind = RangeResource::FreshWater;
  double left = 0;    // l or Wh still available (waste: free volume)
  double perDay = 0;  // mean daily use over the projection (Wh: net)
  double days = std::numeric_limits<double>::infinity();  // inf: > horizon
  bool unlimited() const { return std::isinf(days); }
};

struct ProjectionDay {
  QDate date;
  double tempC = 0, degreeDays = 0;
  bool forecast = false;  // temperature from the forecast
  double freshL = 0, wasteL = 0, fuelL = 0, batteryPct = 0;  // end of day
  double heaterFuelL = 0, netWh = 0, solarWh = 0;
};

struct RangeResult {
  BoatMode mode = BoatMode::Anchor;
  UsageRates rates;
  ResourceRange resources[4];
  RangeResource limit = RangeResource::FreshWater;
  double days = std::numeric_limits<double>::infinity();
  QVector<ProjectionDay> projection;
  const ResourceRange& resource(RangeResource r) const {
    return resources[int(r)];
  }
};

/** What is on board now. */
struct RangeState {
  double freshL = 0, freshCapacityL = 160, wasteL = 0, wasteCapacityL = 40;
  double fuelL = 0, fuelCapacityL = 120, fuelReserveL = 0;
  double soc = 0.8, minSoc = 0.2, capacityAh = 200, voltage = 12.8;
  double solarPeakW = 200;
  static RangeState fromBoat();
};

enum class RangeWindow { Days3, Days7, Days14, LastTrip };

namespace range {
QString resourceName(RangeResource r);
/** Time span of the log the analysis uses. Invalid if there is no data. */
QPair<QDateTime, QDateTime> window(const QVector<ConsumptionSample>& log,
                                   RangeWindow w, const QDateTime& now);
/** The latest period away from shore power (gaps up to 3 h). */
QPair<QDateTime, QDateTime> lastTrip(const QVector<ConsumptionSample>& log);
UsageRates learn(const QVector<ConsumptionSample>& samples, BoatMode mode);
/** Day by day projection from `today` for up to `horizon` days. Days
 *  without a forecast use the mean forecast temperature, or the logged
 *  mean when there is no forecast at all. */
RangeResult project(const UsageRates& rates, const RangeState& state,
                    const QVector<DayForecast>& forecast, const QDate& today,
                    int horizon = 30);
}  // namespace range

/** Daily temperature and radiation forecast (Open-Meteo) for the boat
 *  position, cached in AppData for use without internet. */
class TemperatureForecast : public QObject {
  Q_OBJECT
public:
  static TemperatureForecast& instance();
  const QVector<DayForecast>& days() const { return m_days; }
  QDateTime fetched() const { return m_fetched; }
  QPointF position() const { return m_pos; }  // x = lon, y = lat
  bool busy() const { return m_busy; }
  /** Days from today on. */
  QVector<DayForecast> upcoming(const QDate& today) const;
  /** Fetches when the cache is older than `maxAgeH` hours or more than
   *  30 NM away. */
  void update(double lat, double lon, int maxAgeH = 3);
  void fetch(double lat, double lon);
  static QVector<DayForecast> parse(const QByteArray& json, QString* error);
  /** Replaces the forecast (tests, offline data). */
  void setDays(const QVector<DayForecast>& days, const QPointF& pos,
               const QDateTime& fetched);

signals:
  void updated();
  void failed(const QString& message);

private:
  TemperatureForecast();
  void store() const;
  static QString file();
  QVector<DayForecast> m_days;
  QDateTime m_fetched;
  QPointF m_pos;
  bool m_busy = false;
  QNetworkAccessManager m_net;
};
