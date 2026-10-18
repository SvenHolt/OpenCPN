// Built-in simulator: generates NMEA 0183 (RMC, HDT) for a simulated own
// ship that follows the active route, plus AIS traffic (AIVDM 1 and 5).
#pragma once

#include <QRandomGenerator>
#include <QTimer>
#include <QVector>

#include "core/Connections.h"

namespace AisEncoder {
/** Armored payload of a type 1 position report. */
QString positionReport(quint32 mmsi, double lat, double lon, double sog,
                       double cog, int hdg, int navStatus = 0);
/** Armored payload of a type 5 static report (name, callsign, ship type). */
QString staticReport(quint32 mmsi, const QString& name, const QString& callsign,
                     int shipType);
/** Wraps a payload into one or more !AIVDM sentences. */
QStringList sentences(const QString& payload, int seqId = 0, int fillBits = 0);
}  // namespace AisEncoder

class SimulatorConnection : public Connection {
  Q_OBJECT
public:
  explicit SimulatorConnection(const ConnectionConfig& cfg,
                               QObject* parent = nullptr);
  void start() override;
  void stop() override;

private:
  struct Vessel {
    quint32 mmsi;
    QString name, callsign;
    int type;
    double lat, lon, sog, cog;
  };
  void tick();
  void spawnTraffic();

  QTimer m_timer;
  double m_lat = 54.47, m_lon = 10.28;  // Kiel Bay
  double m_sog = 6.5, m_cog = 20, m_hdg = 20;
  QVector<Vessel> m_vessels;
  int m_tick = 0;
};
