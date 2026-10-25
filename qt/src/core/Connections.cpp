#include "core/Connections.h"

#include <QSettings>
#include <QTcpSocket>
#include <QUdpSocket>
#ifdef OCPN_QT_HAVE_SERIAL
#include <QSerialPort>
#endif

#include "core/NavStore.h"
#include "core/SignalK.h"
#include "core/Simulator.h"

QString ConnectionConfig::typeName() const {
  switch (type) {
    case Type::Simulator:
      return QObject::tr("Simulator");
    case Type::Udp:
      return QStringLiteral("UDP");
    case Type::Tcp:
      return QStringLiteral("TCP");
    case Type::Serial:
      return QObject::tr("Serial");
    case Type::SignalK:
      return QStringLiteral("Signal K");
  }
  return {};
}

QString ConnectionConfig::label() const {
  switch (type) {
    case Type::Simulator:
      return QObject::tr("Built-in simulator");
    case Type::Udp:
      return QObject::tr("UDP port %1").arg(port);
    case Type::Tcp:
      return QStringLiteral("%1:%2").arg(host).arg(port);
    case Type::Serial:
      return QStringLiteral("%1 @ %2").arg(serialPort).arg(baud);
    case Type::SignalK:
      return QStringLiteral("Signal K %1:%2").arg(host).arg(port);
  }
  return {};
}

void Connection::setStatus(Status s, const QString& text) {
  m_status = s;
  m_statusText = text;
  emit statusChanged();
}

void Connection::countLines(const QByteArray& data) {
  if (!m_rateTimer.isActive()) {
    m_rateTimer.setInterval(10000);
    connect(&m_rateTimer, &QTimer::timeout, this, [this] {
      m_rate = m_lines * 6;
      m_lines = 0;
      emit statusChanged();
    });
    m_rateTimer.start();
  }
  m_lines += data.count('\n');
}

namespace {

class UdpConnection : public Connection {
public:
  using Connection::Connection;
  void start() override {
    m_socket = new QUdpSocket(this);
    connect(m_socket, &QUdpSocket::readyRead, this, [this] {
      while (m_socket->hasPendingDatagrams()) {
        QByteArray d(int(m_socket->pendingDatagramSize()), 0);
        m_socket->readDatagram(d.data(), d.size());
        if (!d.endsWith('\n')) d.append("\r\n");
        countLines(d);
        emit dataReceived(d);
      }
    });
    if (m_socket->bind(QHostAddress::AnyIPv4, m_cfg.port,
                       QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint))
      setStatus(Status::Connected, tr("Listening"));
    else
      setStatus(Status::Error, m_socket->errorString());
  }
  void stop() override {
    delete m_socket;
    m_socket = nullptr;
    setStatus(Status::Off);
  }

private:
  QUdpSocket* m_socket = nullptr;
};

class TcpConnection : public Connection {
public:
  using Connection::Connection;
  void start() override {
    m_socket = new QTcpSocket(this);
    m_retry.setSingleShot(true);
    m_retry.setInterval(5000);
    connect(&m_retry, &QTimer::timeout, this, [this] { connectNow(); });
    connect(m_socket, &QTcpSocket::connected, this,
            [this] { setStatus(Status::Connected, tr("Connected")); });
    connect(m_socket, &QTcpSocket::readyRead, this, [this] {
      const QByteArray d = m_socket->readAll();
      countLines(d);
      emit dataReceived(d);
    });
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this] {
      setStatus(Status::Error, m_socket->errorString());
      m_retry.start();
    });
    connect(m_socket, &QTcpSocket::disconnected, this, [this] {
      if (m_socket) m_retry.start();
    });
    connectNow();
  }
  void stop() override {
    m_retry.stop();
    QTcpSocket* s = m_socket;
    m_socket = nullptr;
    delete s;
    setStatus(Status::Off);
  }

private:
  void connectNow() {
    if (!m_socket) return;
    setStatus(Status::Connecting, tr("Connecting…"));
    m_socket->abort();
    m_socket->connectToHost(m_cfg.host, m_cfg.port);
  }
  QTcpSocket* m_socket = nullptr;
  QTimer m_retry;
};

#ifdef OCPN_QT_HAVE_SERIAL
class SerialConnection : public Connection {
public:
  using Connection::Connection;
  void start() override {
    m_port = new QSerialPort(m_cfg.serialPort, this);
    m_port->setBaudRate(m_cfg.baud);
    connect(m_port, &QSerialPort::readyRead, this, [this] {
      const QByteArray d = m_port->readAll();
      countLines(d);
      emit dataReceived(d);
    });
    if (m_port->open(QIODevice::ReadOnly))
      setStatus(Status::Connected, tr("Open"));
    else
      setStatus(Status::Error, m_port->errorString());
  }
  void stop() override {
    delete m_port;
    m_port = nullptr;
    setStatus(Status::Off);
  }

private:
  QSerialPort* m_port = nullptr;
};
#endif

}  // namespace

