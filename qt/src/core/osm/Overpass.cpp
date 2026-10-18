#include "core/osm/Overpass.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUrlQuery>
#include <QUuid>
#include <QXmlStreamReader>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <vector>

#include "core/osm/OsmMerge.h"

namespace {
const char* kUserAgent = "OpenCPN-Qt/0.1 (offline regions)";

QString exe(const QString& name) {
#ifdef Q_OS_WIN
  return name + ".exe";
#else
  return name;
#endif
}

bool hasBinaries(const QString& dir) {
  return !dir.isEmpty() &&
         QFileInfo(dir + "/" + exe("osm3s_query")).isExecutable() &&
         QFileInfo(dir + "/" + exe("update_database")).isExecutable();
}

/** Bounding box of an OSM XML document: its <bounds>, or else the extent of
 *  most nodes (way and relation members may reach far outside an
 *  extract, so 0.5 % outliers on each side are ignored). */
GeoBox boxOf(const QByteArray& xml) {
  std::vector<double> lats, lons;
  QXmlStreamReader r(xml);
  while (!r.atEnd()) {
    if (r.readNext() != QXmlStreamReader::StartElement) continue;
    if (r.name() == u"bounds") {
      const auto a = r.attributes();
      return GeoBox{a.value("minlat").toDouble(), a.value("minlon").toDouble(),
                    a.value("maxlat").toDouble(), a.value("maxlon").toDouble()};
    }
    if (r.name() != u"node") continue;
    lats.push_back(r.attributes().value("lat").toDouble());
    lons.push_back(r.attributes().value("lon").toDouble());
  }
  if (lats.empty()) return {};
  std::sort(lats.begin(), lats.end());
  std::sort(lons.begin(), lons.end());
  const size_t lo = lats.size() / 200, hi = lats.size() - 1 - lo;
  return GeoBox{lats[lo], lons[lo], lats[hi], lons[hi]};
}
}  // namespace

QString GeoBox::overpassBbox() const {
  return QStringLiteral("%1,%2,%3,%4")
      .arg(south, 0, 'f', 6)
      .arg(west, 0, 'f', 6)
      .arg(north, 0, 'f', 6)
      .arg(east, 0, 'f', 6);
}

double GeoBox::widthNm() const {
  return (east - west) * 60 * std::cos((north + south) / 2 * M_PI / 180);
}

double GeoBox::heightNm() const { return (north - south) * 60; }

// --------------------------------------------------------------------------

OverpassService& OverpassService::instance() {
  static OverpassService s;
  return s;
}

OverpassService::OverpassService() {
  QSettings s;
  s.beginGroup("Overpass");
#ifdef Q_OS_WIN
  // osm-3s does not run on Windows: the Docker image is the default there.
  const Backend fallback = Backend::Http;
#else
  const Backend fallback = Backend::Local;
#endif
  m_backend = Backend(s.value("backend", int(fallback)).toInt());
  m_binDir = s.value("binDir").toString();
  m_httpUrl =
      s.value("httpUrl", "http://localhost:12345/api/interpreter").toString();
  m_downloadUrl =
      s.value("downloadUrl", "https://overpass-api.de/api/interpreter")
          .toString();
  s.endGroup();
  if (!hasBinaries(m_binDir)) m_binDir = detectBinDir();
  load();
  // Do not leave osm-3s processes behind.
  if (QCoreApplication::instance())
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
            [this] {
              for (QProcess* p : findChildren<QProcess*>()) {
                p->disconnect(this);
                p->kill();
                p->waitForFinished(2000);
              }
            });
}

void OverpassService::configure(Backend b, const QString& binDir,
                                const QString& httpUrl,
                                const QString& downloadUrl) {
  m_backend = b;
  m_binDir = binDir;
  m_httpUrl = httpUrl;
  m_downloadUrl = downloadUrl;
  QSettings s;
  s.beginGroup("Overpass");
  s.setValue("backend", int(b));
  s.setValue("binDir", binDir);
  s.setValue("httpUrl", httpUrl);
  s.setValue("downloadUrl", downloadUrl);
  s.endGroup();
  emit databaseChanged();
}

