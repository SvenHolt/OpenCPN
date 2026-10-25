#include "core/Radar.h"

#include <QCoreApplication>
#include <QPainter>
#include <QSettings>
#include <cmath>
#include <cstring>

#include "core/GeoMath.h"
#include "core/NavStore.h"
#include "core/WebSocket.h"

namespace {
QString tr(const char* s) { return QCoreApplication::translate("Radar", s); }

// --- protobuf wire format ---------------------------------------------------

bool readVarint(const uchar*& p, const uchar* end, quint64& v) {
  v = 0;
  for (int shift = 0; shift < 64; shift += 7) {
    if (p >= end) return false;
    const uchar b = *p++;
    v |= quint64(b & 0x7F) << shift;
    if (!(b & 0x80)) return true;
  }
  return false;
}

bool skipField(const uchar*& p, const uchar* end, int wire) {
  quint64 v;
  switch (wire) {
    case 0:
      return readVarint(p, end, v);
    case 1:
      if (end - p < 8) return false;
      p += 8;
      return true;
    case 2:
      if (!readVarint(p, end, v) || quint64(end - p) < v) return false;
      p += v;
      return true;
    case 5:
      if (end - p < 4) return false;
      p += 4;
      return true;
    default:
      return false;
  }
}

double readDouble(const uchar* p) {
  quint64 bits = 0;
  for (int i = 7; i >= 0; --i) bits = (bits << 8) | p[i];  // little endian
  double d;
  std::memcpy(&d, &bits, 8);
  return d;
}

bool decodeSpoke(const uchar* p, const uchar* end, RadarSpoke& s) {
  while (p < end) {
    quint64 key;
    if (!readVarint(p, end, key)) return false;
    const int field = int(key >> 3), wire = int(key & 7);
    quint64 v;
    if (wire == 0 && field >= 1 && field <= 4) {
      if (!readVarint(p, end, v)) return false;
      if (field == 1) s.angle = int(v);
      if (field == 2) s.bearing = int(v);
      if (field == 3) s.rangeM = int(v);
      if (field == 4) s.timeMs = qint64(v);
    } else if (wire == 1 && (field == 5 || field == 6)) {
      if (end - p < 8) return false;
      (field == 5 ? s.lat : s.lon) = readDouble(p);
      p += 8;
    } else if (wire == 2 && field == 7) {
      if (!readVarint(p, end, v) || quint64(end - p) < v) return false;
      s.data = QByteArray(reinterpret_cast<const char*>(p), qsizetype(v));
      p += v;
    } else if (!skipField(p, end, wire)) {
      return false;
    }
  }
  return true;
}

void putVarint(QByteArray& out, quint64 v) {
  do {
    uchar b = v & 0x7F;
    v >>= 7;
    if (v) b |= 0x80;
    out.append(char(b));
  } while (v);
}

void putDouble(QByteArray& out, int field, double d) {
  putVarint(out, quint64(field) << 3 | 1);
  quint64 bits;
  std::memcpy(&bits, &d, 8);
  for (int i = 0; i < 8; ++i) out.append(char((bits >> (8 * i)) & 0xFF));
}
}  // namespace

bool RadarProto::decode(const QByteArray& msg, QVector<RadarSpoke>& spokes,
                        int* radar) {
  const auto* p = reinterpret_cast<const uchar*>(msg.constData());
  const uchar* end = p + msg.size();
  while (p < end) {
    quint64 key, v;
    if (!readVarint(p, end, key)) return false;
    const int field = int(key >> 3), wire = int(key & 7);
    if (field == 1 && wire == 0) {
      if (!readVarint(p, end, v)) return false;
      if (radar) *radar = int(v);
    } else if (field == 2 && wire == 2) {
      if (!readVarint(p, end, v) || quint64(end - p) < v) return false;
      RadarSpoke s;
      if (!decodeSpoke(p, p + v, s)) return false;
      spokes << s;
      p += v;
    } else if (!skipField(p, end, wire)) {
      return false;
    }
  }
  return true;
}

