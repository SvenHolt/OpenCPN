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
 * Implement passage_planner_dlg.h -- harbour to harbour passage planner.
 */

#include <algorithm>
#include <cmath>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/datectrl.h>
#include <wx/dateevt.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/listbox.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbmp.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/textctrl.h>
#include <wx/timectrl.h>
#include <wx/utils.h>

#include "model/config_vars.h"
#include "model/gui_vars.h"
#include "model/navobj_db.h"
#include "model/own_ship.h"
#include "model/route.h"
#include "model/route_point.h"
#include "model/routeman.h"
#include "model/select.h"
#include "model/track.h"

#include "logbook.h"
#include "passage_planner_dlg.h"
#include "routemanagerdialog.h"
#include "top_frame.h"

using seachart::Harbour;
using seachart::PassagePlan;

static PassagePlannerDlg *s_planner = nullptr;
static const char *const kConfigPath = "/Settings/PassagePlanner";
static const char *const kKinds[] = {"marina", "harbour", "anchorage", "buoy"};

static wxString U(const std::string &s) { return wxString::FromUTF8(s); }
static std::string S(const wxString &s) { return s.ToStdString(wxConvUTF8); }

static wxString KindLabel(const std::string &kind) {
  if (kind == "marina") return _("Marina");
  if (kind == "harbour") return _("Harbour");
  if (kind == "anchorage") return _("Anchorage");
  if (kind == "buoy") return _("Mooring buoy");
  return U(kind);
}

static wxString CategoryLabel(const std::string &c) {
  if (c == "restaurant") return _("Food & drink");
  if (c == "supermarket") return _("Shopping");
  if (c == "fuel") return _("Fuel");
  if (c == "chandlery") return _("Chandlery");
  if (c == "service") return _("Service");
  if (c == "transport") return _("Transport");
  if (c == "sight") return _("Sight");
  return U(c);
}

static wxString Compass(double deg) {
  static const char *const names[] = {"N",  "NNE", "NE", "ENE", "E",  "ESE",
                                      "SE", "SSE", "S",  "SSW", "SW", "WSW",
                                      "W",  "WNW", "NW", "NNW"};
  return names[(int)std::floor(std::fmod(deg + 11.25 + 360, 360) / 22.5) % 16];
}

wxString SeachartDefaultPath() {
  wxFileName fn(wxStandardPaths::Get().GetDocumentsDir(), "seachart.db");
  fn.AppendDir("seachart");
  return fn.GetFullPath();
}

seachart::SeachartDb &SeachartDatabase() {
  static seachart::SeachartDb db;
  if (!db.IsOpen()) {
    wxString path;
    auto *cfg = TheBaseConfig();
    cfg->SetPath(kConfigPath);
    cfg->Read("Database", &path, SeachartDefaultPath());
    db.Open(S(path));
  }
  return db;
}

// --------------------------------------------------------------- HarbourDlg

