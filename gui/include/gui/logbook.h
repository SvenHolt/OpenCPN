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
 * Logbook: every passage started in the passage planner is stored with its
 * plan, the track, regular position/speed/weather entries and the user's
 * notes in logbook.db in the private data directory.
 */

#ifndef LOGBOOK_H_
#define LOGBOOK_H_

#include <cstdint>
#include <ctime>
#include <memory>
#include <vector>

#include <wx/dialog.h>
#include <wx/event.h>
#include <wx/string.h>
#include <wx/timer.h>

namespace SQLite {
class Database;
}
class wxListCtrl;
class wxTextCtrl;
class wxButton;

namespace logbook {

struct Trip {
  int64_t id = 0;
  wxString name, from, to;
  double from_lat = 0, from_lon = 0, to_lat = 0, to_lon = 0;
  time_t planned_departure = 0, planned_arrival = 0;
  double planned_nm = 0;
  wxString plan;  ///< legs of the plan as text
  wxString weather;
  wxString route_guid, track_guid;
  time_t started = 0, ended = 0;  ///< ended == 0: passage under way
  double distance_nm = 0, max_sog = 0, motor_nm = 0;
  wxString notes;

  double Hours() const {
    return ((ended ? ended : std::time(nullptr)) - started) / 3600.0;
  }
};

struct Entry {
  time_t time = 0;
  double lat = 0, lon = 0, sog = 0, cog = 0;
  double wind_kn = -1, wind_dir = 0;  ///< forecast at that place and time
  wxString event;
};

class Logbook {
public:
  static Logbook &Get();
  bool Open();
  wxString Path() const { return m_path; }

  int64_t AddTrip(Trip &trip);
  bool UpdateTrip(const Trip &trip);
  bool DeleteTrip(int64_t id);
  std::vector<Trip> Trips();
  bool GetTrip(int64_t id, Trip &trip);
  /** The passage currently under way, 0 if none. */
  int64_t ActiveTrip();

  bool AddEntry(int64_t trip, const Entry &e);
  std::vector<Entry> Entries(int64_t trip);

private:
  Logbook();
  ~Logbook();
  std::unique_ptr<SQLite::Database> m_db;
  wxString m_path;
};

/** Writes entries for the active passage while OpenCPN runs. */
class Recorder : public wxEvtHandler {
public:
  static Recorder &Get();
  /** Resume a passage left open when OpenCPN was closed. */
  void Init();
  bool Active() const { return m_trip != 0; }
  int64_t TripId() const { return m_trip; }
  void Start(Trip &trip);
  /** End the passage; returns the trip id. */
  int64_t Stop();
  void AddEvent(const wxString &text);

  int interval_min = 15;  ///< regular entries every n minutes

private:
  Recorder();
  void OnTimer(wxTimerEvent &);
  Entry Now(const wxString &event) const;

  wxTimer m_timer;
  int64_t m_trip = 0;
  double m_last_lat = 0, m_last_lon = 0, m_distance = 0, m_max_sog = 0;
  bool m_have_pos = false;
  time_t m_last_entry = 0;
};

class LogbookDlg : public wxDialog {
public:
  explicit LogbookDlg(wxWindow *parent);

private:
  void Fill();
  void ShowTrip(long index);
  void OnSaveNotes(wxCommandEvent &);
  void OnDelete(wxCommandEvent &);
  void OnExport(wxCommandEvent &);
  void OnShowTrack(wxCommandEvent &);

  std::vector<Trip> m_trips;
  wxListCtrl *m_list;
  wxTextCtrl *m_details, *m_notes;
  wxButton *m_save, *m_delete, *m_track;
  long m_current = -1;
};

void ShowLogbook(wxWindow *parent);
/** Format helpers shared with the passage planner. */
wxString FormatDuration(double hours);
wxString FormatTime(time_t t);

}  // namespace logbook

#endif  // LOGBOOK_H_
