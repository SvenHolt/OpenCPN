// Signal K: a data connection to a Signal K server (delta stream over
// WebSocket) and the hub that keeps the latest values.
//
// Navigation, wind, depth, attitude and engine values go into the NavStore
// like NMEA data. The hub keeps every value of the own vessel (SI units as
// delivered), the water temperature and the radars the server offers
// (Signal K radar API, e.g. the Mayara radar server).
#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QObject>
#include <QRgb>
#include <QTimer>
#include <QUrl>
#include <QVector>

#include "core/Connections.h"
#include "core/Nmea.h"

class WebSocketClient;

/** A radar the server offers. */
struct RadarInfo {
  QString id, name, brand;
  int spokes = 2048;       // per revolution
  int maxSpokeLen = 1024;  // samples per spoke
  QUrl streamUrl;          // WebSocket with the spokes (protobuf)
  QVector<QRgb> legend;    // color per sample value (may be empty)
  QString serverUrl;       // the Signal K server it came from

  /** Parses the answer of the radar API: an object (id → radar) or an
   *  array. Relative stream URLs are resolved against `base`. */
  static QVector<RadarInfo> parseList(const QByteArray& json, const QUrl& base);
};

class SignalKHub : public QObject {
  Q_OBJECT
public:
  static SignalKHub& instance();

  struct Value {
    QJsonValue value;
    QDateTime time;
  };
  /** Latest value of a path of the own vessel ("environment.depth..."). */
  Value value(const QString& path) const { return m_values.value(path); }
  bool has(const QString& path) const { return m_values.contains(path); }
  const QHash<QString, Value>& values() const { return m_values; }
  /** A number; NaN if the path is unknown or older than `maxAgeSecs`. */
  double number(const QString& path, int maxAgeSecs = 30) const;

  const QVector<RadarInfo>& radars() const { return m_radars; }
  void setRadars(const QString& serverUrl, const QVector<RadarInfo>& radars);

  /** Applies a delta message (JSON text) of the stream; values of the own
   *  vessel go into the hub and as a NavUpdate into the NavStore. Returns
   *  the number of values used. */
  int applyDelta(const QByteArray& json);
  /** NavUpdate for one Signal K path and value (SI units); false if the
   *  path has no counterpart. Uses the hub for paired values (wind). */
  bool navUpdate(const QString& path, const QJsonValue& v, NavUpdate& u) const;
  QString selfContext() const { return m_self; }
  void clear();

signals:
  void valuesChanged(const QStringList& paths);
  void radarsChanged();

private:
  SignalKHub() = default;
  QHash<QString, Value> m_values;
  QHash<QString, QVector<RadarInfo>> m_radarsByServer;
  QVector<RadarInfo> m_radars;
  QString m_self;
};

/** Connection to a Signal K server (host, port; 443 = TLS). */
class SignalKConnection : public Connection {
  Q_OBJECT
public:
  explicit SignalKConnection(const ConnectionConfig& cfg,
                             QObject* parent = nullptr);
  ~SignalKConnection() override;
  void start() override;
  void stop() override;
  QUrl baseUrl() const;

private:
  void connectNow();
  void fetchRadars();

  WebSocketClient* m_ws = nullptr;
  QNetworkAccessManager m_net;
  QTimer m_retry, m_radarPoll;
  int m_deltas = 0;
  bool m_stopped = true;
};
