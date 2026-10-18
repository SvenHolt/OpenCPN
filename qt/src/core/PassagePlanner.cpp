#include "core/PassagePlanner.h"

#include <QCoreApplication>
#include <algorithm>
#include <cmath>

#include "core/GeoMath.h"

namespace {
constexpr double kDeg = M_PI / 180;
constexpr double kSegmentNm = 2.0;  // wind is sampled along the legs
constexpr double kStrongWindKn = 25;

QString tr(const char* s) {
  return QCoreApplication::translate("PassagePlanner", s);
}

double angleDiff(double a, double b) {
  double d = std::fmod(std::fabs(a - b), 360.0);
  return d > 180 ? 360 - d : d;
}

/** Relative boat speed over the true wind angle (1 = beam reach). */
double polarFactor(double twa) {
  static const double pts[][2] = {
      {0, 0},      {30, 0},     {40, 0.55},  {45, 0.72}, {52, 0.82},
      {60, 0.9},   {75, 0.97},  {90, 1.0},   {110, 1.0}, {120, 0.97},
      {135, 0.92}, {150, 0.85}, {165, 0.75}, {180, 0.7}};
  for (size_t i = 1; i < std::size(pts); ++i)
    if (twa <= pts[i][0]) {
      const double f = (twa - pts[i - 1][0]) / (pts[i][0] - pts[i - 1][0]);
      return pts[i - 1][1] + f * (pts[i][1] - pts[i - 1][1]);
    }
  return pts[std::size(pts) - 1][1];
}

/** Sailed distance for a leg against (or before) the wind on two boards
 *  at ±`boardDeg` around the wind axis; `offDeg` is the angle between the
 *  leg and the wind axis. Returns the total and the shorter board. */
double boards(double legNm, double boardDeg, double offDeg, double* minor) {
  const double b = boardDeg * kDeg, a = offDeg * kDeg;
  const double s = std::sin(2 * b);
  const double d1 = legNm * std::sin(b + a) / s;
  const double d2 = legNm * std::sin(b - a) / s;
  if (minor) *minor = std::max(0.0, std::min(d1, d2));
  return std::fabs(d1) + std::fabs(d2);
}

bool isNight(double lat, double lon, const QDateTime& t) {
  QDateTime rise, set;
  PassagePlanner::sunTimes(lat, lon, t.toUTC().date(), rise, set);
  if (!rise.isValid() || !set.isValid()) {
    // Polar day or night: decide by the sun's declination sign.
    const int doy = t.toUTC().date().dayOfYear();
    const bool summer = doy > 80 && doy < 266;
    return (lat > 0) != summer;
  }
  if (set < rise) return t > set && t < rise;  // sunset before sunrise (UTC)
  return t < rise || t > set;
}
}  // namespace

double PassagePlanner::boatSpeed(const PassageSettings& s, double windKn,
                                 double twaDeg) {
  if (windKn < 0.5 || twaDeg < s.noGoDeg - 1e-9) return 0;
  // Speed builds up with the wind and approaches the hull speed; more sail
  // area means the same speed in less wind.
  const double eff = windKn * std::sqrt(std::max(0.1, s.sailAreaFactor));
  double v = s.hullSpeedKn * (1 - std::exp(-eff / 8.0)) * 1.08;
  v *= polarFactor(twaDeg);
  if (s.downwindSail && twaDeg >= 100 && twaDeg <= 170) v *= 1.12;
  // Reefed in strong wind, the boat does not get faster any more.
  return std::min(v, s.hullSpeedKn * 1.1);
}

