#include "core/Sonar.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <cmath>

#include "core/NavStore.h"
#include "core/SignalK.h"

SonarService& SonarService::instance() {
  static SonarService s;
  return s;
}

SonarService::SonarService() {
  m_echoPath = QSettings()
                   .value("Sonar/echoPath", "environment.depth.echogram")
                   .toString();
  m_timer.setInterval(1000);
  connect(&m_timer, &QTimer::timeout, this, &SonarService::tick);
  m_timer.start();
  connect(&SignalKHub::instance(), &SignalKHub::valuesChanged, this,
          [this](const QStringList& paths) {
            if (!paths.contains(m_echoPath)) return;
            SonarPing p;
            if (!parseEcho(SignalKHub::instance().value(m_echoPath).value,
                           p.rangeM, p.samples))
              return;
            const Environment& env = NavStore::instance().environment();
            p.time = QDateTime::currentDateTimeUtc();
            if (env.depthTime.isValid() && env.depthTime.secsTo(p.time) < 5)
              p.depthM = env.depth;
            p.waterTempC = env.waterTempC;
            m_lastEcho = p.time;
            addPing(p);
          });
}

void SonarService::setEchoPath(const QString& path) {
  m_echoPath = path.trimmed();
  QSettings().setValue("Sonar/echoPath", m_echoPath);
}

void SonarService::addPing(const SonarPing& p) {
  m_pings << p;
  if (m_pings.size() > kMaxPings) m_pings.remove(0, m_pings.size() - kMaxPings);
  emit pinged();
}

void SonarService::clear() {
  m_pings.clear();
  m_lastEcho = {};
  emit pinged();
}

bool SonarService::receivingEcho() const {
  return m_lastEcho.isValid() &&
         m_lastEcho.secsTo(QDateTime::currentDateTimeUtc()) < 5;
}

void SonarService::tick() {
  // Depth pings once a second while no echo samples arrive.
  if (receivingEcho()) return;
  const Environment& env = NavStore::instance().environment();
  const QDateTime now = QDateTime::currentDateTimeUtc();
  SonarPing p;
  p.time = now;
  if (env.depthTime.isValid() && env.depthTime.secsTo(now) < 5)
    p.depthM = env.depth;
  p.waterTempC = env.waterTempC;
  // No data at all: keep the history as it is (no endless gap).
  if (std::isnan(p.depthM) &&
      (m_pings.isEmpty() || std::isnan(m_pings.last().depthM)))
    return;
  addPing(p);
}

bool SonarService::parseEcho(const QJsonValue& v, float& rangeM,
                             QByteArray& samples) {
  const QJsonObject o = v.toObject();
  const double range = o.value("range").toDouble(o.value("rangeM").toDouble());
  if (!(range > 0)) return false;
  samples.clear();
  if (o.value("samples").isArray()) {
    const QJsonArray a = o.value("samples").toArray();
    bool unit = true;  // 0..1 or 0..255
    for (const auto& x : a)
      if (x.toDouble() > 1.0) unit = false;
    samples.reserve(a.size());
    for (const auto& x : a)
      samples.append(char(std::clamp(
          int(std::lround(x.toDouble() * (unit ? 255 : 1))), 0, 255)));
  } else if (o.value("data").isString()) {
    samples = QByteArray::fromBase64(o.value("data").toString().toLatin1());
  }
  if (samples.isEmpty()) return false;
  rangeM = float(range);
  return true;
}

QString SonarService::source() const {
  if (receivingEcho()) return QStringLiteral("Signal K");
  const auto& hub = SignalKHub::instance();
  for (const char* p :
       {"environment.depth.belowTransducer", "environment.depth.belowKeel",
        "environment.depth.belowSurface"})
    if (!std::isnan(hub.number(p, 10))) return QStringLiteral("Signal K");
  const Environment& env = NavStore::instance().environment();
  if (env.depthTime.isValid() &&
      env.depthTime.secsTo(QDateTime::currentDateTimeUtc()) < 10)
    return QStringLiteral("NMEA 0183");
  return {};
}

double SonarService::belowKeelM() const {
  const auto& hub = SignalKHub::instance();
  const double keel = hub.number("environment.depth.belowKeel", 10);
  if (!std::isnan(keel)) return keel;
  const double t2k = hub.number("environment.depth.transducerToKeel", 0);
  const double bt = hub.number("environment.depth.belowTransducer", 10);
  return std::isnan(t2k) || std::isnan(bt) ? std::nan("") : bt + t2k;
}

void SonarService::depthRange(int secs, double& minM, double& maxM) const {
  minM = std::nan("");
  maxM = std::nan("");
  if (m_pings.isEmpty()) return;
  const QDateTime from = m_pings.last().time.addSecs(-secs);
  for (int i = int(m_pings.size()) - 1; i >= 0 && m_pings[i].time >= from;
       --i) {
    const double d = m_pings[i].depthM;
    if (std::isnan(d)) continue;
    if (std::isnan(minM) || d < minM) minM = d;
    if (std::isnan(maxM) || d > maxM) maxM = d;
  }
}
