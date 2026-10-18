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

/** \file Tests for the passage planner: seachart database and router. */

#include <cstdio>
#include <fstream>

#include <gtest/gtest.h>

#include "std_filesystem.h"

#include "passage_router.h"
#include "seachart_db.h"

using namespace seachart;

namespace {

const time_t kT0 = 1800000000;  // some full hour

/** 0.2 x 0.2 degree of water at 54 N with an island in the middle. */
NavGrid IslandGrid() {
  NavGrid g;
  g.lon0 = 10.0;
  g.lat0 = 54.0;
  g.dlon = g.dlat = 0.005;
  g.nx = g.ny = 41;
  g.data.assign(g.nx * g.ny, 10.0f);
  for (int y = 15; y <= 25; y++)
    for (int x = 15; x <= 25; x++) g.data[y * g.nx + x] = -1;
  return g;
}

Weather ConstantWind(double kn, double from_deg) {
  Weather w;
  for (double lat : {53.9, 54.3})
    for (double lon : {9.9, 10.3}) {
      Weather::Series s;
      s.lat = lat;
      s.lon = lon;
      s.t0 = kT0;
      for (int h = 0; h < 48; h++) {
        WeatherSample ws;
        ws.valid = true;
        ws.wind_kn = kn;
        ws.wind_dir = from_deg;
        ws.gust_kn = kn * 1.4;
        s.hours.push_back(ws);
      }
      w.Add(s);
    }
  return w;
}

bool CrossesLand(const NavGrid &g, const PassagePlan &p) {
  for (size_t i = 0; i + 1 < p.points.size(); i++)
    for (int k = 0; k <= 50; k++) {
      double f = k / 50.0;
      double lat =
          p.points[i].first + f * (p.points[i + 1].first - p.points[i].first);
      double lon = p.points[i].second +
                   f * (p.points[i + 1].second - p.points[i].second);
      int x = g.Ix(lon), y = g.Iy(lat);
      if (g.Inside(x, y) && g.At(x, y) < 0) return true;
    }
  return false;
}

}  // namespace

TEST(PassageRouter, AvoidsLand) {
  NavGrid g = IslandGrid();
  Weather w = ConstantWind(12, 0);  // beam reach west -> east
  BoatModel boat;
  boat.motor = false;
  PassageRouter r(g, w, boat);
  PassagePlan p = r.Plan(54.1, 10.01, 54.1, 10.19, kT0);
  ASSERT_TRUE(p.ok) << p.error;
  EXPECT_TRUE(p.grid_checked);
  EXPECT_FALSE(CrossesLand(g, p));
  double direct = DistanceNm(54.1, 10.01, 54.1, 10.19);
  EXPECT_GT(p.distance_nm, direct);        // around the island
  EXPECT_LT(p.distance_nm, direct * 1.6);  // but not a detour
  EXPECT_NEAR(p.motor_nm, 0, 1e-6);        // engine disabled
  EXPECT_TRUE(p.used_weather);
}

TEST(PassageRouter, TacksUpwindWithoutEngine) {
  NavGrid g = IslandGrid();
  g.data.assign(g.nx * g.ny, 10.0f);  // open water
  Weather w = ConstantWind(12, 0);    // wind from north
  BoatModel boat;
  boat.motor = false;
  PassageRouter r(g, w, boat);
  PassagePlan p = r.Plan(54.01, 10.1, 54.19, 10.1, kT0);
  ASSERT_TRUE(p.ok) << p.error;
  // dead upwind: the boat has to tack, legs are off the direct course
  bool tacked = false;
  for (const auto &leg : p.legs)
    if (leg.distance_nm > 0.5 && leg.twa_deg >= boat.no_go_deg - 1)
      tacked = true;
  EXPECT_TRUE(tacked);
  EXPECT_GT(p.distance_nm, DistanceNm(54.01, 10.1, 54.19, 10.1) * 1.2);
}

TEST(PassageRouter, MotorsInCalm) {
  NavGrid g = IslandGrid();
  g.data.assign(g.nx * g.ny, 10.0f);
  Weather w = ConstantWind(1, 0);
  BoatModel boat;
  PassageRouter r(g, w, boat);
  PassagePlan p = r.Plan(54.05, 10.05, 54.15, 10.15, kT0);
  ASSERT_TRUE(p.ok) << p.error;
  EXPECT_NEAR(p.motor_nm, p.distance_nm, 0.01);
  EXPECT_NEAR(p.distance_nm / p.Hours(), boat.motor_speed_kn, 0.2);
}

