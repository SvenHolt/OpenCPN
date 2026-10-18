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
 * Implement logbook.h -- passage logbook.
 */

#include <cmath>

#include <wx/button.h>
#include <wx/ffile.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "model/gui_vars.h"
#include "model/own_ship.h"
#include "model/route.h"
#include "model/routeman.h"
#include "model/track.h"

#include "logbook.h"
#include "html_style.h"
#include "logbook_db.h"
#include "passage_planner_dlg.h"
#include "seachart_db.h"
#include "top_frame.h"

namespace logbook {

static wxString U(const std::string &s) { return wxString::FromUTF8(s); }
static std::string S(const wxString &s) { return s.ToStdString(wxConvUTF8); }

wxString FormatDuration(double hours) {
  if (hours < 0) hours = 0;
  long minutes = std::lround(hours * 60);
  if (minutes >= 24 * 60)
    return wxString::Format("%ldd %ldh %02ldm", minutes / 1440,
                            minutes / 60 % 24, minutes % 60);
  return wxString::Format("%ldh %02ldm", minutes / 60, minutes % 60);
}

wxString FormatTime(time_t t) {
  if (t <= 0) return "-";
  return wxDateTime(t).Format("%a %d.%m. %H:%M");
}

// ------------------------------------------------------------------ Logbook
// The storage lives in libs/seachart (shared with the Qt6 frontend); this
// adapter converts between its UTF-8 records and wxString.

Logbook &Logbook::Get() {
  static Logbook instance;
  return instance;
}

Logbook::Logbook() : m_db(std::make_unique<seachart::LogbookDb>()) {}
Logbook::~Logbook() = default;

bool Logbook::Open() {
  // logbook.db lies next to seachart.db, so all frontends share it
  std::string path = seachart::LogbookPathFor(SeachartDatabase().Path());
  if (m_db->IsOpen() && m_db->Path() == path) return true;
  m_path = U(path);
  std::string err;
  if (!m_db->Open(path, &err)) {
    wxLogMessage("Logbook: cannot open %s: %s", m_path, U(err));
    return false;
  }
  return true;
}

static seachart::Trip ToDb(const Trip &t) {
  seachart::Trip d;
  d.id = t.id;
  d.name = S(t.name);
  d.from = S(t.from);
  d.to = S(t.to);
  d.from_lat = t.from_lat;
  d.from_lon = t.from_lon;
  d.to_lat = t.to_lat;
  d.to_lon = t.to_lon;
  d.planned_departure = t.planned_departure;
  d.planned_arrival = t.planned_arrival;
  d.planned_nm = t.planned_nm;
  d.plan = S(t.plan);
  d.weather = S(t.weather);
  d.route_guid = S(t.route_guid);
  d.track_guid = S(t.track_guid);
  d.started = t.started;
  d.ended = t.ended;
  d.distance_nm = t.distance_nm;
  d.max_sog = t.max_sog;
  d.motor_nm = t.motor_nm;
  d.notes = S(t.notes);
  return d;
}

static Trip FromDb(const seachart::Trip &d) {
  Trip t;
  t.id = d.id;
  t.name = U(d.name);
  t.from = U(d.from);
  t.to = U(d.to);
  t.from_lat = d.from_lat;
  t.from_lon = d.from_lon;
  t.to_lat = d.to_lat;
  t.to_lon = d.to_lon;
  t.planned_departure = d.planned_departure;
  t.planned_arrival = d.planned_arrival;
  t.planned_nm = d.planned_nm;
  t.plan = U(d.plan);
  t.weather = U(d.weather);
  t.route_guid = U(d.route_guid);
  t.track_guid = U(d.track_guid);
  t.started = d.started;
  t.ended = d.ended;
  t.distance_nm = d.distance_nm;
  t.max_sog = d.max_sog;
  t.motor_nm = d.motor_nm;
  t.notes = U(d.notes);
  return t;
}

int64_t Logbook::AddTrip(Trip &trip) {
  if (!Open()) return 0;
  seachart::Trip d = ToDb(trip);
  trip.id = m_db->AddTrip(d);
  return trip.id;
}

bool Logbook::UpdateTrip(const Trip &trip) {
  return Open() && m_db->UpdateTrip(ToDb(trip));
}

bool Logbook::DeleteTrip(int64_t id) { return Open() && m_db->DeleteTrip(id); }

std::vector<Trip> Logbook::Trips() {
  std::vector<Trip> out;
  if (!Open()) return out;
  for (const auto &d : m_db->Trips()) out.push_back(FromDb(d));
  return out;
}

bool Logbook::GetTrip(int64_t id, Trip &trip) {
  seachart::Trip d;
  if (!Open() || !m_db->GetTrip(id, d)) return false;
  trip = FromDb(d);
  return true;
}

int64_t Logbook::ActiveTrip() { return Open() ? m_db->ActiveTrip() : 0; }

bool Logbook::AddEntry(int64_t trip, const Entry &e) {
  if (!Open()) return false;
  seachart::LogEntry d;
  d.time = e.time;
  d.lat = e.lat;
  d.lon = e.lon;
  d.sog = e.sog;
  d.cog = e.cog;
  d.wind_kn = e.wind_kn;
  d.wind_dir = e.wind_dir;
  d.event = S(e.event);
  return m_db->AddEntry(trip, d);
}

std::vector<Entry> Logbook::Entries(int64_t trip) {
  std::vector<Entry> out;
  if (!Open()) return out;
  for (const auto &d : m_db->Entries(trip)) {
    Entry e;
    e.time = d.time;
    e.lat = d.lat;
    e.lon = d.lon;
    e.sog = d.sog;
    e.cog = d.cog;
    e.wind_kn = d.wind_kn;
    e.wind_dir = d.wind_dir;
    e.event = U(d.event);
    out.push_back(e);
  }
  return out;
}

// ----------------------------------------------------------------- Recorder

static seachart::Weather &CachedWeather(bool reload) {
  static seachart::Weather weather;
  static time_t loaded = 0;
  if (reload || std::time(nullptr) - loaded > 3600) {
    weather = SeachartDatabase().LoadWeather();
    loaded = std::time(nullptr);
  }
  return weather;
}

Recorder &Recorder::Get() {
  static Recorder instance;
  return instance;
}

Recorder::Recorder() : m_timer(this) {
  Bind(wxEVT_TIMER, &Recorder::OnTimer, this);
}

void Recorder::Init() {
  if (m_trip) return;
  int64_t id = Logbook::Get().ActiveTrip();
  if (!id) return;
  Trip t;
  if (!Logbook::Get().GetTrip(id, t)) return;
  m_trip = id;
  m_distance = t.distance_nm;
  m_max_sog = t.max_sog;
  m_have_pos = false;
  m_last_entry = 0;
  AddEvent(_("OpenCPN restarted, logbook continued"));
  m_timer.Start(60 * 1000);
}

Entry Recorder::Now(const wxString &event) const {
  Entry e;
  e.time = std::time(nullptr);
  e.lat = gLat;
  e.lon = gLon;
  e.sog = std::isnan(gSog) ? 0 : gSog;
  e.cog = std::isnan(gCog) ? 0 : gCog;
  e.event = event;
  if (!bGPSValid) e.event += e.event.IsEmpty() ? "[no GPS]" : " [no GPS]";
  const seachart::Weather &w = CachedWeather(false);
  if (!w.Empty() && !std::isnan(gLat)) {
    seachart::WeatherSample s = w.At(gLat, gLon, e.time);
    if (s.valid) {
      e.wind_kn = s.wind_kn;
      e.wind_dir = s.wind_dir;
    }
  }
  return e;
}

void Recorder::Start(Trip &trip) {
  if (m_trip) Stop();
  trip.started = std::time(nullptr);
  trip.ended = 0;
  if (!Logbook::Get().AddTrip(trip)) {
    wxMessageBox(
        _("The logbook could not be written:") + " " + Logbook::Get().Path(),
        _("Logbook"), wxICON_ERROR);
    return;
  }
  m_trip = trip.id;
  m_distance = 0;
  m_max_sog = 0;
  m_have_pos = false;
  CachedWeather(true);
  AddEvent(wxString::Format(_("Departure from %s"), trip.from));
  m_timer.Start(60 * 1000);
}

void Recorder::AddEvent(const wxString &text) {
  if (!m_trip) return;
  Logbook::Get().AddEntry(m_trip, Now(text));
  m_last_entry = std::time(nullptr);
}

void Recorder::OnTimer(wxTimerEvent &) {
  if (!m_trip || std::isnan(gLat) || std::isnan(gLon)) return;
  if (bGPSValid) {
    if (m_have_pos) {
      double d =
          seachart::DistanceM(m_last_lat, m_last_lon, gLat, gLon) / 1852.0;
      // ignore position jumps (GPS loss, simulator moves, ...)
      if (d < 0.5) m_distance += d;
    }
    m_last_lat = gLat;
    m_last_lon = gLon;
    m_have_pos = true;
    if (!std::isnan(gSog)) m_max_sog = std::max(m_max_sog, gSog);
  }
  if (std::time(nullptr) - m_last_entry >= interval_min * 60) {
    Logbook::Get().AddEntry(m_trip, Now(""));
    m_last_entry = std::time(nullptr);
    Trip t;
    if (Logbook::Get().GetTrip(m_trip, t)) {
      t.distance_nm = m_distance;
      t.max_sog = m_max_sog;
      if (t.track_guid.IsEmpty() && g_pActiveTrack)
        t.track_guid = g_pActiveTrack->m_GUID;
      Logbook::Get().UpdateTrip(t);
    }
  }
}

int64_t Recorder::Stop() {
  if (!m_trip) return 0;
  int64_t id = m_trip;
  Trip t;
  Logbook::Get().GetTrip(id, t);
  AddEvent(wxString::Format(_("Arrival at %s"), t.to));
  m_timer.Stop();
  m_trip = 0;
  t.ended = std::time(nullptr);
  t.distance_nm = m_distance;
  t.max_sog = m_max_sog;
  if (t.track_guid.IsEmpty() && g_pActiveTrack)
    t.track_guid = g_pActiveTrack->m_GUID;
  Logbook::Get().UpdateTrip(t);
  Route *active = g_pRouteMan->GetpActiveRoute();
  if (active && active->GetGUID() == t.route_guid)
    g_pRouteMan->DeactivateRoute();
  if (g_bTrackActive) top_frame::Get()->TrackOff(true);
  return id;
}

// -------------------------------------------------------------- LogbookDlg

LogbookDlg::LogbookDlg(wxWindow *parent)
    : wxDialog(parent, wxID_ANY, _("Logbook"), wxDefaultPosition,
               wxSize(900, 600), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
  auto *main = new wxBoxSizer(wxVERTICAL);
  m_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 200),
                          wxLC_REPORT | wxLC_SINGLE_SEL);
  const wxString cols[] = {_("Date"),     _("Passage"), _("Distance"),
                           _("Duration"), _("Planned"), _("Max. SOG")};
  for (const auto &c : cols) m_list->AppendColumn(c);
  main->Add(m_list, 0, wxEXPAND | wxALL, 8);

  auto *row = new wxBoxSizer(wxHORIZONTAL);
  m_details =
      new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                     wxTE_MULTILINE | wxTE_READONLY | wxHSCROLL);
  m_details->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE)));
  row->Add(m_details, 2, wxEXPAND | wxRIGHT, 8);
  auto *notes = new wxBoxSizer(wxVERTICAL);
  notes->Add(new wxStaticText(this, wxID_ANY, _("Notes")));
  m_notes = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                           wxTE_MULTILINE);
  notes->Add(m_notes, 1, wxEXPAND);
  m_save = new wxButton(this, wxID_ANY, _("Save notes"));
  notes->Add(m_save, 0, wxTOP, 4);
  row->Add(notes, 1, wxEXPAND);
  main->Add(row, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

  auto *btns = new wxBoxSizer(wxHORIZONTAL);
  m_track = new wxButton(this, wxID_ANY, _("Show track"));
  auto *exp = new wxButton(this, wxID_ANY, _("Export..."));
  m_delete = new wxButton(this, wxID_ANY, _("Delete"));
  btns->Add(m_track, 0, wxRIGHT, 4);
  btns->Add(exp, 0, wxRIGHT, 4);
  btns->Add(m_delete);
  btns->AddStretchSpacer();
  btns->Add(new wxButton(this, wxID_CLOSE, _("Close")));
  main->Add(btns, 0, wxEXPAND | wxALL, 8);
  SetSizer(main);

  m_list->Bind(wxEVT_LIST_ITEM_SELECTED,
               [this](wxListEvent &e) { ShowTrip(e.GetIndex()); });
  m_save->Bind(wxEVT_BUTTON, &LogbookDlg::OnSaveNotes, this);
  m_delete->Bind(wxEVT_BUTTON, &LogbookDlg::OnDelete, this);
  exp->Bind(wxEVT_BUTTON, &LogbookDlg::OnExport, this);
  m_track->Bind(wxEVT_BUTTON, &LogbookDlg::OnShowTrack, this);
  Bind(
      wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_CLOSE); },
      wxID_CLOSE);
  Fill();
  Centre();
}

