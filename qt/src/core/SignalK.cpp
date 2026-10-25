#include "core/SignalK.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <cmath>

#include "core/NavStore.h"
#include "core/WebSocket.h"

namespace {
constexpr double kRad = 180.0 / M_PI;
constexpr double kMsToKn = 3600.0 / 1852.0;

/** "#RRGGBB", "#RRGGBBAA" (Signal K/Mayara) or a color name. */
QRgb parseColor(const QString& s) {
  QString h = s.trimmed();
  if (h.startsWith('#') && h.size() == 9) {
    bool ok = false;
    const uint v = h.mid(1).toUInt(&ok, 16);
    if (ok) return qRgba(v >> 24, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
  }
  const QColor c = QColor::fromString(h);
  return c.isValid() ? c.rgba() : qRgba(0, 0, 0, 0);
}

QRgb legendColor(const QJsonValue& v) {
  if (v.isString()) return parseColor(v.toString());
  return parseColor(v.toObject().value("color").toString());
}
}  // namespace

QVector<RadarInfo> RadarInfo::parseList(const QByteArray& json,
                                        const QUrl& base) {
  QVector<RadarInfo> out;
  const QJsonDocument doc = QJsonDocument::fromJson(json);
  QVector<QPair<QString, QJsonObject>> items;
  if (doc.isArray()) {
    for (const auto& v : doc.array())
      items << qMakePair(v.toObject().value("id").toString(), v.toObject());
  } else {
    QJsonObject o = doc.object();
    if (o.value("radars").isObject()) o = o.value("radars").toObject();
    for (auto it = o.begin(); it != o.end(); ++it)
      if (it.value().isObject()) items << qMakePair(it.key(), it->toObject());
  }
  for (const auto& [key, o] : items) {
    RadarInfo r;
    r.id = o.value("id").toString(key);
    if (r.id.isEmpty()) continue;
    r.name = o.value("name").toString(r.id);
    r.brand = o.value("brand").toString();
    r.spokes =
        o.value("spokesPerRevolution").toInt(o.value("spokes").toInt(2048));
    r.maxSpokeLen =
        o.value("maxSpokeLen").toInt(o.value("maxSpokeLength").toInt(1024));
    QUrl stream(o.value("streamUrl").toString());
    if (stream.isRelative()) stream = base.resolved(stream);
    if (stream.scheme() == "http") stream.setScheme("ws");
    if (stream.scheme() == "https") stream.setScheme("wss");
    if (stream.isEmpty()) {
      stream = base.resolved(
          QUrl("/signalk/v2/api/vessels/self/radars/" + r.id + "/stream"));
      stream.setScheme(base.scheme() == "https" ? "wss" : "ws");
    }
    r.streamUrl = stream;
    QJsonValue lg = o.value("legend");
    if (lg.isObject() && lg.toObject().value("pixels").isArray())
      lg = lg.toObject().value("pixels");
    if (lg.isArray()) {
      for (const auto& v : lg.toArray()) r.legend << legendColor(v);
    } else if (lg.isObject()) {
      const QJsonObject m = lg.toObject();
      int maxIndex = -1;
      for (auto it = m.begin(); it != m.end(); ++it)
        maxIndex = std::max(maxIndex, it.key().toInt());
      if (maxIndex >= 0 && maxIndex < 256) {
        r.legend.fill(qRgba(0, 0, 0, 0), maxIndex + 1);
        for (auto it = m.begin(); it != m.end(); ++it) {
          bool ok = false;
          const int i = it.key().toInt(&ok);
          if (ok && i >= 0 && i <= maxIndex) r.legend[i] = legendColor(*it);
        }
      }
    }
    out << r;
  }
  return out;
}

// --------------------------------------------------------------------------

SignalKHub& SignalKHub::instance() {
  static SignalKHub h;
  return h;
}

double SignalKHub::number(const QString& path, int maxAgeSecs) const {
  const auto it = m_values.constFind(path);
  if (it == m_values.constEnd() || !it->value.isDouble()) return kNaN;
  if (maxAgeSecs > 0 && it->time.isValid() &&
      it->time.secsTo(QDateTime::currentDateTimeUtc()) > maxAgeSecs)
    return kNaN;
  return it->value.toDouble();
}

void SignalKHub::setRadars(const QString& serverUrl,
                           const QVector<RadarInfo>& radars) {
  QVector<RadarInfo> withServer = radars;
  for (RadarInfo& r : withServer) r.serverUrl = serverUrl;
  if (withServer.isEmpty())
    m_radarsByServer.remove(serverUrl);
  else
    m_radarsByServer.insert(serverUrl, withServer);
  QVector<RadarInfo> all;
  for (const auto& list : m_radarsByServer) all += list;
  const auto same = [](const QVector<RadarInfo>& a,
                       const QVector<RadarInfo>& b) {
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i)
      if (a[i].id != b[i].id || a[i].streamUrl != b[i].streamUrl) return false;
    return true;
  };
  if (same(all, m_radars)) return;
  m_radars = all;
  emit radarsChanged();
}