HarbourDlg::HarbourDlg(wxWindow *parent, seachart::SeachartDb &db,
                       const Harbour &harbour)
    : wxDialog(parent, wxID_ANY,
               harbour.id ? _("Harbour / anchorage") + " - " + U(harbour.name)
                          : _("New harbour / anchorage"),
               wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_db(db),
      m_h(harbour) {
  auto *top = new wxBoxSizer(wxHORIZONTAL);
  auto *left = new wxFlexGridSizer(2, 4, 6);
  left->AddGrowableCol(1);
  auto add = [&](const wxString &label, wxWindow *w) {
    left->Add(new wxStaticText(this, wxID_ANY, label), 0,
              wxALIGN_CENTER_VERTICAL);
    left->Add(w, 1, wxEXPAND);
  };
  m_kind = new wxChoice(this, wxID_ANY);
  for (auto k : kKinds) m_kind->Append(KindLabel(k));
  add(_("Type"), m_kind);
  add(_("Name"), m_name = new wxTextCtrl(this, wxID_ANY));
  add(_("Latitude (decimal)"), m_lat = new wxTextCtrl(this, wxID_ANY));
  add(_("Longitude (decimal)"), m_lon = new wxTextCtrl(this, wxID_ANY));
  add(_("Water depth (m)"), m_depth = new wxTextCtrl(this, wxID_ANY));
  add(_("Berths"), m_berths = new wxTextCtrl(this, wxID_ANY));
  add(_("VHF channel"), m_vhf = new wxTextCtrl(this, wxID_ANY));
  add(_("Phone"), m_phone = new wxTextCtrl(this, wxID_ANY));
  add(_("Website"), m_website = new wxTextCtrl(this, wxID_ANY));
  add(_("Facilities"), m_facilities = new wxTextCtrl(this, wxID_ANY));
  m_facilities->SetToolTip(_("e.g. fuel, water, electricity, showers"));
  add(_("Rating (0-5)"), m_rating = new wxSpinCtrl(this, wxID_ANY));
  m_rating->SetRange(0, 5);
  m_notes = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition,
                           wxSize(300, 110), wxTE_MULTILINE);
  auto *lcol = new wxBoxSizer(wxVERTICAL);
  lcol->Add(left, 0, wxEXPAND);
  lcol->Add(new wxStaticText(this, wxID_ANY, _("Notes")), 0, wxTOP, 8);
  lcol->Add(m_notes, 1, wxEXPAND);
  top->Add(lcol, 1, wxEXPAND | wxALL, 8);

  auto *right = new wxBoxSizer(wxVERTICAL);
  auto *photos = new wxStaticBoxSizer(wxVERTICAL, this, _("Photos"));
  m_photoList = new wxListBox(photos->GetStaticBox(), wxID_ANY,
                              wxDefaultPosition, wxSize(-1, 70));
  m_preview =
      new wxStaticBitmap(photos->GetStaticBox(), wxID_ANY, wxBitmap(320, 200));
  auto *pbtn = new wxBoxSizer(wxHORIZONTAL);
  m_addPhoto =
      new wxButton(photos->GetStaticBox(), wxID_ANY, _("Add photo..."));
  m_removePhoto =
      new wxButton(photos->GetStaticBox(), wxID_ANY, _("Remove photo"));
  pbtn->Add(m_addPhoto, 0, wxRIGHT, 4);
  pbtn->Add(m_removePhoto);
  photos->Add(m_photoList, 0, wxEXPAND | wxALL, 2);
  photos->Add(m_preview, 0, wxALL, 2);
  photos->Add(pbtn, 0, wxALL, 2);
  right->Add(photos, 0, wxEXPAND);

  // nearby POIs and the forecast (read only)
  auto *near = new wxStaticBoxSizer(wxVERTICAL, this, _("Nearby (1.5 km)"));
  auto *pois = new wxListCtrl(near->GetStaticBox(), wxID_ANY, wxDefaultPosition,
                              wxSize(360, 120), wxLC_REPORT | wxLC_SINGLE_SEL);
  pois->AppendColumn(_("Type"));
  pois->AppendColumn(_("Name"), wxLIST_FORMAT_LEFT, 140);
  pois->AppendColumn(_("Distance"), wxLIST_FORMAT_RIGHT);
  pois->AppendColumn(_("Opening hours"), wxLIST_FORMAT_LEFT, 140);
  if (m_h.id) {
    long row = 0;
    for (const auto &p : m_db.PoisNear(m_h.lat, m_h.lon, 1500)) {
      pois->InsertItem(row, CategoryLabel(p.category));
      pois->SetItem(row, 1, U(p.name));
      pois->SetItem(row, 2, wxString::Format("%.0f m", p.distance_m));
      pois->SetItem(row, 3, U(p.opening_hours));
      row++;
    }
  }
  near->Add(pois, 1, wxEXPAND | wxALL, 2);
  right->Add(near, 1, wxEXPAND | wxTOP, 6);

  auto *wxbox = new wxStaticBoxSizer(wxVERTICAL, this, _("Weather forecast"));
  auto *wxlist =
      new wxListCtrl(wxbox->GetStaticBox(), wxID_ANY, wxDefaultPosition,
                     wxSize(360, 120), wxLC_REPORT | wxLC_SINGLE_SEL);
  wxlist->AppendColumn(_("Time"), wxLIST_FORMAT_LEFT, 120);
  wxlist->AppendColumn(_("Wind"), wxLIST_FORMAT_RIGHT);
  wxlist->AppendColumn(_("Gusts"), wxLIST_FORMAT_RIGHT);
  wxlist->AppendColumn(_("Waves"), wxLIST_FORMAT_RIGHT);
  seachart::Weather weather = m_db.LoadWeather();
  if (!weather.Empty()) {
    time_t t0 = std::max(std::time(nullptr), weather.First()) / 3600 * 3600;
    long row = 0;
    for (time_t t = t0; t <= weather.Last() && row < 24; t += 3 * 3600) {
      seachart::WeatherSample w = weather.At(m_h.lat, m_h.lon, t);
      if (!w.valid) continue;
      wxlist->InsertItem(row, logbook::FormatTime(t));
      wxlist->SetItem(
          row, 1,
          wxString::Format("%.0f kn %s", w.wind_kn, Compass(w.wind_dir)));
      wxlist->SetItem(row, 2, wxString::Format("%.0f kn", w.gust_kn));
      wxlist->SetItem(
          row, 3,
          w.wave_m >= 0 ? wxString::Format("%.1f m", w.wave_m) : wxString("-"));
      row++;
    }
  }
  wxbox->Add(wxlist, 1, wxEXPAND | wxALL, 2);
  right->Add(wxbox, 1, wxEXPAND | wxTOP, 6);
  top->Add(right, 0, wxEXPAND | wxALL, 8);

  auto *main = new wxBoxSizer(wxVERTICAL);
  main->Add(top, 1, wxEXPAND);
  main->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL,
            8);
  SetSizerAndFit(main);

  Load();
  FillPhotos();
  Bind(wxEVT_BUTTON, &HarbourDlg::OnOk, this, wxID_OK);
  m_addPhoto->Bind(wxEVT_BUTTON, &HarbourDlg::OnAddPhoto, this);
  m_removePhoto->Bind(wxEVT_BUTTON, &HarbourDlg::OnRemovePhoto, this);
  m_photoList->Bind(wxEVT_LISTBOX,
                    [this](wxCommandEvent &e) { ShowPhoto(e.GetSelection()); });
  Centre();
}

void HarbourDlg::Load() {
  int k = 0;
  for (int i = 0; i < 4; i++)
    if (m_h.kind == kKinds[i]) k = i;
  m_kind->SetSelection(k);
  m_name->SetValue(U(m_h.name));
  m_lat->SetValue(wxString::Format("%.5f", m_h.lat));
  m_lon->SetValue(wxString::Format("%.5f", m_h.lon));
  m_depth->SetValue(m_h.depth >= 0 ? wxString::Format("%.1f", m_h.depth)
                                   : wxString());
  m_berths->SetValue(m_h.berths >= 0 ? wxString::Format("%d", m_h.berths)
                                     : wxString());
  m_vhf->SetValue(U(m_h.vhf));
  m_phone->SetValue(U(m_h.phone));
  m_website->SetValue(U(m_h.website));
  m_facilities->SetValue(U(m_h.facilities));
  m_rating->SetValue(m_h.rating);
  m_notes->SetValue(U(m_h.notes));
}

static bool ParseNumber(const wxString &text, double &out) {
  wxString t = text;
  t.Trim().Trim(false);
  t.Replace(",", ".");
  return t.ToCDouble(&out);
}