QByteArray RadarProto::encode(const QVector<RadarSpoke>& spokes, int radar) {
  QByteArray out;
  putVarint(out, 1 << 3 | 0);
  putVarint(out, quint64(radar));
  for (const RadarSpoke& s : spokes) {
    QByteArray m;
    putVarint(m, 1 << 3 | 0);
    putVarint(m, quint64(s.angle));
    if (s.bearing >= 0) {
      putVarint(m, 2 << 3 | 0);
      putVarint(m, quint64(s.bearing));
    }
    putVarint(m, 3 << 3 | 0);
    putVarint(m, quint64(s.rangeM));
    if (s.timeMs > 0) {
      putVarint(m, 4 << 3 | 0);
      putVarint(m, quint64(s.timeMs));
    }
    if (!std::isnan(s.lat)) putDouble(m, 5, s.lat);
    if (!std::isnan(s.lon)) putDouble(m, 6, s.lon);
    putVarint(m, 7 << 3 | 2);
    putVarint(m, quint64(s.data.size()));
    m += s.data;
    putVarint(out, 2 << 3 | 2);
    putVarint(out, quint64(m.size()));
    out += m;
  }
  return out;
}

// --------------------------------------------------------------------------

void RadarImage::reset(int spokes, int maxLen) {
  m_spokes = std::max(1, spokes);
  m_maxLen = std::max(1, maxLen);
  m_polar.fill(0, m_spokes * m_maxLen);
  m_spokeRange.fill(0, m_spokes);
  m_rangeM = 0;
  m_maxValue = 15;
  m_heading = std::numeric_limits<double>::quiet_NaN();
}

void RadarImage::setSpoke(const RadarSpoke& s) {
  if (m_spokes == 0 || s.angle < 0 || s.angle >= m_spokes || s.rangeM <= 0)
    return;
  uchar* row = m_polar.data() + size_t(s.angle) * size_t(m_maxLen);
  const int len = int(s.data.size());
  const auto* src = reinterpret_cast<const uchar*>(s.data.constData());
  // Samples resampled to the full length: sample j lies at j/maxLen*range.
  for (int j = 0; j < m_maxLen; ++j) {
    const int i = len > 0 ? int(qint64(j) * len / m_maxLen) : 0;
    const uchar v = i < len ? src[i] : 0;
    row[j] = v;
    if (v > m_maxValue && v < 255) m_maxValue = v;
  }
  m_spokeRange[s.angle] = s.rangeM;
  m_rangeM = s.rangeM;
  if (s.bearing >= 0)
    m_heading =
        std::fmod(double(s.bearing - s.angle) * 360.0 / m_spokes + 720, 360);
}

int RadarImage::strongest(double relDeg, double halfWidthDeg, double fromM,
                          double toM) const {
  int best = 0;
  if (m_spokes == 0) return 0;
  const int a0 = int(std::floor((relDeg - halfWidthDeg) / 360 * m_spokes));
  const int a1 = int(std::ceil((relDeg + halfWidthDeg) / 360 * m_spokes));
  for (int a = a0; a <= a1; ++a) {
    const int sp = ((a % m_spokes) + m_spokes) % m_spokes;
    const int r = m_spokeRange[sp];
    if (r <= 0) continue;
    const int j0 = std::max(0, int(fromM / r * m_maxLen));
    const int j1 = std::min(m_maxLen - 1, int(toM / r * m_maxLen));
    for (int j = j0; j <= j1; ++j) best = std::max(best, int(at(sp, j)));
  }
  return best;
}

QRgb RadarImage::defaultColor(int value, int maxValue) {
  if (value <= 0) return qRgba(0, 0, 0, 0);
  const double f = std::clamp(double(value) / std::max(1, maxValue), 0.0, 1.0);
  // Dark green - green - yellow - red, weak returns translucent.
  struct Stop {
    double f;
    int r, g, b;
  };
  static const Stop stops[] = {{0.0, 0, 90, 60},
                               {0.35, 40, 200, 70},
                               {0.7, 240, 220, 40},
                               {1.0, 240, 60, 40}};
  int i = 0;
  while (i < 2 && f > stops[i + 1].f) ++i;
  const Stop& a = stops[i];
  const Stop& b = stops[i + 1];
  const double t = (f - a.f) / (b.f - a.f);
  const int alpha = int(110 + 145 * std::min(1.0, f * 1.6));
  return qRgba(int(a.r + (b.r - a.r) * t), int(a.g + (b.g - a.g) * t),
               int(a.b + (b.b - a.b) * t), alpha);
}