PassagePlan PassagePlanner::plan(const QVector<QPointF>& route,
                                 const PassageSettings& s,
                                 const WindSource& wind) {
  PassagePlan p;
  p.departure = s.departure.toUTC();
  QDateTime t = p.departure;
  for (int i = 1; i < route.size(); ++i) {
    const QPointF a = route[i - 1], b = route[i];
    PassageLeg leg;
    leg.index = i - 1;
    leg.start = t;
    leg.distanceNm = geo::distanceNm(a.y(), a.x(), b.y(), b.x());
    leg.courseDeg = geo::bearingDeg(a.y(), a.x(), b.y(), b.x());
    const int n = std::max(1, int(std::ceil(leg.distanceNm / kSegmentNm)));
    double beatNm = 0, beatMinor = 0, runNm = 0, runMinor = 0;
    double sailH = 0, beatH = 0, runH = 0;
    for (int k = 0; k < n; ++k) {
      const double seg = leg.distanceNm / n;
      const double f = (k + 0.5) / n;
      const double lat = a.y() + (b.y() - a.y()) * f;
      const double lon = a.x() + (b.x() - a.x()) * f;
      double kn = s.manualWindKn, from = s.manualWindFromDeg;
      const bool grib = wind && wind(lat, lon, t, kn, from);
      if (!grib) {
        kn = s.manualWindKn;
        from = s.manualWindFromDeg;
        p.manualUsed = true;
      } else {
        p.gribUsed = true;
      }
      p.maxWindKn = std::max(p.maxWindKn, kn);
      const double twa = angleDiff(leg.courseDeg, from);
      if (k == n / 2) {
        leg.windKn = kn;
        leg.windFromDeg = from;
        leg.twaDeg = twa;
        leg.gribWind = grib;
      }

      // Under sail: direct, beating to windward or gybing downwind.
      PassageLeg::Mode sailMode = PassageLeg::Mode::Sail;
      double sailed = seg, minor = 0, v = 0;
      if (twa < s.noGoDeg) {
        sailMode = PassageLeg::Mode::Beat;
        sailed = boards(seg, s.noGoDeg, twa, &minor);
        v = boatSpeed(s, kn, s.noGoDeg);
      } else if (twa > s.runDeg) {
        sailMode = PassageLeg::Mode::Run;
        sailed = boards(seg, 180 - s.runDeg, 180 - twa, &minor);
        v = boatSpeed(s, kn, s.runDeg);
      } else {
        v = boatSpeed(s, kn, twa);
      }
      const double vmg = v > 0 ? seg / (sailed / v) : 0;

      bool motor = false;
      switch (s.strategy) {
        case PassageSettings::Strategy::Motor:
          motor = true;
          break;
        case PassageSettings::Strategy::MotorSail:
          motor = vmg < s.motorSpeedKn || kn < s.minSailWindKn;
          break;
        case PassageSettings::Strategy::Sail:
          motor = kn < s.minSailWindKn || vmg < s.minSailSpeedKn;
          break;
      }
      double h;
      if (motor) {
        h = seg / s.motorSpeedKn;
        leg.motorHours += h;
        leg.sailedNm += seg;
      } else {
        h = sailed / v;
        leg.sailedNm += sailed;
        if (sailMode == PassageLeg::Mode::Beat) {
          beatNm += sailed;
          beatMinor += minor;
          beatH += h;
        } else if (sailMode == PassageLeg::Mode::Run) {
          runNm += sailed;
          runMinor += minor;
          runH += h;
        } else {
          sailH += h;
        }
      }
      if (isNight(lat, lon, t.addSecs(qint64(h * 1800)))) p.nightHours += h;
      leg.hours += h;
      t = t.addSecs(qint64(std::round(h * 3600)));
    }
    // Tacks/gybes: one per board length, at least one if the shorter
    // board is not negligible.
    if (beatNm > 0 && beatMinor > 0.05)
      leg.tacks = std::max(1, int(std::round(beatNm / s.boardNm)) - 1);
    if (runNm > 0 && runMinor > 0.05)
      leg.gybes = std::max(1, int(std::round(runNm / s.boardNm)) - 1);
    // The leg's mode is the one it spends most time in.
    const double best = std::max({leg.motorHours, sailH, beatH, runH});
    leg.mode = best == leg.motorHours ? PassageLeg::Mode::Motor
               : best == beatH        ? PassageLeg::Mode::Beat
               : best == runH         ? PassageLeg::Mode::Run
                                      : PassageLeg::Mode::Sail;
    leg.speedKn = leg.hours > 0 ? leg.sailedNm / leg.hours : 0;
    leg.end = t;

    p.distanceNm += leg.distanceNm;
    p.sailedNm += leg.sailedNm;
    p.hours += leg.hours;
    p.motorHours += leg.motorHours;
    p.tacks += leg.tacks;
    p.gybes += leg.gybes;
    if (leg.windKn >= kStrongWindKn)
      p.warnings << tr("Strong wind on leg %1: %2 kn")
                        .arg(leg.index + 1)
                        .arg(int(std::round(leg.windKn)));
    p.legs << leg;
  }
  p.arrival = t;
  p.sailHours = p.hours - p.motorHours;
  p.fuelLiters = p.motorHours * s.fuelLph;

  if (!route.isEmpty()) {
    const QPointF dest = route.last();
    sunTimes(dest.y(), dest.x(), t.date(), p.sunrise, p.sunset);
    if (isNight(dest.y(), dest.x(), t)) p.warnings << tr("Arrival in darkness");
  }
  if (p.gribUsed && p.manualUsed)
    p.warnings << tr(
        "The GRIB file does not cover the whole passage; "
        "manual wind used for the rest");
  if (p.maxWindKn >= 30)
    p.warnings << tr("Gale force wind (%1 kn) on the way")
                      .arg(int(std::round(p.maxWindKn)));
  return p;
}