QString OverpassService::detectBinDir() {
  QStringList candidates;
  const QString env = qEnvironmentVariable("OPENCPN_OSM3S_BIN");
  if (!env.isEmpty()) candidates << env;
  const QString app = QCoreApplication::applicationDirPath();
  candidates << app + "/osm-3s/bin" << app + "/../libexec/osm-3s/bin"
             << QDir::homePath() + "/.local/osm-3s/bin"
             << "/usr/local/osm-3s/bin" << "/opt/osm-3s/bin";
  const QString onPath = QStandardPaths::findExecutable("osm3s_query");
  if (!onPath.isEmpty()) candidates << QFileInfo(onPath).absolutePath();
  for (const QString& c : candidates)
    if (hasBinaries(c)) return QDir(c).absolutePath();
  return {};
}

bool OverpassService::localAvailable() const { return hasBinaries(m_binDir); }

QString OverpassService::dataDir() const {
  const QString d =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      "/overpass/regions";
  QDir().mkpath(d);
  return d;
}

QString OverpassService::dbDir() const {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
         "/overpass/db";
}

bool OverpassService::databaseReady() const {
  if (m_backend == Backend::Http) return true;
  return localAvailable() && QFileInfo::exists(dbDir() + "/nodes.map");
}

// --------------------------------------------------------------------------
// Persistence

void OverpassService::load() {
  QFile f(dataDir() + "/../regions.json");
  if (!f.open(QIODevice::ReadOnly)) return;
  for (const auto& v : QJsonDocument::fromJson(f.readAll()).array()) {
    const QJsonObject o = v.toObject();
    OsmRegion r;
    r.id = o.value("id").toString();
    r.name = o.value("name").toString();
    r.box = GeoBox{o.value("south").toDouble(), o.value("west").toDouble(),
                   o.value("north").toDouble(), o.value("east").toDouble()};
    r.updated =
        QDateTime::fromString(o.value("updated").toString(), Qt::ISODate);
    r.bytes = qint64(o.value("bytes").toDouble());
    r.nodes = o.value("nodes").toInt();
    r.ways = o.value("ways").toInt();
    r.relations = o.value("relations").toInt();
    r.source = o.value("source").toString();
    if (QFileInfo::exists(dataDir() + "/" + r.id + ".osm")) m_regions << r;
  }
}

void OverpassService::save() const {
  QJsonArray a;
  for (const auto& r : m_regions)
    a << QJsonObject{{"id", r.id},
                     {"name", r.name},
                     {"south", r.box.south},
                     {"west", r.box.west},
                     {"north", r.box.north},
                     {"east", r.box.east},
                     {"updated", r.updated.toString(Qt::ISODate)},
                     {"bytes", double(r.bytes)},
                     {"nodes", r.nodes},
                     {"ways", r.ways},
                     {"relations", r.relations},
                     {"source", r.source}};
  QSaveFile f(dataDir() + "/../regions.json");
  if (f.open(QIODevice::WriteOnly)) {
    f.write(QJsonDocument(a).toJson());
    f.commit();
  }
}

// --------------------------------------------------------------------------
// Regions

void OverpassService::setBusy(bool busy) {
  if (m_busy == busy) return;
  m_busy = busy;
  emit busyChanged(busy);
}

void OverpassService::fail(const QString& message) {
  m_tileQueue.clear();
  m_tileData.clear();
  setBusy(false);
  emit error(message);
}

void OverpassService::cancel() {
  ++m_generation;
  m_tileQueue.clear();
  m_tilesTotal = 0;
  // Not busy first: the aborted reply and process finish synchronously.
  setBusy(false);
  if (m_reply) m_reply->abort();
  if (m_process) m_process->kill();
}