bool HarbourDlg::Store() {
  double lat, lon;
  if (m_name->GetValue().Trim().IsEmpty()) {
    wxMessageBox(_("Please enter a name."), _("Harbour / anchorage"));
    return false;
  }
  if (!ParseNumber(m_lat->GetValue(), lat) ||
      !ParseNumber(m_lon->GetValue(), lon) || std::fabs(lat) > 90 ||
      std::fabs(lon) > 180) {
    wxMessageBox(_("Please enter the position in decimal degrees, e.g. "
                   "54.3812 and 10.1634."),
                 _("Harbour / anchorage"));
    return false;
  }
  m_h.kind = kKinds[std::max(0, m_kind->GetSelection())];
  m_h.name = S(m_name->GetValue().Trim());
  m_h.lat = lat;
  m_h.lon = lon;
  double v;
  m_h.depth = ParseNumber(m_depth->GetValue(), v) ? v : -1;
  m_h.berths = ParseNumber(m_berths->GetValue(), v) ? (int)v : -1;
  m_h.vhf = S(m_vhf->GetValue());
  m_h.phone = S(m_phone->GetValue());
  m_h.website = S(m_website->GetValue());
  m_h.facilities = S(m_facilities->GetValue());
  m_h.rating = m_rating->GetValue();
  m_h.notes = S(m_notes->GetValue());
  if (!m_db.SaveHarbour(m_h)) {
    wxMessageBox(_("Saving failed:") + " " + U(m_db.LastError()),
                 _("Harbour / anchorage"), wxICON_ERROR);
    return false;
  }
  return true;
}

void HarbourDlg::OnOk(wxCommandEvent &) {
  if (Store()) EndModal(wxID_OK);
}

void HarbourDlg::FillPhotos() {
  m_photos = m_h.id ? m_db.Photos(m_h.id) : std::vector<seachart::Photo>();
  m_photoList->Clear();
  for (const auto &p : m_photos)
    m_photoList->Append(p.caption.empty() ? wxFileName(U(p.path)).GetFullName()
                                          : U(p.caption));
  m_removePhoto->Enable(!m_photos.empty());
  if (!m_photos.empty()) {
    m_photoList->SetSelection(0);
    ShowPhoto(0);
  } else {
    ShowPhoto(-1);
  }
}

void HarbourDlg::ShowPhoto(int index) {
  wxImage img;
  if (index >= 0 && index < (int)m_photos.size() &&
      img.LoadFile(U(m_photos[index].path))) {
    double f = std::min(320.0 / img.GetWidth(), 200.0 / img.GetHeight());
    img.Rescale(std::max(1, (int)(img.GetWidth() * f)),
                std::max(1, (int)(img.GetHeight() * f)), wxIMAGE_QUALITY_HIGH);
    m_preview->SetBitmap(wxBitmap(img));
  } else {
    wxImage blank(320, 200);
    blank.SetRGB(wxRect(0, 0, 320, 200), 220, 220, 220);
    m_preview->SetBitmap(wxBitmap(blank));
  }
  Layout();
}

void HarbourDlg::OnAddPhoto(wxCommandEvent &) {
  if (!m_h.id) {
    // photos need the entry in the database first
    if (!Store()) return;
  }
  wxFileDialog dlg(this, _("Add photo"), "", "",
                   _("Images") +
                       " (*.jpg;*.jpeg;*.png)|*.jpg;*.jpeg;*.png;"
                       "*.JPG;*.JPEG;*.PNG",
                   wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);
  if (dlg.ShowModal() != wxID_OK) return;
  wxArrayString files;
  dlg.GetPaths(files);
  wxString caption =
      wxGetTextFromUser(_("Caption (optional)"), _("Add photo"), "", this);
  for (const auto &f : files) {
    std::string err;
    if (!m_db.AddPhoto(m_h.id, S(f), S(caption), &err))
      wxMessageBox(_("Could not add the photo:") + " " + U(err), _("Add photo"),
                   wxICON_ERROR);
  }
  FillPhotos();
  if (!m_photos.empty()) {
    m_photoList->SetSelection(m_photos.size() - 1);
    ShowPhoto(m_photos.size() - 1);
  }
}

void HarbourDlg::OnRemovePhoto(wxCommandEvent &) {
  int sel = m_photoList->GetSelection();
  if (sel < 0 || sel >= (int)m_photos.size()) return;
  if (wxMessageBox(_("Remove this photo?"), _("Photos"),
                   wxYES_NO | wxICON_QUESTION) != wxYES)
    return;
  m_db.DeletePhoto(m_photos[sel].id);
  FillPhotos();
}

// ----------------------------------------------------------- HarbourListDlg

enum { kColName, kColKind, kColPos, kColNotes };

HarbourListDlg::HarbourListDlg(wxWindow *parent, seachart::SeachartDb &db)
    : wxDialog(parent, wxID_ANY, _("Harbours and anchorages"),
               wxDefaultPosition, wxSize(720, 480),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_db(db) {
  auto *main = new wxBoxSizer(wxVERTICAL);
  auto *row = new wxBoxSizer(wxHORIZONTAL);
  row->Add(new wxStaticText(this, wxID_ANY, _("Search")), 0,
           wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  m_filter = new wxTextCtrl(this, wxID_ANY);
  row->Add(m_filter, 1);
  main->Add(row, 0, wxEXPAND | wxALL, 8);
  m_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                          wxLC_REPORT | wxLC_SINGLE_SEL);
  m_list->AppendColumn(_("Name"), wxLIST_FORMAT_LEFT, 220);
  m_list->AppendColumn(_("Type"), wxLIST_FORMAT_LEFT, 100);
  m_list->AppendColumn(_("Position"), wxLIST_FORMAT_LEFT, 150);
  m_list->AppendColumn(_("Notes"), wxLIST_FORMAT_LEFT, 220);
  main->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

  auto *btns = new wxBoxSizer(wxHORIZONTAL);
  auto button = [&](const wxString &label) {
    auto *b = new wxButton(this, wxID_ANY, label);
    btns->Add(b, 0, wxRIGHT, 4);
    return b;
  };
  wxButton *bnew = button(_("New..."));
  wxButton *bedit = button(_("Edit..."));
  wxButton *bdel = button(_("Delete"));
  wxButton *bshow = button(_("Show on chart"));
  wxButton *bfrom = button(_("Departure"));
  wxButton *bto = button(_("Destination"));
  btns->AddStretchSpacer();
  btns->Add(new wxButton(this, wxID_CLOSE, _("Close")));
  main->Add(btns, 0, wxEXPAND | wxALL, 8);
  SetSizer(main);

  bnew->Bind(wxEVT_BUTTON, &HarbourListDlg::OnNew, this);
  bedit->Bind(wxEVT_BUTTON, &HarbourListDlg::OnEdit, this);
  bdel->Bind(wxEVT_BUTTON, &HarbourListDlg::OnDelete, this);
  bshow->Bind(wxEVT_BUTTON, &HarbourListDlg::OnShow, this);
  auto choose = [this](bool dest) {
    long sel = Selected();
    if (sel < 0) return;
    m_chosen = m_shown[sel].id;
    m_as_destination = dest;
    EndModal(wxID_OK);
  };
  bfrom->Bind(wxEVT_BUTTON, [choose](wxCommandEvent &) { choose(false); });
  bto->Bind(wxEVT_BUTTON, [choose](wxCommandEvent &) { choose(true); });
  Bind(
      wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_CLOSE); },
      wxID_CLOSE);
  m_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent &) {
    wxCommandEvent e;
    OnEdit(e);
  });
  m_filter->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { Fill(); });
  m_all = m_db.Harbours();
  Fill();
  Centre();
}