QImage RadarImage::render(int size, double rotationDeg, double displayRangeM,
                          const Style& style) const {
  QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::transparent);
  if (m_spokes == 0 || displayRangeM <= 0 || size <= 0) return img;
  // Colors per value, gain and threshold applied.
  QRgb lut[256];
  for (int v = 0; v < 256; ++v) {
    const int g = std::min(254, int(std::round(v * style.gain)));
    if (v == 0 || g < style.threshold) {
      lut[v] = 0;
      continue;
    }
    QRgb c;
    if (!style.legend.isEmpty())
      c = style.legend[std::min<int>(std::min(v, g),
                                     int(style.legend.size()) - 1)];
    else
      c = defaultColor(g, m_maxValue);
    lut[v] = qPremultiply(c);
  }
  const double half = size / 2.0;
  const double mPerPx = displayRangeM / half;
  const double spokesPerRad = m_spokes / (2 * M_PI);
  const double rot = rotationDeg * M_PI / 180;
  for (int y = 0; y < size; ++y) {
    auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
    const double dy = y + 0.5 - half;
    for (int x = 0; x < size; ++x) {
      const double dx = x + 0.5 - half;
      const double d2 = dx * dx + dy * dy;
      if (d2 > half * half) continue;
      // Clockwise from up, then relative to the bow.
      double a = std::atan2(dx, -dy) - rot;
      int sp = int(std::lround(a * spokesPerRad)) % m_spokes;
      if (sp < 0) sp += m_spokes;
      const int r = m_spokeRange[sp];
      if (r <= 0) continue;
      const int j = int(std::sqrt(d2) * mPerPx / r * m_maxLen);
      if (j >= m_maxLen) continue;
      line[x] = lut[at(sp, j)];
    }
  }
  return img;
}

// --------------------------------------------------------------------------

int RadarSimulator::rnd(int n) {
  m_seed = m_seed * 1664525u + 1013904223u;
  return n > 0 ? int((m_seed >> 8) % quint32(n)) : 0;
}

void RadarSimulator::prepareLand(double lat, double lon) {
  constexpr int kGrid = 512;
  const double extentM = rangeM * 1.05;
  if (!m_mask.isNull() && m_maskRange == extentM &&
      geo::distanceNm(lat, lon, m_maskLat, m_maskLon) * 1852 <
          extentM / kGrid * 4)
    return;
  m_maskLat = lat;
  m_maskLon = lon;
  m_maskRange = extentM;
  m_mask = QImage(kGrid, kGrid, QImage::Format_Grayscale8);
  m_mask.fill(0);
  if (!land || land->isEmpty()) return;
  const QPointF m0 = geo::toMercator(lat, lon);
  const double unitsPerM = 1.0 / (geo::nmPerMercatorUnit(lat) * 1852);
  const double halfUnits = extentM * unitsPerM;
  const QRectF box(m0.x() - halfUnits, m0.y() - halfUnits, 2 * halfUnits,
                   2 * halfUnits);
  QPainter p(&m_mask);
  p.setRenderHint(QPainter::Antialiasing, false);
  const double k = kGrid / (2 * halfUnits);
  p.translate(kGrid / 2.0, kGrid / 2.0);
  p.scale(k, k);
  p.translate(-m0);
  p.setPen(Qt::NoPen);
  p.setBrush(Qt::white);
  for (const MercatorPolygon& poly : *land)
    if (poly.bounds.intersects(box)) p.drawPath(poly.path);
}

