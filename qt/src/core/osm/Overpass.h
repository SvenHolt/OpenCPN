// Local Overpass API instance and offline OSM regions.
//
// The skipper selects map areas ("regions"). Their raw OSM data is
// downloaded (remote Overpass server, or OpenStreetMap API tiles as a
// fallback), merged and imported into a local Overpass database with
// osm-3s' update_database. Queries then run fully offline through
// osm3s_query. Alternatively an HTTP endpoint can be used, e.g. the
// wiktorn/overpass-api Docker image on Windows and macOS.
#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QProcess>
#include <QQueue>
#include <QVector>
#include <functional>

struct GeoBox {
  double south = 0, west = 0, north = 0, east = 0;
  bool isValid() const { return north > south && east > west; }
  double areaDeg2() const { return (north - south) * (east - west); }
  bool intersects(const GeoBox& o) const {
    return !(o.west > east || o.east < west || o.south > north ||
             o.north < south);
  }
  /** "south,west,north,east" as used in Overpass QL bbox filters. */
  QString overpassBbox() const;
  /** Width/height in nautical miles. */
  double widthNm() const;
  double heightNm() const;
};

struct OsmMergeResult;

struct OsmRegion {
  QString id, name;
  GeoBox box;
  QDateTime updated;
  qint64 bytes = 0;
  int nodes = 0, ways = 0, relations = 0;
  QString source;  // "overpass" or "osm-api" or "file"
};

/** One OSM object from a query result (nodes; ways/relations by center). */
struct OsmElement {
  QString type;  // node, way, relation
  qint64 id = 0;
  double lat = 0, lon = 0;
  QHash<QString, QString> tags;
  QVector<QPointF> geometry;  // x = lon, y = lat ("out geom")
  QString name() const { return tags.value("name"); }
};

class OverpassService : public QObject {
  Q_OBJECT
public:
  enum class Backend { Local, Http };

  static OverpassService& instance();

  // --- Configuration ------------------------------------------------------
  Backend backend() const { return m_backend; }
  QString binDir() const { return m_binDir; }
  QString httpUrl() const { return m_httpUrl; }
  QString downloadUrl() const { return m_downloadUrl; }
  void configure(Backend b, const QString& binDir, const QString& httpUrl,
                 const QString& downloadUrl);
  /** Finds the osm-3s binaries (settings, bundle, PATH, ~/.local). */
  static QString detectBinDir();
  bool localAvailable() const;
  QString dataDir() const;  // region raw data
  QString dbDir() const;    // Overpass database
  bool databaseReady() const;

  // --- Regions ------------------------------------------------------------
  const QVector<OsmRegion>& regions() const { return m_regions; }
  /** Largest region accepted for the OSM API tile download (deg²). */
  static constexpr double kMaxApiArea = 0.25;
  /** Downloads a new region and rebuilds the database. */
  void addRegion(const QString& name, const GeoBox& box);
  void updateRegion(const QString& id);
  void removeRegion(const QString& id);
  void renameRegion(const QString& id, const QString& name);
  /** Imports an OSM XML file as a region (bbox from its contents). */
  bool importFile(const QString& path, const QString& name,
                  QString* error = nullptr);
  void rebuildDatabase();
  bool isBusy() const { return m_busy; }
  void cancel();

  // --- Queries ------------------------------------------------------------
  using Callback =
      std::function<void(const QByteArray& result, const QString& error)>;
  /** Runs an Overpass QL query on the configured backend. */
  void query(const QString& ql, Callback done);
  /** Parses [out:json] results ("out center" for ways/relations). */
  static QVector<OsmElement> parseJson(const QByteArray& json,
                                       QString* error = nullptr);
  /** Splits a box into OSM API tiles of at most `step` degrees. */
  static QVector<GeoBox> tiles(const GeoBox& box, double step);

signals:
  void regionsChanged();
  void busyChanged(bool busy);
  void progress(int done, int total, const QString& text);
  void databaseChanged();
  void error(const QString& message);

private:
  OverpassService();
  void load();
  void save() const;
  void startDownload(const OsmRegion& region);
  void downloadViaOverpass();
  void downloadNextTile();
  void finishDownload();
  void importDatabase();
  void runImport(const QString& combined);
  void runMerge(QVector<QByteArray> docs, QStringList files, const QString& out,
                std::function<void(const OsmMergeResult&)> done);
  void setBusy(bool busy);
  void fail(const QString& message);

  Backend m_backend = Backend::Local;
  QString m_binDir, m_httpUrl, m_downloadUrl;
  QVector<OsmRegion> m_regions;
  QNetworkAccessManager m_net;

  // Running download
  bool m_busy = false;
  int m_generation = 0;  // incremented by cancel()
  OsmRegion m_current;
  QQueue<GeoBox> m_tileQueue;
  int m_tilesTotal = 0;
  QVector<QByteArray> m_tileData;
  QPointer<QNetworkReply> m_reply;
  QPointer<QProcess> m_process;
};