void HarbourListDlg::Fill() {
  wxString f = m_filter->GetValue().Lower();
  m_shown.clear();
  m_list->DeleteAllItems();
  for (const auto &h : m_all) {
    if (!f.IsEmpty() && !U(h.name).Lower().Contains(f) &&
        !U(h.notes).Lower().Contains(f) &&
        !KindLabel(h.kind).Lower().Contains(f))
      continue;
    long i =
        m_list->InsertItem(m_shown.size(), U(h.name) + (h.edited ? " *" : ""));
    m_list->SetItem(i, kColKind, KindLabel(h.kind));
    m_list->SetItem(i, kColPos, wxString::Format("%.4f, %.4f", h.lat, h.lon));
    wxString notes = U(h.notes);
    notes.Replace("\n", " ");
    m_list->SetItem(i, kColNotes, notes.Left(80));
    m_shown.push_back(h);
  }
}

long HarbourListDlg::Selected() const {
  return m_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
}

void HarbourListDlg::OnNew(wxCommandEvent &) {
  Harbour h;
  h.kind = "anchorage";
  h.lat = gLat;
  h.lon = gLon;
  HarbourDlg dlg(this, m_db, h);
  if (dlg.ShowModal() == wxID_OK) {
    m_all = m_db.Harbours();
    Fill();
  }
}

void HarbourListDlg::OnEdit(wxCommandEvent &) {
  long sel = Selected();
  if (sel < 0) return;
  HarbourDlg dlg(this, m_db, m_shown[sel]);
  if (dlg.ShowModal() == wxID_OK) {
    m_all = m_db.Harbours();
    Fill();
  }
}

void HarbourListDlg::OnDelete(wxCommandEvent &) {
  long sel = Selected();
  if (sel < 0) return;
  if (wxMessageBox(wxString::Format(_("Delete '%s' and its photos?"),
                                    U(m_shown[sel].name)),
                   _("Harbours and anchorages"),
                   wxYES_NO | wxICON_QUESTION) != wxYES)
    return;
  m_db.DeleteHarbour(m_shown[sel].id);
  m_all = m_db.Harbours();
  Fill();
}

void HarbourListDlg::OnShow(wxCommandEvent &) {
  long sel = Selected();
  if (sel < 0) return;
  top_frame::Get()->JumpToPosition(m_shown[sel].lat, m_shown[sel].lon);
}

// -------------------------------------------------------- PassagePlannerDlg

PassagePlannerDlg::PassagePlannerDlg(wxWindow *parent)
    : wxDialog(parent, wxID_ANY, _("Passage planner"), wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
  BuildUi();
  LoadSettings();
  Reload();
  UpdateButtons();
  Bind(wxEVT_CLOSE_WINDOW, &PassagePlannerDlg::OnClose, this);
}

PassagePlannerDlg::~PassagePlannerDlg() { s_planner = nullptr; }

