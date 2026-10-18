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
 * Implement seachart_db.h -- offline harbour, weather and grid database.
 */

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <cmath>

#include <SQLiteCpp/SQLiteCpp.h>

#include "std_filesystem.h"

#include "seachart_db.h"

namespace seachart {

static constexpr double kPi = 3.14159265358979323846;

static int64_t GridKey(double lat, double lon) {
  return std::llround(lat * 1e4) * 4000000LL + std::llround(lon * 1e4) +
         2000000LL;
}

// Same layout as tools/seachart/seachart/store.py.  Only the tables the
// passage planner may write to are created here.
static const char *const kSchema = R"(
CREATE TABLE IF NOT EXISTS harbours (
    id INTEGER PRIMARY KEY, kind TEXT NOT NULL, name TEXT NOT NULL,
    lon REAL NOT NULL, lat REAL NOT NULL, osm_id TEXT UNIQUE,
    source TEXT NOT NULL DEFAULT 'user', edited INTEGER NOT NULL DEFAULT 0,
    notes TEXT NOT NULL DEFAULT '', depth REAL, berths INTEGER,
    vhf TEXT NOT NULL DEFAULT '', phone TEXT NOT NULL DEFAULT '',
    website TEXT NOT NULL DEFAULT '', facilities TEXT NOT NULL DEFAULT '',
    rating INTEGER, updated TEXT);
CREATE TABLE IF NOT EXISTS photos (
    id INTEGER PRIMARY KEY, harbour_id INTEGER NOT NULL, path TEXT NOT NULL,
    caption TEXT NOT NULL DEFAULT '', added TEXT);
CREATE TABLE IF NOT EXISTS pois (
    id INTEGER PRIMARY KEY, osm_id TEXT UNIQUE, category TEXT NOT NULL,
    name TEXT NOT NULL DEFAULT '', lon REAL NOT NULL, lat REAL NOT NULL,
    tags TEXT NOT NULL DEFAULT '{}');
CREATE TABLE IF NOT EXISTS weather (
    lon REAL NOT NULL, lat REAL NOT NULL, time INTEGER NOT NULL,
    wind_kn REAL, wind_dir REAL, gust_kn REAL, wave_m REAL, wave_dir REAL,
    PRIMARY KEY (lon, lat, time));
CREATE TABLE IF NOT EXISTS weather_meta (
    lon REAL NOT NULL, lat REAL NOT NULL, fetched INTEGER NOT NULL,
    PRIMARY KEY (lon, lat));
CREATE TABLE IF NOT EXISTS navgrid (
    id INTEGER PRIMARY KEY, lon0 REAL, lat0 REAL, dlon REAL, dlat REAL,
    nx INTEGER, ny INTEGER, data BLOB, created TEXT);
)";

static std::string NowIso() {
  std::time_t t = std::time(nullptr);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S+00:00", std::gmtime(&t));
  return buf;
}

static std::string Text(const SQLite::Column &c) {
  return c.isNull() ? std::string() : c.getString();
}

/** Value of a "key": "value" pair in a flat JSON object (tags column). */
static std::string JsonValue(const std::string &json, const std::string &key) {
  std::string pattern = "\"" + key + "\": \"";
  size_t p = json.find(pattern);
  if (p == std::string::npos) {
    pattern = "\"" + key + "\":\"";
    p = json.find(pattern);
    if (p == std::string::npos) return "";
  }
  p += pattern.size();
  std::string out;
  for (; p < json.size() && json[p] != '"'; p++) {
    if (json[p] == '\\' && p + 1 < json.size()) p++;
    out += json[p];
  }
  return out;
}

double DistanceM(double lat1, double lon1, double lat2, double lon2) {
  double k = std::cos((lat1 + lat2) / 2 * kPi / 180.0);
  return std::hypot((lon2 - lon1) * k, lat2 - lat1) * 111320.0;
}

// ---------------------------------------------------------------- Weather

