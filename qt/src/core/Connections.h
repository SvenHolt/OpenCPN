// Data connections (UDP, TCP, serial, simulator) feeding NMEA 0183 into the
// NavStore through a shared parser.
#pragma once

#include <QAbstractSocket>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVector>

#include "core/Nmea.h"

struct ConnectionConfig {
  enum class Type { Simulator, Udp, Tcp, Serial };
  QString id;
  Type type = Type::Udp;
  QString host = QStringLiteral("127.0.0.1");
  quint16 port = 10110;
  QString serialPort;
  int baud = 4800;
  bool enabled = true;

  QString typeName() const;
  QString label() const;
};

class Connection : public QObject {
  Q_OBJECT
public:
  enum class Status { Off, Connecting, Connected, Error };

  explicit Connection(const ConnectionConfig& cfg, QObject* parent = nullptr)
      : QObject(parent), m_cfg(cfg) {}
  ~Connection() override = default;

  const ConnectionConfig& config() const { return m_cfg; }
  Status status() const { return m_status; }
  QString statusText() const { return m_statusText; }
  int sentencesPerMinute() const { return m_rate; }

  virtual void start() = 0;
  virtual void stop() = 0;

  static Connection* create(const ConnectionConfig& cfg, QObject* parent);

signals:
  void dataReceived(const QByteArray& data);
  void statusChanged();

protected:
  void setStatus(Status s, const QString& text = {});
  void countLines(const QByteArray& data);

  ConnectionConfig m_cfg;
  Status m_status = Status::Off;
  QString m_statusText;
  int m_rate = 0;
  int m_lines = 0;
  QTimer m_rateTimer;
};

class ConnectionManager : public QObject {
  Q_OBJECT
public:
  static ConnectionManager& instance();

  const QVector<Connection*>& connections() const { return m_connections; }
  NmeaParser& parser() { return m_parser; }

  void add(const ConnectionConfig& cfg);
  void remove(const QString& id);
  void setEnabled(const QString& id, bool enabled);

  void load();
  void save() const;
  bool anyConnected() const;

signals:
  void connectionsChanged();
  void statusChanged();

private:
  ConnectionManager();
  Connection* attach(const ConnectionConfig& cfg);

  QVector<Connection*> m_connections;
  NmeaParser m_parser;
};