void PassagePlannerDlg::BuildUi() {
  auto *main = new wxBoxSizer(wxVERTICAL);

  auto *dbrow = new wxBoxSizer(wxHORIZONTAL);
  m_dbInfo = new wxStaticText(this, wxID_ANY, "");
  auto *dbbtn = new wxButton(this, wxID_ANY, _("Database..."));
  dbrow->Add(m_dbInfo, 1, wxALIGN_CENTER_VERTICAL);
  dbrow->Add(dbbtn);
  main->Add(dbrow, 0, wxEXPAND | wxALL, 8);

  auto *route = new wxStaticBoxSizer(wxVERTICAL, this, _("Passage"));
  wxWindow *rb = route->GetStaticBox();
  auto *grid = new wxFlexGridSizer(3, 4, 6);
  grid->AddGrowableCol(1);
  m_from = new wxChoice(rb, wxID_ANY);
  m_to = new wxChoice(rb, wxID_ANY);
  auto *finfo = new wxButton(rb, wxID_ANY, _("Info..."));
  auto *tinfo = new wxButton(rb, wxID_ANY, _("Info..."));
  grid->Add(new wxStaticText(rb, wxID_ANY, _("From")), 0,
            wxALIGN_CENTER_VERTICAL);
  grid->Add(m_from, 1, wxEXPAND);
  grid->Add(finfo);
  grid->Add(new wxStaticText(rb, wxID_ANY, _("To")), 0,
            wxALIGN_CENTER_VERTICAL);
  grid->Add(m_to, 1, wxEXPAND);
  grid->Add(tinfo);
  grid->Add(new wxStaticText(rb, wxID_ANY, _("Departure")), 0,
            wxALIGN_CENTER_VERTICAL);
  auto *dep = new wxBoxSizer(wxHORIZONTAL);
  m_date = new wxDatePickerCtrl(rb, wxID_ANY);
  m_time = new wxTimePickerCtrl(rb, wxID_ANY);
  dep->Add(m_date, 0, wxRIGHT, 4);
  dep->Add(m_time);
  grid->Add(dep);
  auto *harbours = new wxButton(rb, wxID_ANY, _("Harbours..."));
  grid->Add(harbours);
  route->Add(grid, 0, wxEXPAND | wxALL, 4);
  m_weatherInfo = new wxStaticText(rb, wxID_ANY, "");
  route->Add(m_weatherInfo, 0, wxALL, 4);
  main->Add(route, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

  auto *boat = new wxStaticBoxSizer(wxVERTICAL, this, _("Boat"));
  wxWindow *bb = boat->GetStaticBox();
  auto *bg = new wxFlexGridSizer(4, 4, 12);
  auto spin = [&](const wxString &label, double lo, double hi, double inc,
                  wxSpinCtrlDouble *&ctrl) {
    bg->Add(new wxStaticText(bb, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
    ctrl =
        new wxSpinCtrlDouble(bb, wxID_ANY, "", wxDefaultPosition,
                             wxSize(130, -1), wxSP_ARROW_KEYS, lo, hi, lo, inc);
    ctrl->SetDigits(inc < 1 ? 1 : 0);
    bg->Add(ctrl);
  };
  spin(_("Hull speed (kn)"), 2, 30, 0.5, m_hull);
  spin(_("Wind for hull speed (kn)"), 5, 40, 1, m_designWind);
  spin(_("Closest angle to wind (deg)"), 20, 70, 1, m_noGo);
  spin(_("Draft (m)"), 0.3, 6, 0.1, m_draft);
  spin(_("Engine speed (kn)"), 1, 30, 0.5, m_motorSpeed);
  spin(_("Engine below (kn)"), 0, 10, 0.5, m_motorBelow);
  spin(_("Warn above wind (kn)"), 5, 60, 1, m_maxWind);
  spin(_("Warn above gusts (kn)"), 5, 80, 1, m_maxGust);
  boat->Add(bg, 0, wxALL, 4);
  m_motor = new wxCheckBox(bb, wxID_ANY,
                           _("Use the engine when sailing is too slow"));
  boat->Add(m_motor, 0, wxALL, 4);
  main->Add(boat, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

  auto *actions = new wxBoxSizer(wxHORIZONTAL);
  auto *calc = new wxButton(this, wxID_ANY, _("Calculate route"));
  auto *compare = new wxButton(this, wxID_ANY, _("Compare departures"));
  compare->SetToolTip(
      _("Calculate the passage for departures every 3 hours over the next "
        "two days."));
  m_createRoute = new wxButton(this, wxID_ANY, _("Create route"));
  m_start = new wxButton(this, wxID_ANY, _("Start passage"));
  m_stop = new wxButton(this, wxID_ANY, _("End passage"));
  auto *log = new wxButton(this, wxID_ANY, _("Logbook..."));
  for (auto *b : {calc, compare, m_createRoute, m_start, m_stop})
    actions->Add(b, 0, wxRIGHT, 4);
  actions->AddStretchSpacer();
  actions->Add(log);
  main->Add(actions, 0, wxEXPAND | wxALL, 8);

  m_summary = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition,
                             wxSize(-1, 110), wxTE_MULTILINE | wxTE_READONLY);
  // the departure comparison is a fixed width table
  m_summary->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE)));
  main->Add(m_summary, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
  m_legs = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(760, 180),
                          wxLC_REPORT | wxLC_SINGLE_SEL);
  const wxString cols[] = {_("Leg"),    _("Start"), _("Arrival"), _("Distance"),
                           _("Course"), _("Speed"), _("Wind"),    _("TWA"),
                           _("Gusts"),  _("Waves"), _("Engine")};
  for (const auto &c : cols) m_legs->AppendColumn(c);
  main->Add(m_legs, 1, wxEXPAND | wxALL, 8);
  SetSizerAndFit(main);

  dbbtn->Bind(wxEVT_BUTTON, &PassagePlannerDlg::OnDatabase, this);
  finfo->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { OnInfo(false); });
  tinfo->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { OnInfo(true); });
  harbours->Bind(wxEVT_BUTTON, &PassagePlannerDlg::OnHarbours, this);
  calc->Bind(wxEVT_BUTTON, &PassagePlannerDlg::OnCalculate, this);
  compare->Bind(wxEVT_BUTTON, &PassagePlannerDlg::OnCompare, this);
  m_createRoute->Bind(wxEVT_BUTTON, &PassagePlannerDlg::OnCreateRoute, this);
  m_start->Bind(wxEVT_BUTTON, &PassagePlannerDlg::OnStart, this);
  m_stop->Bind(wxEVT_BUTTON, &PassagePlannerDlg::OnStop, this);
  log->Bind(wxEVT_BUTTON, &PassagePlannerDlg::OnLogbook, this);
  m_motor->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) {
    m_motorSpeed->Enable(m_motor->GetValue());
    m_motorBelow->Enable(m_motor->GetValue());
  });
}

void PassagePlannerDlg::LoadSettings() {
  auto *cfg = TheBaseConfig();
  cfg->SetPath(kConfigPath);
  seachart::BoatModel d;
  double v;
  auto rd = [&](const char *key, double def, wxSpinCtrlDouble *c) {
    cfg->Read(key, &v, def);
    c->SetValue(v);
  };
  rd("HullSpeed", d.hull_speed_kn, m_hull);
  rd("DesignWind", d.design_wind_kn, m_designWind);
  rd("NoGo", d.no_go_deg, m_noGo);
  rd("Draft", d.draft_m, m_draft);
  rd("MotorSpeed", d.motor_speed_kn, m_motorSpeed);
  rd("MotorBelow", d.motor_below_kn, m_motorBelow);
  rd("MaxWind", d.max_wind_kn, m_maxWind);
  rd("MaxGust", d.max_gust_kn, m_maxGust);
  bool motor;
  cfg->Read("Motor", &motor, d.motor);
  m_motor->SetValue(motor);
  m_motorSpeed->Enable(motor);
  m_motorBelow->Enable(motor);
  // default departure: next full hour
  wxDateTime t = wxDateTime::Now();
  t.SetMinute(0);
  t.SetSecond(0);
  t += wxTimeSpan::Hour();
  m_date->SetValue(t);
  m_time->SetValue(t);
}