TEST(PassageRouter, RespectsDraft) {
  NavGrid g = IslandGrid();
  g.data.assign(g.nx * g.ny, 10.0f);
  for (int y = 0; y < g.ny; y++) g.data[y * g.nx + 20] = 1.5f;  // shoal
  g.data[38 * g.nx + 20] = 4.0f;                                // channel
  Weather w;
  BoatModel boat;  // draft 1.8 m + 0.5 m
  PassageRouter r(g, w, boat);
  PassagePlan p = r.Plan(54.1, 10.05, 54.1, 10.15, kT0);
  ASSERT_TRUE(p.ok) << p.error;
  double north = 0;
  for (const auto &pt : p.points) north = std::max(north, pt.first);
  EXPECT_GT(north, 54.17);  // through the channel in the north
  EXPECT_FALSE(p.used_weather);
}

TEST(PassageRouter, NoConnection) {
  NavGrid g = IslandGrid();
  for (int y = 0; y < g.ny; y++) g.data[y * g.nx + 20] = -1;  // wall
  Weather w;
  PassageRouter r(g, w, BoatModel());
  PassagePlan p = r.Plan(54.1, 10.05, 54.1, 10.15, kT0);
  EXPECT_FALSE(p.ok);
  EXPECT_FALSE(p.error.empty());
}

TEST(Weather, InterpolatesSpaceAndTime) {
  Weather w;
  Weather::Series a, b;
  a.lat = b.lat = 54;
  a.lon = 10;
  b.lon = 10.25;
  a.t0 = b.t0 = kT0;
  for (int h = 0; h < 2; h++) {
    WeatherSample s;
    s.valid = true;
    s.wind_dir = 350;
    s.wind_kn = h == 0 ? 10 : 20;
    s.gust_kn = 0;
    a.hours.push_back(s);
    s.wind_dir = 10;
    b.hours.push_back(s);
  }
  w.Add(a);
  w.Add(b);
  WeatherSample s = w.At(54, 10.125, kT0 + 1800);
  ASSERT_TRUE(s.valid);
  EXPECT_NEAR(s.wind_kn, 15, 0.01);
  EXPECT_LT(std::min(s.wind_dir, 360 - s.wind_dir), 1.0);  // ~ north
}

TEST(SeachartDb, HarboursPhotosWeather) {
  fs::path dir = fs::temp_directory_path() / "seachart_db_test";
  fs::remove_all(dir);
  fs::create_directories(dir);
  std::string path = (dir / "seachart.db").u8string();
  {
    SeachartDb db;
    ASSERT_TRUE(db.Open(path));
    Harbour h;
    h.kind = "anchorage";
    h.name = "Bucht";
    h.lat = 54.4;
    h.lon = 10.2;
    h.notes = "Sandgrund";
    ASSERT_TRUE(db.SaveHarbour(h));
    EXPECT_GT(h.id, 0);
    std::string img = (dir / "photo.jpg").u8string();
    std::ofstream(img) << "x";
    ASSERT_TRUE(db.AddPhoto(h.id, img, "Einfahrt"));
    auto photos = db.Photos(h.id);
    ASSERT_EQ(photos.size(), 1u);
    EXPECT_TRUE(fs::exists(fs::u8path(photos[0].path)));
    EXPECT_EQ(photos[0].caption, "Einfahrt");
    h.depth = 3.5;
    ASSERT_TRUE(db.SaveHarbour(h));
  }
  SeachartDb db;
  ASSERT_TRUE(db.Open(path));
  auto all = db.Harbours();
  ASSERT_EQ(all.size(), 1u);
  EXPECT_EQ(all[0].name, "Bucht");
  EXPECT_DOUBLE_EQ(all[0].depth, 3.5);
  EXPECT_TRUE(all[0].edited);
  EXPECT_TRUE(db.LoadWeather().Empty());
  EXPECT_TRUE(db.LoadNavGrid(54, 10, 54.1, 10.1).Empty());
  ASSERT_TRUE(db.DeleteHarbour(all[0].id));
  EXPECT_TRUE(db.Harbours().empty());
  fs::remove_all(dir);
}
