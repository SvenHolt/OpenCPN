#include "MediaController.h"

#include <QNetworkReply>
#include <QSettings>

#include "MprisSource.h"
#include "SpotifyWebSource.h"

MediaController::MediaController(QObject* parent) : QObject(parent) {
  m_preferred = QSettings().value("Media/preferredSource").toString();
  m_spotify = new SpotifyWebSource(this);
  addSource(m_spotify);
  connect(m_spotify, &SpotifyWebSource::connectionChanged, this,
          &MediaController::changed);
  m_mpris = new MprisWatcher(this);
  connect(m_mpris, &MprisWatcher::playerAdded, this,
          [this](MprisSource* s) { addSource(s); });
  connect(m_mpris, &MprisWatcher::playerRemoved, this,
          [this](const QString& bus) {
            for (auto& s : m_sources)
              if (auto* m = qobject_cast<MprisSource*>(s.data());
                  m && m->busName() == bus)
                m->deleteLater();
            QMetaObject::invokeMethod(this, &MediaController::changed,
                                      Qt::QueuedConnection);
          });
}

bool MediaController::mprisAvailable() const {
  return m_mpris && m_mpris->isAvailable();
}

void MediaController::addSource(MediaSource* s) {
  s->setParent(this);
  m_sources << s;
  connect(s, &MediaSource::changed, this, [this] {
    updateArtwork();
    emit changed();
  });
  connect(s, &MediaSource::message, this, &MediaController::message);
  connect(s, &QObject::destroyed, this, [this] {
    m_sources.removeAll(nullptr);
    emit changed();
  });
  emit changed();
}

QVector<MediaSource*> MediaController::sources() const {
  QVector<MediaSource*> out;
  for (const auto& s : m_sources)
    if (s && s->isAvailable()) out << s;
  return out;
}

MediaSource* MediaController::active() const {
  const auto list = sources();
  for (MediaSource* s : list)
    if (s->id() == m_preferred) return s;
  // Automatic: something playing, else the last active, else any track.
  for (MediaSource* s : list)
    if (s->state().playing) {
      const_cast<MediaController*>(this)->m_lastActive = s->id();
      return s;
    }
  for (MediaSource* s : list)
    if (s->id() == m_lastActive) return s;
  for (MediaSource* s : list)
    if (s->state().hasTrack()) return s;
  return list.isEmpty() ? nullptr : list.first();
}

void MediaController::setPreferred(const QString& id) {
  m_preferred = id;
  QSettings().setValue("Media/preferredSource", id);
  updateArtwork();
  emit changed();
}

void MediaController::updateArtwork() {
  MediaSource* s = active();
  const QUrl url = s ? s->state().artUrl : QUrl();
  if (url == m_artUrl) return;
  m_artUrl = url;
  m_art = m_artCache.value(url);
  if (!m_art.isNull() || url.isEmpty()) return;
  if (url.isLocalFile()) {
    m_art.load(url.toLocalFile());
    m_artCache.insert(url, m_art);
    return;
  }
  QNetworkReply* r = m_net.get(QNetworkRequest(url));
  connect(r, &QNetworkReply::finished, this, [this, r, url] {
    r->deleteLater();
    QImage img;
    img.loadFromData(r->readAll());
    if (img.isNull()) return;
    img = img.scaled(256, 256, Qt::KeepAspectRatioByExpanding,
                     Qt::SmoothTransformation);
    if (m_artCache.size() > 50) m_artCache.clear();
    m_artCache.insert(url, img);
    if (url == m_artUrl) {
      m_art = img;
      emit changed();
    }
  });
}
