// Harmonic tide and current prediction from the XTide "legacy" ASCII data
// shipped with OpenCPN (data/tcdata/HARMONICS_NO_US + .IDX).
#pragma once

#include <QDateTime>
#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QPointF>
#include <QVector>
#include <limits>
#include <memory>

struct TideStation {
  enum class Type { Tide, Current };
  QString name;
  double lat = 0, lon = 0;
  Type type = Type::Tide;
  bool reference = true;
  int harmonic = -1;  // index into TideData::harmonics
  // Offsets of subordinate stations (minutes / factors / station units)
  double highTimeOffMin = 0, lowTimeOffMin = 0;
  double highMpy = 1, lowMpy = 1, highOff = 0, lowOff = 0;
  // Current stations: directions (deg true, towards) of flood and ebb;
  // NaN when the data has none (reference current stations).
  double floodDir = std::numeric_limits<double>::quiet_NaN();
  double ebbDir = std::numeric_limits<double>::quiet_NaN();
};

struct HarmonicStation {
  QString name;
  double meridianSecs = 0;  // time zone of the epochs
  double datum = 0;
  double toMeters = 1;         // unit conversion (feet -> m), 1 for knots
  QString unit;                // "m" or "kn"
  QVector<int> csts;           // constituent indices with non-zero amplitude
  QVector<double> amp, kappa;  // amplitude, epoch (radians)
};

struct TideData {
  int firstYear = 0, numYears = 0;
  QVector<QString> cstNames;
  QVector<double> speeds;                // rad/s
  QVector<QVector<double>> equilibrium;  // [cst][year] radians
  QVector<QVector<double>> nodes;        // [cst][year]
  QVector<HarmonicStation> harmonics;
  QVector<TideStation> stations;
};

struct TideExtreme {
  QDateTime time;
  double value = 0;
  bool high = true;
};

class TideDatabase : public QObject {
  Q_OBJECT
public:
  static TideDatabase& instance();

  /** Loads HARMONICS_NO_US(.IDX) from the directory in the background. */
  void loadAsync(const QString& dir);
  /** Synchronous parser (also used by tests). */
  static std::shared_ptr<TideData> load(const QString& harmonicsFile,
                                        const QString& indexFile,
                                        QString* error = nullptr);
  void setData(std::shared_ptr<const TideData> data);

  bool isLoaded() const { return bool(m_data); }
  const QVector<TideStation>& stations() const;
  std::shared_ptr<const TideData> data() const { return m_data; }

  /** Water level (m above chart datum) or current (kn, + = flood). */
  static double value(const TideData& d, const TideStation& s,
                      const QDateTime& utc);
  double value(const TideStation& s, const QDateTime& utc) const;
  /** High and low waters / slack and max currents in [from, to]. */
  QVector<TideExtreme> extremes(const TideStation& s, const QDateTime& from,
                                const QDateTime& to) const;
  QString unit(const TideStation& s) const;
  /** Nearest stations to a position, closest first. */
  QVector<const TideStation*> nearest(double lat, double lon, int count,
                                      TideStation::Type type) const;

signals:
  void loaded();

private:
  TideDatabase() = default;
  std::shared_ptr<const TideData> m_data;
  QFutureWatcher<std::shared_ptr<TideData>> m_watcher;
};
