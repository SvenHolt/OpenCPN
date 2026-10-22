#include "core/Simulator.h"

#include <QDateTime>
#include <QSettings>
#include <cmath>

#include "core/BoatConfig.h"
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
  setStatus(Status::Connected, tr("Simulating"));
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

void SimulatorConnection::simulateBoat(QByteArray& out) {
  const SailConfig& sails = BoatConfig::instance().sails();
  const BoatConfig& boat = BoatConfig::instance();
  auto* rng = QRandomGenerator::global();
  // Wind: slow veering plus gusts.
  m_twd = geo::normalizeDeg(240 + 12 * std::sin(m_tick / 300.0) +
                            rng->bounded(4.0) - 2);
  m_tws = 14 + 3 * std::sin(m_tick / 45.0) + rng->bounded(2.0) - 1;

  double twa = geo::angleDiff(m_hdg, m_twd);  // wind from, relative to bow
  // A sailing-only boat cannot point higher than ~40° to the true wind.
  if (!sails.motoring() && std::fabs(twa) < 40) {
    m_cog = geo::normalizeDeg(m_twd - (twa >= 0 ? 40 : -40));
    twa = twa >= 0 ? 40 : -40;
  }

  // Very simple polar: fraction of TWS by true wind angle, hull speed cap.
  const double a = std::fabs(twa);
  double polar = a < 35    ? 0.05
                 : a < 60  ? 0.30 + (a - 35) / 25 * 0.20
                 : a < 150 ? 0.50 + (a - 60) / 90 * 0.08
                           : 0.52;
  double area = sails.main == SailConfig::Main::Down &&
                        sails.head == SailConfig::Head::None
                    ? 0.0
                    : std::min(1.0, std::sqrt(sails.areaFactor()));
  if (sails.downwind != SailConfig::Downwind::None && a > 70) area *= 1.2;
  const double sailSpeed = std::min(7.6, m_tws * polar * area);

  // Engine
  const double targetRpm =
      sails.propulsion == SailConfig::Propulsion::Motor ? boat.cruiseRpm
      : sails.propulsion == SailConfig::Propulsion::MotorSailing
          ? boat.cruiseRpm * 0.8
          : 0.0;
  m_rpm += (targetRpm - m_rpm) * 0.25;
  if (m_rpm < 50) m_rpm = 0;
  const double motorSpeed = 6.3 * std::min(1.2, m_rpm / boat.cruiseRpm);
  const double target =
      std::max(sailSpeed, motorSpeed) + (motorSpeed > 0 ? sailSpeed * 0.3 : 0);
  m_sog += (std::min(target, 8.2) - m_sog) * 0.08;
  const double running = m_rpm > 100;
  m_coolant += ((running ? 82.0 : 20.0) - m_coolant) * (running ? 0.02 : 0.004);
  if (running) {
    const double lph =
        boat.consumptionLph * std::pow(m_rpm / boat.cruiseRpm, 2.5);
    m_fuelPct = std::max(0.0, m_fuelPct - lph / 3600.0 / boat.tankLiters * 100);
    m_engineHours += 1 / 3600.0;
  }

  // Apparent wind = true wind + headwind from the boat's own speed.
  const double tr = twa * M_PI / 180;
  const double ax = m_tws * std::cos(tr) + m_sog, ay = m_tws * std::sin(tr);
  const double aws = std::hypot(ax, ay);
  const double awa = std::atan2(ay, ax) * 180 / M_PI;
  auto line = [&out](const QString& body) {
    out += NmeaParser::makeSentence(body).toLatin1() + "\r\n";
  };
  line(QStringLiteral("WIMWV,%1,R,%2,N,A")
           .arg(geo::normalizeDeg(awa), 0, 'f', 1)
           .arg(aws, 0, 'f', 1));
  line(QStringLiteral("WIMWV,%1,T,%2,N,A")
           .arg(geo::normalizeDeg(twa), 0, 'f', 1)
           .arg(m_tws, 0, 'f', 1));
  line(QStringLiteral("VWVHW,,T,,M,%1,N,%2,K")
           .arg(m_sog * 0.97, 0, 'f', 1)
           .arg(m_sog * 0.97 * 1.852, 0, 'f', 1));
  // Heel: sail force grows with the apparent wind across the boat; wind
  // from starboard heels her to port (negative).
  const double heel =
      std::min(28.0, 0.09 * aws * aws * std::abs(std::sin(awa * M_PI / 180)) *
                         sails.areaFactor()) *
          (awa > 0 ? -1 : 1) +
      (rng->generateDouble() - 0.5) * 1.5;
  line(QStringLiteral("IIXDR,A,%1,D,ROLL").arg(heel, 0, 'f', 1));
  line(QStringLiteral("SDDPT,%1,0.5")
           .arg(14 + 6 * std::sin(m_tick / 60.0), 0, 'f', 1));
  line(QStringLiteral("ERRPM,E,1,%1,,A").arg(m_rpm, 0, 'f', 0));
  line(QStringLiteral("ERXDR,C,%1,C,ENGINE#0,P,%2,B,ENGOIL#0,V,%3,P,FUEL#0,"
                      "U,%4,V,BATTERY#0,G,%5,H,ENGINEHOURS#0")
           .arg(m_coolant, 0, 'f', 1)
           .arg(running ? 3.4 + rng->bounded(0.2) : 0.0, 0, 'f', 1)
           .arg(m_fuelPct, 0, 'f', 1)
           .arg(running ? 14.1 : 12.7, 0, 'f', 1)
           .arg(m_engineHours, 0, 'f', 1));
}

void SimulatorConnection::tick() {
  ++m_tick;
  const Guidance& g = NavStore::instance().guidance();
  if (g.active && g.btwDeg == g.btwDeg) {
    // Simple autopilot: steer towards the next waypoint, 4°/s turn rate.
    const double d = geo::angleDiff(m_cog, g.btwDeg);
    m_cog = geo::normalizeDeg(m_cog + qBound(-4.0, d, 4.0));
  }
  QByteArray boat;
  simulateBoat(boat);
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

  out += boat;
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
