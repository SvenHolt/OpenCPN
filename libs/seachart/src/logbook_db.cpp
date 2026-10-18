/**************************************************************************
 *   Copyright (C) 2026 by the OpenCPN development team                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <https://www.gnu.org/licenses/>. *
 ***************************************************************************/

/**
 * \file
 *
 * Implement logbook_db.h -- passage logbook storage.
 */

#include <SQLiteCpp/SQLiteCpp.h>

#include "std_filesystem.h"

#include "logbook_db.h"

namespace seachart {

static const char *const kSchema = R"(
CREATE TABLE IF NOT EXISTS trips (
    id INTEGER PRIMARY KEY, name TEXT NOT NULL DEFAULT '',
    from_name TEXT NOT NULL DEFAULT '', to_name TEXT NOT NULL DEFAULT '',
    from_lat REAL, from_lon REAL, to_lat REAL, to_lon REAL,
    planned_departure INTEGER, planned_arrival INTEGER, planned_nm REAL,
    plan TEXT NOT NULL DEFAULT '', weather TEXT NOT NULL DEFAULT '',
    route_guid TEXT NOT NULL DEFAULT '', track_guid TEXT NOT NULL DEFAULT '',
    started INTEGER, ended INTEGER, distance_nm REAL DEFAULT 0,
    max_sog REAL DEFAULT 0, motor_nm REAL DEFAULT 0,
    notes TEXT NOT NULL DEFAULT '');
CREATE TABLE IF NOT EXISTS entries (
    id INTEGER PRIMARY KEY, trip_id INTEGER NOT NULL, time INTEGER NOT NULL,
    lat REAL, lon REAL, sog REAL, cog REAL, wind_kn REAL, wind_dir REAL,
    event TEXT NOT NULL DEFAULT '');
CREATE INDEX IF NOT EXISTS entries_trip ON entries (trip_id, time);
)";

static const char *const kTripCols =
    "id, name, from_name, to_name, from_lat, from_lon, to_lat, to_lon,"
    " planned_departure, planned_arrival, planned_nm, plan, weather,"
    " route_guid, track_guid, started, ended, distance_nm, max_sog,"
    " motor_nm, notes";

std::string LogbookPathFor(const std::string &seachart_db) {
  fs::path p = fs::u8path(seachart_db);
  return (p.parent_path() / "logbook.db").u8string();
}

LogbookDb::LogbookDb() = default;
LogbookDb::~LogbookDb() = default;

