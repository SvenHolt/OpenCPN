// Spotify Connect control through the Spotify Web API (all platforms).
//
// Uses the OAuth 2 authorization code flow with PKCE, so no client secret is
// needed: the user registers an app at developer.spotify.com, adds the
// redirect URI http://127.0.0.1:8898/callback and enters the client ID.
// Playback control requires Spotify Premium (a Spotify API restriction).
#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTimer>

#include "MediaSource.h"

class SpotifyWebSource : public MediaSource {
  Q_OBJECT
public:
  static constexpr quint16 kRedirectPort = 8898;
  static QString redirectUri();

  explicit SpotifyWebSource(QObject* parent = nullptr);

  QString id() const override { return QStringLiteral("spotify-web"); }
  QString service() const override;
  NowPlaying state() const override { return m_state; }
  bool isAvailable() const override { return isConnected(); }

  void playPause() override;
  void next() override;
  void previous() override;

  QString clientId() const { return m_clientId; }
  void setClientId(const QString& id);
  bool isConnected() const { return !m_refreshToken.isEmpty(); }
  /** Opens the browser for the Spotify login (PKCE). */
  void connectAccount();
  void disconnectAccount();

  // Helpers (public for tests)
  static QString makeVerifier();
  static QString challengeFor(const QString& verifier);
  static QUrl authorizeUrl(const QString& clientId, const QString& challenge,
                           const QString& state);
  /** Parses GET /v1/me/player. */
  static NowPlaying parsePlayer(const QJsonObject& o, QString* device);

signals:
  void connectionChanged();

private:
  void handleCallback(QTcpSocket* socket);
  void requestToken(const QList<QPair<QString, QString>>& params);
  void poll();
  void command(const QByteArray& verb, const QString& path);
  QNetworkRequest apiRequest(const QString& path) const;

  QNetworkAccessManager m_net;
  QTcpServer m_server;
  QTimer m_poll;
  QString m_clientId, m_verifier, m_stateNonce;
  QString m_accessToken, m_refreshToken;
  QDateTime m_expires;
  QString m_device;
  NowPlaying m_state;
};
