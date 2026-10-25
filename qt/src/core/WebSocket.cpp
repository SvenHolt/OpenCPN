#include "core/WebSocket.h"

#include <QCryptographicHash>
#include <QPointer>
#include <QRandomGenerator>
#include <QTcpSocket>
#ifndef QT_NO_SSL
#include <QSslSocket>
#endif

WebSocketClient::WebSocketClient(QObject* parent) : QObject(parent) {}

WebSocketClient::~WebSocketClient() {
  if (m_socket) m_socket->abort();
}

QByteArray WebSocketClient::frame(int opcode, const QByteArray& payload,
                                  bool mask) {
  QByteArray f;
  f.append(char(0x80 | (opcode & 0x0F)));
  const quint64 len = quint64(payload.size());
  const char maskBit = mask ? char(0x80) : char(0);
  if (len < 126) {
    f.append(char(maskBit | char(len)));
  } else if (len < 65536) {
    f.append(char(maskBit | 126));
    f.append(char(len >> 8));
    f.append(char(len & 0xFF));
  } else {
    f.append(char(maskBit | 127));
    for (int i = 7; i >= 0; --i) f.append(char((len >> (8 * i)) & 0xFF));
  }
  if (!mask) return f + payload;
  uchar key[4];
  const quint32 r = QRandomGenerator::global()->generate();
  for (int i = 0; i < 4; ++i) key[i] = uchar(r >> (8 * i));
  f.append(reinterpret_cast<const char*>(key), 4);
  QByteArray body = payload;
  for (qsizetype i = 0; i < body.size(); ++i) body[i] ^= key[i % 4];
  return f + body;
}

void WebSocketClient::open(const QUrl& in,
                           const QHash<QByteArray, QByteArray>& headers) {
  close();
  m_url = in;
  const QString scheme = in.scheme().toLower();
  const bool tls = scheme == "wss" || scheme == "https";
  const quint16 port = quint16(in.port(tls ? 443 : 80));
#ifndef QT_NO_SSL
  if (tls)
    m_socket = new QSslSocket(this);
  else
#endif
    m_socket = new QTcpSocket(this);
  m_handshake = false;
  m_open = false;
  m_buffer.clear();
  m_parser = {};
  QByteArray raw(16, 0);
  for (char& c : raw) c = char(QRandomGenerator::global()->bounded(256));
  m_key = raw.toBase64();

  const auto sendHandshake = [this, headers] {
    QByteArray path = m_url.path(QUrl::FullyEncoded).toUtf8();
    if (path.isEmpty()) path = "/";
    if (m_url.hasQuery())
      path += "?" + m_url.query(QUrl::FullyEncoded).toUtf8();
    QByteArray req = "GET " + path + " HTTP/1.1\r\n";
    req += "Host: " + m_url.host().toUtf8();
    if (m_url.port() > 0) req += ":" + QByteArray::number(m_url.port());
    req += "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n";
    req += "Sec-WebSocket-Key: " + m_key + "\r\n";
    req += "Sec-WebSocket-Version: 13\r\n";
    for (auto it = headers.begin(); it != headers.end(); ++it)
      req += it.key() + ": " + it.value() + "\r\n";
    req += "\r\n";
    m_socket->write(req);
  };
#ifndef QT_NO_SSL
  if (auto* ssl = qobject_cast<QSslSocket*>(m_socket)) {
    connect(ssl, &QSslSocket::encrypted, this, sendHandshake);
    ssl->connectToHostEncrypted(m_url.host(), port);
  } else
#endif
  {
    connect(m_socket, &QTcpSocket::connected, this, sendHandshake);
    m_socket->connectToHost(m_url.host(), port);
  }
  connect(m_socket, &QTcpSocket::readyRead, this,
          &WebSocketClient::onReadyRead);
  connect(m_socket, &QTcpSocket::errorOccurred, this,
          [this] { fail(m_socket->errorString()); });
  connect(m_socket, &QTcpSocket::disconnected, this, [this] {
    const bool was = m_open || m_handshake;
    m_open = false;
    m_handshake = false;
    if (was) emit disconnected();
  });
}

void WebSocketClient::close() {
  if (!m_socket) return;
  QTcpSocket* s = m_socket;
  m_socket = nullptr;
  if (m_open && s->state() == QAbstractSocket::ConnectedState) {
    s->write(frame(8, {}, true));
    s->flush();
  }
  s->disconnect(this);
  s->abort();
  s->deleteLater();
  const bool was = m_open;
  m_open = false;
  m_handshake = false;
  if (was) emit disconnected();
}

void WebSocketClient::sendText(const QString& text) {
  if (m_open) m_socket->write(frame(1, text.toUtf8(), true));
}

void WebSocketClient::sendBinary(const QByteArray& data) {
  if (m_open) m_socket->write(frame(2, data, true));
}

void WebSocketClient::fail(const QString& error) {
  emit errorOccurred(error);
  if (m_socket) {
    const bool was = m_open;
    m_socket->disconnect(this);
    m_socket->abort();
    m_socket->deleteLater();
    m_socket = nullptr;
    m_open = false;
    m_handshake = false;
    if (was) emit disconnected();
  }
}

void WebSocketClient::onReadyRead() {
  QByteArray data = m_socket->readAll();
  if (!m_handshake) {
    m_buffer += data;
    const int end = int(m_buffer.indexOf("\r\n\r\n"));
    if (end < 0) {
      if (m_buffer.size() > 16384) fail(tr("Invalid WebSocket handshake"));
      return;
    }
    const QList<QByteArray> lines = m_buffer.left(end).split('\n');
    const QByteArray status = lines.value(0).trimmed();
    if (!status.contains(" 101")) {
      fail(tr("WebSocket refused: %1").arg(QString::fromUtf8(status)));
      return;
    }
    const QByteArray expect =
        QCryptographicHash::hash(m_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
                                 QCryptographicHash::Sha1)
            .toBase64();
    bool accepted = false;
    for (const QByteArray& l : lines) {
      const int colon = int(l.indexOf(':'));
      if (colon > 0 &&
          l.left(colon).trimmed().toLower() == "sec-websocket-accept")
        accepted = l.mid(colon + 1).trimmed() == expect;
    }
    if (!accepted) {
      fail(tr("Invalid WebSocket handshake"));
      return;
    }
    data = m_buffer.mid(end + 4);
    m_buffer.clear();
    m_handshake = true;
    m_open = true;
    emit connected();
    if (!m_socket) return;  // closed in a slot
  }
  QPointer<WebSocketClient> self(this);
  const bool ok =
      m_parser.feed(data, [this, self](int op, const QByteArray& payload) {
        if (!self || !m_socket) return;
        switch (op) {
          case 1:
            emit textMessageReceived(QString::fromUtf8(payload));
            break;
          case 2:
            emit binaryMessageReceived(payload);
            break;
          case 8:
            close();
            break;
          case 9:
            m_socket->write(frame(10, payload, true));
            break;
          default:
            break;
        }
      });
  if (!ok && self && m_socket) fail(tr("WebSocket protocol error"));
}
