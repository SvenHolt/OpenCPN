#include "core/SailPlan.h"

#include <QCoreApplication>
#include <QSettings>
#include <algorithm>
#include <cmath>

#include "core/GeoMath.h"

namespace {
QString tr(const char* s) {
  return QCoreApplication::translate("SailPlanner", s);
}

constexpr double kDeg = M_PI / 180;
constexpr double kStepNm = 2.0;  // wind is sampled at least this often
constexpr double kMinBoardNm = 0.1;

double norm180(double d) {
  d = std::fmod(d + 180, 360);
  if (d < 0) d += 360;
  return d - 180;
}

double angleDiff(double a, double b) { return std::abs(norm180(a - b)); }

/** Local flat coordinates (NM, x east, y north) around a reference. */
struct Local {
  double lat0, lon0, k;
  explicit Local(const QPointF& ref)
      : lat0(ref.y()), lon0(ref.x()), k(std::cos(ref.y() * kDeg)) {}
  QPointF to(const QPointF& lonLat) const {
    return {(lonLat.x() - lon0) * 60 * k, (lonLat.y() - lat0) * 60};
  }
  QPointF from(const QPointF& p) const {
    return {lon0 + p.x() / 60 / k, lat0 + p.y() / 60};
  }
};

QPointF unit(double headingDeg) {
  return {std::sin(headingDeg * kDeg), std::cos(headingDeg * kDeg)};
}

double cross(const QPointF& a, const QPointF& b) {
  return a.x() * b.y() - a.y() * b.x();
}

double headingOf(const QPointF& d) {
  return std::fmod(std::atan2(d.x(), d.y()) / kDeg + 360, 360);
}

/** Sailed distance on two boards (as PassagePlanner). */
double boardsNm(double legNm, double boardDeg, double offDeg) {
  const double b = boardDeg * kDeg, a = offDeg * kDeg;
  const double s = std::sin(2 * b);
  return std::fabs(legNm * std::sin(b + a) / s) +
         std::fabs(legNm * std::sin(b - a) / s);
}

int furlStep(int pct) { return int(std::round(pct / 10.0)); }
}  // namespace

// --------------------------------------------------------------------------
// Inventory

SailInventory SailInventory::load() {
  QSettings s;
  s.beginGroup("SailInventory");
  SailInventory i;
  i.genoa = s.value("genoa", i.genoa).toBool();
  i.jib = s.value("jib", i.jib).toBool();
  i.storm = s.value("storm", i.storm).toBool();
  i.gennaker = s.value("gennaker", i.gennaker).toBool();
  i.spinnaker = s.value("spinnaker", i.spinnaker).toBool();
  i.code0 = s.value("code0", i.code0).toBool();
  i.reefs = std::clamp(s.value("reefs", i.reefs).toInt(), 0, 3);
  i.reef1Aws = s.value("reef1Aws", i.reef1Aws).toDouble();
  i.reef2Aws = s.value("reef2Aws", i.reef2Aws).toDouble();
  i.reef3Aws = s.value("reef3Aws", i.reef3Aws).toDouble();
  i.furlStartAws = s.value("furlStartAws", i.furlStartAws).toDouble();
  i.downwindMaxTws = s.value("downwindMaxTws", i.downwindMaxTws).toDouble();
  return i;
}

void SailInventory::save() const {
  QSettings s;
  s.beginGroup("SailInventory");
  s.setValue("genoa", genoa);
  s.setValue("jib", jib);
  s.setValue("storm", storm);
  s.setValue("gennaker", gennaker);
  s.setValue("spinnaker", spinnaker);
  s.setValue("code0", code0);
  s.setValue("reefs", reefs);
  s.setValue("reef1Aws", reef1Aws);
  s.setValue("reef2Aws", reef2Aws);
  s.setValue("reef3Aws", reef3Aws);
  s.setValue("furlStartAws", furlStartAws);
  s.setValue("downwindMaxTws", downwindMaxTws);
}

// --------------------------------------------------------------------------
// Advice

QStringList SailTrim::lines() const {
  QStringList l;
  const auto add = [&](const QString& what, const QString& how) {
    if (!how.isEmpty()) l << QStringLiteral("%1: %2").arg(what, how);
  };
  add(tr("Mainsheet"), mainsheet);
  add(tr("Traveller"), traveller);
  add(tr("Vang"), vang);
  add(tr("Outhaul"), outhaul);
  add(tr("Backstay"), backstay);
  add(tr("Cunningham"), cunningham);
  add(tr("Headsail"), headsail);
  add(tr("Sheet car"), car);
  if (!extra.isEmpty()) l << extra;
  return l;
}

