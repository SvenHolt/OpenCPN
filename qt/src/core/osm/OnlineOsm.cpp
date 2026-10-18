#include "core/osm/OnlineOsm.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInformation>
#include <QNetworkReply>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>
#include <cmath>

namespace {
const char* kUserAgent = "OpenCPN-Qt/0.1 (chart plotter; harbour data)";
const int kCacheDays = 14;
const int kOfflineRetrySecs = 60;

QStringList defaultServers() {
  return {"https://overpass-api.de/api/interpreter",
          "https://overpass.kumi.systems/api/interpreter",
          "https://overpass.private.coffee/api/interpreter"};
}

/** Serializes elements for the tile cache. */
QByteArray toJson(const QVector<OsmElement>& elements) {
  QJsonArray a;
  for (const OsmElement& e : elements) {
    QJsonObject tags;
    for (auto it = e.tags.begin(); it != e.tags.end(); ++it)
      tags.insert(it.key(), it.value());
    a << QJsonObject{{"type", e.type},
                     {"id", double(e.id)},
                     {"lat", e.lat},
                     {"lon", e.lon},
                     {"tags", tags}};
  }
  return QJsonDocument(QJsonObject{{"elements", a}})
      .toJson(QJsonDocument::Compact);
}
}  // namespace

OnlineOsm& OnlineOsm::instance() {
  static OnlineOsm s;
  return s;
}

OnlineOsm::OnlineOsm() {
  QSettings s;
  m_enabled = s.value("Overpass/online", true).toBool();
  m_servers =
      s.value("Overpass/onlineServers", defaultServers()).toStringList();
  if (m_servers.isEmpty()) m_servers = defaultServers();
  // The system's view of the connection, where Qt has a backend for it.
  if (QNetworkInformation::loadDefaultBackend()) {
    if (auto* ni = QNetworkInformation::instance())
      connect(ni, &QNetworkInformation::reachabilityChanged, this,
              [this](QNetworkInformation::Reachability r) {
                if (r == QNetworkInformation::Reachability::Online)
                  setOnline(true);
              });
  }
}

void OnlineOsm::setEnabled(bool on) {
  m_enabled = on;
  QSettings().setValue("Overpass/online", on);
}

void OnlineOsm::setServers(const QStringList& urls) {
  m_servers = urls.isEmpty() ? defaultServers() : urls;
  m_goodServer = 0;
  QSettings().setValue("Overpass/onlineServers", m_servers);
}

bool OnlineOsm::isOnline() const {
  if (auto* ni = QNetworkInformation::instance())
    if (ni->reachability() == QNetworkInformation::Reachability::Disconnected)
      return false;
  if (m_online) return true;
  // Try again after a while.
  return m_offlineSince.secsTo(QDateTime::currentDateTimeUtc()) >
         kOfflineRetrySecs;
}

void OnlineOsm::setOnline(bool on) {
  if (!on) m_offlineSince = QDateTime::currentDateTimeUtc();
  if (on == m_online) return;
  m_online = on;
  emit onlineChanged(on);
}

// --------------------------------------------------------------------------
// Overpass

/** One query raced over the servers. */
struct OsmQueryRace {
  bool done = false;
  int running = 0;
  int next = 0;
  QString lastError;
  bool noNetwork = true;
};

void OnlineOsm::query(const QString& ql, Callback done) {
  if (!m_enabled) {
    done({}, tr("Online data is switched off."));
    return;
  }
  if (!isOnline()) {
    done({}, tr("No network connection."));
    return;
  }
  auto race = std::make_shared<OsmQueryRace>();
  post(race, ql, std::move(done));
}

