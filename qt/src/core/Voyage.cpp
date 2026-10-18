#include "core/Voyage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <cmath>

#include "core/GeoMath.h"
#include "core/Grib.h"
#include "core/osm/OnlineOsm.h"

double Voyage::lengthNm() const {
  double d = 0;
  for (int i = 1; i < route.size(); ++i)
    d += geo::distanceNm(route[i - 1].y(), route[i - 1].x(), route[i].y(),
                         route[i].x());
  return d;
}

QJsonObject Voyage::toJson() const {
  QJsonArray pts;
  for (const QPointF& p : route) pts << QJsonArray{p.x(), p.y()};
  QJsonObject fc;
  for (auto it = forecasts.begin(); it != forecasts.end(); ++it)
    fc.insert(it.key(), it.value().toJson());
  return QJsonObject{{"id", id},
                     {"name", name},
                     {"created", created.toString(Qt::ISODate)},
                     {"updated", updated.toString(Qt::ISODate)},
                     {"from", fromName},
                     {"to", toName},
                     {"route", pts},
                     {"settings", settings},
                     {"forecasts", fc},
                     {"grib", gribFile},
                     {"tiles", tiles},
                     {"regions", QJsonArray::fromStringList(regions)},
                     {"status", status}};
}

Voyage Voyage::fromJson(const QJsonObject& o) {
  Voyage v;
  v.id = o.value("id").toString();
  v.name = o.value("name").toString();
  v.created = QDateTime::fromString(o.value("created").toString(), Qt::ISODate);
  v.updated = QDateTime::fromString(o.value("updated").toString(), Qt::ISODate);
  v.fromName = o.value("from").toString();
  v.toName = o.value("to").toString();
  for (const auto& p : o.value("route").toArray()) {
    const QJsonArray a = p.toArray();
    v.route << QPointF(a.at(0).toDouble(), a.at(1).toDouble());
  }
  v.settings = o.value("settings").toObject();
  const QJsonObject fc = o.value("forecasts").toObject();
  for (auto it = fc.begin(); it != fc.end(); ++it)
    v.forecasts.insert(it.key(),
                       ModelForecast::fromJson(it.value().toObject()));
  v.gribFile = o.value("grib").toString();
  v.tiles = o.value("tiles").toInt();
  for (const auto& r : o.value("regions").toArray()) v.regions << r.toString();
  v.status = o.value("status").toString();
  return v;
}

// --------------------------------------------------------------------------

VoyageStore& VoyageStore::instance() {
  static VoyageStore s;
  return s;
}

VoyageStore::VoyageStore() { load(); }

QString VoyageStore::folder(const QString& id) const {
  const QString d =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      "/voyages/" + id;
  QDir().mkpath(d);
  return d;
}

void VoyageStore::load() {
  const QDir dir(
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      "/voyages");
  for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    QFile f(dir.filePath(sub + "/voyage.json"));
    if (!f.open(QIODevice::ReadOnly)) continue;
    const Voyage v =
        Voyage::fromJson(QJsonDocument::fromJson(f.readAll()).object());
    if (!v.id.isEmpty()) m_voyages << v;
  }
  std::sort(
      m_voyages.begin(), m_voyages.end(),
      [](const Voyage& a, const Voyage& b) { return a.created > b.created; });
}

void VoyageStore::write(const Voyage& v) const {
  QSaveFile f(folder(v.id) + "/voyage.json");
  if (f.open(QIODevice::WriteOnly)) {
    f.write(QJsonDocument(v.toJson()).toJson(QJsonDocument::Compact));
    f.commit();
  }
}

const Voyage* VoyageStore::voyage(const QString& id) const {
  for (const Voyage& v : m_voyages)
    if (v.id == id) return &v;
  return nullptr;
}

Voyage* VoyageStore::find(const QString& id) {
  for (Voyage& v : m_voyages)
    if (v.id == id) return &v;
  return nullptr;
}

QString VoyageStore::save(Voyage v) {
  const QDateTime now = QDateTime::currentDateTimeUtc();
  if (v.id.isEmpty()) {
    v.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    v.created = now;
  }
  v.updated = now;
  if (Voyage* old = find(v.id)) {
    *old = v;
  } else {
    m_voyages.prepend(v);
  }
  write(v);
  emit changed();
  return v.id;
}

void VoyageStore::remove(const QString& id) {
  QDir(folder(id)).removeRecursively();
  m_voyages.erase(std::remove_if(m_voyages.begin(), m_voyages.end(),
                                 [&](const Voyage& v) { return v.id == id; }),
                  m_voyages.end());
  emit changed();
}