void PassagePlannerDlg::SaveSettings() {
  auto *cfg = TheBaseConfig();
  cfg->SetPath(kConfigPath);
  cfg->Write("HullSpeed", m_hull->GetValue());
  cfg->Write("DesignWind", m_designWind->GetValue());
  cfg->Write("NoGo", m_noGo->GetValue());
  cfg->Write("Draft", m_draft->GetValue());
  cfg->Write("MotorSpeed", m_motorSpeed->GetValue());
  cfg->Write("MotorBelow", m_motorBelow->GetValue());
  cfg->Write("MaxWind", m_maxWind->GetValue());
  cfg->Write("MaxGust", m_maxGust->GetValue());
  cfg->Write("Motor", m_motor->GetValue());
  cfg->Write("Database", U(SeachartDatabase().Path()));
  if (m_from->GetSelection() >= 0)
    cfg->Write("From", (long)m_harbours[m_from->GetSelection()].id);
  if (m_to->GetSelection() >= 0)
    cfg->Write("To", (long)m_harbours[m_to->GetSelection()].id);
  cfg->Flush();
}

void PassagePlannerDlg::Reload() {
  seachart::SeachartDb &db = SeachartDatabase();
  int64_t from = 0, to = 0;
  if (m_from->GetSelection() >= 0 &&
      m_from->GetSelection() < (int)m_harbours.size())
    from = m_harbours[m_from->GetSelection()].id;
  if (m_to->GetSelection() >= 0 &&
      m_to->GetSelection() < (int)m_harbours.size())
    to = m_harbours[m_to->GetSelection()].id;
  if (!from && !to) {
    auto *cfg = TheBaseConfig();
    cfg->SetPath(kConfigPath);
    long f = 0, t = 0;
    cfg->Read("From", &f, 0);
    cfg->Read("To", &t, 0);
    from = f;
    to = t;
  }
  m_harbours = db.Harbours();
  m_weather = db.LoadWeather();
  m_from->Clear();
  m_to->Clear();
  for (const auto &h : m_harbours) {
    wxString label = U(h.name) + " (" + KindLabel(h.kind) + ")";
    m_from->Append(label);
    m_to->Append(label);
  }
  SelectHarbour(from, false);
  SelectHarbour(to, true);

  if (db.IsOpen())
    m_dbInfo->SetLabel(wxString::Format(_("%zu harbours/anchorages in %s"),
                                        m_harbours.size(), U(db.Path())));
  else
    m_dbInfo->SetLabel(_("Database not found:") + " " + U(db.Path()));
  if (m_weather.Empty())
    m_weatherInfo->SetLabel(
        _("No weather forecast offline - run 'seachart weather' before "
          "leaving. Calculated with the default speed."));
  else
    m_weatherInfo->SetLabel(
        wxString::Format(_("Weather forecast from %s, valid until %s"),
                         logbook::FormatTime(m_weather.Fetched()),
                         logbook::FormatTime(m_weather.Last())));
  Layout();
}

void PassagePlannerDlg::SelectHarbour(int64_t id, bool destination) {
  for (size_t i = 0; i < m_harbours.size(); i++)
    if (m_harbours[i].id == id)
      (destination ? m_to : m_from)->SetSelection((int)i);
}

seachart::BoatModel PassagePlannerDlg::ReadBoat() const {
  seachart::BoatModel b;
  b.hull_speed_kn = m_hull->GetValue();
  b.design_wind_kn = m_designWind->GetValue();
  b.no_go_deg = m_noGo->GetValue();
  b.draft_m = m_draft->GetValue();
  b.motor = m_motor->GetValue();
  b.motor_speed_kn = m_motorSpeed->GetValue();
  b.motor_below_kn = m_motorBelow->GetValue();
  b.max_wind_kn = m_maxWind->GetValue();
  b.max_gust_kn = m_maxGust->GetValue();
  b.default_speed_kn = b.motor ? b.motor_speed_kn : 0.7 * b.hull_speed_kn;
  return b;
}

wxDateTime PassagePlannerDlg::Departure() const {
  wxDateTime d = m_date->GetValue();
  wxDateTime t = m_time->GetValue();
  d.SetHour(t.GetHour());
  d.SetMinute(t.GetMinute());
  d.SetSecond(0);
  return d;
}

bool PassagePlannerDlg::Endpoints(Harbour &from, Harbour &to) const {
  int f = m_from->GetSelection(), t = m_to->GetSelection();
  if (f < 0 || t < 0 || f == t) return false;
  from = m_harbours[f];
  to = m_harbours[t];
  return true;
}

PassagePlan PassagePlannerDlg::Compute(time_t departure) {
  Harbour from, to;
  PassagePlan plan;
  if (!Endpoints(from, to)) {
    plan.error = S(_("Please choose two different harbours."));
    return plan;
  }
  seachart::NavGrid grid =
      SeachartDatabase().LoadNavGrid(from.lat, from.lon, to.lat, to.lon);
  seachart::PassageRouter router(grid, m_weather, ReadBoat());
  return router.Plan(from.lat, from.lon, to.lat, to.lon, departure);
}

void PassagePlannerDlg::OnCalculate(wxCommandEvent &) {
  wxBusyCursor busy;
  m_plan = Compute(Departure().GetTicks());
  m_routeGuid.Clear();
  ShowPlan();
  UpdateButtons();
  SaveSettings();
}

