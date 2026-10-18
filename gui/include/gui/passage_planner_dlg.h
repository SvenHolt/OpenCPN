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
 * Passage planner: plan from harbour/anchorage to harbour/anchorage with
 * the offline weather forecast, create the route, start and stop the
 * passage (track + logbook).  Also the editor for the user's harbour and
 * anchorage list with photos, nearby POIs and the forecast.
 */

#ifndef PASSAGE_PLANNER_DLG_H_
#define PASSAGE_PLANNER_DLG_H_

#include <memory>
#include <vector>

#include <wx/dialog.h>
#include <wx/string.h>

#include "passage_router.h"
#include "seachart_db.h"

class wxButton;
class wxCheckBox;
class wxChoice;
class wxDatePickerCtrl;
class wxListCtrl;
class wxSpinCtrlDouble;
class wxStaticBitmap;
class wxStaticText;
class wxTextCtrl;
class wxTimePickerCtrl;
class wxListBox;
class wxSpinCtrl;
class Route;

/** Show the (single, modeless) passage planner. */
void ShowPassagePlanner(wxWindow *parent);
/** Open the harbour editor for a new entry at the given position. */
void ShowNewHarbourDialog(wxWindow *parent, double lat, double lon);

/** The shared database (opened on first use). */
seachart::SeachartDb &SeachartDatabase();
wxString SeachartDefaultPath();

class HarbourDlg : public wxDialog {
public:
  HarbourDlg(wxWindow *parent, seachart::SeachartDb &db,
             const seachart::Harbour &harbour);
  const seachart::Harbour &GetHarbour() const { return m_h; }

private:
  void Load();
  bool Store();
  void OnOk(wxCommandEvent &);
  void OnAddPhoto(wxCommandEvent &);
  void OnRemovePhoto(wxCommandEvent &);
  void ShowPhoto(int index);
  void FillPhotos();

  seachart::SeachartDb &m_db;
  seachart::Harbour m_h;
  std::vector<seachart::Photo> m_photos;
  wxChoice *m_kind;
  wxTextCtrl *m_name, *m_lat, *m_lon, *m_depth, *m_berths, *m_vhf, *m_phone,
      *m_website, *m_facilities, *m_notes;
  wxSpinCtrl *m_rating;
  wxListBox *m_photoList;
  wxStaticBitmap *m_preview;
  wxButton *m_addPhoto, *m_removePhoto;
};

class HarbourListDlg : public wxDialog {
public:
  HarbourListDlg(wxWindow *parent, seachart::SeachartDb &db);
  /** Harbour chosen with "Departure"/"Destination", 0 if none. */
  int64_t ChosenId() const { return m_chosen; }
  bool ChosenAsDestination() const { return m_as_destination; }

private:
  void Fill();
  long Selected() const;
  void OnNew(wxCommandEvent &);
  void OnEdit(wxCommandEvent &);
  void OnDelete(wxCommandEvent &);
  void OnShow(wxCommandEvent &);

  seachart::SeachartDb &m_db;
  std::vector<seachart::Harbour> m_all, m_shown;
  wxTextCtrl *m_filter;
  wxListCtrl *m_list;
  int64_t m_chosen = 0;
  bool m_as_destination = false;
};

class PassagePlannerDlg : public wxDialog {
public:
  explicit PassagePlannerDlg(wxWindow *parent);
  ~PassagePlannerDlg() override;

  void Reload();
  void SelectHarbour(int64_t id, bool destination);

private:
  void BuildUi();
  void LoadSettings();
  void SaveSettings();
  seachart::BoatModel ReadBoat() const;
  wxDateTime Departure() const;
  bool Endpoints(seachart::Harbour &from, seachart::Harbour &to) const;
  seachart::PassagePlan Compute(time_t departure);
  void ShowPlan();
  void UpdateButtons();
  Route *CreateRoute();

  void OnCalculate(wxCommandEvent &);
  void OnCompare(wxCommandEvent &);
  void OnCreateRoute(wxCommandEvent &);
  void OnStart(wxCommandEvent &);
  void OnStop(wxCommandEvent &);
  void OnHarbours(wxCommandEvent &);
  void OnInfo(bool destination);
  void OnDatabase(wxCommandEvent &);
  void OnLogbook(wxCommandEvent &);
  void OnClose(wxCloseEvent &);

  std::vector<seachart::Harbour> m_harbours;
  seachart::Weather m_weather;
  seachart::PassagePlan m_plan;
  wxString m_routeGuid;  ///< route created from m_plan

  wxStaticText *m_dbInfo, *m_weatherInfo;
  wxChoice *m_from, *m_to;
  wxDatePickerCtrl *m_date;
  wxTimePickerCtrl *m_time;
  wxSpinCtrlDouble *m_hull, *m_designWind, *m_noGo, *m_motorSpeed,
      *m_motorBelow, *m_draft, *m_maxWind, *m_maxGust;
  wxCheckBox *m_motor;
  wxTextCtrl *m_summary;
  wxListCtrl *m_legs;
  wxButton *m_createRoute, *m_start, *m_stop;
};

#endif  // PASSAGE_PLANNER_DLG_H_
