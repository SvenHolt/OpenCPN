#include "core/Simulator.h"

#include <QDateTime>
#include <QSettings>
#include <cmath>

#include "core/GeoMath.h"
#include "core/NavStore.h"

namespace AisEncoder {
namespace {
class BitWriter {
public:
  void put(quint32 v, int len) {
    for (int i = len - 1; i >= 0; --i) m_bits.push_back((v >> i) & 1);
  }
  void putText(const QString& s, int chars) {
    for (int i = 0; i < chars; ++i) {
      int c = i < s.size() ? s[i].toUpper().unicode() : '@';
      if (c >= 64) c -= 64;
      put(quint32(c & 0x3f), 6);
    }
  }
  QString armor() const {
    QString out;
    for (size_t i = 0; i < m_bits.size(); i += 6) {
      int v = 0;
      for (size_t j = 0; j < 6; ++j)
        v = (v << 1) | (i + j < m_bits.size() ? m_bits[i + j] : 0);
      v += 48;
      if (v > 87) v += 8;
      out.append(QChar(v));
    }
    return out;
  }

private:
  std::vector<quint8> m_bits;
};
}  // namespace

QString positionReport(quint32 mmsi, double lat, double lon, double sog,
                       double cog, int hdg, int navStatus) {
  BitWriter w;
  w.put(1, 6);  // type
  w.put(0, 2);  // repeat
  w.put(mmsi, 30);
  w.put(quint32(navStatus), 4);
  w.put(0x80, 8);  // ROT not available
  w.put(quint32(std::lround(sog * 10)), 10);
  w.put(1, 1);  // accuracy
  w.put(quint32(qint32(std::lround(lon * 600000))) & 0x0FFFFFFF, 28);
  w.put(quint32(qint32(std::lround(lat * 600000))) & 0x07FFFFFF, 27);
  w.put(quint32(std::lround(geo::normalizeDeg(cog) * 10)) % 3600, 12);
  w.put(quint32(hdg), 9);
  w.put(QDateTime::currentDateTimeUtc().time().second(), 6);
  w.put(0, 2 + 3 + 1 + 19);  // maneuver, spare, RAIM, radio
  return w.armor();
}

QString staticReport(quint32 mmsi, const QString& name, const QString& callsign,
                     int shipType) {
  BitWriter w;
  w.put(5, 6);
  w.put(0, 2);
  w.put(mmsi, 30);
  w.put(0, 2);   // AIS version
  w.put(0, 30);  // IMO
  w.putText(callsign, 7);
  w.putText(name, 20);
  w.put(quint32(shipType), 8);
  w.put(0, 9 + 9 + 6 + 6);   // dimensions
  w.put(1, 4);               // EPFD GPS
  w.put(0, 4 + 5 + 5 + 6);   // ETA
  w.put(0, 8);               // draught
  w.putText(QString(), 20);  // destination
  w.put(0, 2);               // DTE + spare
  return w.armor();
}

QStringList sentences(const QString& payload, int seqId, int fillBits) {
  constexpr int kMax = 60;
  const int count = int((payload.size() + kMax - 1) / kMax);
  QStringList out;
  for (int i = 0; i < count; ++i) {
    const QString part = payload.mid(i * kMax, kMax);
    const int f = i == count - 1 ? fillBits : 0;
    out << NmeaParser::makeSentence(
        QStringLiteral("AIVDM,%1,%2,%3,A,%4,%5")
            .arg(count)
            .arg(i + 1)
            .arg(count > 1 ? QString::number(seqId) : QString())
            .arg(part)
            .arg(f),
        '!');
  }
  return out;
}
}  // namespace AisEncoder

SimulatorConnection::SimulatorConnection(const ConnectionConfig& cfg,
                                         QObject* parent)
    : Connection(cfg, parent) {
  m_timer.setInterval(1000);
  connect(&m_timer, &QTimer::timeout, this, &SimulatorConnection::tick);
  QSettings s;
  m_lat = s.value("Simulator/lat", m_lat).toDouble();
  m_lon = s.value("Simulator/lon", m_lon).toDouble();
  m_cog = m_hdg = s.value("Simulator/cog", m_cog).toDouble();
}

void SimulatorConnection::start() {
  spawnTraffic();
  m_timer.start();
  setStatus(Status::Connected, tr("Running"));
  tick();
}

void SimulatorConnection::stop() {
  m_timer.stop();
  QSettings s;
  s.setValue("Simulator/lat", m_lat);
  s.setValue("Simulator/lon", m_lon);
  s.setValue("Simulator/cog", m_cog);
  setStatus(Status::Off);
}