void OverpassService::addRegion(const QString& name, const GeoBox& box) {
  OsmRegion r;
  r.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  r.name = name.isEmpty() ? tr("Region %1").arg(m_regions.size() + 1) : name;
  r.box = box;
  startDownload(r);
}

void OverpassService::updateRegion(const QString& id) {
  for (const auto& r : m_regions)
    if (r.id == id) {
      startDownload(r);
      return;
    }
}

void OverpassService::removeRegion(const QString& id) {
  QFile::remove(dataDir() + "/" + id + ".osm");
  m_regions.erase(
      std::remove_if(m_regions.begin(), m_regions.end(),
                     [&](const OsmRegion& r) { return r.id == id; }),
      m_regions.end());
  save();
  emit regionsChanged();
  rebuildDatabase();
}

void OverpassService::renameRegion(const QString& id, const QString& name) {
  for (auto& r : m_regions)
    if (r.id == id) r.name = name;
  save();
  emit regionsChanged();
}

bool OverpassService::importFile(const QString& path, const QString& name,
                                 QString* error) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) *error = f.errorString();
    return false;
  }
  if (m_busy) {
    if (error) *error = tr("Another download is still running.");
    return false;
  }
  const QByteArray xml = f.readAll();
  if (!xml.left(4096).contains("<osm")) {
    if (error) *error = tr("Not an OpenStreetMap XML file.");
    return false;
  }
  OsmRegion r;
  r.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  r.name = name.isEmpty() ? QFileInfo(path).completeBaseName() : name;
  r.box = boxOf(xml);
  r.source = "file";
  m_current = r;
  m_tileData = {xml};
  setBusy(true);
  finishDownload();
  return true;
}

void OverpassService::startDownload(const OsmRegion& region) {
  if (m_busy) {
    emit error(tr("Another download is still running."));
    return;
  }
  m_current = region;
  m_tileData.clear();
  setBusy(true);
  if (!m_downloadUrl.isEmpty())
    downloadViaOverpass();
  else
    downloadNextTile();
}

void OverpassService::downloadViaOverpass() {
  const GeoBox& b = m_current.box;
  const QString q =
      QStringLiteral("[out:xml][timeout:300];(node(%1);<;);(._;>;);out meta;")
          .arg(b.overpassBbox());
  QNetworkRequest req{QUrl(m_downloadUrl)};
  req.setHeader(QNetworkRequest::ContentTypeHeader,
                "application/x-www-form-urlencoded");
  req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
  req.setTransferTimeout(330000);
  QUrlQuery form;
  form.addQueryItem("data", q);
  emit progress(0, 1, tr("Downloading %1 from Overpass…").arg(m_current.name));
  m_reply = m_net.post(req, form.toString(QUrl::FullyEncoded).toUtf8());
  connect(m_reply, &QNetworkReply::finished, this, [this] {
    QNetworkReply* r = m_reply;
    r->deleteLater();
    if (!m_busy) return;
    const QByteArray body = r->readAll();
    if (r->error() == QNetworkReply::NoError && body.contains("<osm")) {
      m_current.source = "overpass";
      m_tileData = {body};
      finishDownload();
      return;
    }
    // Fall back to OpenStreetMap API tiles for small areas.
    if (m_current.box.areaDeg2() > kMaxApiArea) {
      fail(tr("Overpass download failed (%1). The area is too large for the "
              "OpenStreetMap API fallback; select a smaller area or import "
              "an OSM file.")
               .arg(r->errorString()));
      return;
    }
    m_tileQueue.clear();
    for (const GeoBox& t : tiles(m_current.box, 0.025)) m_tileQueue << t;
    m_tilesTotal = int(m_tileQueue.size());
    downloadNextTile();
  });
}

