// Aggregates media sources and selects the one shown on the player card.
#pragma once

#include <QHash>
#include <QImage>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QVector>

#include "MediaSource.h"

class MprisWatcher;
class SpotifyWebSource;

class MediaController : public QObject {
  Q_OBJECT
public:
  explicit MediaController(QObject* parent = nullptr);

  QVector<MediaSource*> sources() const;
  MediaSource* active() const;
  /** Pins a source (empty id: automatic, prefers whatever plays). */
  void setPreferred(const QString& id);
  QString preferred() const { return m_preferred; }

  SpotifyWebSource* spotifyWeb() const { return m_spotify; }
  bool mprisAvailable() const;
  /** Album art of the active track (null while loading / unavailable). */
  QImage artwork() const { return m_art; }

  void addSource(MediaSource* s);

signals:
  void changed();
  void message(const QString& text);

private:
  void updateArtwork();

  QVector<QPointer<MediaSource>> m_sources;
  QString m_preferred;
  QString m_lastActive;
  MprisWatcher* m_mpris = nullptr;
  SpotifyWebSource* m_spotify = nullptr;
  QNetworkAccessManager m_net;
  QUrl m_artUrl;
  QImage m_art;
  QHash<QUrl, QImage> m_artCache;
};
