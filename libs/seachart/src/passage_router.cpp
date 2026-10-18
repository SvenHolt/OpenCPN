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
 * Implement passage_router.h -- weather dependent harbour to harbour routing.
 */

#include <algorithm>
#include <cmath>
#include <queue>

#include "passage_router.h"

namespace seachart {

static constexpr double kPi = 3.14159265358979323846;
static constexpr double kDeg = kPi / 180.0;
/** Search radius for the nearest water cell around a harbour. */
static constexpr double kSnapRadiusM = 5000;

double DistanceNm(double lat0, double lon0, double lat1, double lon1) {
  double k = std::cos((lat0 + lat1) / 2 * kDeg);
  return std::hypot((lon1 - lon0) * k, lat1 - lat0) * 60.0;
}

double CourseDeg(double lat0, double lon0, double lat1, double lon1) {
  double k = std::cos((lat0 + lat1) / 2 * kDeg);
  double c = std::atan2((lon1 - lon0) * k, lat1 - lat0) / kDeg;
  return c < 0 ? c + 360 : c;
}

static double Interp(const double (*table)[2], int n, double x) {
  if (x <= table[0][0]) return table[0][1];
  for (int i = 1; i < n; i++) {
    if (x <= table[i][0]) {
      double f = (x - table[i - 1][0]) / (table[i][0] - table[i - 1][0]);
      return table[i - 1][1] + f * (table[i][1] - table[i - 1][1]);
    }
  }
  return table[n - 1][1];
}

double BoatModel::SailSpeed(double tws, double twa, double wave_m) const {
  if (tws < 2 || twa < no_go_deg) return 0;
  double ng = std::min(no_go_deg, 70.0);
  // fraction of the hull speed over the true wind angle
  const double polar[][2] = {{ng, 0.6},  {ng + 15, 0.82}, {90, 1.0},
                             {120, 1.0}, {150, 0.88},     {180, 0.75}};
  double ratio = Interp(polar, 6, twa);
  double wind = std::min(1.0, std::pow(tws / design_wind_kn, 0.75));
  double waves = 1.0;
  if (wave_m > 0)
    waves = std::max(0.4, 1.0 - (twa < 90 ? 0.12 : 0.05) * wave_m);
  return hull_speed_kn * ratio * wind * waves;
}

PassageRouter::PassageRouter(const NavGrid &grid, const Weather &weather,
                             const BoatModel &boat)
    : m_grid(grid),
      m_weather(weather),
      m_boat(boat),
      m_min_depth(boat.draft_m + boat.safety_m) {}

double PassageRouter::Speed(double lat, double lon, double course, time_t t,
                            Propulsion *mode, WeatherSample *wx) const {
  WeatherSample w;
  if (!m_weather.Empty()) w = m_weather.At(lat, lon, t);
  if (wx) *wx = w;
  if (!w.valid) {
    if (mode) *mode = Propulsion::kUnknown;
    return m_boat.default_speed_kn;
  }
  double twa = std::fabs(std::fmod(w.wind_dir - course + 540.0, 360.0) - 180);
  double sail = m_boat.SailSpeed(w.wind_kn, twa, w.wave_m);
  if (m_boat.motor && sail < m_boat.motor_below_kn) {
    double motor = m_boat.motor_speed_kn;
    // head seas slow the engine down, too
    if (w.wave_m > 0 && twa < 60) motor *= std::max(0.5, 1.0 - 0.1 * w.wave_m);
    if (motor > sail) {
      if (mode) *mode = Propulsion::kMotor;
      return motor;
    }
  }
  if (mode) *mode = Propulsion::kSail;
  return sail;
}

bool PassageRouter::Passable(int ix, int iy) const {
  if (!m_grid.Inside(ix, iy)) return false;
  float v = m_grid.At(ix, iy);
  return v == 0 || v >= m_min_depth;
}

bool PassageRouter::Snap(double lat, double lon, int &ix, int &iy) const {
  int cx = m_grid.Ix(lon), cy = m_grid.Iy(lat);
  double best = kSnapRadiusM;
  bool found = false;
  int ry = (int)std::ceil(kSnapRadiusM / 111320.0 / m_grid.dlat);
  int rx = (int)std::ceil(kSnapRadiusM / 111320.0 /
                          std::max(0.1, std::cos(lat * kDeg)) / m_grid.dlon);
  for (int y = cy - ry; y <= cy + ry; y++) {
    for (int x = cx - rx; x <= cx + rx; x++) {
      if (!Passable(x, y)) continue;
      double d = DistanceM(lat, lon, m_grid.Lat(y), m_grid.Lon(x));
      if (d < best) {
        best = d;
        ix = x;
        iy = y;
        found = true;
      }
    }
  }
  return found;
}

bool PassageRouter::LineClear(double lat0, double lon0, double lat1,
                              double lon1) const {
  double cells = std::max(std::fabs(lat1 - lat0) / m_grid.dlat,
                          std::fabs(lon1 - lon0) / m_grid.dlon);
  int n = std::max(1, (int)std::ceil(cells * 2));
  for (int i = 0; i <= n; i++) {
    double f = double(i) / n;
    if (!Passable(m_grid.Ix(lon0 + f * (lon1 - lon0)),
                  m_grid.Iy(lat0 + f * (lat1 - lat0))))
      return false;
  }
  return true;
}

PassageLeg PassageRouter::Sail(double lat0, double lon0, double lat1,
                               double lon1, time_t start) const {
  PassageLeg leg;
  leg.lat0 = lat0;
  leg.lon0 = lon0;
  leg.lat1 = lat1;
  leg.lon1 = lon1;
  leg.start = start;
  leg.distance_nm = DistanceNm(lat0, lon0, lat1, lon1);
  leg.course_deg = CourseDeg(lat0, lon0, lat1, lon1);
  int steps = std::max(1, (int)std::ceil(leg.distance_nm / 0.5));
  double step_nm = leg.distance_nm / steps;
  double t = (double)start, wx_x = 0, wx_y = 0, twa_sum = 0, wind_nm = 0;
  for (int i = 0; i < steps; i++) {
    double f = (i + 0.5) / steps;
    double lat = lat0 + f * (lat1 - lat0), lon = lon0 + f * (lon1 - lon0);
    Propulsion mode;
    WeatherSample w;
    double v = Speed(lat, lon, leg.course_deg, (time_t)t, &mode, &w);
    if (v <= 0.05) {
      // dead upwind without engine: tack, roughly 35 % of the hull speed
      // made good towards the target
      v = std::max(0.5, 0.35 * m_boat.hull_speed_kn);
    }
    if (mode == Propulsion::kMotor) leg.motor_nm += step_nm;
    if (w.valid) {
      wx_x += step_nm * w.wind_kn * std::sin(w.wind_dir * kDeg);
      wx_y += step_nm * w.wind_kn * std::cos(w.wind_dir * kDeg);
      leg.avg_wind_kn += step_nm * w.wind_kn;
      twa_sum +=
          step_nm *
          std::fabs(std::fmod(w.wind_dir - leg.course_deg + 540.0, 360.0) -
                    180);
      wind_nm += step_nm;
      leg.max_gust_kn = std::max(leg.max_gust_kn, w.gust_kn);
      leg.max_wave_m = std::max(leg.max_wave_m, w.wave_m);
    }
    t += step_nm / v * 3600.0;
  }
  leg.end = (time_t)std::llround(t);
  if (wind_nm > 0) {
    leg.avg_wind_kn /= wind_nm;
    leg.twa_deg = twa_sum / wind_nm;
    leg.wind_dir = std::fmod(std::atan2(wx_x, wx_y) / kDeg + 360, 360);
  }
  double hours = (leg.end - leg.start) / 3600.0;
  leg.speed_kn = hours > 0 ? leg.distance_nm / hours : 0;
  leg.warning = leg.avg_wind_kn > m_boat.max_wind_kn ||
                leg.max_gust_kn > m_boat.max_gust_kn;
  return leg;
}

std::vector<std::pair<double, double>> PassageRouter::Search(
    int sx, int sy, int ex, int ey, time_t departure,
    std::vector<double> &times, std::string &error) const {
  static const int kMoves[16][2] = {
      {1, 0}, {-1, 0}, {0, 1},  {0, -1},  {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
      {2, 1}, {2, -1}, {-2, 1}, {-2, -1}, {1, 2}, {1, -2}, {-1, 2}, {-1, -2}};
  const int nx = m_grid.nx;
  const size_t n = (size_t)m_grid.nx * m_grid.ny;
  std::vector<double> g(n, 1e300);
  std::vector<int64_t> parent(n, -1);
  std::vector<char> closed(n, 0);
  double vmax = m_boat.hull_speed_kn;
  if (m_boat.motor) vmax = std::max(vmax, m_boat.motor_speed_kn);
  if (m_weather.Empty()) vmax = m_boat.default_speed_kn;
  vmax = std::max(vmax, m_boat.default_speed_kn);
  double elat = m_grid.Lat(ey), elon = m_grid.Lon(ex);
  auto h = [&](int x, int y) {
    return DistanceNm(m_grid.Lat(y), m_grid.Lon(x), elat, elon) / vmax * 3600.0;
  };
  using Item = std::pair<double, int64_t>;
  std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
  int64_t start = (int64_t)sy * nx + sx, goal = (int64_t)ey * nx + ex;
  g[start] = 0;
  open.push({h(sx, sy), start});
  while (!open.empty()) {
    int64_t cur = open.top().second;
    open.pop();
    if (closed[cur]) continue;
    closed[cur] = 1;
    if (cur == goal) break;
    int cx = (int)(cur % nx), cy = (int)(cur / nx);
    double clat = m_grid.Lat(cy), clon = m_grid.Lon(cx);
    for (const auto &mv : kMoves) {
      int x = cx + mv[0], y = cy + mv[1];
      if (!Passable(x, y)) continue;
      // knight moves cross two cells: both must be water
      if (std::abs(mv[0]) == 2 && !Passable(cx + mv[0] / 2, y)) continue;
      if (std::abs(mv[1]) == 2 && !Passable(x, cy + mv[1] / 2)) continue;
      if (std::abs(mv[0]) + std::abs(mv[1]) == 2 &&
          (!Passable(x, cy) || !Passable(cx, y)))
        continue;  // no corner cutting
      int64_t nb = (int64_t)y * nx + x;
      if (closed[nb]) continue;
      double lat = m_grid.Lat(y), lon = m_grid.Lon(x);
      double dist = DistanceNm(clat, clon, lat, lon);
      double v = Speed((clat + lat) / 2, (clon + lon) / 2,
                       CourseDeg(clat, clon, lat, lon),
                       departure + (time_t)g[cur], nullptr);
      if (v <= 0.05) continue;
      double ng = g[cur] + dist / v * 3600.0;
      if (ng < g[nb]) {
        g[nb] = ng;
        parent[nb] = cur;
        open.push({ng + h(x, y), nb});
      }
    }
  }
  std::vector<std::pair<double, double>> path;
  if (!closed[goal]) {
    error = "no connection by water was found";
    return path;
  }
  for (int64_t c = goal; c >= 0; c = parent[c]) {
    int x = (int)(c % nx), y = (int)(c / nx);
    path.push_back({m_grid.Lat(y), m_grid.Lon(x)});
    times.push_back(g[c]);
  }
  std::reverse(path.begin(), path.end());
  std::reverse(times.begin(), times.end());
  return path;
}

PassagePlan PassageRouter::Plan(double lat0, double lon0, double lat1,
                                double lon1, time_t departure) const {
  PassagePlan plan;
  plan.departure = departure;
  std::vector<std::pair<double, double>> pts;
  if (m_grid.Empty()) {
    pts = {{lat0, lon0}, {lat1, lon1}};
  } else {
    int sx = 0, sy = 0, ex = 0, ey = 0;
    if (!Snap(lat0, lon0, sx, sy)) {
      plan.error = "no navigable water near the departure";
      return plan;
    }
    if (!Snap(lat1, lon1, ex, ey)) {
      plan.error = "no navigable water near the destination";
      return plan;
    }
    std::vector<double> times;
    auto path = Search(sx, sy, ex, ey, departure, times, plan.error);
    if (path.empty()) return plan;
    plan.grid_checked = true;
    // Straighten the staircase of grid cells: skip points as long as the
    // shortcut stays in the water and is not slower (which keeps tacks).
    pts.push_back({lat0, lon0});
    size_t i = 0;
    while (i + 1 < path.size()) {
      size_t best = i + 1;
      int misses = 0;
      for (size_t j = i + 2; j < path.size() && misses < 40; j++) {
        bool ok = LineClear(path[i].first, path[i].second, path[j].first,
                            path[j].second);
        if (ok) {
          PassageLeg leg = Sail(path[i].first, path[i].second, path[j].first,
                                path[j].second, departure + (time_t)times[i]);
          ok = (leg.end - leg.start) <= (times[j] - times[i]) * 1.02 + 60;
        }
        if (ok) {
          best = j;
          misses = 0;
        } else {
          misses++;
        }
      }
      pts.push_back(path[i]);
      i = best;
    }
    pts.push_back(path.back());
    pts.push_back({lat1, lon1});
    // drop points that coincide (harbour on the snapped cell, ...)
    std::vector<std::pair<double, double>> uniq;
    for (const auto &p : pts)
      if (uniq.empty() || DistanceM(uniq.back().first, uniq.back().second,
                                    p.first, p.second) > 30)
        uniq.push_back(p);
    if (uniq.size() == 1) uniq.push_back({lat1, lon1});
    // merge almost straight continuations (grid artefacts)
    std::vector<std::pair<double, double>> merged{uniq.front()};
    for (size_t k = 1; k + 1 < uniq.size(); k++) {
      const auto &a = merged.back(), &b = uniq[k], &c = uniq[k + 1];
      double turn = std::fabs(
          std::fmod(CourseDeg(a.first, a.second, b.first, b.second) -
                        CourseDeg(b.first, b.second, c.first, c.second) + 540.0,
                    360.0) -
          180.0);
      bool inner = merged.size() > 1 && k + 2 < uniq.size();
      // a straight continuation covers the cells already checked by the
      // search; slight bends are checked again
      bool straight = turn < 2 || (turn < 8 && LineClear(a.first, a.second,
                                                         c.first, c.second));
      if (!(inner && straight)) merged.push_back(b);
    }
    merged.push_back(uniq.back());
    pts.swap(merged);
  }
  time_t t = departure;
  for (size_t k = 0; k + 1 < pts.size(); k++) {
    PassageLeg leg = Sail(pts[k].first, pts[k].second, pts[k + 1].first,
                          pts[k + 1].second, t);
    t = leg.end;
    plan.distance_nm += leg.distance_nm;
    plan.motor_nm += leg.motor_nm;
    plan.max_wind_kn = std::max(plan.max_wind_kn, leg.avg_wind_kn);
    plan.max_gust_kn = std::max(plan.max_gust_kn, leg.max_gust_kn);
    plan.max_wave_m = std::max(plan.max_wave_m, leg.max_wave_m);
    if (leg.warning) plan.warnings++;
    plan.legs.push_back(leg);
  }
  plan.points = pts;
  plan.arrival = t;
  plan.used_weather = !m_weather.Empty() &&
                      m_weather.First() <= departure + 3600 &&
                      m_weather.Last() + 3600 >= departure;
  plan.beyond_forecast = m_weather.Empty() || t > m_weather.Last() + 3600;
  plan.ok = true;
  return plan;
}

}  // namespace seachart