QVector<GeoBox> OverpassService::tiles(const GeoBox& box, double step) {
  QVector<GeoBox> out;
  for (double s = box.south; s < box.north - 1e-9; s += step)
    for (double w = box.west; w < box.east - 1e-9; w += step)
      out << GeoBox{s, w, std::min(s + step, box.north),
                    std::min(w + step, box.east)};
  return out;
}

void OverpassService::downloadNextTile() {
  if (!m_busy) return;
  if (m_tileQueue.isEmpty() && m_tilesTotal == 0) {
    // Called directly (no Overpass download URL configured).
    if (m_current.box.areaDeg2() > kMaxApiArea) {
      fail(tr("The area is too large for the OpenStreetMap API (max. "
              "%1 square degrees).")
               .arg(kMaxApiArea));
      return;
    }
    for (const GeoBox& t : tiles(m_current.box, 0.025)) m_tileQueue << t;
    m_tilesTotal = int(m_tileQueue.size());
  }
  if (m_tileQueue.isEmpty()) {
    m_current.source = "osm-api";
    m_tilesTotal = 0;
    finishDownload();
    return;
  }
  const GeoBox t = m_tileQueue.head();
  emit progress(m_tilesTotal - int(m_tileQueue.size()), m_tilesTotal,
                tr("Downloading %1: tile %2 of %3")
                    .arg(m_current.name)
                    .arg(m_tilesTotal - m_tileQueue.size() + 1)
                    .arg(m_tilesTotal));
  QUrl url("https://api.openstreetmap.org/api/0.6/map");
  url.setQuery(QStringLiteral("bbox=%1,%2,%3,%4")
                   .arg(t.west, 0, 'f', 6)
                   .arg(t.south, 0, 'f', 6)
                   .arg(t.east, 0, 'f', 6)
                   .arg(t.north, 0, 'f', 6));
  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
  req.setTransferTimeout(120000);
  m_reply = m_net.get(req);
  connect(m_reply, &QNetworkReply::finished, this, [this] {
    QNetworkReply* r = m_reply;
    r->deleteLater();
    if (!m_busy) return;
    const int status =
        r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const GeoBox t = m_tileQueue.dequeue();
    if (status == 400 && (t.north - t.south) > 0.004) {
      // Too many nodes in this tile: split it into four.
      const double hs = (t.north - t.south) / 2, hw = (t.east - t.west) / 2;
      for (const GeoBox& q : tiles(t, std::max(hs, hw))) m_tileQueue << q;
      m_tilesTotal += 3;
      downloadNextTile();
      return;
    }
    if (r->error() != QNetworkReply::NoError) {
      fail(tr("Download failed: %1").arg(r->errorString()));
      return;
    }
    m_tileData << r->readAll();
    downloadNextTile();
  });
}

struct OsmMergeResult {
  QString error;
  qint64 bytes = 0;
  int nodes = 0, ways = 0, relations = 0;
};

namespace {
/** Merges OSM documents (in memory and files) into one file. Runs in a
 *  worker thread: regions can be hundreds of megabytes. */
OsmMergeResult mergeToFile(const QVector<QByteArray>& docs,
                           const QStringList& files, const QString& out) {
  OsmMergeResult res;
  OsmMerger merger;
  for (const QByteArray& d : docs)
    if (!merger.add(d, &res.error)) return res;
  for (const QString& path : files) {
    QFile f(path);
    if (f.open(QIODevice::ReadOnly) && !merger.add(f.readAll(), &res.error))
      return res;
  }
  QSaveFile f(out);
  if (!f.open(QIODevice::WriteOnly)) {
    res.error = f.errorString();
    return res;
  }
  merger.write(&f);
  if (!f.commit()) {
    res.error = f.errorString();
    return res;
  }
  res.bytes = QFileInfo(out).size();
  res.nodes = int(merger.nodeCount());
  res.ways = int(merger.wayCount());
  res.relations = int(merger.relationCount());
  return res;
}
}  // namespace