PassagePlan PassagePlanner::planWindow(const QVector<QPointF>& route,
                                       const PassageSettings& s,
                                       const WindSource& wind,
                                       QVector<DepartureOption>* options) {
  if (options) options->clear();
  if (!s.latestDeparture.isValid() || s.latestDeparture <= s.departure) {
    PassagePlan p = plan(route, s, wind);
    if (options)
      *options << DepartureOption{p.departure,  p.hours, p.sailShare(),
                                  p.nightHours, p.tacks, true};
    return p;
  }
  const auto score = [&s](const PassagePlan& p) {
    switch (s.optimize) {
      case PassageSettings::Optimize::Sailing:
        return -p.sailShare() * 10 + p.hours * 0.05;
      case PassageSettings::Optimize::Daylight:
        return p.nightHours * 10 + p.hours;
      default:
        return p.hours + p.motorHours * 0.01;
    }
  };
  PassagePlan best;
  double bestScore = 1e300;
  int bestIndex = -1;
  const int step = std::max(10, s.windowStepMin) * 60;
  PassageSettings cur = s;
  int i = 0;
  for (QDateTime d = s.departure; d <= s.latestDeparture && i < 200;
       d = d.addSecs(step), ++i) {
    cur.departure = d;
    PassagePlan p = plan(route, cur, wind);
    if (options)
      *options << DepartureOption{p.departure,  p.hours, p.sailShare(),
                                  p.nightHours, p.tacks, false};
    const double sc = score(p);
    if (sc < bestScore - 1e-9) {
      bestScore = sc;
      best = p;
      bestIndex = i;
    }
  }
  if (options && bestIndex >= 0) (*options)[bestIndex].best = true;
  return best;
}

void PassagePlanner::sunTimes(double lat, double lon, const QDate& date,
                              QDateTime& sunrise, QDateTime& sunset) {
  const auto compute = [&](bool rising) -> QDateTime {
    const int n = date.dayOfYear();
    const double lngHour = lon / 15;
    const double t = n + ((rising ? 6 : 18) - lngHour) / 24;
    const double m = 0.9856 * t - 3.289;
    double l = m + 1.916 * std::sin(m * kDeg) + 0.020 * std::sin(2 * m * kDeg) +
               282.634;
    l = std::fmod(l + 360, 360);
    double ra = std::atan(0.91764 * std::tan(l * kDeg)) / kDeg;
    ra = std::fmod(ra + 360, 360);
    ra += std::floor(l / 90) * 90 - std::floor(ra / 90) * 90;
    ra /= 15;
    const double sinDec = 0.39782 * std::sin(l * kDeg);
    const double cosDec = std::cos(std::asin(sinDec));
    const double cosH =
        (std::cos(90.833 * kDeg) - sinDec * std::sin(lat * kDeg)) /
        (cosDec * std::cos(lat * kDeg));
    if (cosH > 1 || cosH < -1) return {};
    double h = std::acos(cosH) / kDeg;
    if (rising) h = 360 - h;
    h /= 15;
    const double tLocal = h + ra - 0.06571 * t - 6.622;
    double ut = std::fmod(tLocal - lngHour + 48, 24);
    const QTime time = QTime(0, 0).addSecs(int(std::round(ut * 3600)) % 86400);
    return QDateTime(date, time, Qt::UTC);
  };
  sunrise = compute(true);
  sunset = compute(false);
}