void SignalKHub::clear() {
  m_values.clear();
  m_self.clear();
}

bool SignalKHub::navUpdate(const QString& path, const QJsonValue& v,
                           NavUpdate& u) const {
  const QDateTime now = QDateTime::currentDateTimeUtc();
  const auto recent = [&](const QString& p) {
    const auto it = m_values.constFind(p);
    return it != m_values.constEnd() && it->time.isValid() &&
           it->time.secsTo(now) < 5;
  };
  const double x = v.toDouble(kNaN);
  if (path == "navigation.position") {
    const QJsonObject o = v.toObject();
    if (!o.value("latitude").isDouble() || !o.value("longitude").isDouble())
      return false;
    u.lat = o.value("latitude").toDouble();
    u.lon = o.value("longitude").toDouble();
    u.time = now;
    return true;
  }
  if (std::isnan(x) && path != "navigation.attitude") return false;
  if (path == "navigation.speedOverGround") {
    u.sog = x * kMsToKn;
  } else if (path == "navigation.courseOverGroundTrue") {
    u.cog = std::fmod(x * kRad + 360, 360);
  } else if (path == "navigation.headingTrue") {
    u.hdg = std::fmod(x * kRad + 360, 360);
  } else if (path == "navigation.headingMagnetic") {
    if (recent("navigation.headingTrue")) return false;
    const double var = number("navigation.magneticVariation", 3600);
    u.hdg = std::fmod((x + (std::isnan(var) ? 0 : var)) * kRad + 360, 360);
  } else if (path == "navigation.speedThroughWater") {
    u.stw = x * kMsToKn;
  } else if (path == "navigation.attitude") {
    const QJsonValue roll = v.toObject().value("roll");
    if (!roll.isDouble()) return false;
    u.heel = roll.toDouble() * kRad;
  } else if (path == "navigation.gnss.satellites") {
    u.satellites = int(x);
  } else if (path == "environment.depth.belowTransducer") {
    u.depth = x;
  } else if (path == "environment.depth.belowKeel") {
    if (recent("environment.depth.belowTransducer")) return false;
    u.depth = x;
  } else if (path == "environment.depth.belowSurface") {
    if (recent("environment.depth.belowTransducer") ||
        recent("environment.depth.belowKeel"))
      return false;
    u.depth = x;
  } else if (path == "environment.wind.angleApparent" ||
             path == "environment.wind.speedApparent") {
    // NavStore wants both at once: take the other from the hub.
    const bool angle = path.endsWith("angleApparent");
    const double other = number(angle ? "environment.wind.speedApparent"
                                      : "environment.wind.angleApparent",
                                10);
    if (std::isnan(other)) return false;
    const double a = angle ? x : other, s = angle ? other : x;
    u.awa = std::remainder(a * kRad, 360.0);
    u.aws = s * kMsToKn;
  } else if (path == "environment.water.temperature") {
    u.waterTempC = x - 273.15;
  } else if (path == "environment.wind.angleTrueWater") {
    u.twa = std::remainder(x * kRad, 360.0);
  } else if (path == "environment.wind.speedTrue") {
    u.tws = x * kMsToKn;
  } else if (path == "environment.wind.directionTrue") {
    u.twd = std::fmod(x * kRad + 360, 360);
  } else if (path.startsWith("propulsion.")) {
    if (path.endsWith(".revolutions"))
      u.rpm = x * 60;
    else if (path.endsWith(".temperature") ||
             path.endsWith(".coolantTemperature"))
      u.coolantC = x - 273.15;
    else if (path.endsWith(".oilPressure"))
      u.oilBar = x / 1e5;
    else if (path.endsWith(".runTime"))
      u.engineHours = x / 3600;
    else if (path.endsWith(".fuel.rate"))
      u.fuelRateLph = x * 3.6e6;
    else if (path.endsWith(".alternatorVoltage"))
      u.voltage = x;
    else
      return false;
  } else {
    return false;
  }
  return true;
}

