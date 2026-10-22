// Logbook: one report per day (crew, weather, free text) next to the data
// of the day's legs. Stored in AppData/logbook.json; exported as Markdown.
#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QObject>

struct LogEntry {
  QDate date;
  QString crew, weather, report;
  QDateTime updated;
  bool isEmpty() const {
    return crew.trimmed().isEmpty() && weather.trimmed().isEmpty() &&
           report.trimmed().isEmpty();
  }
  QJsonObject toJson() const;
  static LogEntry fromJson(const QJsonObject& o);
};

/** Facts of a day from the recorded legs. */
struct DayFacts {
  QDate date;
  int legs = 0;
  double distanceNm = 0, hours = 0, engineHours = 0, fuelL = 0;
  double maxSog = 0, avgTws = 0, maxTws = 0, maxHeel = 0;
  double minTempC = 0, maxTempC = 0;
  QStringList places;  // from, via, to
  QString windDir;     // prevailing, e.g. "SW"
  /** One line, e.g. "Kiel – Marstal · 32.5 NM in 6:10 h · SW 12–18 kn". */
  QString text() const;
  static DayFacts of(const QDate& date);
};

class Logbook : public QObject {
  Q_OBJECT
public:
  static Logbook& instance();
  const QMap<QDate, LogEntry>& entries() const { return m_entries; }
  LogEntry entry(const QDate& date) const;
  /** Stores (an empty entry removes the day). */
  void save(const LogEntry& e);
  /** Days with an entry or a leg, newest first. */
  QVector<QDate> days() const;
  /** All days as Markdown (data and reports). */
  QString markdown() const;
  void reload();

signals:
  void changed();

private:
  Logbook();
  void load();
  void store() const;
  static QString file();
  QMap<QDate, LogEntry> m_entries;
};
