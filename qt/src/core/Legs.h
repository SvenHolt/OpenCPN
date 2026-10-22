// Legs (Etappen): the boat records position, speed, heel, wind, depth,
// outside temperature, energy and diesel every 30 seconds while underway.
// A leg starts when the boat moves (> 1.5 kn or engine on) and ends after
// 20 minutes at rest. Stored in AppData/legs/<id>.json.
#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVector>
#include <limits>

struct LegSample {
  qint64 t = 0;  // seconds since epoch (UTC)
  double lat = 0, lon = 0;
  double sog = kLegNaN, stw = kLegNaN, cog = kLegNaN, hdg = kLegNaN;
  double heel = kLegNaN;                // deg, + = starboard down
  double tws = kLegNaN, twd = kLegNaN;  // true wind kn, from deg
  double aws = kLegNaN, awa = kLegNaN;  // apparent
  double depth = kLegNaN, tempC = kLegNaN;
  double loadsW = kLegNaN, chargeW = kLegNaN;  // battery loads, charging
  double soc = kLegNaN;                        // 0..1
  double fuelLph = kLegNaN;                    // engine and heater
  double rpm = kLegNaN;

  static constexpr double kLegNaN = std::numeric_limits<double>::quiet_NaN();
  QJsonObject toJson() const;
  static LegSample fromJson(const QJsonObject& o);
};

struct LegSummary {
  double distanceNm = 0;
  double hours = 0;
  double avgSog = 0, maxSog = 0;
  double maxHeel = 0, avgHeel = 0;  // absolute
  double avgTws = 0, maxTws = 0;
  double engineHours = 0, fuelL = 0;
  double energyWh = 0, chargedWh = 0;
  double minTempC = 0, maxTempC = 0;
  double sailingShare = 0;  // time without engine, 0..1
};

struct Leg {
  QString id, name;
  QString from, to;  // harbour names (editable)
  QVector<LegSample> samples;
  bool simulated = false;

  QDateTime start() const;
  QDateTime end() const;
  LegSummary summary() const;
  /** Sample nearest to `t` (index, -1 if empty). */
  int indexAt(qint64 t) const;
  QJsonObject toJson() const;
  static Leg fromJson(const QJsonObject& o);
};

class LegStore : public QObject {
  Q_OBJECT
public:
  static LegStore& instance();

  /** Newest first. */
  const QVector<Leg>& legs() const { return m_legs; }
  const Leg* leg(const QString& id) const;
  /** Legs that touch the local day `date`. */
  QVector<const Leg*> legsOn(const QDate& date) const;
  /** The leg being recorded (empty id when none). */
  const Leg& current() const { return m_current; }
  bool recording() const { return !m_current.id.isEmpty(); }

  /** Adds or replaces a finished leg and stores it. */
  void save(const Leg& leg);
  void remove(const QString& id);
  void rename(const QString& id, const QString& name, const QString& from,
              const QString& to);
  /** Feeds one sample (normally every 30 s from the navigation data).
   *  Starts, extends and closes legs. */
  void addSample(const LegSample& s, bool engineRunning);
  /** Closes the current leg now (end of recording, tests). */
  void finishCurrent();
  /** A sample from the navigation data and the boat systems now. */
  static LegSample sampleNow();
  /** Generates past legs in the western Baltic (simulator only). */
  void seedSimulated(const QDateTime& now);
  void reload();

  static constexpr int kIntervalSecs = 30;
  static constexpr double kMovingKn = 1.5;
  static constexpr int kRestMinutes = 20;

signals:
  void changed();
  void currentChanged();

private:
  LegStore();
  void load();
  void store(const Leg& leg) const;
  static QString dir();
  QVector<Leg> m_legs;
  Leg m_current;
  qint64 m_lastMoving = 0;
  QTimer m_timer;
};
