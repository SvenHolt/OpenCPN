// A small WebSocket client (RFC 6455) on QTcpSocket/QSslSocket: enough for
// the Signal K delta stream and binary radar spoke streams, without
// depending on the Qt WebSockets module.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QUrl>

class QTcpSocket;

class WebSocketClient : public QObject {
  Q_OBJECT
public:
  explicit WebSocketClient(QObject* parent = nullptr);
  ~WebSocketClient() override;

  /** ws:// or wss:// (http/https are accepted as well). */
  void open(const QUrl& url, const QHash<QByteArray, QByteArray>& headers = {});
  void close();
  bool isOpen() const { return m_open; }
  QUrl url() const { return m_url; }
  void sendText(const QString& text);
  void sendBinary(const QByteArray& data);

  /** One frame, as a client sends it (masked) or a server (unmasked). */
  static QByteArray frame(int opcode, const QByteArray& payload, bool mask);
  /** Parses the frames at the start of `buffer` (consumed); returns false on
   *  a protocol error. Complete messages go to `onMessage(opcode, data)`. */
  struct Parser {
    QByteArray buffer, partial;
    int partialOpcode = 0;
    template <typename F>
    bool feed(const QByteArray& data, F onMessage);
  };

signals:
  void connected();
  void disconnected();
  void textMessageReceived(const QString& message);
  void binaryMessageReceived(const QByteArray& message);
  void errorOccurred(const QString& error);

private:
  void onReadyRead();
  void fail(const QString& error);

  QTcpSocket* m_socket = nullptr;
  QUrl m_url;
  QByteArray m_key, m_buffer;
  bool m_handshake = false, m_open = false;
  Parser m_parser;
};

template <typename F>
bool WebSocketClient::Parser::feed(const QByteArray& data, F onMessage) {
  buffer += data;
  for (;;) {
    if (buffer.size() < 2) return true;
    const auto* b = reinterpret_cast<const uchar*>(buffer.constData());
    const bool fin = b[0] & 0x80;
    const int opcode = b[0] & 0x0F;
    const bool masked = b[1] & 0x80;
    quint64 len = b[1] & 0x7F;
    int pos = 2;
    if (len == 126) {
      if (buffer.size() < 4) return true;
      len = (quint64(b[2]) << 8) | b[3];
      pos = 4;
    } else if (len == 127) {
      if (buffer.size() < 10) return true;
      len = 0;
      for (int i = 0; i < 8; ++i) len = (len << 8) | b[2 + i];
      pos = 10;
    }
    if (len > 64ull * 1024 * 1024) return false;
    uchar mask[4] = {0, 0, 0, 0};
    if (masked) {
      if (buffer.size() < pos + 4) return true;
      for (int i = 0; i < 4; ++i) mask[i] = b[pos + i];
      pos += 4;
    }
    if (quint64(buffer.size()) < pos + len) return true;
    QByteArray payload = buffer.mid(pos, qsizetype(len));
    buffer.remove(0, pos + qsizetype(len));
    if (masked)
      for (qsizetype i = 0; i < payload.size(); ++i) payload[i] ^= mask[i % 4];
    if (opcode >= 8) {  // control frames are never fragmented
      onMessage(opcode, payload);
      continue;
    }
    if (opcode != 0) {
      partial = payload;
      partialOpcode = opcode;
    } else {
      partial += payload;
    }
    if (fin) {
      onMessage(partialOpcode, partial);
      partial.clear();
    }
  }
}