void PassagePlannerDlg::ShowPlan() {
  m_legs->DeleteAllItems();
  if (!m_plan.ok) {
    wxString err = U(m_plan.error);
    if (m_plan.error.find("no connection") != std::string::npos)
      err =
          _("No connection by water was found. The navigation grid may be "
            "too coarse for a narrow passage: rebuild it with "
            "'seachart navgrid --nav-resolution 0.002'.");
    else if (m_plan.error.find("no navigable water") != std::string::npos)
      err =
          _("No navigable water near the harbour. Is the area covered by "
            "'seachart navgrid'?");
    m_summary->SetValue(err);
    return;
  }
  Harbour from, to;
  Endpoints(from, to);
  wxString s;
  s << U(from.name) << "  ->  " << U(to.name) << "\n";
  s << wxString::Format(_("Distance %.1f NM, duration %s, departure %s, "
                          "arrival %s"),
                        m_plan.distance_nm,
                        logbook::FormatDuration(m_plan.Hours()),
                        logbook::FormatTime(m_plan.departure),
                        logbook::FormatTime(m_plan.arrival))
    << "\n";
  s << wxString::Format(_("Under engine %.1f NM, wind up to %.0f kn, gusts "
                          "up to %.0f kn"),
                        m_plan.motor_nm, m_plan.max_wind_kn,
                        m_plan.max_gust_kn);
  if (m_plan.max_wave_m >= 0)
    s << wxString::Format(_(", waves up to %.1f m"), m_plan.max_wave_m);
  s << "\n";
  if (m_plan.warnings)
    s << wxString::Format(_("WARNING: %d legs above your wind or gust limit."),
                          m_plan.warnings)
      << "\n";
  if (!m_plan.used_weather)
    s << _("No forecast for this departure - calculated with the default "
           "speed.")
      << "\n";
  else if (m_plan.beyond_forecast)
    s << _("The passage ends after the end of the forecast.") << "\n";
  if (!m_plan.grid_checked)
    s << _("No navigation grid for this area: the route is a straight line "
           "and NOT checked for land or shallows.")
      << "\n";
  m_summary->SetValue(s);

  long row = 0;
  for (const auto &l : m_plan.legs) {
    m_legs->InsertItem(row, wxString::Format("%ld", row + 1));
    m_legs->SetItem(row, 1, logbook::FormatTime(l.start));
    m_legs->SetItem(row, 2, logbook::FormatTime(l.end));
    m_legs->SetItem(row, 3, wxString::Format("%.1f NM", l.distance_nm));
    m_legs->SetItem(row, 4, wxString::Format("%03.0f", l.course_deg));
    m_legs->SetItem(row, 5, wxString::Format("%.1f kn", l.speed_kn));
    if (l.avg_wind_kn > 0) {
      m_legs->SetItem(
          row, 6,
          wxString::Format("%.0f kn %s", l.avg_wind_kn, Compass(l.wind_dir)));
      m_legs->SetItem(row, 7, wxString::Format("%.0f", l.twa_deg));
      m_legs->SetItem(row, 8, wxString::Format("%.0f kn", l.max_gust_kn));
    }
    m_legs->SetItem(row, 9,
                    l.max_wave_m >= 0 ? wxString::Format("%.1f m", l.max_wave_m)
                                      : wxString("-"));
    m_legs->SetItem(
        row, 10,
        wxString::Format("%.0f%%", l.distance_nm > 0
                                       ? 100 * l.motor_nm / l.distance_nm
                                       : 0.0));
    if (l.warning) m_legs->SetItemTextColour(row, *wxRED);
    row++;
  }
  for (int c = 0; c < m_legs->GetColumnCount(); c++)
    m_legs->SetColumnWidth(c, wxLIST_AUTOSIZE_USEHEADER);
}

void PassagePlannerDlg::OnCompare(wxCommandEvent &) {
  wxBusyCursor busy;
  wxString s = _("Departure            Duration   Arrival              "
                 "Engine   Gusts") +
               "\n";
  time_t base = Departure().GetTicks();
  time_t best_t = 0;
  double best_h = 1e9;
  for (int k = 0; k <= 16; k++) {
    time_t dep = base + k * 3 * 3600;
    PassagePlan p = Compute(dep);
    if (!p.ok) {
      m_summary->SetValue(U(p.error));
      return;
    }
    bool ok = p.warnings == 0;
    s << wxString::Format(
        "%-20s %-10s %-20s %4.1f NM %3.0f kn%s\n", logbook::FormatTime(dep),
        logbook::FormatDuration(p.Hours()), logbook::FormatTime(p.arrival),
        p.motor_nm, p.max_gust_kn, ok ? "" : "  !");
    if (ok && p.Hours() < best_h) {
      best_h = p.Hours();
      best_t = dep;
    }
    if (p.beyond_forecast && m_weather.Last() < dep) break;
  }
  if (best_t)
    s << "\n"
      << wxString::Format(_("Fastest departure within your limits: %s (%s)"),
                          logbook::FormatTime(best_t),
                          logbook::FormatDuration(best_h));
  m_summary->SetValue(s);
  if (best_t) {
    wxDateTime d((time_t)best_t);
    m_date->SetValue(d);
    m_time->SetValue(d);
  }
}

Route *PassagePlannerDlg::CreateRoute() {
  if (!m_plan.ok) return nullptr;
  if (!m_routeGuid.IsEmpty()) {
    Route *r = g_pRouteMan->FindRouteByGUID(m_routeGuid);
    if (r) return r;
  }
  Harbour from, to;
  Endpoints(from, to);
  Route *route = new Route();
  RoutePoint *prev = nullptr;
  size_t n = m_plan.points.size();
  for (size_t i = 0; i < n; i++) {
    wxString name, icon = g_default_routepoint_icon;
    if (i == 0) {
      name = U(from.name);
      icon = from.kind == "anchorage" ? "anchorage" : "Service-Marina";
    } else if (i + 1 == n) {
      name = U(to.name);
      icon = to.kind == "anchorage" ? "anchorage" : "Service-Marina";
    } else {
      name = wxString::Format("WP%02zu", i);
    }
    auto *wp = new RoutePoint(m_plan.points[i].first, m_plan.points[i].second,
                              icon, name, wxEmptyString);
    wp->m_bShowName = (i == 0 || i + 1 == n);
    // Route::UpdateSegmentDistance() reads the leg speed from the point
    // the leg ends at.
    if (i > 0) wp->SetPlannedSpeed(m_plan.legs[i - 1].speed_kn);
    route->AddPoint(wp);
    pSelect->AddSelectableRoutePoint(wp->m_lat, wp->m_lon, wp);
    if (prev)
      pSelect->AddSelectableRouteSegment(prev->m_lat, prev->m_lon, wp->m_lat,
                                         wp->m_lon, prev, wp, route);
    prev = wp;
  }
  route->m_RouteNameString = U(from.name) + " - " + U(to.name);
  route->m_RouteStartString = U(from.name);
  route->m_RouteEndString = U(to.name);
  route->m_RouteDescription = m_summary->GetValue();
  route->m_PlannedSpeed =
      m_plan.Hours() > 0 ? m_plan.distance_nm / m_plan.Hours() : 5;
  route->SetDepartureDate(wxDateTime((time_t)m_plan.departure));
  pRouteList->push_back(route);
  NavObj_dB::GetInstance().InsertRoute(route);
  route->UpdateSegmentDistances(route->m_PlannedSpeed);
  m_routeGuid = route->GetGUID();
  if (pRouteManagerDialog && pRouteManagerDialog->IsShown())
    pRouteManagerDialog->UpdateRouteListCtrl();
  top_frame::Get()->RefreshAllCanvas();
  return route;
}