void Weather::Add(Series s) {
  if (s.hours.empty()) return;
  time_t last = s.t0 + 3600 * (time_t)(s.hours.size() - 1);
  if (m_points.empty() || s.t0 < m_first) m_first = s.t0;
  if (m_points.empty() || last > m_last) m_last = last;
  auto insert_sorted = [](std::vector<double> &v, double x) {
    auto it = std::lower_bound(v.begin(), v.end(), x - 1e-6);
    if (it == v.end() || std::fabs(*it - x) > 1e-6) v.insert(it, x);
  };
  insert_sorted(m_lats, s.lat);
  insert_sorted(m_lons, s.lon);
  m_index[GridKey(s.lat, s.lon)] = m_points.size();
  m_points.push_back(std::move(s));
}

const Weather::Series *Weather::Find(double lat, double lon) const {
  auto it = m_index.find(GridKey(lat, lon));
  return it == m_index.end() ? nullptr : &m_points[it->second];
}

WeatherSample Weather::AtPoint(const Series &s, time_t t) const {
  WeatherSample out;
  double f = double(t - s.t0) / 3600.0;
  if (f < -1 || f > (double)s.hours.size()) return out;  // outside forecast
  f = std::max(0.0, std::min(f, (double)s.hours.size() - 1));
  size_t i = (size_t)f;
  size_t j = std::min(i + 1, s.hours.size() - 1);
  double w = f - i;
  const WeatherSample &a = s.hours[i], &b = s.hours[j];
  if (!a.valid || !b.valid) return a.valid ? a : b;
  // interpolate the wind as a vector so 350 deg and 10 deg average to 0
  double ax = a.wind_kn * std::sin(a.wind_dir * kPi / 180);
  double ay = a.wind_kn * std::cos(a.wind_dir * kPi / 180);
  double bx = b.wind_kn * std::sin(b.wind_dir * kPi / 180);
  double by = b.wind_kn * std::cos(b.wind_dir * kPi / 180);
  double x = ax + (bx - ax) * w, y = ay + (by - ay) * w;
  out.valid = true;
  out.wind_kn = a.wind_kn + (b.wind_kn - a.wind_kn) * w;
  out.wind_dir = std::fmod(std::atan2(x, y) * 180 / kPi + 360, 360);
  out.gust_kn = a.gust_kn + (b.gust_kn - a.gust_kn) * w;
  out.wave_m = (a.wave_m >= 0 && b.wave_m >= 0)
                   ? a.wave_m + (b.wave_m - a.wave_m) * w
                   : std::max(a.wave_m, b.wave_m);
  return out;
}

WeatherSample Weather::At(double lat, double lon, time_t t) const {
  WeatherSample out;
  if (m_points.empty()) return out;
  // grid cell around the position
  auto bracket = [](const std::vector<double> &v, double x, double &lo,
                    double &hi) {
    auto it = std::lower_bound(v.begin(), v.end(), x);
    if (it == v.end()) {
      lo = hi = v.back();
    } else if (it == v.begin()) {
      lo = hi = v.front();
    } else {
      hi = *it;
      lo = *(it - 1);
    }
  };
  double la0, la1, lo0, lo1;
  bracket(m_lats, lat, la0, la1);
  bracket(m_lons, lon, lo0, lo1);
  double corners[4][2] = {{la0, lo0}, {la0, lo1}, {la1, lo0}, {la1, lo1}};
  double wsum = 0, x = 0, y = 0, kn = 0, gust = 0, wave = 0, wave_w = 0;
  for (auto &c : corners) {
    const Series *s = Find(c[0], c[1]);
    if (!s) continue;
    WeatherSample p = AtPoint(*s, t);
    if (!p.valid) continue;
    double wy = la1 > la0 ? 1 - std::fabs(lat - c[0]) / (la1 - la0) : 1;
    double wx = lo1 > lo0 ? 1 - std::fabs(lon - c[1]) / (lo1 - lo0) : 1;
    double w = std::max(0.0, wx) * std::max(0.0, wy) + 1e-9;
    wsum += w;
    kn += w * p.wind_kn;
    gust += w * p.gust_kn;
    x += w * p.wind_kn * std::sin(p.wind_dir * kPi / 180);
    y += w * p.wind_kn * std::cos(p.wind_dir * kPi / 180);
    if (p.wave_m >= 0) {
      wave += w * p.wave_m;
      wave_w += w;
    }
  }
  if (wsum <= 0) {
    // just outside the grid: use the nearest point, but not for places
    // far away from the downloaded area
    const Series *best = nullptr;
    double bd = 50000;
    for (const auto &s : m_points) {
      double d = DistanceM(lat, lon, s.lat, s.lon);
      if (d < bd) {
        bd = d;
        best = &s;
      }
    }
    return best ? AtPoint(*best, t) : out;
  }
  out.valid = true;
  out.wind_kn = kn / wsum;
  out.gust_kn = gust / wsum;
  out.wind_dir = std::fmod(std::atan2(x, y) * 180 / kPi + 360, 360);
  out.wave_m = wave_w > 0 ? wave / wave_w : -1;
  return out;
}

