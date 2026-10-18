// A controllable music source (MPRIS player, Spotify Web API, ...).
#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

struct NowPlaying {
  QString title, artist, album;
  QUrl artUrl;
  qint64 positionMs = 0, durationMs = 0;
  bool playing = false;
  bool hasTrack() const { return !title.isEmpty(); }
};

class MediaSource : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;

  /** Stable identifier, e.g. "mpris:org.mpris.MediaPlayer2.spotify". */
  virtual QString id() const = 0;
  /** Service shown to the user: "Spotify", "YouTube Music", "Firefox"… */
  virtual QString service() const = 0;
  virtual NowPlaying state() const = 0;
  virtual bool isAvailable() const { return true; }

  virtual void playPause() = 0;
  virtual void next() = 0;
  virtual void previous() = 0;

signals:
  void changed();
  /** Error/notice for the user (e.g. "Spotify Premium required"). */
  void message(const QString& text);
};