QVector<RadarSpoke> RadarSimulator::spokesFor(int firstAngle, int count,
                                              double lat, double lon,
                                              double hdg,
                                              const QVector<Target>& targets) {
  prepareLand(lat, lon);
  const int grid = m_mask.width();
  const double mPerCell = 2 * m_maskRange / std::max(1, grid);
  const double sampleM = double(rangeM) / maxLen;
  // Targets relative to the ship: bearing (deg true) and distance (m).
  struct Rel {
    double brg, dist, size;
  };
  QVector<Rel> rel;
  for (const Target& t : targets) {
    const double d = geo::distanceNm(lat, lon, t.lat, t.lon) * 1852;
    if (d < rangeM && d > 20)
      rel << Rel{geo::bearingDeg(lat, lon, t.lat, t.lon), d, t.sizeM};
  }
  QVector<RadarSpoke> out;
  out.reserve(count);
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  for (int k = 0; k < count; ++k) {
    RadarSpoke s;
    s.angle = (firstAngle + k) % spokes;
    const double relDeg = s.angle * 360.0 / spokes;
    const double brgDeg = std::fmod(hdg + relDeg + 360, 360);
    s.bearing = int(std::lround(brgDeg / 360 * spokes)) % spokes;
    s.rangeM = rangeM;
    s.timeMs = now;
    s.lat = lat;
    s.lon = lon;
    s.data = QByteArray(maxLen, 0);
    auto* d = reinterpret_cast<uchar*>(s.data.data());
    const double sn = std::sin(brgDeg * M_PI / 180);
    const double cs = std::cos(brgDeg * M_PI / 180);
    // The boat is never on land: a coarse coastline under it is skipped
    // until the ray reaches water. Gaps of water under 60 m (a ray along a
    // polygon edge) do not make a new coastline.
    bool started = false;
    int landRun = 0, waterRun = 0, prevLandRun = 0;
    for (int j = 0; j < maxLen; ++j) {
      const double dm = (j + 0.5) * sampleM;
      int v = 0;
      // Land: a strong coastline, weaker land behind it, then shadow.
      const int gx = int(grid / 2.0 + dm * sn / mPerCell);
      const int gy = int(grid / 2.0 - dm * cs / mPerCell);
      const bool isLand = gx >= 0 && gy >= 0 && gx < grid && gy < grid &&
                          m_mask.constScanLine(gy)[gx] > 127;
      if (!isLand) started = true;
      if (isLand && started) {
        if (waterRun > 0 && waterRun * sampleM < 60 && landRun == 0)
          landRun = prevLandRun + waterRun;
        waterRun = 0;
        const double inland = landRun * sampleM;
        if (landRun < 2)
          v = 13 + rnd(3);
        else if (inland < 400)
          v = 5 + rnd(7);
        else if (inland < 900)
          v = rnd(4) == 0 ? 3 + rnd(5) : 0;
        ++landRun;
      } else {
        if (isLand) {  // under the boat
          d[j] = 0;
          continue;
        }
        if (landRun > 0) {
          prevLandRun = landRun;
          waterRun = 0;
        }
        landRun = 0;
        ++waterRun;
        // Sea clutter near the boat, a little noise.
        if (dm < 700) {
          const double f = 1 - dm / 700;
          if (rnd(1000) < int(f * f * 600)) v = 1 + rnd(int(2 + f * 8));
        } else if (rnd(2000) == 0) {
          v = 1 + rnd(3);
        }
      }
      d[j] = uchar(v);
    }
    // Targets: a blob a few samples long, as wide as the beam.
    for (const Rel& r : rel) {
      const double off = std::abs(std::remainder(brgDeg - r.brg, 360.0));
      const double half =
          0.9 + std::atan2(r.size / 2, r.dist) * 180 / M_PI;  // beam + target
      if (off > half) continue;
      const int j0 = int((r.dist - r.size / 2) / sampleM);
      const int j1 = std::max(j0 + 1, int((r.dist + r.size / 2) / sampleM));
      for (int j = std::max(0, j0); j <= std::min(maxLen - 1, j1); ++j)
        d[j] = uchar(std::max<int>(d[j], 12 + rnd(4)));
    }
    out << s;
  }
  return out;
}

// --------------------------------------------------------------------------

RadarService& RadarService::instance() {
  static RadarService s;
  return s;
}

RadarService::RadarService() {
  m_current = QSettings().value("Radar/source").toString();
  m_notify.setInterval(100);
  connect(&m_notify, &QTimer::timeout, this, [this] {
    if (!m_dirty) return;
    m_dirty = false;
    emit updated();
  });
  m_simTimer.setInterval(50);
  connect(&m_simTimer, &QTimer::timeout, this, &RadarService::simulate);
  m_retry.setSingleShot(true);
  m_retry.setInterval(5000);
  connect(&m_retry, &QTimer::timeout, this, &RadarService::restart);
  m_sim.rangeM = QSettings().value("Radar/simRange", 1852 * 2).toInt();
  connect(&SignalKHub::instance(), &SignalKHub::radarsChanged, this, [this] {
    emit sourcesChanged();
    // Automatic choice: the first radar of a server, else the simulator.
    if (QSettings().value("Radar/source").toString().isEmpty() ||
        (m_current != "sim" && !SignalKHub::instance().radars().isEmpty() &&
         std::none_of(
             SignalKHub::instance().radars().begin(),
             SignalKHub::instance().radars().end(),
             [this](const RadarInfo& r) { return r.id == m_current; })))
      m_current.clear();
    if (active()) restart();
  });
}

QVector<RadarService::Source> RadarService::sources() const {
  QVector<Source> out;
  for (const RadarInfo& r : SignalKHub::instance().radars())
    out << Source{r.id, r.brand.isEmpty() ? r.name : r.brand + " " + r.name,
                  false};
  out << Source{"sim", tr("Simulator"), true};
  return out;
}

void RadarService::select(const QString& id) {
  QSettings().setValue("Radar/source", id);
  if (id == m_current) return;
  m_current = id;
  restart();
  emit sourcesChanged();
}

bool RadarService::receiving() const {
  return m_lastSpoke.isValid() &&
         m_lastSpoke.msecsTo(QDateTime::currentDateTimeUtc()) < 5000;
}

