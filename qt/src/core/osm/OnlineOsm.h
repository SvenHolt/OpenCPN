// Online OpenStreetMap data while a network connection is available:
// harbours and coastlines from public Overpass servers (with fallback
// mirrors and a tile cache on disk) and place search via Nominatim.
// Offline regions in the local Overpass instance take precedence.
#pragma once

#include <QDateTime>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <functional>
#include <memory>

#include "core/osm/Overpass.h"

class OnlineOsm : public QObject {
  Q_OBJECT
public:
  static OnlineOsm& instance();

  /** Online loading switched on in the settings. */
  bool isEnabled() const { return m_enabled; }
  void setEnabled(bool on);
  /** False after requests failed for lack of a connection (or if the
   *  system reports no network); retried after a while. */
  bool isOnline() const;
  QStringList servers() const { return m_servers; }
  void setServers(const QStringList& urls);

  using Callback = OverpassService::Callback;
  /** Runs an Overpass QL query on the public servers (failover). */
  void query(const QString& ql, Callback done);

  /** Runs a query on the best source for an area: the local instance when
   *  an offline region covers it, else the public servers (if enabled). */
  void queryArea(const GeoBox& box, const QString& ql, Callback done);
  bool coveredOffline(const GeoBox& box) const;

  // --- Tiles for the chart layer -------------------------------------------
  static constexpr double kTileDeg = 0.1;
  /** Tile keys ("lat_lon" of the south west corner, in tenths) of a box. */
  static QStringList tilesFor(const GeoBox& box);
  static GeoBox tileBox(const QString& key);
  /** Elements of a tile if loaded (memory or disk cache, max. 14 days). */
  bool tile(const QString& key, QVector<OsmElement>* out);
  /** Loads missing tiles one after the other; emits tileLoaded. Only the
   *  latest request is kept, older pending tiles are dropped. */
  void requestTiles(const QStringList& keys, const QString& tileQuery);

  // --- Place search --------------------------------------------------------
  using PlacesCallback = std::function<void(const QVector<OsmElement>&)>;
  /** Nominatim search near a position (1 request per second at most). */
  void searchPlaces(const QString& text, double nearLat, double nearLon,
                    PlacesCallback done);
  /** Parses a Nominatim jsonv2 answer. */
  static QVector<OsmElement> parseNominatim(const QByteArray& json);

signals:
  void tileLoaded(const QString& key);
  void onlineChanged(bool online);

private:
  OnlineOsm();
  void post(std::shared_ptr<struct OsmQueryRace> race, const QString& ql,
            Callback done);
  void setOnline(bool on);
  void nextTile();
  void runSearch();
  QString cacheDir() const;

  QNetworkAccessManager m_net;
  bool m_enabled = true;
  bool m_online = true;
  QDateTime m_offlineSince;
  QStringList m_servers;
  int m_goodServer = 0;

  QHash<QString, QVector<OsmElement>> m_tiles;
  QQueue<QString> m_tileQueue;
  QSet<QString> m_failed;
  QString m_tileQuery;
  bool m_tileRunning = false;

  struct Search {
    QString text;
    double lat = 0, lon = 0;
    PlacesCallback done;
  };
  Search m_pendingSearch;
  bool m_searchPending = false;
  QDateTime m_lastSearch;
};