void PassagePlannerDlg::OnCreateRoute(wxCommandEvent &) {
  Route *r = CreateRoute();
  if (!r) return;
  top_frame::Get()->JumpToPosition(m_plan.points.front().first,
                                   m_plan.points.front().second);
  UpdateButtons();
}

void PassagePlannerDlg::OnStart(wxCommandEvent &) {
  Route *route = CreateRoute();
  if (!route) return;
  Harbour from, to;
  Endpoints(from, to);
  if (g_pRouteMan->GetpActiveRoute()) g_pRouteMan->DeactivateRoute();
  // already in the departure harbour: steer to the first waypoint after it
  RoutePoint *first = nullptr;
  if (route->GetnPoints() > 1 && bGPSValid &&
      seachart::DistanceM(gLat, gLon, from.lat, from.lon) < 1852)
    first = route->GetPoint(2);
  g_pRouteMan->ActivateRoute(route, first);
  if (!g_bTrackActive) top_frame::Get()->TrackOn();

  logbook::Trip trip;
  trip.name = route->m_RouteNameString;
  trip.from = U(from.name);
  trip.to = U(to.name);
  trip.from_lat = from.lat;
  trip.from_lon = from.lon;
  trip.to_lat = to.lat;
  trip.to_lon = to.lon;
  trip.planned_departure = m_plan.departure;
  trip.planned_arrival = m_plan.arrival;
  trip.planned_nm = m_plan.distance_nm;
  trip.route_guid = route->GetGUID();
  if (g_pActiveTrack) trip.track_guid = g_pActiveTrack->m_GUID;
  trip.plan = m_summary->GetValue();
  for (const auto &l : m_plan.legs)
    trip.plan << wxString::Format(
        "\n%s  %5.1f NM  %03.0f  %4.1f kn  wind %2.0f kn %s",
        logbook::FormatTime(l.start), l.distance_nm, l.course_deg, l.speed_kn,
        l.avg_wind_kn, Compass(l.wind_dir));
  trip.weather =
      m_weather.Empty()
          ? wxString(_("no forecast"))
          : wxString::Format(_("forecast from %s"),
                             logbook::FormatTime(m_weather.Fetched()));
  logbook::Recorder::Get().Start(trip);
  UpdateButtons();
  wxMessageBox(_("Passage started: route activated, track and logbook are "
                 "recording."),
               _("Passage planner"));
}

void PassagePlannerDlg::OnStop(wxCommandEvent &) {
  int64_t id = logbook::Recorder::Get().Stop();
  UpdateButtons();
  if (id) logbook::ShowLogbook(this);
}

void PassagePlannerDlg::UpdateButtons() {
  bool running = logbook::Recorder::Get().Active();
  m_createRoute->Enable(m_plan.ok);
  m_start->Enable(m_plan.ok && !running);
  m_stop->Enable(running);
}

void PassagePlannerDlg::OnHarbours(wxCommandEvent &) {
  HarbourListDlg dlg(this, SeachartDatabase());
  dlg.ShowModal();
  Reload();
  if (dlg.ChosenId()) SelectHarbour(dlg.ChosenId(), dlg.ChosenAsDestination());
}

void PassagePlannerDlg::OnInfo(bool destination) {
  int sel = (destination ? m_to : m_from)->GetSelection();
  if (sel < 0) return;
  HarbourDlg dlg(this, SeachartDatabase(), m_harbours[sel]);
  if (dlg.ShowModal() == wxID_OK) Reload();
}

void PassagePlannerDlg::OnDatabase(wxCommandEvent &) {
  wxFileDialog dlg(this, _("seachart database"),
                   wxFileName(U(SeachartDatabase().Path())).GetPath(),
                   "seachart.db", "seachart.db|*.db", wxFD_OPEN);
  if (dlg.ShowModal() != wxID_OK) return;
  std::string err;
  if (!SeachartDatabase().Open(S(dlg.GetPath()), &err))
    wxMessageBox(U(err), _("seachart database"), wxICON_ERROR);
  m_harbours.clear();
  m_from->SetSelection(wxNOT_FOUND);
  m_to->SetSelection(wxNOT_FOUND);
  Reload();
  SaveSettings();
}

void PassagePlannerDlg::OnLogbook(wxCommandEvent &) {
  logbook::ShowLogbook(this);
}

void PassagePlannerDlg::OnClose(wxCloseEvent &) {
  SaveSettings();
  Hide();
}

// ---------------------------------------------------------------- helpers

void ShowPassagePlanner(wxWindow *parent) {
  if (!s_planner) s_planner = new PassagePlannerDlg(parent);
  s_planner->Reload();
  s_planner->Show();
  s_planner->Raise();
}

void ShowNewHarbourDialog(wxWindow *parent, double lat, double lon) {
  Harbour h;
  h.kind = "anchorage";
  h.lat = lat;
  h.lon = lon;
  HarbourDlg dlg(parent, SeachartDatabase(), h);
  if (dlg.ShowModal() == wxID_OK && s_planner) s_planner->Reload();
}
