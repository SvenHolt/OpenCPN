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
 * Weather dependent passage routing from harbour to harbour.
 *
 * The route is searched on the navigation grid (land, depth) with a time
 * dependent A* search: the cost of a step is the time the boat needs for
 * it, given the forecast wind at the time it gets there.  Sailing speed
 * comes from a simple polar (no-go zone, reaching is fastest, speed grows
 * with the wind up to the hull speed), reduced in waves; below a threshold
 * the engine is used.  Tacking emerges from the search because direct
 * upwind steps are impossible under sail.
 */

#ifndef PASSAGE_ROUTER_H_
#define PASSAGE_ROUTER_H_

#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include "seachart_db.h"

namespace seachart {

struct BoatModel {
  double hull_speed_kn = 6.5;  ///< best speed under sail
  double design_wind_kn = 14;  ///< true wind giving hull speed on a reach
  double no_go_deg = 40;       ///< closest angle to the true wind
  bool motor = true;           ///< use the engine when sailing is slow
  double motor_speed_kn = 5.5;
  double motor_below_kn = 3.0;  ///< motor when sailing slower than this
  double draft_m = 1.8;
  double safety_m = 0.5;        ///< extra water under the keel
  double max_wind_kn = 25;      ///< warn above this mean wind
  double max_gust_kn = 32;      ///< warn above these gusts
  double default_speed_kn = 5;  ///< without weather data

  /** Speed under sail for true wind speed/angle, 0 inside the no-go zone. */
  double SailSpeed(double tws, double twa_deg, double wave_m) const;
};

enum class Propulsion { kSail, kMotor, kUnknown };

/** One leg between two consecutive route points. */
struct PassageLeg {
  double lat0 = 0, lon0 = 0, lat1 = 0, lon1 = 0;
  time_t start = 0, end = 0;
  double distance_nm = 0, course_deg = 0, speed_kn = 0;
  double avg_wind_kn = 0, wind_dir = 0, max_gust_kn = 0, max_wave_m = -1;
  double twa_deg = 0;
  double motor_nm = 0;  ///< part of the leg under engine
  bool warning = false;
};

struct PassagePlan {
  bool ok = false;
  std::string error;
  std::vector<std::pair<double, double>> points;  ///< lat, lon
  std::vector<PassageLeg> legs;
  time_t departure = 0, arrival = 0;
  double distance_nm = 0, motor_nm = 0;
  double max_wind_kn = 0, max_gust_kn = 0, max_wave_m = -1;
  bool used_weather = false;     ///< forecast available for the passage
  bool beyond_forecast = false;  ///< part of the passage after the forecast
  bool grid_checked = false;     ///< route checked against land and depth
  int warnings = 0;

  double Hours() const { return (arrival - departure) / 3600.0; }
};

class PassageRouter {
public:
  PassageRouter(const NavGrid &grid, const Weather &weather,
                const BoatModel &boat);

  PassagePlan Plan(double lat0, double lon0, double lat1, double lon1,
                   time_t departure) const;

  /** Speed (kn) and propulsion for a course at a place and time. */
  double Speed(double lat, double lon, double course_deg, time_t t,
               Propulsion *mode, WeatherSample *wx = nullptr) const;

private:
  bool Passable(int ix, int iy) const;
  bool Snap(double lat, double lon, int &ix, int &iy) const;
  bool LineClear(double lat0, double lon0, double lat1, double lon1) const;
  /** Simulate a straight leg in short steps. */
  PassageLeg Sail(double lat0, double lon0, double lat1, double lon1,
                  time_t start) const;
  std::vector<std::pair<double, double>> Search(int sx, int sy, int ex, int ey,
                                                time_t departure,
                                                std::vector<double> &times,
                                                std::string &error) const;

  const NavGrid &m_grid;
  const Weather &m_weather;
  BoatModel m_boat;
  double m_min_depth;
};

/** Great circle approximations used by the planner (nautical miles). */
double DistanceNm(double lat0, double lon0, double lat1, double lon1);
double CourseDeg(double lat0, double lon0, double lat1, double lon1);

}  // namespace seachart

#endif  // PASSAGE_ROUTER_H_