// ------------------------------------------------------------- SeachartDb

SeachartDb::SeachartDb() = default;
SeachartDb::~SeachartDb() = default;

bool SeachartDb::Open(const std::string &path, std::string *error) {
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

std::vector<Harbour> SeachartDb::Harbours() {
  std::vector<Harbour> out;
  if (!m_db) return out;
  try {
    SQLite::Statement q(*m_db,
                        "SELECT id, kind, name, lat, lon, notes, vhf, phone,"
                        " website, facilities, depth, berths, rating, edited"
                        " FROM harbours ORDER BY name COLLATE NOCASE");
    while (q.executeStep()) {
      Harbour h;
      h.id = q.getColumn(0).getInt64();
      h.kind = Text(q.getColumn(1));
      h.name = Text(q.getColumn(2));
      h.lat = q.getColumn(3).getDouble();
      h.lon = q.getColumn(4).getDouble();
      h.notes = Text(q.getColumn(5));
      h.vhf = Text(q.getColumn(6));
      h.phone = Text(q.getColumn(7));
      h.website = Text(q.getColumn(8));
      h.facilities = Text(q.getColumn(9));
      h.depth = q.getColumn(10).isNull() ? -1 : q.getColumn(10).getDouble();
      h.berths = q.getColumn(11).isNull() ? -1 : q.getColumn(11).getInt();
      h.rating = q.getColumn(12).isNull() ? 0 : q.getColumn(12).getInt();
      h.edited = q.getColumn(13).getInt() != 0;
      out.push_back(h);
    }
  } catch (std::exception &e) {
    m_error = e.what();
  }
  return out;
}

bool SeachartDb::SaveHarbour(Harbour &h) {
  if (!m_db) return false;
  try {
    const char *sql =
        h.id == 0
            ? "INSERT INTO harbours (kind, name, lat, lon, notes, vhf, phone,"
              " website, facilities, depth, berths, rating, source, edited,"
              " updated) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,'user',1,?)"
            : "UPDATE harbours SET kind=?, name=?, lat=?, lon=?, notes=?,"
              " vhf=?, phone=?, website=?, facilities=?, depth=?, berths=?,"
              " rating=?, edited=1, updated=? WHERE id=?";
    SQLite::Statement q(*m_db, sql);
    q.bind(1, h.kind);
    q.bind(2, h.name);
    q.bind(3, h.lat);
    q.bind(4, h.lon);
    q.bind(5, h.notes);
    q.bind(6, h.vhf);
    q.bind(7, h.phone);
    q.bind(8, h.website);
    q.bind(9, h.facilities);
    if (h.depth >= 0)
      q.bind(10, h.depth);
    else
      q.bind(10);
    if (h.berths >= 0)
      q.bind(11, h.berths);
    else
      q.bind(11);
    if (h.rating > 0)
      q.bind(12, h.rating);
    else
      q.bind(12);
    q.bind(13, NowIso());
    if (h.id != 0) q.bind(14, (int64_t)h.id);
    q.exec();
    if (h.id == 0) h.id = m_db->getLastInsertRowid();
    h.edited = true;
  } catch (std::exception &e) {
    m_error = e.what();
    return false;
  }
  return true;
}

bool SeachartDb::DeleteHarbour(int64_t id) {
  if (!m_db) return false;
  for (const auto &p : Photos(id)) DeletePhoto(p.id);
  try {
    SQLite::Statement q(*m_db, "DELETE FROM harbours WHERE id=?");
    q.bind(1, (int64_t)id);
    q.exec();
  } catch (std::exception &e) {
    m_error = e.what();
    return false;
  }
  return true;
}

std::vector<Photo> SeachartDb::Photos(int64_t harbour_id) {
  std::vector<Photo> out;
  if (!m_db) return out;
  fs::path base = fs::u8path(m_path).parent_path();
  try {
    SQLite::Statement q(*m_db,
                        "SELECT id, path, caption FROM photos WHERE"
                        " harbour_id=? ORDER BY id");
    q.bind(1, (int64_t)harbour_id);
    while (q.executeStep()) {
      Photo p;
      p.id = q.getColumn(0).getInt64();
      p.path = (base / fs::u8path(Text(q.getColumn(1)))).u8string();
      p.caption = Text(q.getColumn(2));
      out.push_back(p);
    }
  } catch (std::exception &e) {
    m_error = e.what();
  }
  return out;
}

bool SeachartDb::AddPhoto(int64_t harbour_id, const std::string &file,
                          const std::string &caption, std::string *error) {
  if (!m_db) return false;
  try {
    fs::path db = fs::u8path(m_path);
    fs::path folder = db.parent_path() / (db.stem().u8string() + "_media") /
                      std::to_string(harbour_id);
    fs::create_directories(folder);
    fs::path src = fs::u8path(file);
    fs::path target = folder / src.filename();
    for (int k = 1; fs::exists(target); k++)
      target =
          folder / fs::u8path(src.stem().u8string() + "_" + std::to_string(k) +
                              src.extension().u8string());
    fs::copy_file(src, target);
    std::string rel = fs::relative(target, db.parent_path()).generic_u8string();
    SQLite::Statement q(*m_db,
                        "INSERT INTO photos (harbour_id, path, caption, added)"
                        " VALUES (?,?,?,?)");
    q.bind(1, (int64_t)harbour_id);
    q.bind(2, rel);
    q.bind(3, caption);
    q.bind(4, NowIso());
    q.exec();
    SQLite::Statement u(*m_db,
                        "UPDATE harbours SET edited=1, updated=? WHERE id=?");
    u.bind(1, NowIso());
    u.bind(2, (int64_t)harbour_id);
    u.exec();
  } catch (std::exception &e) {
    m_error = e.what();
    if (error) *error = m_error;
    return false;
  }
  return true;
}

bool SeachartDb::DeletePhoto(int64_t photo_id) {
  if (!m_db) return false;
  try {
    SQLite::Statement s(*m_db, "SELECT path FROM photos WHERE id=?");
    s.bind(1, (int64_t)photo_id);
    if (s.executeStep()) {
      fs::path p =
          fs::u8path(m_path).parent_path() / fs::u8path(Text(s.getColumn(0)));
      std::error_code ec;
      fs::remove(p, ec);
    }
    SQLite::Statement q(*m_db, "DELETE FROM photos WHERE id=?");
    q.bind(1, (int64_t)photo_id);
    q.exec();
  } catch (std::exception &e) {
    m_error = e.what();
    return false;
  }
  return true;
}

std::vector<Poi> SeachartDb::PoisNear(double lat, double lon, double radius_m) {
  std::vector<Poi> out;
  if (!m_db) return out;
  double d = radius_m / 111320.0;
  double dl = d / std::max(0.2, std::cos(lat * kPi / 180));
  try {
    SQLite::Statement q(*m_db,
                        "SELECT category, name, lat, lon, tags FROM pois"
                        " WHERE lat BETWEEN ? AND ? AND lon BETWEEN ? AND ?");
    q.bind(1, lat - d);
    q.bind(2, lat + d);
    q.bind(3, lon - dl);
    q.bind(4, lon + dl);
    while (q.executeStep()) {
      Poi p;
      p.category = Text(q.getColumn(0));
      p.name = Text(q.getColumn(1));
      p.lat = q.getColumn(2).getDouble();
      p.lon = q.getColumn(3).getDouble();
      p.distance_m = DistanceM(lat, lon, p.lat, p.lon);
      if (p.distance_m > radius_m) continue;
      std::string tags = Text(q.getColumn(4));
      p.opening_hours = JsonValue(tags, "opening_hours");
      p.phone = JsonValue(tags, "phone");
      if (p.phone.empty()) p.phone = JsonValue(tags, "contact:phone");
      p.website = JsonValue(tags, "website");
      out.push_back(p);
    }
  } catch (std::exception &e) {
    m_error = e.what();
  }
  std::sort(out.begin(), out.end(), [](const Poi &a, const Poi &b) {
    return a.distance_m < b.distance_m;
  });
  return out;
}

Weather SeachartDb::LoadWeather() {
  Weather w;
  if (!m_db) return w;
  try {
    SQLite::Statement q(*m_db,
                        "SELECT lat, lon, time, wind_kn, wind_dir, gust_kn,"
                        " wave_m FROM weather ORDER BY lat, lon, time");
    Weather::Series cur;
    bool have = false;
    while (q.executeStep()) {
      double lat = q.getColumn(0).getDouble();
      double lon = q.getColumn(1).getDouble();
      time_t t = (time_t)q.getColumn(2).getInt64();
      if (!have || lat != cur.lat || lon != cur.lon) {
        if (have) w.Add(std::move(cur));
        cur = Weather::Series();
        cur.lat = lat;
        cur.lon = lon;
        cur.t0 = t;
        have = true;
      }
      // fill gaps so that hours[i] is always t0 + i hours
      size_t idx = (size_t)std::max<time_t>(0, (t - cur.t0) / 3600);
      if (idx >= cur.hours.size()) cur.hours.resize(idx + 1);
      WeatherSample &s = cur.hours[idx];
      s.valid = !q.getColumn(3).isNull();
      s.wind_kn = q.getColumn(3).getDouble();
      s.wind_dir = q.getColumn(4).getDouble();
      s.gust_kn =
          q.getColumn(5).isNull() ? s.wind_kn : q.getColumn(5).getDouble();
      s.wave_m = q.getColumn(6).isNull() ? -1 : q.getColumn(6).getDouble();
    }
    if (have) w.Add(std::move(cur));
    SQLite::Statement m(*m_db, "SELECT max(fetched) FROM weather_meta");
    if (m.executeStep() && !m.getColumn(0).isNull())
      w.SetFetched((time_t)m.getColumn(0).getInt64());
  } catch (std::exception &e) {
    m_error = e.what();
  }
  return w;
}

NavGrid SeachartDb::LoadNavGrid(double lat1, double lon1, double lat2,
                                double lon2) {
  NavGrid best;
  if (!m_db) return best;
  try {
    SQLite::Statement q(*m_db,
                        "SELECT lon0, lat0, dlon, dlat, nx, ny, data FROM"
                        " navgrid ORDER BY dlat");
    while (q.executeStep()) {
      NavGrid g;
      g.lon0 = q.getColumn(0).getDouble();
      g.lat0 = q.getColumn(1).getDouble();
      g.dlon = q.getColumn(2).getDouble();
      g.dlat = q.getColumn(3).getDouble();
      g.nx = q.getColumn(4).getInt();
      g.ny = q.getColumn(5).getInt();
      if (g.nx <= 0 || g.ny <= 0 || g.dlon <= 0 || g.dlat <= 0) continue;
      if (!g.Contains(lat1, lon1) || !g.Contains(lat2, lon2)) continue;
      SQLite::Column blob = q.getColumn(6);
      size_t n = (size_t)g.nx * g.ny;
      if ((size_t)blob.getBytes() != n * sizeof(float)) continue;
      g.data.resize(n);
      // stored little-endian float32, as on all supported platforms
      std::memcpy(g.data.data(), blob.getBlob(), n * sizeof(float));
      return g;  // ordered by resolution: the first match is the finest
    }
  } catch (std::exception &e) {
    m_error = e.what();
  }
  return best;
}

}  // namespace seachart