void LogbookDlg::Fill() {
  m_trips = Logbook::Get().Trips();
  m_list->DeleteAllItems();
  long i = 0;
  for (const auto &t : m_trips) {
    m_list->InsertItem(i, FormatTime(t.started));
    m_list->SetItem(i, 1,
                    t.from + " - " + t.to +
                        (t.ended ? wxString() : " (" + _("under way") + ")"));
    m_list->SetItem(i, 2, wxString::Format("%.1f NM", t.distance_nm));
    m_list->SetItem(i, 3, FormatDuration(t.Hours()));
    m_list->SetItem(i, 4,
                    wxString::Format("%.1f NM / %s", t.planned_nm,
                                     FormatDuration((t.planned_arrival -
                                                     t.planned_departure) /
                                                    3600.0)));
    m_list->SetItem(i, 5, wxString::Format("%.1f kn", t.max_sog));
    i++;
  }
  for (int c = 0; c < m_list->GetColumnCount(); c++)
    m_list->SetColumnWidth(c, wxLIST_AUTOSIZE_USEHEADER);
  m_current = -1;
  m_details->Clear();
  m_notes->Clear();
  m_save->Enable(false);
  m_delete->Enable(false);
  m_track->Enable(false);
  if (!m_trips.empty()) {
    m_list->SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    ShowTrip(0);
  }
}

