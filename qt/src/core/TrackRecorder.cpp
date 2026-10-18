#include "core/TrackRecorder.h"

#include <QDir>
#include <QStandardPaths>
#include <cmath>

#include "core/AppSettings.h"
#include "core/GeoMath.h"
#include "core/GpxIO.h"

TrackRecorder& TrackRecorder::instance() {
  static TrackRecorder r;
  return r;
}

TrackRecorder::TrackRecorder() {
  connect(&NavStore::instance(), &NavStore::ownShipChanged, this,
          &TrackRecorder::onOwnShip);
  m_autosave.setInterval(60 * 1000);
  connect(&m_autosave, &QTimer::timeout, this, [this] { saveToDisk(); });
}

const Route* TrackRecorder::track(const QString& id) const {
  for (const auto& t : m_tracks)
    if (t.id == id) return &t;
  return nullptr;
}

void TrackRecorder::start() {
  if (m_recording) return;
  m_recording = true;
  m_active = Route();
  m_active.id = NavStore::newId();
  m_active.name =
      tr("Track %1")
          .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"));
  m_lastCog = kNaN;
  m_autosave.start();
  emit recordingChanged(true);
  emit activeTrackChanged();
}

void TrackRecorder::stop() {
  if (!m_recording) return;
  m_recording = false;
  m_autosave.stop();
  if (m_active.points.size() >= 2) m_tracks.push_back(m_active);
  m_active = Route();
  saveToDisk();
  emit recordingChanged(false);
  emit activeTrackChanged();
  emit tracksChanged();
}

void TrackRecorder::removeTrack(const QString& id) {
  m_tracks.erase(std::remove_if(m_tracks.begin(), m_tracks.end(),
                                [&](const Route& t) { return t.id == id; }),
                 m_tracks.end());
  saveToDisk();
  emit tracksChanged();
}

void TrackRecorder::setTrackVisible(const QString& id, bool visible) {
  for (auto& t : m_tracks)
    if (t.id == id) t.visible = visible;
  saveToDisk();
  emit tracksChanged();
}

void TrackRecorder::addTrack(Route track) {
  if (track.id.isEmpty()) track.id = NavStore::newId();
  m_tracks.push_back(track);
  saveToDisk();
  emit tracksChanged();
}

qint64 TrackRecorder::durationSecs(const Route& t) {
  if (t.points.size() < 2) return 0;
  const QDateTime a = t.points.first().time, b = t.points.last().time;
  return a.isValid() && b.isValid() ? a.secsTo(b) : 0;
}

void TrackRecorder::onOwnShip() {
  if (!m_recording) return;
  const OwnShip& s = NavStore::instance().ownShip();
  if (!s.valid) return;
  addFix(s.lat, s.lon, s.cog, QDateTime::currentDateTimeUtc());
}

bool TrackRecorder::addFix(double lat, double lon, double cog,
                           const QDateTime& time) {
  if (!m_recording) return false;
  if (!m_active.points.isEmpty()) {
    const Waypoint& last = m_active.points.last();
    const qint64 dt = last.time.secsTo(time);
    const double d = geo::distanceNm(last.lat, last.lon, lat, lon);
    const bool turned = !std::isnan(cog) && !std::isnan(m_lastCog) &&
                        std::fabs(geo::angleDiff(m_lastCog, cog)) > 15;
    // Thin the track: keep points on turns, after enough distance or after
    // the maximum interval while moving.
    const bool keep =
        dt >= kMinIntervalSecs &&
        (d >= kMinDistanceNm * 4 || (turned && d >= kMinDistanceNm) ||
         (dt >= kMaxIntervalSecs && d >= kMinDistanceNm));
    if (!keep) return false;
  }
  Waypoint p;
  p.lat = lat;
  p.lon = lon;
  p.time = time;
  m_active.points.push_back(p);
  m_lastCog = cog;
  emit activeTrackChanged();
  return true;
}

QString TrackRecorder::storagePath() const {
  const QString dir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dir);
  return dir + "/tracks.gpx";
}

void TrackRecorder::loadFromDisk() {
  GpxData d;
  if (!GpxIO::read(storagePath(), d)) return;
  m_tracks = d.tracks;
  emit tracksChanged();
}

void TrackRecorder::saveToDisk() const {
  GpxData d;
  d.tracks = m_tracks;
  if (m_recording && m_active.points.size() >= 2) d.tracks << m_active;
  GpxIO::write(storagePath(), d);
}
