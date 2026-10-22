// Consumption log: every 30 minutes the water, waste water, diesel and
// energy used on board, the outside temperature and whether the boat lay at
// anchor, was underway or on shore power. The range analysis learns the
// daily use from it. Kept for 30 days in AppData/consumption.json.
#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVector>
#include <algorithm>

#include "core/BoatSystems.h"

enum class BoatMode { Anchor, Underway, Marina };

struct ConsumptionSample {
  QDateTime end;  // end of the interval (UTC)
  double hours = 0.5;
  BoatMode mode = BoatMode::Anchor;
  double outsideC = 15;
  // Used within the interval
  double freshL = 0, wasteL = 0;
  double heaterFuelL = 0, engineFuelL = 0;
  double loadsWh = 0, heaterWh = 0;
  double solarWh = 0, shoreWh = 0, alternatorWh = 0;
  double engineH = 0;
  // State at the end
  double freshLevelL = 0, wasteLevelL = 0, fuelLevelL = 0, soc = 0;

  QDateTime start() const { return end.addSecs(qint64(-hours * 3600)); }
  /** Heating degree days of the interval (base 18 °C). */
  double degreeDays() const {
    return std::max(0.0, 18 - outsideC) * hours / 24;
  }
  QJsonObject toJson() const;
  static ConsumptionSample fromJson(const QJsonObject& o);
};

QString boatModeName(BoatMode m);

class ConsumptionLog : public QObject {
  Q_OBJECT
public:
  static ConsumptionLog& instance();

  const QVector<ConsumptionSample>& samples() const { return m_samples; }
  /** Samples overlapping [from, to). */
  QVector<ConsumptionSample> between(const QDateTime& from,
                                     const QDateTime& to) const;
  /** Adds a sample (sorted by time), drops samples older than 30 days and
   *  stores the log. */
  void add(const ConsumptionSample& s);
  void clear();
  /** True while the log holds generated history (simulator). */
  bool simulatedHistory() const { return m_simulated; }
  /** Generates `days` of plausible history up to `end`: changing weather,
   *  days at anchor, underway and in a marina (simulator only). */
  void seedSimulated(const QDateTime& end, int days = 10);
  /** The current mode from navigation and shore power. */
  static BoatMode currentMode();
  /** Takes a sample from the counters of the boat systems now. */
  void sampleNow();
  void reload();

  static constexpr int kKeepDays = 30;
  static constexpr int kIntervalMin = 30;

signals:
  void changed();

private:
  ConsumptionLog();
  void load();
  void store() const;
  static QString file();
  QVector<ConsumptionSample> m_samples;
  BoatCounters m_last;
  QDateTime m_lastTime;
  bool m_simulated = false;
  QTimer m_timer;
};