QVector<GeoBox> VoyageStore::corridor(const QVector<QPointF>& route,
                                      double marginNm, double maxDeg2) {
  QVector<GeoBox> out;
  if (route.isEmpty()) return out;
  // Points every nautical mile along the route.
  QVector<QPointF> pts{route.first()};
  for (int i = 1; i < route.size(); ++i) {
    const QPointF a = route[i - 1], b = route[i];
    const int n = std::max(
        1, int(std::ceil(geo::distanceNm(a.y(), a.x(), b.y(), b.x()))));
    for (int k = 1; k <= n; ++k) pts << a + (b - a) * (double(k) / n);
  }
  const auto padded = [marginNm](GeoBox b) {
    const double dLat = marginNm / 60;
    const double dLon =
        marginNm /
        (60 * std::max(0.1, std::cos((b.north + b.south) / 2 * M_PI / 180)));
    return GeoBox{b.south - dLat, b.west - dLon, b.north + dLat, b.east + dLon};
  };
  GeoBox cur{pts[0].y(), pts[0].x(), pts[0].y(), pts[0].x()};
  for (int i = 1; i < pts.size(); ++i) {
    const QPointF p = pts[i];
    GeoBox grown{std::min(cur.south, p.y()), std::min(cur.west, p.x()),
                 std::max(cur.north, p.y()), std::max(cur.east, p.x())};
    if (padded(grown).areaDeg2() > maxDeg2) {
      out << padded(cur);
      cur = GeoBox{pts[i - 1].y(), pts[i - 1].x(), pts[i - 1].y(),
                   pts[i - 1].x()};
      grown = GeoBox{std::min(cur.south, p.y()), std::min(cur.west, p.x()),
                     std::max(cur.north, p.y()), std::max(cur.east, p.x())};
    }
    cur = grown;
  }
  out << padded(cur);
  return out;
}

// --------------------------------------------------------------------------
// Offline preparation: forecasts → GRIB copy → harbour tiles → regions

void VoyageStore::prepareOffline(const QString& id, const QString& tileQuery) {
  if (!m_preparing.isEmpty() || !find(id)) return;
  m_preparing = id;
  m_tileQuery = tileQuery;
  m_stage = 0;
  m_notes.clear();
  step();
}

void VoyageStore::step() {
  Voyage* v = find(m_preparing);
  if (!v) {
    m_preparing.clear();
    return;
  }
  const QString id = v->id;
  switch (m_stage++) {
    case 0: {
      emit progress(id, tr("Loading wind forecasts of the weather models…"), 0,
                    4);
      QStringList models;
      for (const auto& m : WeatherModels::available()) models << m.id;
      WeatherModels::instance().fetch(
          WeatherModels::samplePoints(v->route), models,
          [this, id](const QHash<QString, ModelForecast>& f,
                     const QString& err) {
            if (Voyage* vv = find(id)) {
              if (!f.isEmpty()) vv->forecasts = f;
              m_notes << (err.isEmpty() ? tr("%n weather model(s)", nullptr,
                                             int(f.size()))
                                        : tr("weather models: %1").arg(err));
            }
            step();
          });
      return;
    }
    case 1: {
      emit progress(id, tr("Storing the GRIB file…"), 1, 4);
      const auto grib = GribManager::instance().file();
      if (grib && QFileInfo::exists(grib->path())) {
        const QString name = "forecast." + QFileInfo(grib->path()).suffix();
        QFile::remove(folder(id) + "/" + name);
        if (QFile::copy(grib->path(), folder(id) + "/" + name)) {
          v->gribFile = name;
          m_notes << tr("GRIB file");
        }
      }
      step();
      return;
    }
    case 2: {
      // Harbour tiles along the route, kept without expiry.
      m_tileKeys.clear();
      for (const GeoBox& b : corridor(v->route, 5, 0.3))
        for (const QString& k : OnlineOsm::tilesFor(b))
          if (!m_tileKeys.contains(k)) m_tileKeys << k;
      auto& online = OnlineOsm::instance();
      if (!online.isEnabled() || m_tileKeys.isEmpty()) {
        step();
        return;
      }
      online.prefetchTiles(
          m_tileKeys, m_tileQuery,
          [this, id](int done, int total) {
            emit progress(id,
                          tr("Harbours along the route: %1 of %2 tiles")
                              .arg(done)
                              .arg(total),
                          2, 4);
          },
          [this, id](int ok, int failed) {
            if (Voyage* vv = find(id)) {
              vv->tiles = ok;
              m_notes << (failed ? tr("%1 of %2 harbour tiles")
                                       .arg(ok)
                                       .arg(ok + failed)
                                 : tr("%n harbour tile(s)", nullptr, ok));
            }
            step();
          });
      return;
    }
    case 3: {
      // Full OSM data (coastline, harbours) for the local Overpass
      // instance, in boxes the download servers accept.
      emit progress(id, tr("Queueing offline regions…"), 3, 4);
      auto& svc = OverpassService::instance();
      const QVector<GeoBox> boxes =
          corridor(v->route, 3, OverpassService::kMaxApiArea * 0.8);
      v->regions.clear();
      int i = 0;
      for (const GeoBox& b : boxes) {
        bool covered = false;
        for (const auto& r : svc.regions())
          covered |= r.box.south <= b.south && r.box.north >= b.north &&
                     r.box.west <= b.west && r.box.east >= b.east;
        if (covered) continue;
        const QString name = boxes.size() > 1 ? QStringLiteral("%1 (%2/%3)")
                                                    .arg(v->name)
                                                    .arg(++i)
                                                    .arg(boxes.size())
                                              : v->name;
        v->regions << name;
        svc.addRegion(name, b);
      }
      if (!v->regions.isEmpty())
        m_notes << tr("%n offline region(s) queued", nullptr,
                      int(v->regions.size()));
      step();
      return;
    }
    default: {
      v->status = m_notes.join(" · ");
      v->updated = QDateTime::currentDateTimeUtc();
      write(*v);
      m_preparing.clear();
      emit changed();
      emit prepared(id, !v->forecasts.isEmpty() || v->tiles > 0, v->status);
    }
  }
}
