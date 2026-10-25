// Sonar / echo sounder: a history of pings from the depth and the water
// temperature (Signal K or NMEA 0183), and echo samples when a Signal K
// path delivers them.
//
// Signal K has no standard path for the raw returns of a sonar. Echo
// samples are read from a configurable path (default
// "environment.depth.echogram") whose value is
//   {"range": <m>, "samples": [<0..255 or 0..1>, ...]}  (surface downwards)
// or the same with "data" as Base64 bytes. Without samples the echogram is
// drawn from the depth readings.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonValue>
#include <QObject>
#include <QTimer>
#include <QVector>
#include <limits>

struct SonarPing {
  QDateTime time;
  double depthM = std::numeric_limits<double>::quiet_NaN();
  double waterTempC = std::numeric_limits<double>::quiet_NaN();
  float rangeM = 0;    // depth of the last echo sample
  QByteArray samples;  // echo strength 0..255, surface downwards
  bool hasEcho() const { return rangeM > 0 && !samples.isEmpty(); }
};

class SonarService : public QObject {
  Q_OBJECT
public:
  static SonarService& instance();
  static constexpr int kMaxPings = 7200;  // two hours at 1 Hz

  const QVector<SonarPing>& pings() const { return m_pings; }
  void addPing(const SonarPing& p);
  void clear();
  /** Signal K path with echo samples. */
  QString echoPath() const { return m_echoPath; }
  void setEchoPath(const QString& path);
  /** Parses an echo value (see above). */
  static bool parseEcho(const QJsonValue& v, float& rangeM,
                        QByteArray& samples);
  /** Where the data comes from: "Signal K", "NMEA 0183" or empty. */
  QString source() const;
  bool receivingEcho() const;
  /** Below keel: from Signal K (belowKeel or transducerToKeel), else NaN. */
  double belowKeelM() const;
  /** Min, max of the depth over the last `secs` seconds. */
  void depthRange(int secs, double& minM, double& maxM) const;

signals:
  void pinged();

private:
  SonarService();
  void tick();

  QVector<SonarPing> m_pings;
  QString m_echoPath;
  QTimer m_timer;
  QDateTime m_lastEcho;
};
