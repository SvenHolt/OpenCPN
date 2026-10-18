#include "SpotifyWebSource.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QSettings>
#include <QTcpSocket>
#include <QUrlQuery>

namespace {
const char* kAuthUrl = "https://accounts.spotify.com/authorize";
const char* kTokenUrl = "https://accounts.spotify.com/api/token";
const char* kApi = "https://api.spotify.com/v1";
const char* kScopes =
    "user-read-playback-state user-modify-playback-state "
    "user-read-currently-playing";
}  // namespace

QString SpotifyWebSource::redirectUri() {
  return QStringLiteral("http://127.0.0.1:%1/callback").arg(kRedirectPort);
}

SpotifyWebSource::SpotifyWebSource(QObject* parent) : MediaSource(parent) {
  QSettings s;
  m_clientId = s.value("Media/spotifyClientId").toString();
  m_refreshToken = s.value("Media/spotifyRefreshToken").toString();
  m_poll.setInterval(3000);
  connect(&m_poll, &QTimer::timeout, this, &SpotifyWebSource::poll);
  connect(&m_server, &QTcpServer::newConnection, this, [this] {
    while (QTcpSocket* sock = m_server.nextPendingConnection())
      connect(sock, &QTcpSocket::readyRead, this,
              [this, sock] { handleCallback(sock); });
  });
  if (isConnected()) {
    m_poll.start();
    QTimer::singleShot(0, this, &SpotifyWebSource::poll);
  }
}

QString SpotifyWebSource::service() const {
  return m_device.isEmpty() ? QStringLiteral("Spotify")
                            : QStringLiteral("Spotify · %1").arg(m_device);
}

void SpotifyWebSource::setClientId(const QString& id) {
  m_clientId = id.trimmed();
  QSettings().setValue("Media/spotifyClientId", m_clientId);
}

QString SpotifyWebSource::makeVerifier() {
  static const char kChars[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
  QString v;
  for (int i = 0; i < 64; ++i)
    v += QChar(
        kChars[QRandomGenerator::system()->bounded(int(sizeof(kChars) - 1))]);
  return v;
}

QString SpotifyWebSource::challengeFor(const QString& verifier) {
  const QByteArray hash =
      QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256);
  return QString::fromLatin1(hash.toBase64(QByteArray::Base64UrlEncoding |
                                           QByteArray::OmitTrailingEquals));
}

QUrl SpotifyWebSource::authorizeUrl(const QString& clientId,
                                    const QString& challenge,
                                    const QString& state) {
  QUrl u(kAuthUrl);
  QUrlQuery q;
  q.addQueryItem("client_id", clientId);
  q.addQueryItem("response_type", "code");
  q.addQueryItem("redirect_uri", redirectUri());
  q.addQueryItem("code_challenge_method", "S256");
  q.addQueryItem("code_challenge", challenge);
  q.addQueryItem("scope", kScopes);
  q.addQueryItem("state", state);
  u.setQuery(q);
  return u;
}

void SpotifyWebSource::connectAccount() {
  if (m_clientId.isEmpty()) {
    emit message(tr("Enter your Spotify client ID first."));
    return;
  }
  if (!m_server.isListening() &&
      !m_server.listen(QHostAddress::LocalHost, kRedirectPort)) {
    emit message(tr("Cannot listen on port %1: %2")
                     .arg(kRedirectPort)
                     .arg(m_server.errorString()));
    return;
  }
  m_verifier = makeVerifier();
  m_stateNonce = makeVerifier().left(16);
  QDesktopServices::openUrl(
      authorizeUrl(m_clientId, challengeFor(m_verifier), m_stateNonce));
  emit message(tr("Continue the Spotify login in your browser."));
}

void SpotifyWebSource::disconnectAccount() {
  m_poll.stop();
  m_accessToken.clear();
  m_refreshToken.clear();
  m_state = NowPlaying();
  QSettings().remove("Media/spotifyRefreshToken");
  emit connectionChanged();
  emit changed();
}

void SpotifyWebSource::handleCallback(QTcpSocket* sock) {
  const QByteArray req = sock->readAll();
  const QList<QByteArray> parts = req.split(' ');
  const QUrl url(QString::fromLatin1(parts.value(1)));
  const QUrlQuery q(url);
  const bool ok = url.path() == "/callback" && q.hasQueryItem("code") &&
                  q.queryItemValue("state") == m_stateNonce;
  const QByteArray body =
      ok ? "<html><body style='font-family:sans-serif;background:#111;"
           "color:#eee;text-align:center;padding-top:20vh'><h2>Spotify "
           "connected</h2><p>You can close this window and return to "
           "OpenCPN.</p></body></html>"
         : "<html><body><h2>Spotify login failed</h2></body></html>";
  sock->write(
      "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: "
      "close\r\nContent-Length: " +
      QByteArray::number(body.size()) + "\r\n\r\n" + body);
  sock->disconnectFromHost();
  sock->deleteLater();
  if (!ok) {
    if (q.hasQueryItem("error"))
      emit message(
          tr("Spotify login failed: %1").arg(q.queryItemValue("error")));
    return;
  }
  m_server.close();
  requestToken({{"grant_type", "authorization_code"},
                {"code", q.queryItemValue("code")},
                {"redirect_uri", redirectUri()},
                {"client_id", m_clientId},
                {"code_verifier", m_verifier}});
}