void OnlineOsm::post(std::shared_ptr<OsmQueryRace> race, const QString& ql,
                     Callback done) {
  if (race->done || race->next >= m_servers.size()) return;
  const int server = (m_goodServer + race->next++) % int(m_servers.size());
  ++race->running;
  QNetworkRequest req{QUrl(m_servers[server])};
  req.setHeader(QNetworkRequest::ContentTypeHeader,
                "application/x-www-form-urlencoded");
  req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
  req.setTransferTimeout(60000);
  QUrlQuery form;
  form.addQueryItem("data", ql);
  QNetworkReply* r =
      m_net.post(req, form.toString(QUrl::FullyEncoded).toUtf8());
  // A server that does not answer quickly gets company: the next mirror
  // is asked in parallel, the first good answer wins.
  QTimer::singleShot(6000, r, [this, race, ql, done] {
    if (!race->done) post(race, ql, done);
  });
  connect(r, &QNetworkReply::finished, this, [this, r, race, server, ql, done] {
    r->deleteLater();
    --race->running;
    if (race->done) return;
    const QByteArray body = r->readAll();
    const int status =
        r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (r->error() == QNetworkReply::NoError && status == 200 &&
        body.trimmed().startsWith('{')) {
      race->done = true;
      m_goodServer = server;
      setOnline(true);
      done(body, {});
      return;
    }
    race->noNetwork &= status == 0;
    race->lastError = r->error() != QNetworkReply::NoError
                          ? r->errorString()
                          : tr("Overpass server error %1").arg(status);
    if (race->next < m_servers.size()) {
      post(race, ql, done);
      return;
    }
    if (race->running > 0) return;  // another server may still answer
    race->done = true;
    if (race->noNetwork) setOnline(false);
    done({}, race->lastError);
  });
}

bool OnlineOsm::coveredOffline(const GeoBox& box) const {
  auto& svc = OverpassService::instance();
  if (!svc.databaseReady()) return false;
  for (const auto& r : svc.regions())
    if (r.box.south <= box.south && r.box.north >= box.north &&
        r.box.west <= box.west && r.box.east >= box.east)
      return true;
  return false;
}

void OnlineOsm::queryArea(const GeoBox& box, const QString& ql, Callback done) {
  if (coveredOffline(box) || !m_enabled) {
    OverpassService::instance().query(ql, std::move(done));
    return;
  }
  // Online first; the local instance (partial coverage) as a fallback.
  query(ql, [ql, done](const QByteArray& out, const QString& err) {
    if (err.isEmpty() || !OverpassService::instance().databaseReady()) {
      done(out, err);
      return;
    }
    OverpassService::instance().query(ql, done);
  });
}

// --------------------------------------------------------------------------
// Tiles

QStringList OnlineOsm::tilesFor(const GeoBox& box) {
  QStringList out;
  const int s = int(std::floor(box.south / kTileDeg));
  const int n = int(std::floor(box.north / kTileDeg));
  const int w = int(std::floor(box.west / kTileDeg));
  const int e = int(std::floor(box.east / kTileDeg));
  for (int y = s; y <= n; ++y)
    for (int x = w; x <= e; ++x) out << QStringLiteral("%1_%2").arg(y).arg(x);
  return out;
}

GeoBox OnlineOsm::tileBox(const QString& key) {
  const QStringList p = key.split('_');
  const double s = p.value(0).toInt() * kTileDeg;
  const double w = p.value(1).toInt() * kTileDeg;
  return GeoBox{s, w, s + kTileDeg, w + kTileDeg};
}

QString OnlineOsm::cacheDir() const {
  const QString d =
      QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
      "/osm-tiles";
  QDir().mkpath(d);
  return d;
}

bool OnlineOsm::tile(const QString& key, QVector<OsmElement>* out) {
  auto it = m_tiles.constFind(key);
  if (it != m_tiles.constEnd()) {
    if (out) *out = *it;
    return true;
  }
  const QString hash = QString::fromLatin1(
      QCryptographicHash::hash(m_tileQuery.toUtf8(), QCryptographicHash::Md5)
          .toHex()
          .left(8));
  QFile f(cacheDir() + "/" + hash + "_" + key + ".json");
  if (!f.exists() || QFileInfo(f).lastModified().daysTo(
                         QDateTime::currentDateTime()) > kCacheDays)
    return false;
  if (!f.open(QIODevice::ReadOnly)) return false;
  const QVector<OsmElement> e = OverpassService::parseJson(f.readAll());
  m_tiles.insert(key, e);
  if (out) *out = e;
  return true;
}

void OnlineOsm::requestTiles(const QStringList& keys,
                             const QString& tileQuery) {
  if (tileQuery != m_tileQuery) {
    m_tileQuery = tileQuery;
    m_tiles.clear();
  }
  m_tileQueue.clear();
  for (const QString& k : keys)
    if (!m_failed.contains(k) && !tile(k, nullptr)) m_tileQueue << k;
  if (!m_tileRunning) nextTile();
}

