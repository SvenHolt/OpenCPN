// Media plugin tests: MPRIS discovery and control against a fake player on a
// private session bus (run through dbus-run-session), Spotify PKCE and
// Web API parsing.
#include <QtTest>

#include "MediaController.h"
#include "MprisSource.h"
#include "SpotifyWebSource.h"

#ifdef OCPN_MEDIA_HAVE_DBUS
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>

class FakeRoot : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
  Q_PROPERTY(QString Identity READ identity)
public:
  explicit FakeRoot(QObject* parent) : QDBusAbstractAdaptor(parent) {}
  QString identity() const { return "Spotify"; }
};

class FakePlayer : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
  Q_PROPERTY(QString PlaybackStatus READ status)
  Q_PROPERTY(QVariantMap Metadata READ metadata)
  Q_PROPERTY(qlonglong Position READ position)
public:
  explicit FakePlayer(QObject* parent) : QDBusAbstractAdaptor(parent) {}
  QString status() const { return playing ? "Playing" : "Paused"; }
  QVariantMap metadata() const {
    return {{"xesam:title", title},
            {"xesam:artist", QStringList{"Sea Shanty Band"}},
            {"xesam:album", "Harbour Songs"},
            {"mpris:length", qlonglong(200'000'000)},
            {"mpris:artUrl", "https://i.scdn.co/image/abc"}};
  }
  qlonglong position() const { return 42'000'000; }

  bool playing = true;
  QString title = "Wellerman";
  int nextCalls = 0;

  void notify() {
    QDBusMessage sig = QDBusMessage::createSignal(
        "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties",
        "PropertiesChanged");
    sig << QString("org.mpris.MediaPlayer2.Player")
        << QVariantMap{{"PlaybackStatus", status()}, {"Metadata", metadata()}}
        << QStringList();
    QDBusConnection::sessionBus().send(sig);
  }

public slots:
  void PlayPause() {
    playing = !playing;
    notify();
  }
  void Next() {
    ++nextCalls;
    title = "Drunken Sailor";
    notify();
  }
  void Previous() {}
};
#endif

class MediaTest : public QObject {
  Q_OBJECT
private slots:
  void pkceChallenge() {
    // RFC 7636, appendix B
    QCOMPARE(SpotifyWebSource::challengeFor(
                 "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"),
             QString("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
    const QString v = SpotifyWebSource::makeVerifier();
    QCOMPARE(v.size(), 64);
    const QUrl u = SpotifyWebSource::authorizeUrl("cid", "chal", "st");
    const QUrlQuery q(u);
    QCOMPARE(q.queryItemValue("code_challenge_method"), QString("S256"));
    QCOMPARE(q.queryItemValue("redirect_uri", QUrl::FullyDecoded),
             QString("http://127.0.0.1:8898/callback"));
  }

  void spotifyPlayerJson() {
    const QByteArray json = R"({
      "device": {"name": "Cockpit speaker"},
      "is_playing": true, "progress_ms": 61000,
      "item": {"name": "Sailing", "duration_ms": 245000,
               "artists": [{"name": "Rod Stewart"}],
               "album": {"name": "Atlantic Crossing",
                         "images": [{"url": "https://i.scdn.co/x"}]}}})";
    QString device;
    const NowPlaying np = SpotifyWebSource::parsePlayer(
        QJsonDocument::fromJson(json).object(), &device);
    QCOMPARE(np.title, QString("Sailing"));
    QCOMPARE(np.artist, QString("Rod Stewart"));
    QCOMPARE(np.positionMs, 61000);
    QCOMPARE(np.durationMs, 245000);
    QVERIFY(np.playing);
    QCOMPARE(np.artUrl, QUrl("https://i.scdn.co/x"));
    QCOMPARE(device, QString("Cockpit speaker"));
  }

  void serviceNames() {
    QCOMPARE(MprisSource::serviceName("Chromium",
                                      "org.mpris.MediaPlayer2."
                                      "chromium.instance1",
                                      "https://music.youtube.com/watch?v=x"),
             QString("YouTube Music"));
    QCOMPARE(MprisSource::serviceName(
                 "YouTube Music", "org.mpris.MediaPlayer2.youtubemusic", ""),
             QString("YouTube Music"));
    QCOMPARE(MprisSource::serviceName("", "org.mpris.MediaPlayer2.spotify", ""),
             QString("Spotify"));
  }

  void mprisPlayer() {
#ifndef OCPN_MEDIA_HAVE_DBUS
    QSKIP("built without D-Bus");
#else
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) QSKIP("no D-Bus session bus");
    QStandardPaths::setTestModeEnabled(true);
    QObject playerObj;
    new FakeRoot(&playerObj);
    auto* player = new FakePlayer(&playerObj);
    QVERIFY(bus.registerObject("/org/mpris/MediaPlayer2", &playerObj));

    MediaController ctl;
    // Register the name after the controller started watching, as a
    // player started later would.
    QVERIFY(bus.registerService("org.mpris.MediaPlayer2.spotify"));
    QTRY_VERIFY(ctl.active() && ctl.active()->state().hasTrack());
    MediaSource* s = ctl.active();
    QTRY_COMPARE(s->service(), QString("Spotify"));
    QCOMPARE(s->state().title, QString("Wellerman"));
    QCOMPARE(s->state().artist, QString("Sea Shanty Band"));
    QCOMPARE(s->state().durationMs, 200000);
    QVERIFY(s->state().positionMs >= 42000);
    QVERIFY(s->state().playing);

    s->next();
    QTRY_COMPARE(player->nextCalls, 1);
    QTRY_COMPARE(s->state().title, QString("Drunken Sailor"));
    s->playPause();
    QTRY_VERIFY(!s->state().playing);

    bus.unregisterService("org.mpris.MediaPlayer2.spotify");
    QTRY_VERIFY(ctl.sources().size() == 0);
#endif
  }
};

QTEST_MAIN(MediaTest)
#include "test_media.moc"