void SpotifyWebSource::requestToken(
    const QList<QPair<QString, QString>>& params) {
  QUrlQuery form;
  for (const auto& p : params) form.addQueryItem(p.first, p.second);
  QNetworkRequest req{QUrl(kTokenUrl)};
  req.setHeader(QNetworkRequest::ContentTypeHeader,
                "application/x-www-form-urlencoded");
  QNetworkReply* r =
      m_net.post(req, form.toString(QUrl::FullyEncoded).toUtf8());
  connect(r, &QNetworkReply::finished, this, [this, r] {
    r->deleteLater();
    const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
    if (r->error() != QNetworkReply::NoError || !o.contains("access_token")) {
      emit message(
          tr("Spotify authorization failed: %1")
              .arg(o.value("error_description").toString(r->errorString())));
      if (o.value("error").toString() == "invalid_grant") disconnectAccount();
      return;
    }
    m_accessToken = o.value("access_token").toString();
    m_expires = QDateTime::currentDateTimeUtc().addSecs(
        o.value("expires_in").toInt(3600) - 60);
    if (o.contains("refresh_token")) {
      m_refreshToken = o.value("refresh_token").toString();
      QSettings().setValue("Media/spotifyRefreshToken", m_refreshToken);
    }
    if (!m_poll.isActive()) m_poll.start();
    emit connectionChanged();
    poll();
  });
}

QNetworkRequest SpotifyWebSource::apiRequest(const QString& path) const {
  QNetworkRequest req{QUrl(QString(kApi) + path)};
  req.setRawHeader("Authorization", "Bearer " + m_accessToken.toLatin1());
  return req;
}

NowPlaying SpotifyWebSource::parsePlayer(const QJsonObject& o,
                                         QString* device) {
  NowPlaying np;
  np.playing = o.value("is_playing").toBool();
  np.positionMs = qint64(o.value("progress_ms").toDouble());
  const QJsonObject item = o.value("item").toObject();
  np.title = item.value("name").toString();
  np.durationMs = qint64(item.value("duration_ms").toDouble());
  QStringList artists;
  for (const auto& a : item.value("artists").toArray())
    artists << a.toObject().value("name").toString();
  if (artists.isEmpty())  // podcasts
    artists << item.value("show").toObject().value("name").toString();
  np.artist = artists.join(", ");
  const QJsonObject album = item.value("album").toObject();
  np.album = album.value("name").toString();
  const QJsonArray images = album.value("images").isArray()
                                ? album.value("images").toArray()
                                : item.value("images").toArray();
  if (!images.isEmpty())
    np.artUrl = QUrl(images.first().toObject().value("url").toString());
  if (device) *device = o.value("device").toObject().value("name").toString();
  return np;
}

void SpotifyWebSource::poll() {
  if (!isConnected()) return;
  if (m_accessToken.isEmpty() || QDateTime::currentDateTimeUtc() >= m_expires) {
    requestToken({{"grant_type", "refresh_token"},
                  {"refresh_token", m_refreshToken},
                  {"client_id", m_clientId}});
    return;
  }
  QNetworkReply* r = m_net.get(apiRequest("/me/player"));
  connect(r, &QNetworkReply::finished, this, [this, r] {
    r->deleteLater();
    const int status =
        r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 401) {
      m_accessToken.clear();
      return;
    }
    if (status == 204) {  // nothing playing
      m_state = NowPlaying();
      emit changed();
      return;
    }
    if (status != 200) return;
    m_state =
        parsePlayer(QJsonDocument::fromJson(r->readAll()).object(), &m_device);
    emit changed();
  });
}

void SpotifyWebSource::command(const QByteArray& verb, const QString& path) {
  if (m_accessToken.isEmpty()) return;
  QNetworkReply* r =
      m_net.sendCustomRequest(apiRequest(path), verb, QByteArray());
  connect(r, &QNetworkReply::finished, this, [this, r] {
    r->deleteLater();
    const int status =
        r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 403)
      emit message(tr("Spotify playback control requires Premium."));
    else if (status == 404)
      emit message(
          tr("No active Spotify device. Start playback on a "
             "phone, computer or speaker first."));
    QTimer::singleShot(400, this, &SpotifyWebSource::poll);
  });
}

void SpotifyWebSource::playPause() {
  m_state.playing = !m_state.playing;  // optimistic update
  emit changed();
  command("PUT", m_state.playing ? "/me/player/play" : "/me/player/pause");
}

void SpotifyWebSource::next() { command("POST", "/me/player/next"); }
void SpotifyWebSource::previous() { command("POST", "/me/player/previous"); }