void OverpassService::runMerge(
    QVector<QByteArray> docs, QStringList files, const QString& out,
    std::function<void(const OsmMergeResult&)> done) {
  auto* watcher = new QFutureWatcher<OsmMergeResult>(this);
  const int generation = m_generation;
  connect(watcher, &QFutureWatcherBase::finished, this,
          [this, watcher, done, generation] {
            watcher->deleteLater();
            // Dropped when the operation was cancelled meanwhile.
            if (generation == m_generation) done(watcher->result());
          });
  watcher->setFuture(
      QtConcurrent::run(mergeToFile, std::move(docs), std::move(files), out));
}

void OverpassService::finishDownload() {
  emit progress(0, 0, tr("Merging %1…").arg(m_current.name));
  const QVector<QByteArray> docs = std::move(m_tileData);
  m_tileData.clear();
  runMerge(docs, {}, dataDir() + "/" + m_current.id + ".osm",
           [this](const OsmMergeResult& res) {
             if (!m_busy) return;  // cancelled
             if (!res.error.isEmpty()) {
               fail(tr("Invalid OSM data: %1").arg(res.error));
               return;
             }
             m_current.bytes = res.bytes;
             m_current.nodes = res.nodes;
             m_current.ways = res.ways;
             m_current.relations = res.relations;
             m_current.updated = QDateTime::currentDateTimeUtc();
             bool replaced = false;
             for (auto& r : m_regions)
               if (r.id == m_current.id) {
                 r = m_current;
                 replaced = true;
               }
             if (!replaced) m_regions << m_current;
             save();
             emit regionsChanged();
             importDatabase();
           });
}

void OverpassService::rebuildDatabase() {
  if (m_busy) return;
  setBusy(true);
  importDatabase();
}

void OverpassService::importDatabase() {
  // The combined file is also what a Docker instance imports.
  const QString combined = dataDir() + "/../combined.osm";
  emit progress(0, 0, tr("Preparing Overpass import…"));
  QStringList files;
  for (const auto& r : m_regions) files << dataDir() + "/" + r.id + ".osm";
  runMerge({}, files, combined, [this, combined](const OsmMergeResult& res) {
    if (!m_busy) return;
    if (!res.error.isEmpty()) {
      fail(res.error);
      return;
    }
    runImport(combined);
  });
}

void OverpassService::runImport(const QString& combined) {
  if (m_backend == Backend::Http || !localAvailable()) {
    setBusy(false);
    emit databaseChanged();
    if (m_backend == Backend::Local)
      emit error(
          tr("osm-3s (Overpass) was not found. Install it or set its "
             "bin folder in the offline settings."));
    return;
  }
  QDir(dbDir()).removeRecursively();
  QDir().mkpath(dbDir());
  if (m_regions.isEmpty()) {
    setBusy(false);
    emit databaseChanged();
    return;
  }
  emit progress(0, 0, tr("Importing into the local Overpass database…"));
  auto* p = new QProcess(this);
  m_process = p;
  p->setProgram(m_binDir + "/" + exe("update_database"));
  // Default compression: works with every osm-3s build (lz4 is optional).
  p->setArguments({"--db-dir=" + dbDir() + "/", "--meta=no"});
  p->setStandardInputFile(combined);
  p->setProcessChannelMode(QProcess::MergedChannels);
  const int generation = m_generation;
  connect(p, &QProcess::finished, this,
          [this, p, generation](int code, QProcess::ExitStatus status) {
            const QByteArray log = p->readAll();
            p->deleteLater();
            m_process = nullptr;
            if (generation != m_generation) {
              // Cancelled: a half written database is useless.
              QDir(dbDir()).removeRecursively();
              emit databaseChanged();
              return;
            }
            setBusy(false);
            if (status != QProcess::NormalExit || code != 0) {
              QDir(dbDir()).removeRecursively();
              emit error(tr("Overpass import failed: %1")
                             .arg(QString::fromLocal8Bit(log.right(400))));
            }
            emit databaseChanged();
          });
  connect(p, &QProcess::errorOccurred, this,
          [this, p](QProcess::ProcessError e) {
            if (e != QProcess::FailedToStart) return;
            p->deleteLater();
            m_process = nullptr;
            setBusy(false);
            emit error(
                tr("Cannot start update_database: %1").arg(p->errorString()));
          });
  p->start();
}