void SimulatorConnection::spawnTraffic() {
  static const char* kNames[] = {"NORDLICHT",   "HELGOLAND EXPRESS",
                                 "MAERSK KIEL", "SEA BREEZE",
                                 "ALBATROS",    "WIND OF CHANGE"};
  static const int kTypes[] = {36, 60, 70, 37, 30, 36};
  auto* rng = QRandomGenerator::global();
  m_vessels.clear();
  for (int i = 0; i < 6; ++i) {
    Vessel v;
    v.mmsi = 211000000 + rng->bounded(999999);
    v.name = kNames[i];
    v.callsign = QStringLiteral("DA%1").arg(1000 + i * 37);
    v.type = kTypes[i];
    const double brg = rng->bounded(360.0);
    const double dist = 0.8 + rng->bounded(4.0);
    geo::destination(m_lat, m_lon, brg, dist, v.lat, v.lon);
    v.sog = 3 + rng->bounded(14.0);
    v.cog = rng->bounded(360.0);
    if (i == 2) {
      // One ship on a near collision course to keep things interesting.
      geo::destination(m_lat, m_lon, m_cog, 3.0, v.lat, v.lon);
      v.cog = geo::normalizeDeg(m_cog + 180 + 4);
      v.sog = 12;
    }
    m_vessels.push_back(v);
  }
}

void SimulatorConnection::tick() {
  ++m_tick;
  const Guidance& g = NavStore::instance().guidance();
  if (g.active && g.btwDeg == g.btwDeg) {
    // Simple autopilot: steer towards the next waypoint, 4°/s turn rate.
    const double d = geo::angleDiff(m_cog, g.btwDeg);
    m_cog = geo::normalizeDeg(m_cog + qBound(-4.0, d, 4.0));
  }
  m_hdg = geo::normalizeDeg(m_cog + 2 * std::sin(m_tick / 7.0));
  geo::destination(m_lat, m_lon, m_cog, m_sog / 3600.0, m_lat, m_lon);

  const QDateTime now = QDateTime::currentDateTimeUtc();
  auto dm = [](double v, int degDigits) {
    const double a = std::fabs(v);
    const int deg = int(a);
    return QStringLiteral("%1%2")
        .arg(deg, degDigits, 10, QChar('0'))
        .arg((a - deg) * 60.0, 7, 'f', 4, QChar('0'));
  };
  QByteArray out;
  out += NmeaParser::makeSentence(
             QStringLiteral("GPRMC,%1,A,%2,%3,%4,%5,%6,%7,%8,,,A")
                 .arg(now.toString("hhmmss.zzz").left(9))
                 .arg(dm(m_lat, 2))
                 .arg(m_lat >= 0 ? "N" : "S")
                 .arg(dm(m_lon, 3))
                 .arg(m_lon >= 0 ? "E" : "W")
                 .arg(m_sog, 0, 'f', 1)
                 .arg(m_cog, 0, 'f', 1)
                 .arg(now.toString("ddMMyy")))
             .toLatin1() +
         "\r\n";
  out += NmeaParser::makeSentence(
             QStringLiteral("HEHDT,%1,T").arg(m_hdg, 0, 'f', 1))
             .toLatin1() +
         "\r\n";
  out += NmeaParser::makeSentence(
             QStringLiteral("GPGGA,%1,%2,%3,%4,%5,1,09,0.9,2.0,M,,,,")
                 .arg(now.toString("hhmmss"))
                 .arg(dm(m_lat, 2))
                 .arg(m_lat >= 0 ? "N" : "S")
                 .arg(dm(m_lon, 3))
                 .arg(m_lon >= 0 ? "E" : "W"))
             .toLatin1() +
         "\r\n";

  int seq = 0;
  for (Vessel& v : m_vessels) {
    geo::destination(v.lat, v.lon, v.cog, v.sog / 3600.0, v.lat, v.lon);
    if (m_tick % 3 == 0)
      for (const auto& s : AisEncoder::sentences(AisEncoder::positionReport(
               v.mmsi, v.lat, v.lon, v.sog, v.cog, int(v.cog) % 360)))
        out += s.toLatin1() + "\r\n";
    if (m_tick % 30 == 1)
      for (const auto& s : AisEncoder::sentences(
               AisEncoder::staticReport(v.mmsi, v.name, v.callsign, v.type),
               seq++ % 10, 2))
        out += s.toLatin1() + "\r\n";
  }
  // Respawn traffic that drifted too far away.
  for (Vessel& v : m_vessels)
    if (geo::distanceNm(m_lat, m_lon, v.lat, v.lon) > 8) {
      geo::destination(m_lat, m_lon, QRandomGenerator::global()->bounded(360.0),
                       5, v.lat, v.lon);
      v.cog = geo::bearingDeg(v.lat, v.lon, m_lat, m_lon) +
              QRandomGenerator::global()->bounded(60.0) - 30;
    }
  countLines(out);
  emit dataReceived(out);
}
