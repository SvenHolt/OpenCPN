// Passage planning from harbour to harbour, the user's harbour and anchorage
// list and the passage logbook. The data lives in the seachart database
// (tools/seachart) and logbook.db next to it, shared with the wx GUI.
#pragma once

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QVector>
#include <memory>

#include "logbook_db.h"
#include "passage_router.h"
#include "seachart_db.h"

class PassageService : public QObject {
  Q_OBJECT
public:
  static PassageService& instance();

  /** <Documents>/seachart/seachart.db, as used by tools/seachart. */
  static QString defaultDatabasePath();
  QString databasePath() const;
  bool openDatabase(const QString& path, QString* error = nullptr);
  bool isOpen() const { return m_db.IsOpen(); }
  /** Opens (and creates) the database at databasePath() if needed. */
  bool ensureOpen();
  seachart::SeachartDb& db() { return m_db; }

  // -- harbours and anchorages ------------------------------------------
  const QVector<seachart::Harbour>& harbours() const { return m_harbours; }
  const seachart::Harbour* harbour(qint64 id) const;
  /** Nearest harbour within maxNm, or nullptr. */
  const seachart::Harbour* nearest(double lat, double lon,
                                   double maxNm = 1.0) const;
  bool saveHarbour(seachart::Harbour& h);
  bool removeHarbour(qint64 id);
  bool addPhoto(qint64 id, const QString& file, const QString& caption,
                QString* error = nullptr);
  bool removePhoto(qint64 photoId);
  void reload();

  // -- weather ----------------------------------------------------------
  /** The loaded GRIB file if it covers the time, else the offline
   *  Open-Meteo forecast of the database. */
  const seachart::Weather& weatherFor(const QDateTime& time);
  QString weatherSource() const { return m_weatherSource; }
  /** Converts the GRIB file of the Weather page (empty if none). */
  static seachart::Weather fromGrib();

  // -- boat and planning ------------------------------------------------
  seachart::BoatModel boat() const { return m_boat; }
  void setBoat(const seachart::BoatModel& b);
  /** The boat model adjusted to the sail plan of the Sails page. */
  seachart::BoatModel effectiveBoat() const;

  seachart::PassagePlan plan(qint64 fromId, qint64 toId,
                             const QDateTime& departure);
  const seachart::PassagePlan& currentPlan() const { return m_plan; }
  /** Route created from the current plan (empty if none). */
  QString routeId() const { return m_routeId; }
  qint64 planFrom() const { return m_planFrom; }
  qint64 planTo() const { return m_planTo; }
  void clearPlan();
  /** Adds the current plan as a route; returns the route id. */
  QString createRoute();

  // -- passage and logbook ----------------------------------------------
  bool underway() const { return m_trip != 0; }
  qint64 tripId() const { return m_trip; }
  /** Creates the route if needed, activates it, starts the track
   *  recording and the logbook. */
  bool startPassage();
  /** Ends the passage; returns the trip id. */
  qint64 stopPassage();
  seachart::LogbookDb& logbook() { return m_logbook; }
  void addLogEvent(const QString& text);

  /** Plain text report of a trip (also used for the export). */
  QString tripReport(qint64 tripId);

  static QString durationText(double hours);

signals:
  void databaseChanged();
  void harboursChanged();
  void planChanged();
  void passageChanged();

private:
  PassageService();
  void loadSettings();
  void onTimer();
  seachart::LogEntry entryNow(const QString& event);

  QString m_path;
  seachart::SeachartDb m_db;
  seachart::LogbookDb m_logbook;
  QVector<seachart::Harbour> m_harbours;
  seachart::Weather m_dbWeather, m_gribWeather, m_noWeather;
  QString m_weatherSource;
  seachart::BoatModel m_boat;
  seachart::PassagePlan m_plan;
  qint64 m_planFrom = 0, m_planTo = 0;
  QString m_routeId;

  qint64 m_trip = 0;
  QTimer m_timer;
  double m_lastLat = 0, m_lastLon = 0, m_distance = 0, m_maxSog = 0;
  bool m_havePos = false;
  QDateTime m_lastEntry;
};