// --------------------------------------------------------------------------
// Queries

void OverpassService::query(const QString& ql, Callback done) {
  if (m_backend == Backend::Http) {
    QNetworkRequest req{QUrl(m_httpUrl)};
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "application/x-www-form-urlencoded");
    req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    QUrlQuery form;
    form.addQueryItem("data", ql);
    QNetworkReply* r =
        m_net.post(req, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(r, &QNetworkReply::finished, this, [r, done] {
      r->deleteLater();
      if (r->error() != QNetworkReply::NoError)
        done({}, r->errorString());
      else
        done(r->readAll(), {});
    });
    return;
  }
  if (m_process) {
    done({}, tr("The local Overpass database is being imported."));
    return;
  }
  if (!databaseReady()) {
    done({}, tr("The local Overpass database is empty."));
    return;
  }
  auto* p = new QProcess(this);
  p->setProgram(m_binDir + "/" + exe("osm3s_query"));
  p->setArguments({"--db-dir=" + dbDir() + "/"});
  connect(p, &QProcess::finished, this,
          [p, done](int code, QProcess::ExitStatus) {
            const QByteArray out = p->readAllStandardOutput();
            const QByteArray err = p->readAllStandardError();
            p->deleteLater();
            // osm3s_query exits with 0 on parse errors; it then prints
            // nothing on stdout and the messages on stderr.
            if (code != 0 || out.trimmed().isEmpty())
              done({}, QString::fromLocal8Bit(err.right(400)));
            else
              done(out, {});
          });
  connect(p, &QProcess::errorOccurred, this,
          [p, done](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) {
              done({}, p->errorString());
              p->deleteLater();
            }
          });
  p->start();
  p->write(ql.toUtf8());
  p->closeWriteChannel();
}

QVector<OsmElement> OverpassService::parseJson(const QByteArray& json,
                                               QString* error) {
  QVector<OsmElement> out;
  // osm3s_query may print a few lines before the JSON document.
  const qsizetype start = json.indexOf('{');
  QJsonParseError pe;
  const QJsonDocument doc =
      QJsonDocument::fromJson(start >= 0 ? json.mid(start) : json, &pe);
  if (doc.isNull()) {
    if (error) *error = pe.errorString();
    return out;
  }
  for (const auto& v : doc.object().value("elements").toArray()) {
    const QJsonObject o = v.toObject();
    OsmElement e;
    e.type = o.value("type").toString();
    e.id = qint64(o.value("id").toDouble());
    // "out geom": the coordinates of a way.
    for (const auto& g : o.value("geometry").toArray()) {
      const QJsonObject c = g.toObject();
      e.geometry << QPointF(c.value("lon").toDouble(),
                            c.value("lat").toDouble());
    }
    if (o.contains("lat")) {
      e.lat = o.value("lat").toDouble();
      e.lon = o.value("lon").toDouble();
    } else if (!e.geometry.isEmpty()) {
      e.lat = e.geometry.first().y();
      e.lon = e.geometry.first().x();
    } else if (o.contains("center")) {
      const QJsonObject c = o.value("center").toObject();
      e.lat = c.value("lat").toDouble();
      e.lon = c.value("lon").toDouble();
    } else {
      continue;
    }
    const QJsonObject tags = o.value("tags").toObject();
    for (auto it = tags.begin(); it != tags.end(); ++it)
      e.tags.insert(it.key(), it.value().toString());
    out << e;
  }
  return out;
}
