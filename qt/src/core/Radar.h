// Radar: spokes from a Signal K radar stream (Signal K radar API / Mayara:
// protobuf RadarMessage over WebSocket), a polar image of one revolution,
// rendering to a plan position indicator, and a simulator that builds
// echoes from the coastline, AIS targets and sea clutter.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QRgb>
#include <QSet>
#include <QTimer>
#include <QVector>
#include <functional>
#include <memory>
#include <limits>

#include "core/Shapefile.h"
#include "core/SignalK.h"

class WebSocketClient;

struct RadarSpoke {
  int angle = 0;     // 0 .. spokes-1, clockwise from the bow
  int bearing = -1;  // 0 .. spokes-1 from true north (-1: unknown)
  int rangeM = 0;    // range of the last sample
  qint64 timeMs = 0;
  double lat = std::numeric_limits<double>::quiet_NaN();
  double lon = std::numeric_limits<double>::quiet_NaN();
  QByteArray data;  // one byte per sample, from the antenna outwards
};

/** The RadarMessage protobuf of the Signal K radar API (Mayara):
 *  message RadarMessage { uint32 radar = 1; repeated Spoke spokes = 2; }
 *  message Spoke { uint32 angle = 1; optional uint32 bearing = 2;
 *    uint32 range = 3; optional uint64 time = 4; optional double lat = 5;
 *    optional double lon = 6; bytes data = 7; } */
namespace RadarProto {
bool decode(const QByteArray& msg, QVector<RadarSpoke>& spokes,
            int* radar = nullptr);
QByteArray encode(const QVector<RadarSpoke>& spokes, int radar = 0);
}  // namespace RadarProto

/** One revolution of spokes. */
class RadarImage {
public:
  void reset(int spokes, int maxLen);
  int spokes() const { return m_spokes; }
  int maxLen() const { return m_maxLen; }
  /** Range of the newest spoke (m). */
  int rangeM() const { return m_rangeM; }
  /** Heading derived from spokes with bearing (deg), NaN if none. */
  double headingDeg() const { return m_heading; }
  int maxValue() const { return m_maxValue; }
  void setSpoke(const RadarSpoke& s);
  /** Sample at a spoke and sample index. */
  uchar at(int spoke, int sample) const {
    return m_polar[size_t(spoke) * size_t(m_maxLen) + size_t(sample)];
  }
  /** The strongest return in the sector around a bearing relative to the
   *  bow (deg) within [fromM, toM]. */
  int strongest(double relDeg, double halfWidthDeg, double fromM,
                double toM) const;

  struct Style {
    QVector<QRgb> legend;  // from the server; empty: built-in colors
    double gain = 1.0;     // display gain
    int threshold = 1;     // weaker returns are hidden
  };
  /** Plan position indicator of `sizePx` square. `rotationDeg` turns the
   *  bow direction (0 = head up; north up: the heading). Shows up to
   *  `displayRangeM` at the edge. Transparent outside of returns. */
  QImage render(int sizePx, double rotationDeg, double displayRangeM,
                const Style& style) const;
  static QRgb defaultColor(int value, int maxValue);

private:
  int m_spokes = 0, m_maxLen = 0, m_rangeM = 0, m_maxValue = 15;
  double m_heading = std::numeric_limits<double>::quiet_NaN();
  QVector<uchar> m_polar;
  QVector<int> m_spokeRange;
};

/** Echoes for the simulator: own ship, land and targets. */
class RadarSimulator {
public:
  /** Land polygons (Mercator), e.g. the detailed basemap. */
  using Land = std::shared_ptr<const QVector<MercatorPolygon>>;
  struct Target {
    double lat, lon;
    double sizeM = 30;
  };
  int spokes = 2048, maxLen = 512;
  int rangeM = 3704;  // 2 NM
  Land land;
  /** `count` spokes from `firstAngle` for a ship at lat/lon/heading. */
  QVector<RadarSpoke> spokesFor(int firstAngle, int count, double lat,
                                double lon, double headingDeg,
                                const QVector<Target>& targets);

private:
  // Land along each spoke, recomputed when the ship or the range moves.
  void prepareLand(double lat, double lon);
  QImage m_mask;  // local grid around the ship: 255 = land
  double m_maskLat = 999, m_maskLon = 999, m_maskRange = 0;
  quint32 m_seed = 12345;
  int rnd(int n);
};

/** Selects a radar (one of the Signal K hub or the simulator), receives
 *  its spokes and keeps the image. Runs only while someone wants it. */
class RadarService : public QObject {
  Q_OBJECT
public:
  static RadarService& instance();
  struct Source {
    QString id, name;
    bool simulated = false;
  };
  QVector<Source> sources() const;
  QString current() const { return m_current; }
  void select(const QString& id);
  bool simulated() const { return m_current == "sim"; }
  const RadarImage& image() const { return m_image; }
  QVector<QRgb> legend() const { return m_legend; }
  QString status() const { return m_status; }
  bool receiving() const;
  /** Simulator range (the real radar's range is set at the radar). */
  void setSimulatorRange(int meters);
  void setLand(RadarSimulator::Land land);
  /** Viewers register; the stream runs while any wants it. */
  void want(QObject* who, bool on);
  bool active() const { return !m_wanted.isEmpty(); }
  /** Spokes received (tests). */
  int spokeCount() const { return m_spokeCount; }
  void feedMessage(const QByteArray& protobuf);

signals:
  void sourcesChanged();
  void updated();  // new spokes (at most every 100 ms)
  void statusChanged();

private:
  RadarService();
  void restart();
  void stopStream();
  void setStatus(const QString& s);
  void simulate();

  QString m_current;
  RadarImage m_image;
  QVector<QRgb> m_legend;
  QString m_status;
  WebSocketClient* m_ws = nullptr;
  QTimer m_simTimer, m_notify, m_retry;
  RadarSimulator m_sim;
  int m_simAngle = 0;
  bool m_dirty = false;
  int m_spokeCount = 0;
  QDateTime m_lastSpoke;
  QSet<QObject*> m_wanted;
};
