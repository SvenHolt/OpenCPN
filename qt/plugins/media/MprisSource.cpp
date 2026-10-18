#include "MprisSource.h"

#ifdef OCPN_MEDIA_HAVE_DBUS
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#endif

namespace {
const char* kPath = "/org/mpris/MediaPlayer2";
const char* kPlayer = "org.mpris.MediaPlayer2.Player";
const char* kRoot = "org.mpris.MediaPlayer2";
const char* kProps = "org.freedesktop.DBus.Properties";

#ifdef OCPN_MEDIA_HAVE_DBUS
/** Unwraps QDBusArgument/QDBusVariant values into plain QVariants. */
QVariant plain(const QVariant& v) {
  if (v.canConvert<QDBusVariant>())
    return plain(v.value<QDBusVariant>().variant());
  if (v.canConvert<QDBusArgument>()) {
    const QDBusArgument arg = v.value<QDBusArgument>();
    if (arg.currentType() == QDBusArgument::MapType) {
      QVariantMap m;
      arg >> m;
      for (auto it = m.begin(); it != m.end(); ++it) it.value() = plain(*it);
      return m;
    }
    if (arg.currentType() == QDBusArgument::ArrayType) {
      QStringList l;
      arg >> l;
      return l;
    }
  }
  return v;
}
#endif
}  // namespace

QString MprisSource::serviceName(const QString& identity, const QString& bus,
                                 const QString& url) {
  const QString all = (identity + ' ' + bus + ' ' + url).toLower();
  if (all.contains("music.youtube.com") || all.contains("youtube music") ||
      all.contains("youtube-music") || all.contains("youtubemusic"))
    return QStringLiteral("YouTube Music");
  if (all.contains("spotify")) return QStringLiteral("Spotify");
  if (!identity.isEmpty()) return identity;
  return bus.section('.', 3, 3);
}

void MprisSource::applyMetadata(const QVariantMap& md, NowPlaying& np,
                                QString* url) {
  np.title = md.value("xesam:title").toString();
  const QVariant artist = md.value("xesam:artist");
  np.artist = artist.typeId() == QMetaType::QStringList
                  ? artist.toStringList().join(", ")
                  : artist.toString();
  np.album = md.value("xesam:album").toString();
  np.artUrl = QUrl(md.value("mpris:artUrl").toString());
  np.durationMs = md.value("mpris:length").toLongLong() / 1000;
  if (url) *url = md.value("xesam:url").toString();
}

MprisSource::MprisSource(const QString& busName, QObject* parent)
    : MediaSource(parent), m_bus(busName) {
#ifdef OCPN_MEDIA_HAVE_DBUS
  QDBusConnection::sessionBus().connect(
      m_bus, kPath, kProps, "PropertiesChanged", this,
      SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
  // The position is not signalled; poll it now and then and extrapolate.
  m_poll.setInterval(5000);
  connect(&m_poll, &QTimer::timeout, this, &MprisSource::refresh);
  m_poll.start();
  // Identity (application name)
  QDBusMessage get =
      QDBusMessage::createMethodCall(m_bus, kPath, kProps, "Get");
  get << QString(kRoot) << QString("Identity");
  auto* w = new QDBusPendingCallWatcher(
      QDBusConnection::sessionBus().asyncCall(get), this);
  connect(w, &QDBusPendingCallWatcher::finished, this,
          [this](QDBusPendingCallWatcher* call) {
            QDBusPendingReply<QDBusVariant> r = *call;
            if (r.isValid()) m_identity = r.value().variant().toString();
            call->deleteLater();
            emit changed();
          });
  refresh();
#endif
}

QString MprisSource::service() const {
  return serviceName(m_identity, m_bus, m_url);
}

NowPlaying MprisSource::state() const {
  NowPlaying s = m_state;
  if (s.playing && m_positionClock.isValid()) {
    s.positionMs += m_positionClock.elapsed();
    if (s.durationMs > 0) s.positionMs = std::min(s.positionMs, s.durationMs);
  }
  return s;
}

void MprisSource::call(const QString& method) {
#ifdef OCPN_MEDIA_HAVE_DBUS
  QDBusConnection::sessionBus().asyncCall(
      QDBusMessage::createMethodCall(m_bus, kPath, kPlayer, method));
  QTimer::singleShot(300, this, &MprisSource::refresh);
#else
  Q_UNUSED(method);
#endif
}

void MprisSource::refresh() {
#ifdef OCPN_MEDIA_HAVE_DBUS
  QDBusMessage getAll =
      QDBusMessage::createMethodCall(m_bus, kPath, kProps, "GetAll");
  getAll << QString(kPlayer);
  auto* w = new QDBusPendingCallWatcher(
      QDBusConnection::sessionBus().asyncCall(getAll), this);
  connect(w, &QDBusPendingCallWatcher::finished, this,
          [this](QDBusPendingCallWatcher* call) {
            QDBusPendingReply<QVariantMap> r = *call;
            if (r.isValid()) applyPlayer(r.value());
            call->deleteLater();
          });
#endif
}

void MprisSource::applyPlayer(const QVariantMap& raw) {
#ifdef OCPN_MEDIA_HAVE_DBUS
  QVariantMap props;
  for (auto it = raw.begin(); it != raw.end(); ++it)
    props.insert(it.key(), plain(it.value()));
  if (props.contains("Metadata"))
    applyMetadata(props.value("Metadata").toMap(), m_state, &m_url);
  if (props.contains("PlaybackStatus"))
    m_state.playing = props.value("PlaybackStatus").toString() == "Playing";
  if (props.contains("Position")) {
    m_state.positionMs = props.value("Position").toLongLong() / 1000;
    m_positionClock.restart();
  } else if (props.contains("Metadata")) {
    m_state.positionMs = 0;  // new track
    m_positionClock.restart();
  }
  emit changed();
#else
  Q_UNUSED(raw);
#endif
}

void MprisSource::onPropertiesChanged(const QString& iface,
                                      const QVariantMap& changed,
                                      const QStringList&) {
  if (iface != kPlayer) return;
  // Freeze the extrapolated position before applying a state change.
  if (m_state.playing && m_positionClock.isValid())
    m_state.positionMs += m_positionClock.restart();
  applyPlayer(changed);
}

// --------------------------------------------------------------------------

MprisWatcher::MprisWatcher(QObject* parent) : QObject(parent) {
#ifdef OCPN_MEDIA_HAVE_DBUS
  QDBusConnection bus = QDBusConnection::sessionBus();
  if (!bus.isConnected()) return;
  m_available = true;
  bus.connect("org.freedesktop.DBus", "/org/freedesktop/DBus",
              "org.freedesktop.DBus", "NameOwnerChanged", this,
              SLOT(onNameOwnerChanged(QString, QString, QString)));
  QTimer::singleShot(0, this, [this] {
    const QStringList names =
        QDBusConnection::sessionBus().interface()->registeredServiceNames();
    for (const QString& n : names)
      if (n.startsWith("org.mpris.MediaPlayer2."))
        emit playerAdded(new MprisSource(n));
  });
#endif
}

void MprisWatcher::onNameOwnerChanged(const QString& name,
                                      const QString& oldOwner,
                                      const QString& newOwner) {
  if (!name.startsWith("org.mpris.MediaPlayer2.")) return;
  if (!oldOwner.isEmpty()) emit playerRemoved(name);
  if (!newOwner.isEmpty()) emit playerAdded(new MprisSource(name));
}