static wxString TripText(const Trip &t, const std::vector<Entry> &entries) {
  wxString s;
  s << t.name << "\n";
  s << _("Departure") << ": " << FormatTime(t.started) << "   " << _("Arrival")
    << ": " << (t.ended ? FormatTime(t.ended) : wxString(_("under way")))
    << "\n";
  s << wxString::Format(_("Distance %.1f NM in %s, max. %.1f kn"),
                        t.distance_nm, FormatDuration(t.Hours()), t.max_sog)
    << "\n";
  s << wxString::Format(_("Planned: %.1f NM, departure %s, arrival %s"),
                        t.planned_nm, FormatTime(t.planned_departure),
                        FormatTime(t.planned_arrival))
    << "\n";
  if (!t.weather.IsEmpty()) s << _("Weather") << ": " << t.weather << "\n";
  s << "\n" << _("Entries") << ":\n";
  for (const auto &e : entries) {
    s << wxString::Format("%-16s %8.4f %9.4f  %4.1f kn %03.0f",
                          FormatTime(e.time), e.lat, e.lon, e.sog, e.cog);
    if (e.wind_kn >= 0)
      s << wxString::Format("  %s %2.0f kn %03.0f", _("wind"), e.wind_kn,
                            e.wind_dir);
    if (!e.event.IsEmpty()) s << "  " << e.event;
    s << "\n";
  }
  if (!t.plan.IsEmpty()) s << "\n" << _("Plan") << ":\n" << t.plan << "\n";
  return s;
}

