// Records the own ship's track and manages saved tracks.
#pragma once

#include <QObject>
#include <QTimer>
#include <QVector>

#include "core/NavStore.h"

class TrackRecorder : public QObject {
  Q_OBJECT
public:
  static TrackRecorder& instance();

  bool isRecording() const { return m_recording; }
  void start();
  void stop();

  /** The track currently being recorded (empty when not recording). */
  const Route& activeTrack() const { return m_active; }
  const QVector<Route>& tracks() const { return m_tracks; }
  const Route* track(const QString& id) const;
  void removeTrack(const QString& id);
  void setTrackVisible(const QString& id, bool visible);
  void addTrack(Route track);

  /** Duration of a track in seconds (first to last point). */
  static qint64 durationSecs(const Route& t);

  /** Adds a fix; public for tests. Returns true if a point was stored. */
  bool addFix(double lat, double lon, double cog, const QDateTime& time);

  QString storagePath() const;
  void loadFromDisk();
  void saveToDisk() const;

  // Thinning parameters
  static constexpr double kMinDistanceNm = 0.008;  // ~15 m
  static constexpr int kMinIntervalSecs = 5;
  static constexpr int kMaxIntervalSecs = 60;

signals:
  void recordingChanged(bool on);
  void activeTrackChanged();
  void tracksChanged();

private:
  TrackRecorder();
  void onOwnShip();

  bool m_recording = false;
  Route m_active;
  QVector<Route> m_tracks;
  double m_lastCog = kNaN;
  QTimer m_autosave;
};
