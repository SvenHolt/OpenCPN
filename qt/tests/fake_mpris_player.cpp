// Development helper: a fake MPRIS player on the session bus, e.g. to try
// the media plugin without Spotify:
//   fake_mpris_player "Title" "Artist" /path/cover.png [busname] [identity]
#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QUrl>

class Root : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
  Q_PROPERTY(QString Identity READ identity)
public:
  explicit Root(QObject* p, QString id) : QDBusAbstractAdaptor(p), m_id(id) {}
  QString identity() const { return m_id; }

private:
  QString m_id;
};

class Player : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
  Q_PROPERTY(QString PlaybackStatus READ status)
  Q_PROPERTY(QVariantMap Metadata READ metadata)
  Q_PROPERTY(qlonglong Position READ position)
public:
  explicit Player(QObject* p) : QDBusAbstractAdaptor(p) {}
  QString status() const { return playing ? "Playing" : "Paused"; }
  QVariantMap metadata() const {
    return {{"xesam:title", title},
            {"xesam:artist", QStringList{artist}},
            {"mpris:length", qlonglong(214'000'000)},
            {"mpris:artUrl", art}};
  }
  qlonglong position() const { return 83'000'000; }
  QString title, artist, art;
  bool playing = true;
public slots:
  void PlayPause() { playing = !playing; }
  void Next() {}
  void Previous() {}
};

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const QStringList a = app.arguments();
  QObject obj;
  new Root(&obj, a.value(5, "Spotify"));
  auto* p = new Player(&obj);
  p->title = a.value(1, "Sailing");
  p->artist = a.value(2, "Rod Stewart");
  p->art = a.size() > 3 ? QUrl::fromLocalFile(a[3]).toString() : QString();
  auto bus = QDBusConnection::sessionBus();
  bus.registerObject("/org/mpris/MediaPlayer2", &obj);
  bus.registerService(a.value(4, "org.mpris.MediaPlayer2.spotify"));
  return app.exec();
}

#include "fake_mpris_player.moc"