bool LogbookDb::Open(const std::string &path, std::string *error) {
  m_db.reset();
  m_path = path;
  try {
    fs::path p = fs::u8path(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path());
    m_db = std::make_unique<SQLite::Database>(
        path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    m_db->exec(kSchema);
  } catch (std::exception &e) {
    m_error = e.what();
    if (error) *error = m_error;
    m_db.reset();
    return false;
  }
  return true;
}

static void BindTrip(SQLite::Statement &q, const Trip &t) {
  q.bind(1, t.name);
  q.bind(2, t.from);
  q.bind(3, t.to);
  q.bind(4, t.from_lat);
  q.bind(5, t.from_lon);
  q.bind(6, t.to_lat);
  q.bind(7, t.to_lon);
  q.bind(8, (int64_t)t.planned_departure);
  q.bind(9, (int64_t)t.planned_arrival);
  q.bind(10, t.planned_nm);
  q.bind(11, t.plan);
  q.bind(12, t.weather);
  q.bind(13, t.route_guid);
  q.bind(14, t.track_guid);
  q.bind(15, (int64_t)t.started);
  if (t.ended)
    q.bind(16, (int64_t)t.ended);
  else
    q.bind(16);
  q.bind(17, t.distance_nm);
  q.bind(18, t.max_sog);
  q.bind(19, t.motor_nm);
  q.bind(20, t.notes);
}

static Trip ReadTrip(SQLite::Statement &q) {
  Trip t;
  auto txt = [&q](int i) {
    return q.getColumn(i).isNull() ? std::string() : q.getColumn(i).getString();
  };
  auto num = [&q](int i) {
    return q.getColumn(i).isNull() ? 0.0 : q.getColumn(i).getDouble();
  };
  auto tim = [&q](int i) {
    return q.getColumn(i).isNull() ? (time_t)0
                                   : (time_t)q.getColumn(i).getInt64();
  };
  t.id = q.getColumn(0).getInt64();
  t.name = txt(1);
  t.from = txt(2);
  t.to = txt(3);
  t.from_lat = num(4);
  t.from_lon = num(5);
  t.to_lat = num(6);
  t.to_lon = num(7);
  t.planned_departure = tim(8);
  t.planned_arrival = tim(9);
  t.planned_nm = num(10);
  t.plan = txt(11);
  t.weather = txt(12);
  t.route_guid = txt(13);
  t.track_guid = txt(14);
  t.started = tim(15);
  t.ended = tim(16);
  t.distance_nm = num(17);
  t.max_sog = num(18);
  t.motor_nm = num(19);
  t.notes = txt(20);
  return t;
}

int64_t LogbookDb::AddTrip(Trip &trip) {
  if (!m_db) return 0;
  try {
    SQLite::Statement q(
        *m_db,
        "INSERT INTO trips (name, from_name, to_name, from_lat, from_lon,"
        " to_lat, to_lon, planned_departure, planned_arrival, planned_nm,"
        " plan, weather, route_guid, track_guid, started, ended, distance_nm,"
        " max_sog, motor_nm, notes) VALUES"
        " (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    BindTrip(q, trip);
    q.exec();
    trip.id = m_db->getLastInsertRowid();
  } catch (std::exception &e) {
    m_error = e.what();
    return 0;
  }
  return trip.id;
}

bool LogbookDb::UpdateTrip(const Trip &trip) {
  if (!m_db) return false;
  try {
    SQLite::Statement q(
        *m_db,
        "UPDATE trips SET name=?, from_name=?, to_name=?, from_lat=?,"
        " from_lon=?, to_lat=?, to_lon=?, planned_departure=?,"
        " planned_arrival=?, planned_nm=?, plan=?, weather=?, route_guid=?,"
        " track_guid=?, started=?, ended=?, distance_nm=?, max_sog=?,"
        " motor_nm=?, notes=? WHERE id=?");
    BindTrip(q, trip);
    q.bind(21, (int64_t)trip.id);
    q.exec();
  } catch (std::exception &e) {
    m_error = e.what();
    return false;
  }
  return true;
}

bool LogbookDb::DeleteTrip(int64_t id) {
  if (!m_db) return false;
  try {
    SQLite::Statement e(*m_db, "DELETE FROM entries WHERE trip_id=?");
    e.bind(1, (int64_t)id);
    e.exec();
    SQLite::Statement t(*m_db, "DELETE FROM trips WHERE id=?");
    t.bind(1, (int64_t)id);
    t.exec();
  } catch (std::exception &e) {
    m_error = e.what();
    return false;
  }
  return true;
}

std::vector<Trip> LogbookDb::Trips() {
  std::vector<Trip> out;
  if (!m_db) return out;
  try {
    SQLite::Statement q(*m_db, std::string("SELECT ") + kTripCols +
                                   " FROM trips ORDER BY started DESC");
    while (q.executeStep()) out.push_back(ReadTrip(q));
  } catch (std::exception &e) {
    m_error = e.what();
  }
  return out;
}

bool LogbookDb::GetTrip(int64_t id, Trip &trip) {
  if (!m_db) return false;
  try {
    SQLite::Statement q(
        *m_db, std::string("SELECT ") + kTripCols + " FROM trips WHERE id=?");
    q.bind(1, (int64_t)id);
    if (q.executeStep()) {
      trip = ReadTrip(q);
      return true;
    }
  } catch (std::exception &e) {
    m_error = e.what();
  }
  return false;
}

int64_t LogbookDb::ActiveTrip() {
  if (!m_db) return 0;
  try {
    SQLite::Statement q(*m_db,
                        "SELECT id FROM trips WHERE ended IS NULL ORDER BY"
                        " started DESC LIMIT 1");
    if (q.executeStep()) return q.getColumn(0).getInt64();
  } catch (std::exception &e) {
    m_error = e.what();
  }
  return 0;
}

bool LogbookDb::AddEntry(int64_t trip, const LogEntry &e) {
  if (!m_db) return false;
  try {
    SQLite::Statement q(*m_db,
                        "INSERT INTO entries (trip_id, time, lat, lon, sog,"
                        " cog, wind_kn, wind_dir, event) VALUES"
                        " (?,?,?,?,?,?,?,?,?)");
    q.bind(1, (int64_t)trip);
    q.bind(2, (int64_t)e.time);
    q.bind(3, e.lat);
    q.bind(4, e.lon);
    q.bind(5, e.sog);
    q.bind(6, e.cog);
    if (e.wind_kn >= 0) {
      q.bind(7, e.wind_kn);
      q.bind(8, e.wind_dir);
    } else {
      q.bind(7);
      q.bind(8);
    }
    q.bind(9, e.event);
    q.exec();
  } catch (std::exception &ex) {
    m_error = ex.what();
    return false;
  }
  return true;
}

std::vector<LogEntry> LogbookDb::Entries(int64_t trip) {
  std::vector<LogEntry> out;
  if (!m_db) return out;
  try {
    SQLite::Statement q(*m_db,
                        "SELECT time, lat, lon, sog, cog, wind_kn, wind_dir,"
                        " event FROM entries WHERE trip_id=? ORDER BY time,"
                        " id");
    q.bind(1, (int64_t)trip);
    while (q.executeStep()) {
      LogEntry e;
      e.time = (time_t)q.getColumn(0).getInt64();
      e.lat = q.getColumn(1).getDouble();
      e.lon = q.getColumn(2).getDouble();
      e.sog = q.getColumn(3).getDouble();
      e.cog = q.getColumn(4).getDouble();
      e.wind_kn = q.getColumn(5).isNull() ? -1 : q.getColumn(5).getDouble();
      e.wind_dir = q.getColumn(6).getDouble();
      e.event = q.getColumn(7).getString();
      out.push_back(e);
    }
  } catch (std::exception &ex) {
    m_error = ex.what();
  }
  return out;
}

}  // namespace seachart