void RadarService::setSimulatorRange(int meters) {
  if (meters == m_sim.rangeM) return;
  m_sim.rangeM = meters;
  QSettings().setValue("Radar/simRange", meters);
}

void RadarService::setLand(RadarSimulator::Land land) {
  m_sim.land = std::move(land);
}

void RadarService::want(QObject* who, bool on) {
  const bool was = active();
  if (on && !m_wanted.contains(who)) {
    m_wanted.insert(who);
    connect(who, &QObject::destroyed, this,
            [this](QObject* o) { want(o, false); });
  } else if (!on) {
    m_wanted.remove(who);
  }
  if (active() != was) {
    if (active())
      restart();
    else
      stopStream();
  }
}

void RadarService::setStatus(const QString& s) {
  if (s == m_status) return;
  m_status = s;
  emit statusChanged();
}

void RadarService::stopStream() {
  m_simTimer.stop();
  m_notify.stop();
  m_retry.stop();
  if (m_ws) {
    m_ws->disconnect(this);
    m_ws->close();
    m_ws->deleteLater();
    m_ws = nullptr;
  }
}

void RadarService::restart() {
  stopStream();
  if (!active()) return;
  const auto& radars = SignalKHub::instance().radars();
  if (m_current.isEmpty())
    m_current = radars.isEmpty() ? QStringLiteral("sim") : radars.first().id;
  m_notify.start();
  m_lastSpoke = {};
  if (m_current == "sim") {
    m_image.reset(m_sim.spokes, m_sim.maxLen);
    m_legend.clear();
    m_simTimer.start();
    setStatus(tr("Simulated radar"));
    return;
  }
  const auto it =
      std::find_if(radars.begin(), radars.end(),
                   [this](const RadarInfo& r) { return r.id == m_current; });
  if (it == radars.end()) {
    m_image.reset(1, 1);
    setStatus(tr("Radar not available – no Signal K server offers it"));
    return;
  }
  m_image.reset(it->spokes, it->maxSpokeLen);
  m_legend = it->legend;
  m_ws = new WebSocketClient(this);
  connect(m_ws, &WebSocketClient::connected, this,
          [this] { setStatus(tr("Connected, waiting for spokes")); });
  connect(m_ws, &WebSocketClient::binaryMessageReceived, this,
          &RadarService::feedMessage);
  connect(m_ws, &WebSocketClient::errorOccurred, this,
          [this](const QString& e) {
            setStatus(tr("Radar stream: %1").arg(e));
            m_retry.start();
          });
  connect(m_ws, &WebSocketClient::disconnected, this, [this] {
    setStatus(tr("Radar stream closed"));
    m_retry.start();
  });
  setStatus(tr("Connecting to %1…").arg(it->name));
  m_ws->open(it->streamUrl);
}

void RadarService::feedMessage(const QByteArray& msg) {
  QVector<RadarSpoke> spokes;
  if (!RadarProto::decode(msg, spokes)) return;
  for (const RadarSpoke& s : spokes) m_image.setSpoke(s);
  m_spokeCount += int(spokes.size());
  if (!spokes.isEmpty()) {
    if (!receiving() && m_current != "sim") setStatus(tr("Receiving"));
    m_lastSpoke = QDateTime::currentDateTimeUtc();
    m_dirty = true;
  }
}

void RadarService::simulate() {
  const OwnShip& own = NavStore::instance().ownShip();
  const double lat = own.valid ? own.lat : 54.47;
  const double lon = own.valid ? own.lon : 10.28;
  const double hdg = !std::isnan(own.hdg)   ? own.hdg
                     : !std::isnan(own.cog) ? own.cog
                                            : 0;
  QVector<RadarSimulator::Target> targets;
  for (const AisTarget& t : NavStore::instance().aisTargets())
    if (t.hasPosition())
      // Cargo ships and tankers echo larger than yachts.
      targets << RadarSimulator::Target{
          t.lat, t.lon, t.shipType >= 70 && t.shipType < 90 ? 120.0 : 25.0};
  // 24 revolutions per minute.
  const int count = std::max(1, m_sim.spokes * 50 / 2500);
  const QVector<RadarSpoke> spokes =
      m_sim.spokesFor(m_simAngle, count, lat, lon, hdg, targets);
  m_simAngle = (m_simAngle + count) % m_sim.spokes;
  for (const RadarSpoke& s : spokes) m_image.setSpoke(s);
  m_spokeCount += count;
  m_lastSpoke = QDateTime::currentDateTimeUtc();
  m_dirty = true;
}
