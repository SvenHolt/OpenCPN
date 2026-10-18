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
 * Passage logbook storage (logbook.db next to seachart.db), shared by the
 * wx GUI and the Qt6 frontend: every passage with its plan, track id,
 * regular entries and the skipper's notes.
 */

#ifndef LOGBOOK_DB_H_
#define LOGBOOK_DB_H_

#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

namespace SQLite {
class Database;
}

namespace seachart {

struct Trip {
  int64_t id = 0;
  std::string name, from, to;
  double from_lat = 0, from_lon = 0, to_lat = 0, to_lon = 0;
  time_t planned_departure = 0, planned_arrival = 0;
  double planned_nm = 0;
  std::string plan;  ///< legs of the plan as text
  std::string weather;
  std::string route_guid, track_guid;
  time_t started = 0, ended = 0;  ///< ended == 0: passage under way
  double distance_nm = 0, max_sog = 0, motor_nm = 0;
  std::string notes;

  double Hours() const {
    return ((ended ? ended : std::time(nullptr)) - started) / 3600.0;
  }
};

struct LogEntry {
  time_t time = 0;
  double lat = 0, lon = 0, sog = 0, cog = 0;
  double wind_kn = -1, wind_dir = 0;  ///< forecast at that place and time
  std::string event;
};

class LogbookDb {
public:
  LogbookDb();
  ~LogbookDb();

  bool Open(const std::string &path, std::string *error = nullptr);
  bool IsOpen() const { return m_db != nullptr; }
  const std::string &Path() const { return m_path; }

  int64_t AddTrip(Trip &trip);
  bool UpdateTrip(const Trip &trip);
  bool DeleteTrip(int64_t id);
  std::vector<Trip> Trips();
  bool GetTrip(int64_t id, Trip &trip);
  /** The passage currently under way, 0 if none. */
  int64_t ActiveTrip();

  bool AddEntry(int64_t trip, const LogEntry &e);
  std::vector<LogEntry> Entries(int64_t trip);

  const std::string &LastError() const { return m_error; }

private:
  std::unique_ptr<SQLite::Database> m_db;
  std::string m_path, m_error;
};

/** logbook.db in the same folder as the given seachart.db. */
std::string LogbookPathFor(const std::string &seachart_db);

}  // namespace seachart

#endif  // LOGBOOK_DB_H_
