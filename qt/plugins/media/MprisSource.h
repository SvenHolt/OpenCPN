// Linux desktop media players via MPRIS 2 on the D-Bus session bus. This
// covers the Spotify desktop client, the YouTube Music desktop app and
// music.youtube.com / open.spotify.com in browsers.
#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QVariantMap>

#include "MediaSource.h"

class MprisSource : public MediaSource {
  Q_OBJECT
public:
  explicit MprisSource(const QString& busName, QObject* parent = nullptr);

  QString id() const override { return "mpris:" + m_bus; }
  QString service() const override;
  NowPlaying state() const override;

  void playPause() override { call("PlayPause"); }
  void next() override { call("Next"); }
  void previous() override { call("Previous"); }

  QString busName() const { return m_bus; }
  /** Maps MPRIS metadata to NowPlaying (public for tests). */
  static void applyMetadata(const QVariantMap& md, NowPlaying& np,
                            QString* url = nullptr);
  /** Service name from MPRIS identity / bus name / track URL. */
  static QString serviceName(const QString& identity, const QString& bus,
                             const QString& url);

private slots:
  void onPropertiesChanged(const QString& iface, const QVariantMap& changed,
                           const QStringList& invalidated);

private:
  void call(const QString& method);
  void refresh();
  void applyPlayer(const QVariantMap& props);

  QString m_bus, m_identity, m_url;
  NowPlaying m_state;
  QElapsedTimer m_positionClock;
  QTimer m_poll;
};

/** Tracks MPRIS players appearing and disappearing on the bus. */
class MprisWatcher : public QObject {
  Q_OBJECT
public:
  explicit MprisWatcher(QObject* parent = nullptr);
  bool isAvailable() const { return m_available; }

signals:
  void playerAdded(MprisSource* source);
  void playerRemoved(const QString& busName);

private slots:
  void onNameOwnerChanged(const QString& name, const QString& oldOwner,
                          const QString& newOwner);

private:
  bool m_available = false;
};