void LogbookDlg::ShowTrip(long index) {
  if (index < 0 || index >= (long)m_trips.size()) return;
  m_current = index;
  const Trip &t = m_trips[index];
  m_details->SetValue(TripText(t, Logbook::Get().Entries(t.id)));
  m_notes->SetValue(t.notes);
  m_save->Enable(true);
  m_delete->Enable(t.ended != 0);
  m_track->Enable(!t.track_guid.IsEmpty());
}

void LogbookDlg::OnSaveNotes(wxCommandEvent &) {
  if (m_current < 0) return;
  Trip t;
  if (!Logbook::Get().GetTrip(m_trips[m_current].id, t)) return;
  t.notes = m_notes->GetValue();
  Logbook::Get().UpdateTrip(t);
  m_trips[m_current].notes = t.notes;
}

void LogbookDlg::OnDelete(wxCommandEvent &) {
  if (m_current < 0) return;
  if (wxMessageBox(_("Delete this passage from the logbook?"), _("Logbook"),
                   wxYES_NO | wxICON_QUESTION) != wxYES)
    return;
  Logbook::Get().DeleteTrip(m_trips[m_current].id);
  Fill();
}

static wxString Html(const wxString &s) {
  wxString o = s;
  o.Replace("&", "&amp;");
  o.Replace("<", "&lt;");
  o.Replace(">", "&gt;");
  return o;
}

