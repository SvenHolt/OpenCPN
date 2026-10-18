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
 * Access to the offline database written by tools/seachart: harbours and
 * anchorages maintained by the user (with photos), points of interest,
 * the weather forecast grid and the navigation grid used by the passage
 * planner.  The table layout is defined in tools/seachart/seachart/store.py.
 */

#ifndef SEACHART_DB_H_
#define SEACHART_DB_H_

#include <cmath>
#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace SQLite {
class Database;
}

namespace seachart {

/** A marina, harbour, anchorage or mooring buoy. */
struct Harbour {
  int64_t id = 0;
  std::string kind = "marina";  ///< marina, harbour, anchorage, buoy
  std::string name;
  double lat = 0, lon = 0;
  std::string notes, vhf, phone, website, facilities;
  double depth = -1;  ///< metres, < 0 = unknown
  int berths = -1;    ///< < 0 = unknown
  int rating = 0;     ///< 1..5, 0 = none
  bool edited = false;
};

struct Photo {
  int64_t id = 0;
  std::string path;  ///< absolute path
  std::string caption;
};

struct Poi {
  std::string category, name;
  double lat = 0, lon = 0, distance_m = 0;
  std::string opening_hours, phone, website;
};

/** Forecast at one place and time. */
struct WeatherSample {
  bool valid = false;
  double wind_kn = 0, wind_dir = 0;  ///< direction the wind comes from
  double gust_kn = 0;
  double wave_m = -1;  ///< < 0 = unknown
};

/**
 * Raster of the sea area: < 0 land, 0 water of unknown depth, > 0 depth in
 * metres.  Row 0 is the southern row.
 */
struct NavGrid {
  double lon0 = 0, lat0 = 0, dlon = 0, dlat = 0;
  int nx = 0, ny = 0;
  std::vector<float> data;

  bool Empty() const { return data.empty(); }
  float At(int ix, int iy) const { return data[(size_t)iy * nx + ix]; }
  bool Inside(int ix, int iy) const {
    return ix >= 0 && iy >= 0 && ix < nx && iy < ny;
  }
  double Lon(int ix) const { return lon0 + ix * dlon; }
  double Lat(int iy) const { return lat0 + iy * dlat; }
  int Ix(double lon) const { return (int)std::lround((lon - lon0) / dlon); }
  int Iy(double lat) const { return (int)std::lround((lat - lat0) / dlat); }
  bool Contains(double lat, double lon) const {
    return Inside(Ix(lon), Iy(lat));
  }
};

/** Hourly forecast on the stored grid points, interpolated on request. */
class Weather {
public:
  bool Empty() const { return m_points.empty(); }
  /** Bilinear in space between grid points, linear in time. */
  WeatherSample At(double lat, double lon, time_t t) const;
  time_t Fetched() const { return m_fetched; }
  time_t First() const { return m_first; }
  time_t Last() const { return m_last; }

  struct Series {
    double lat = 0, lon = 0;
    time_t t0 = 0;  ///< time of the first hourly value
    std::vector<WeatherSample> hours;
  };
  void Add(Series s);
  void SetFetched(time_t t) { m_fetched = t; }

private:
  WeatherSample AtPoint(const Series &s, time_t t) const;
  const Series *Find(double lat, double lon) const;
  std::vector<Series> m_points;
  std::vector<double> m_lats, m_lons;  ///< sorted unique grid coordinates
  std::unordered_map<int64_t, size_t> m_index;
  time_t m_fetched = 0, m_first = 0, m_last = 0;
};

class SeachartDb {
public:
  SeachartDb();
  ~SeachartDb();

  /** Open (and create if needed) the database.  */
  bool Open(const std::string &path, std::string *error = nullptr);
  bool IsOpen() const { return m_db != nullptr; }
  const std::string &Path() const { return m_path; }

  std::vector<Harbour> Harbours();
  bool SaveHarbour(Harbour &h);  ///< insert (id == 0) or update
  bool DeleteHarbour(int64_t id);

  std::vector<Photo> Photos(int64_t harbour_id);
  /** Copy an image next to the database and link it to the harbour. */
  bool AddPhoto(int64_t harbour_id, const std::string &file,
                const std::string &caption, std::string *error = nullptr);
  bool DeletePhoto(int64_t photo_id);

  std::vector<Poi> PoisNear(double lat, double lon, double radius_m);

  /** Load the complete forecast into memory. */
  Weather LoadWeather();
  /** Finest navigation grid containing both positions (empty if none). */
  NavGrid LoadNavGrid(double lat1, double lon1, double lat2, double lon2);

  const std::string &LastError() const { return m_error; }

private:
  std::unique_ptr<SQLite::Database> m_db;
  std::string m_path, m_error;
};

/** Distance in metres (equirectangular, fine for harbour distances). */
double DistanceM(double lat1, double lon1, double lat2, double lon2);

}  // namespace seachart

#endif  // SEACHART_DB_H_