QString SailAdvice::summary() const {
  if (!note.isEmpty() && !sails.anySail()) return note;
  return sails.summary();
}

bool SailAdvice::sameSails(const SailAdvice& o) const {
  const SailConfig& a = sails;
  const SailConfig& b = o.sails;
  return a.main == b.main && a.head == b.head && a.downwind == b.downwind &&
         (a.head != SailConfig::Head::Genoa ||
          furlStep(a.headFurlPct) == furlStep(b.headFurlPct));
}

void SailAdvisor::apparent(double tws, double twa, double boatKn, double& aws,
                           double& awa) {
  const double x = tws * std::cos(twa * kDeg) + boatKn;
  const double y = tws * std::sin(twa * kDeg);
  aws = std::hypot(x, y);
  awa = std::abs(std::atan2(y, x) / kDeg);
}

QString SailAdvisor::pointOfSail(double twa) {
  twa = std::abs(twa);
  if (twa < 55) return tr("Close-hauled");
  if (twa < 75) return tr("Close reach");
  if (twa < 110) return tr("Beam reach");
  if (twa < 150) return tr("Broad reach");
  return tr("Running");
}

SailAdvice SailAdvisor::advise(double tws, double twaSigned, double boatKn,
                               const SailInventory& inv) {
  using Main = SailConfig::Main;
  using Head = SailConfig::Head;
  using Down = SailConfig::Downwind;
  SailAdvice a;
  a.tws = tws;
  a.twa = std::abs(twaSigned);
  apparent(tws, a.twa, boatKn, a.aws, a.awa);
  SailConfig& c = a.sails;
  c.propulsion = SailConfig::Propulsion::Sailing;
  const double twa = a.twa, aws = a.aws;

  // Main: reefs by the apparent wind (downwind a little earlier, the
  // apparent wind rises quickly when rounding up).
  const double reefWind = twa > 120 ? aws * 1.15 : aws;
  int reef = 0;
  if (reefWind >= inv.reef1Aws) reef = 1;
  if (reefWind >= inv.reef2Aws) reef = 2;
  if (reefWind >= inv.reef3Aws) reef = 3;
  reef = std::min(reef, inv.reefs);
  c.main = reef == 0   ? Main::Full
           : reef == 1 ? Main::Reef1
           : reef == 2 ? Main::Reef2
                       : Main::Reef3;

  // Headsail: genoa furled step by step, jib and storm jib if on board.
  c.head = inv.genoa ? Head::Genoa : inv.jib ? Head::Jib : Head::None;
  c.headFurlPct = 100;
  if (c.head == Head::Genoa && aws > inv.furlStartAws) {
    const double f = (aws - inv.furlStartAws) /
                     std::max(1.0, inv.reef3Aws - inv.furlStartAws);
    // In 10 % steps: what can be set from the furling line marks.
    c.headFurlPct =
        std::clamp(int(std::round((100 - 60 * f) / 10)) * 10, 30, 100);
  }
  if (inv.jib && aws >= inv.reef2Aws + 1) {
    c.head = Head::Jib;
    c.headFurlPct = 100;
  }
  if (inv.storm && aws >= inv.reef3Aws + 5) {
    c.head = Head::Storm;
    c.headFurlPct = 100;
  }

  // Downwind sails in light and moderate wind; the genoa is furled.
  const bool light = tws >= 3 && tws <= inv.downwindMaxTws && reef == 0;
  if (light && inv.spinnaker && twa >= 120)
    c.downwind = Down::Spinnaker;
  else if (light && inv.gennaker && twa >= 90 && twa <= 160)
    c.downwind = Down::Gennaker;
  else if (inv.code0 && tws >= 3 && tws <= 11 && twa >= 55 && twa < 110 &&
           reef == 0)
    c.downwind = Down::Code0;
  if (c.downwind != Down::None) c.head = Head::None, c.headFurlPct = 100;

  if (tws < 3) a.note = tr("Too little wind to sail");

  // Trim.
  SailTrim& t = a.trim;
  t.pointOfSail = pointOfSail(twa);
  t.starboardTack = twaSigned > 0;
  t.boomDeg = std::clamp((a.awa - 20) / 2, 0.0, 85.0);
  const bool lightAir = aws < 10, strong = aws >= inv.reef1Aws - 2;
  const QString side = t.starboardTack ? tr("to port") : tr("to starboard");
  if (twa < 55) {
    t.mainsheet = strong ? tr("hard, ease in the gusts")
                         : tr("in until the top leech telltale stalls now "
                              "and then");
    t.traveller = lightAir ? tr("slightly to windward, boom on the centreline")
                  : strong ? tr("down to leeward to spill the gusts")
                           : tr("centred");
    t.vang = strong ? tr("firm") : tr("slack");
    t.outhaul = lightAir ? tr("eased, fuller foot")
                : strong ? tr("tight, flat foot")
                         : tr("medium");
    t.backstay = lightAir ? tr("slack") : strong ? tr("hard") : tr("medium");
    t.cunningham = strong ? tr("pulled, draft forward") : QString();
    t.car = lightAir ? tr("forward, fuller headsail")
            : strong ? tr("aft, twist off the top")
                     : tr("in the middle, telltales break evenly");
    t.headsail = tr("sheeted in, a hand's width off the spreader");
  } else if (twa < 110) {
    t.mainsheet = tr("eased until the luff lifts, then in a little");
    t.traveller = tr("down to leeward");
    t.vang = tr("firm, keeps the leech closed");
    t.outhaul = lightAir ? tr("eased") : tr("medium");
    t.backstay = tr("slack");
    t.car = tr("forward and outboard (barber hauler)");
    t.headsail = tr("eased, telltales flying");
  } else {
    t.mainsheet = twa >= 150 ? tr("fully eased, boom just off the shrouds")
                             : tr("well eased");
    t.traveller = tr("at the leeward end");
    t.vang = tr("firm, the boom must not rise");
    t.outhaul = tr("eased");
    t.backstay = tr("slack");
    t.extra = tr("Rig a preventer.");
    if (c.downwind == Down::None && c.head != Head::None) {
      t.headsail = twa >= 150 ? tr("poled out to windward (wing-on-wing)")
                              : tr("eased, telltales flying");
      t.car = tr("forward and outboard");
    }
  }
  switch (c.downwind) {
    case Down::Spinnaker:
      t.headsail = tr("furled");
      t.extra += (t.extra.isEmpty() ? "" : " ") +
                 tr("Spinnaker: pole square to the apparent wind, pole "
                    "height so both clews are level; ease the sheet until "
                    "the luff curls.");
      break;
    case Down::Gennaker:
      t.headsail = tr("furled");
      t.extra += (t.extra.isEmpty() ? "" : " ") +
                 tr("Gennaker: tack line eased about 0.5 m, sheet eased "
                    "until the luff curls, then in a little.");
      break;
    case Down::Code0:
      t.headsail = tr("furled");
      t.extra += (t.extra.isEmpty() ? "" : " ") +
                 tr("Code 0: sheet well aft, luff tension high.");
      break;
    default:
      break;
  }
  if (!t.extra.isEmpty() || twa >= 90)
    t.extra = tr("Boom %1.").arg(side) + " " + t.extra;
  return a;
}