void LogbookDlg::OnExport(wxCommandEvent &) {
  wxFileDialog dlg(this, _("Export logbook"), "", "logbook.html",
                   _("HTML (all passages)") + " (*.html)|*.html|" +
                       _("CSV (entries of all passages)") + " (*.csv)|*.csv",
                   wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
  if (dlg.ShowModal() != wxID_OK) return;
  wxString out;
  if (dlg.GetFilterIndex() == 1) {
    out << "trip;passage;time_utc;lat;lon;sog_kn;cog;wind_kn;wind_dir;event\n";
    for (const auto &t : m_trips)
      for (const auto &e : Logbook::Get().Entries(t.id)) {
        wxString ev = e.event;
        ev.Replace(";", ",");
        out << wxString::Format(
            "%lld;%s - %s;%s;%.5f;%.5f;%.1f;%.0f;%s;%s;%s\n", (long long)t.id,
            t.from, t.to, wxDateTime(e.time).ToUTC().FormatISOCombined(' '),
            e.lat, e.lon, e.sog, e.cog,
            e.wind_kn >= 0 ? wxString::Format("%.1f", e.wind_kn) : "",
            e.wind_kn >= 0 ? wxString::Format("%.0f", e.wind_dir) : "", ev);
      }
  } else {
    // same look as the Qt6 frontend and the seachart harbour guide
    out << "<!doctype html><html><meta charset=utf-8><title>" << _("Logbook")
        << "</title><style>" << seachart::kHtmlStyle << "</style><header><h1>"
        << _("Logbook") << "</h1></header><main>";
    for (const auto &t : m_trips) {
      out << "<section><h2>" << Html(t.from + " - " + t.to) << "</h2>";
      if (!t.notes.IsEmpty()) out << "<p>" << Html(t.notes) << "</p>";
      out << "<pre>" << Html(TripText(t, Logbook::Get().Entries(t.id)))
          << "</pre></section>";
    }
    out << "</main></html>";
  }
  wxFFile f(dlg.GetPath(), "w");
  if (!f.IsOpened() || !f.Write(out, wxConvUTF8))
    wxMessageBox(_("Export failed."), _("Logbook"), wxICON_ERROR);
}

void LogbookDlg::OnShowTrack(wxCommandEvent &) {
  if (m_current < 0) return;
  const wxString &guid = m_trips[m_current].track_guid;
  for (Track *tr : g_TrackList) {
    if (tr->m_GUID == guid && tr->GetnPoints() > 0) {
      TrackPoint *p = tr->GetPoint(0);
      tr->SetVisible(true);
      top_frame::Get()->JumpToPosition(p->m_lat, p->m_lon);
      top_frame::Get()->RefreshAllCanvas();
      return;
    }
  }
  wxMessageBox(_("The track of this passage was not found (deleted?)."),
               _("Logbook"));
}

void ShowLogbook(wxWindow *parent) {
  LogbookDlg dlg(parent);
  dlg.ShowModal();
}

}  // namespace logbook