int SignalKHub::applyDelta(const QByteArray& json) {
  const QJsonObject o = QJsonDocument::fromJson(json).object();
  if (o.contains("self") && !o.contains("updates")) {  // hello message
    m_self = o.value("self").toString();
    return 0;
  }
  const QString ctx = o.value("context").toString("vessels.self");
  // Before the hello names the own vessel, the stream (subscribe=self)
  // only carries own values.
  if (!m_self.isEmpty() && ctx != "vessels.self" && ctx != m_self &&
      !(m_self.startsWith("vessels.") && ctx == m_self.mid(8)))
    return 0;
  QStringList paths;
  NavUpdate u;
  bool any = false;
  for (const auto& up : o.value("updates").toArray()) {
    const QJsonObject uo = up.toObject();
    QDateTime t = QDateTime::fromString(uo.value("timestamp").toString(),
                                        Qt::ISODateWithMs);
    // Server clocks drift; in the future or very old: now.
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (!t.isValid() || t > now.addSecs(5) || t < now.addSecs(-600)) t = now;
    for (const auto& vv : uo.value("values").toArray()) {
      const QJsonObject val = vv.toObject();
      const QString path = val.value("path").toString();
      const QJsonValue value = val.value("value");
      if (path.isEmpty()) {
        // Values without a path (e.g. name, mmsi) are skipped.
        continue;
      }
      any |= navUpdate(path, value, u);
      m_values.insert(path, {value, t});
      paths << path;
    }
  }
  if (any) NavStore::instance().applyNavUpdate(u);
  if (!paths.isEmpty()) emit valuesChanged(paths);
  return int(paths.size());
}

// --------------------------------------------------------------------------

SignalKConnection::SignalKConnection(const ConnectionConfig& cfg,
                                     QObject* parent)
    : Connection(cfg, parent) {
  m_retry.setSingleShot(true);
  m_retry.setInterval(5000);
  connect(&m_retry, &QTimer::timeout, this, &SignalKConnection::connectNow);
  m_radarPoll.setInterval(30000);
  connect(&m_radarPoll, &QTimer::timeout, this,
          &SignalKConnection::fetchRadars);
}

SignalKConnection::~SignalKConnection() {
  if (!m_stopped) SignalKHub::instance().setRadars(baseUrl().toString(), {});
}

QUrl SignalKConnection::baseUrl() const {
  QUrl u;
  u.setScheme(m_cfg.port == 443 ? "https" : "http");
  u.setHost(m_cfg.host);
  if (m_cfg.port != 443 && m_cfg.port != 80) u.setPort(m_cfg.port);
  return u;
}

void SignalKConnection::start() {
  m_stopped = false;
  m_ws = new WebSocketClient(this);
  connect(m_ws, &WebSocketClient::connected, this, [this] {
    setStatus(Status::Connected, tr("Connected"));
    fetchRadars();
    m_radarPoll.start();
  });
  connect(m_ws, &WebSocketClient::textMessageReceived, this,
          [this](const QString& msg) {
            const int n = SignalKHub::instance().applyDelta(msg.toUtf8());
            if (n > 0) countLines(QByteArray(n, '\n'));
          });
  connect(m_ws, &WebSocketClient::errorOccurred, this,
          [this](const QString& err) {
            if (m_stopped) return;
            setStatus(Status::Error, err);
            // The radar server may run without a delta stream (Mayara).
            fetchRadars();
            m_retry.start();
          });
  connect(m_ws, &WebSocketClient::disconnected, this, [this] {
    if (m_stopped) return;
    setStatus(Status::Connecting, tr("Reconnecting…"));
    m_retry.start();
  });
  connectNow();
}

void SignalKConnection::stop() {
  m_stopped = true;
  m_retry.stop();
  m_radarPoll.stop();
  if (m_ws) {
    m_ws->close();
    m_ws->deleteLater();
    m_ws = nullptr;
  }
  SignalKHub::instance().setRadars(baseUrl().toString(), {});
  setStatus(Status::Off);
}

void SignalKConnection::connectNow() {
  if (!m_ws || m_stopped) return;
  setStatus(Status::Connecting, tr("Connecting…"));
  QUrl url = baseUrl();
  url.setScheme(m_cfg.port == 443 ? "wss" : "ws");
  url.setPath("/signalk/v1/stream");
  url.setQuery("subscribe=self");
  m_ws->open(url);
}

void SignalKConnection::fetchRadars() {
  // The Signal K radar API, else the path of a stand-alone Mayara server.
  const QUrl base = baseUrl();
  const QString key = base.toString();
  QPointer<SignalKConnection> self(this);
  const auto get = [this](const QString& path) {
    QNetworkRequest req(baseUrl().resolved(QUrl(path)));
    req.setTransferTimeout(8000);
    return m_net.get(req);
  };
  QNetworkReply* r = get("/signalk/v2/api/vessels/self/radars");
  connect(r, &QNetworkReply::finished, this, [self, r, base, key, get] {
    r->deleteLater();
    if (!self || self->m_stopped) return;
    const QByteArray body = r->readAll();
    if (r->error() == QNetworkReply::NoError) {
      SignalKHub::instance().setRadars(key, RadarInfo::parseList(body, base));
      return;
    }
    QNetworkReply* r2 = get("/v2/api/radars");
    connect(r2, &QNetworkReply::finished, self, [self, r2, base, key] {
      r2->deleteLater();
      if (!self || self->m_stopped) return;
      SignalKHub::instance().setRadars(
          key, r2->error() == QNetworkReply::NoError
                   ? RadarInfo::parseList(r2->readAll(), base)
                   : QVector<RadarInfo>{});
    });
  });
}