// --------------------------------------------------------------------------

QString SailEvent::kindName(Kind k) {
  switch (k) {
    case Kind::Start:
      return tr("Departure");
    case Kind::Tack:
      return tr("Tack");
    case Kind::Gybe:
      return tr("Gybe");
    case Kind::SailChange:
      return tr("Sails");
    case Kind::MotorOn:
      return tr("Engine on");
    case Kind::MotorOff:
      return tr("Engine off");
    case Kind::Waypoint:
      return tr("Waypoint");
    case Kind::Arrival:
      return tr("Arrival");
  }
  return {};
}

QVector<SailEvent> SailingPlan::eventsOf(SailEvent::Kind k) const {
  QVector<SailEvent> out;
  for (const auto& e : events)
    if (e.kind == k) out << e;
  return out;
}

SailingPlan SailPlanner::plan(const QVector<QPointF>& route,
                              const PassageSettings& s, const WindSource& wind,
                              const SailInventory& inv, double corridorNm,
                              const WaterTest& water) {
  SailingPlan out;
  if (route.size() < 2) return out;
  out.departure = s.departure.toUTC();
  QDateTime t = out.departure;
  out.track << route.first();
  out.events << SailEvent{SailEvent::Kind::Start, route.first(), t, {}};
  const bool polar = s.polar && s.polar->isValid();

  int lastSail = -1;  // index of the last board under sail
  int lastSide = 0;   // tack of the last beat/run board: +1 stbd, -1 port
  PassageLeg::Mode lastMode = PassageLeg::Mode::Sail;
  bool lastMotor = false;
  bool narrowWarned = false;

  for (int leg = 1; leg < route.size(); ++leg) {
    const Local loc(route[leg - 1]);
    const QPointF B = loc.to(route[leg]);
    const double legNm = std::hypot(B.x(), B.y());
    if (legNm < 1e-6) continue;
    const QPointF dirAB = B / legNm;
    QPointF P(0, 0);
    int switches = 0;  // tack changes without progress

    // Free distance along a heading before land (or `len` if clear).
    const auto clearNm = [&](const QPointF& from, const QPointF& u,
                             double len) {
      if (!water) return len;
      for (double d = 0.05; d <= len + 1e-9; d += 0.05) {
        const QPointF q = from + u * d;
        // Harbours at the ends may lie on the coarse basemap's land.
        if (std::hypot(q.x(), q.y()) < 0.3 ||
            std::hypot(q.x() - B.x(), q.y() - B.y()) < 0.3)
          continue;
        const QPointF g = loc.from(q);
        if (!water(g.y(), g.x())) return std::max(0.0, d - 0.1);
      }
      return len;
    };

    for (int iter = 0; iter < 5000; ++iter) {
      const QPointF D = B - P;
      const double dist = std::hypot(D.x(), D.y());
      if (dist < 0.02) break;
      const QPointF here = loc.from(P);
      double kn = s.manualWindKn, from = s.manualWindFromDeg;
      if (!wind || !wind(here.y(), here.x(), t, kn, from)) {
        kn = s.manualWindKn;
        from = s.manualWindFromDeg;
      }
      const double brg = headingOf(D);
      const double twa = angleDiff(brg, from);
      const double noGo = polar ? s.polar->bestUpwindTwa(kn) : s.noGoDeg;
      const double run = polar ? s.polar->bestDownwindTwa(kn) : s.runDeg;
      PassageLeg::Mode mode = PassageLeg::Mode::Sail;
      double v = 0, sailed = dist;
      // On the layline (within half a degree) the beat goes on.
      const double hyst = lastMotor ? 0 : 0.5;
      const double noGoH =
          noGo + (lastMode == PassageLeg::Mode::Beat ? hyst : 0);
      const double runH = run - (lastMode == PassageLeg::Mode::Run ? hyst : 0);
      if (twa < noGoH) {
        mode = PassageLeg::Mode::Beat;
        v = PassagePlanner::boatSpeed(s, kn, noGo);
        sailed = boardsNm(dist, noGo, twa);
      } else if (twa > runH && run < 179) {
        mode = PassageLeg::Mode::Run;
        v = PassagePlanner::boatSpeed(s, kn, run);
        sailed = boardsNm(dist, 180 - run, 180 - twa);
      } else {
        v = PassagePlanner::boatSpeed(s, kn, twa);
      }
      const double vmg = v > 0 ? dist / (sailed / v) : 0;
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

      double heading = brg, len = std::min(dist, kStepNm), speed = v;
      int side = 0;
      if (motor) {
        mode = PassageLeg::Mode::Motor;
        speed = s.motorSpeedKn;
      } else if (mode != PassageLeg::Mode::Sail) {
        // Two headings at the best VMG angle, one per tack.
        const double base = mode == PassageLeg::Mode::Beat ? from : from + 180;
        const double ang = mode == PassageLeg::Mode::Beat ? noGo : 180 - run;
        const double hPort = std::fmod(base + ang + 360, 360);
        const double hStbd = std::fmod(base - ang + 360, 360);
        const auto sideOf = [&](double h) {
          return norm180(from - h) > 0 ? 1 : -1;
        };
        int cur;
        if (lastMode == mode && !lastMotor && lastSide != 0)
          cur = lastSide;  // keep the tack
        else
          cur = angleDiff(hPort, brg) <= angleDiff(hStbd, brg) ? sideOf(hPort)
                                                               : sideOf(hStbd);
        const auto headingFor = [&](int sd) {
          return sideOf(hPort) == sd ? hPort : hStbd;
        };
        const QPointF uc = unit(headingFor(cur)), uo = unit(headingFor(-cur));
        const double det = cross(uc, uo);
        double a = cross(D, uo) / det, b = cross(uc, D) / det;
        // Lane around the rhumb line.
        const double lr = cross(uc, dirAB), off = cross(P, dirAB);
        double edge = 1e9;
        if (std::abs(lr) > 1e-6) {
          const double target = (lr > 0 ? 1 : -1) * corridorNm;
          edge = (target - off) / lr;
          if (edge < 0) edge = 0;
        }
        double boardLen = std::min({a, edge, kStepNm});
        boardLen = clearNm(P, uc, boardLen);
        if ((boardLen < kMinBoardNm && b > kMinBoardNm) || a <= 0.02) {
          // Tack (or gybe) here; give up after many changes in a row.
          if (++switches < 4) {
            lastSide = -cur;
            lastMode = mode;
            lastMotor = false;
            if (!out.boards.isEmpty() && !out.boards.last().motor &&
                out.boards.last().mode == mode) {
              const bool beat = mode == PassageLeg::Mode::Beat;
              out.events << SailEvent{
                  beat ? SailEvent::Kind::Tack : SailEvent::Kind::Gybe, here, t,
                  tr("%1 to %2 tack, new heading %3°")
                      .arg(beat ? tr("Tack") : tr("Gybe"),
                           -cur > 0 ? tr("starboard") : tr("port"))
                      .arg(int(std::round(headingFor(-cur))) % 360)};
              beat ? ++out.tacks : ++out.gybes;
            }
            continue;
          }
          // Too narrow: motor through.
          motor = true;
          mode = PassageLeg::Mode::Motor;
          speed = s.motorSpeedKn;
          if (!narrowWarned)
            out.warnings << tr("Too narrow to tack near %1 %2 – motor")
                                .arg(geo::formatLat(here.y()),
                                     geo::formatLon(here.x()));
          narrowWarned = true;
        } else {
          heading = headingFor(cur);
          len = std::max(std::min(boardLen, a), 0.02);
          side = cur;
        }
      }
      switches = 0;
      if (speed <= 0.1) speed = s.motorSpeedKn, motor = true;

      SailBoard bd;
      bd.leg = leg - 1;
      bd.from = here;
      const QPointF P2 = P + unit(heading) * len;
      bd.to = loc.from(P2);
      bd.start = t;
      t = t.addSecs(qint64(std::round(len / speed * 3600)));
      bd.end = t;
      bd.headingDeg = heading;
      bd.distanceNm = len;
      bd.speedKn = speed;
      bd.tws = kn;
      bd.twd = from;
      bd.twa = norm180(from - heading);
      bd.motor = motor;
      bd.mode = mode;
      if (!motor) bd.advice = SailAdvisor::advise(kn, bd.twa, speed, inv);
      if (side != 0) lastSide = side;
      lastMode = mode;

      // Events: engine on/off, sail changes.
      if (motor != lastMotor)
        out.events << SailEvent{
            motor ? SailEvent::Kind::MotorOn : SailEvent::Kind::MotorOff,
            here,
            bd.start,
            {}};
      lastMotor = motor;
      if (!motor &&
          (lastSail < 0 || !out.boards[lastSail].advice.sameSails(bd.advice))) {
        out.events << SailEvent{SailEvent::Kind::SailChange, here, bd.start,
                                bd.advice.summary()};
        if (lastSail >= 0) ++out.sailChanges;
      }

      // Merge with the previous board when nothing changed.
      SailBoard* prev = out.boards.isEmpty() ? nullptr : &out.boards.last();
      if (prev && prev->leg == bd.leg && prev->motor == bd.motor &&
          prev->mode == bd.mode && angleDiff(prev->headingDeg, heading) < 2 &&
          (motor || prev->advice.sameSails(bd.advice))) {
        prev->to = bd.to;
        prev->end = bd.end;
        prev->speedKn = (prev->speedKn * prev->distanceNm + speed * len) /
                        (prev->distanceNm + len);
        prev->distanceNm += len;
        out.track.last() = bd.to;
      } else {
        out.boards << bd;
        out.track << bd.to;
      }
      if (!out.boards.last().motor) lastSail = int(out.boards.size()) - 1;
      P = P2;
    }
    out.track.last() = route[leg];
    if (!out.boards.isEmpty()) out.boards.last().to = route[leg];
    if (leg < route.size() - 1)
      out.events << SailEvent{SailEvent::Kind::Waypoint, route[leg], t,
                              tr("WP %1").arg(leg)};
  }
  out.events << SailEvent{SailEvent::Kind::Arrival, route.last(), t, {}};
  out.arrival = t;
  for (const auto& b : out.boards) {
    out.sailedNm += b.distanceNm;
    out.hours += b.hours();
    if (b.motor) out.motorHours += b.hours();
  }
  return out;
}