Connection* Connection::create(const ConnectionConfig& cfg, QObject* parent) {
  switch (cfg.type) {
    case ConnectionConfig::Type::Simulator:
      return new SimulatorConnection(cfg, parent);
    case ConnectionConfig::Type::Udp:
      return new UdpConnection(cfg, parent);
    case ConnectionConfig::Type::Tcp:
      return new TcpConnection(cfg, parent);
    case ConnectionConfig::Type::SignalK:
      return new SignalKConnection(cfg, parent);
    case ConnectionConfig::Type::Serial:
#ifdef OCPN_QT_HAVE_SERIAL
      return new SerialConnection(cfg, parent);
#else
      return nullptr;
#endif
  }
  return nullptr;
}

ConnectionManager& ConnectionManager::instance() {
  static ConnectionManager m;
  return m;
}

ConnectionManager::ConnectionManager() {
  auto& store = NavStore::instance();
  connect(&m_parser, &NmeaParser::navUpdate, &store, &NavStore::applyNavUpdate);
  connect(&m_parser, &NmeaParser::aisMessage, &store, &NavStore::applyAis);
}

Connection* ConnectionManager::attach(const ConnectionConfig& cfg) {
  Connection* c = Connection::create(cfg, this);
  if (!c) return nullptr;
  connect(c, &Connection::dataReceived, &m_parser, &NmeaParser::feed);
  connect(c, &Connection::statusChanged, this,
          &ConnectionManager::statusChanged);
  m_connections.push_back(c);
  if (cfg.enabled) c->start();
  return c;
}

void ConnectionManager::add(const ConnectionConfig& in) {
  ConnectionConfig cfg = in;
  if (cfg.id.isEmpty()) cfg.id = NavStore::newId();
  attach(cfg);
  save();
  emit connectionsChanged();
}

void ConnectionManager::remove(const QString& id) {
  for (int i = 0; i < m_connections.size(); ++i) {
    if (m_connections[i]->config().id == id) {
      m_connections[i]->stop();
      m_connections[i]->deleteLater();
      m_connections.removeAt(i);
      break;
    }
  }
  save();
  emit connectionsChanged();
}

void ConnectionManager::setEnabled(const QString& id, bool enabled) {
  for (int i = 0; i < m_connections.size(); ++i) {
    Connection* c = m_connections[i];
    if (c->config().id != id || c->config().enabled == enabled) continue;
    ConnectionConfig cfg = c->config();
    cfg.enabled = enabled;
    c->stop();
    c->deleteLater();
    m_connections.removeAt(i);
    Connection* n = Connection::create(cfg, this);
    if (!n) break;
    connect(n, &Connection::dataReceived, &m_parser, &NmeaParser::feed);
    connect(n, &Connection::statusChanged, this,
            &ConnectionManager::statusChanged);
    m_connections.insert(i, n);
    if (enabled) n->start();
    break;
  }
  save();
  emit connectionsChanged();
}

bool ConnectionManager::anyConnected() const {
  for (auto* c : m_connections)
    if (c->status() == Connection::Status::Connected) return true;
  return false;
}

void ConnectionManager::load() {
  QSettings s;
  const int n = s.beginReadArray("Connections");
  for (int i = 0; i < n; ++i) {
    s.setArrayIndex(i);
    ConnectionConfig c;
    c.id = s.value("id").toString();
    c.type = ConnectionConfig::Type(s.value("type").toInt());
    c.host = s.value("host", c.host).toString();
    c.port = quint16(s.value("port", c.port).toUInt());
    c.serialPort = s.value("serialPort").toString();
    c.baud = s.value("baud", c.baud).toInt();
    c.enabled = s.value("enabled", true).toBool();
    if (c.id.isEmpty()) c.id = NavStore::newId();
    attach(c);
  }
  s.endArray();
  if (n == 0 && !s.contains("ConnectionsInitialized")) {
    // First start: offer the simulator and the standard NMEA UDP port.
    ConnectionConfig sim;
    sim.id = NavStore::newId();
    sim.type = ConnectionConfig::Type::Simulator;
    attach(sim);
    ConnectionConfig udp;
    udp.id = NavStore::newId();
    udp.type = ConnectionConfig::Type::Udp;
    udp.port = 10110;
    attach(udp);
    save();
  }
  emit connectionsChanged();
}

void ConnectionManager::save() const {
  QSettings s;
  s.setValue("ConnectionsInitialized", true);
  s.beginWriteArray("Connections", int(m_connections.size()));
  for (int i = 0; i < m_connections.size(); ++i) {
    s.setArrayIndex(i);
    const auto& c = m_connections[i]->config();
    s.setValue("id", c.id);
    s.setValue("type", int(c.type));
    s.setValue("host", c.host);
    s.setValue("port", c.port);
    s.setValue("serialPort", c.serialPort);
    s.setValue("baud", c.baud);
    s.setValue("enabled", c.enabled);
  }
  s.endArray();
}