void OnlineOsm::nextTile() {
  if (m_tileQueue.isEmpty() || !m_enabled || !isOnline()) {
    m_tileRunning = false;
    return;
  }
  m_tileRunning = true;
  const QString key = m_tileQueue.dequeue();
  const QString ql = m_tileQuery.arg(tileBox(key).overpassBbox());
  const QString queryAtStart = m_tileQuery;
  query(ql,
        [this, key, queryAtStart](const QByteArray& out, const QString& err) {
          if (queryAtStart == m_tileQuery) {
            if (err.isEmpty()) {
              const QVector<OsmElement> e = OverpassService::parseJson(out);
              m_tiles.insert(key, e);
              const QString hash = QString::fromLatin1(
                  QCryptographicHash::hash(m_tileQuery.toUtf8(),
                                           QCryptographicHash::Md5)
                      .toHex()
                      .left(8));
              QSaveFile f(cacheDir() + "/" + hash + "_" + key + ".json");
              if (f.open(QIODevice::WriteOnly)) {
                f.write(toJson(e));
                f.commit();
              }
              emit tileLoaded(key);
            } else if (isOnline()) {
              // Server trouble with this tile: do not hammer it again now.
              m_failed.insert(key);
              QTimer::singleShot(5 * 60 * 1000, this,
                                 [this, key] { m_failed.remove(key); });
            }
          }
          // Be gentle with the public servers.
          QTimer::singleShot(300, this, &OnlineOsm::nextTile);
        });
}

// --------------------------------------------------------------------------
// Nominatim

void OnlineOsm::searchPlaces(const QString& text, double nearLat,
                             double nearLon, PlacesCallback done) {
  if (!m_enabled || !isOnline() || text.trimmed().size() < 3) {
    done({});
    return;
  }
  // Nominatim's usage policy: at most one request per second. Only the
  // latest search is kept while waiting.
  if (m_searchPending) m_pendingSearch.done({});
  m_pendingSearch = Search{text.trimmed(), nearLat, nearLon, std::move(done)};
  if (m_searchPending) return;
  m_searchPending = true;
  const qint64 wait =
      m_lastSearch.isValid()
          ? std::max<qint64>(
                0, 1100 - m_lastSearch.msecsTo(QDateTime::currentDateTimeUtc()))
          : 0;
  QTimer::singleShot(int(wait), this, &OnlineOsm::runSearch);
}

void OnlineOsm::runSearch() {
  m_searchPending = false;
  m_lastSearch = QDateTime::currentDateTimeUtc();
  const Search s = m_pendingSearch;
  m_pendingSearch = {};
  QUrl url("https://nominatim.openstreetmap.org/search");
  QUrlQuery q;
  q.addQueryItem("q", s.text);
  q.addQueryItem("format", "jsonv2");
  q.addQueryItem("limit", "10");
  // Prefer results around the boat / chart, but do not restrict to it.
  q.addQueryItem("viewbox", QStringLiteral("%1,%2,%3,%4")
                                .arg(s.lon - 1.5)
                                .arg(s.lat + 1.0)
                                .arg(s.lon + 1.5)
                                .arg(s.lat - 1.0));
  url.setQuery(q);
  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
  req.setTransferTimeout(15000);
  QNetworkReply* r = m_net.get(req);
  connect(r, &QNetworkReply::finished, this, [this, r, s] {
    r->deleteLater();
    if (r->error() != QNetworkReply::NoError) {
      if (r->error() == QNetworkReply::HostNotFoundError) setOnline(false);
      s.done({});
      return;
    }
    setOnline(true);
    s.done(parseNominatim(r->readAll()));
  });
}

QVector<OsmElement> OnlineOsm::parseNominatim(const QByteArray& json) {
  QVector<OsmElement> out;
  for (const auto& v : QJsonDocument::fromJson(json).array()) {
    const QJsonObject o = v.toObject();
    OsmElement e;
    e.type = o.value("osm_type").toString();
    e.id = qint64(o.value("osm_id").toDouble());
    e.lat = o.value("lat").toString().toDouble();
    e.lon = o.value("lon").toString().toDouble();
    QString name = o.value("name").toString();
    if (name.isEmpty())
      name = o.value("display_name").toString().section(',', 0, 0);
    e.tags.insert("name", name);
    e.tags.insert("display_name", o.value("display_name").toString());
    // "category" in jsonv2 is the OSM key, "type" its value.
    const QString key = o.value("category").toString();
    if (!key.isEmpty()) e.tags.insert(key, o.value("type").toString());
    out << e;
  }
  return out;
}
