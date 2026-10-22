// Sail planning: sail configuration and trim, boards with tack and gybe
// positions, sail changes, lane and land limits.
#include <QtTest>

#include "core/GeoMath.h"
#include "core/SailPlan.h"

namespace {
PassageSettings settings(double windFrom, double windKn) {
  PassageSettings s;
  s.departure = QDateTime(QDate(2026, 6, 1), QTime(8, 0), Qt::UTC);
  s.manualWindFromDeg = windFrom;
  s.manualWindKn = windKn;
  s.hullSpeedKn = 6.5;
  s.noGoDeg = 45;
  s.runDeg = 150;
  return s;
}

/** 10 NM due north from 54.5 N 10.0 E. */
QVector<QPointF> north(double nm = 10) {
  return {QPointF(10.0, 54.5), QPointF(10.0, 54.5 + nm / 60)};
}

/** Distance of a point from the meridian 10.0 E in NM. */
double offsetNm(const QPointF& p) {
  return std::abs(p.x() - 10.0) * 60 * std::cos(p.y() * M_PI / 180);
}
}  // namespace

class SailPlanTest : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("opencpn-qt-tests");
    QStandardPaths::setTestModeEnabled(true);
  }

  void sailsByWind() {
    SailInventory inv;  // genoa, gennaker, three reefs
    // Light air close-hauled: full sails, fuller trim.
    SailAdvice a = SailAdvisor::advise(6, 40, 4, inv);
    QCOMPARE(a.sails.main, SailConfig::Main::Full);
    QCOMPARE(a.sails.head, SailConfig::Head::Genoa);
    QCOMPARE(a.sails.headFurlPct, 100);
    QCOMPARE(a.sails.downwind, SailConfig::Downwind::None);
    QVERIFY(a.aws > 6 && a.awa < 40);  // apparent wind ahead of true
    QCOMPARE(a.trim.pointOfSail, QStringLiteral("Close-hauled"));
    QVERIFY(a.trim.boomDeg < 6);
    QVERIFY(a.trim.starboardTack);
    QVERIFY(a.trim.outhaul.contains("eased"));

    // Fresh breeze upwind: reefs and a furled genoa.
    a = SailAdvisor::advise(20, 45, 5.5, inv);
    QVERIFY2(a.aws > 23, qPrintable(QString::number(a.aws)));
    QCOMPARE(a.sails.main, SailConfig::Main::Reef2);
    QVERIFY(a.sails.headFurlPct < 75);
    QVERIFY(a.trim.traveller.contains("leeward"));
    QVERIFY(!a.trim.cunningham.isEmpty());
    // With a jib on board it replaces the genoa; the storm jib in a gale.
    SailInventory jib = inv;
    jib.jib = true;
    jib.storm = true;
    QCOMPARE(SailAdvisor::advise(20, 45, 5.5, jib).sails.head,
             SailConfig::Head::Jib);
    QCOMPARE(SailAdvisor::advise(30, 45, 5, jib).sails.head,
             SailConfig::Head::Storm);
    QCOMPARE(SailAdvisor::advise(30, 45, 5, jib).sails.main,
             SailConfig::Main::Reef3);
    SailInventory tworeefs = inv;
    tworeefs.reefs = 2;
    QCOMPARE(SailAdvisor::advise(30, 45, 5, tworeefs).sails.main,
             SailConfig::Main::Reef2);

    // Broad reach in moderate wind: gennaker, genoa furled, preventer.
    a = SailAdvisor::advise(12, -135, 6, inv);
    QCOMPARE(a.sails.downwind, SailConfig::Downwind::Gennaker);
    QCOMPARE(a.sails.head, SailConfig::Head::None);
    QVERIFY(!a.trim.starboardTack);
    QVERIFY(a.trim.boomDeg > 30);
    QVERIFY(a.trim.extra.contains("preventer"));
    QVERIFY(a.trim.extra.contains("starboard"));  // boom to starboard
    // Too much wind for the gennaker.
    QCOMPARE(SailAdvisor::advise(20, 135, 7, inv).sails.downwind,
             SailConfig::Downwind::None);
    // Dead downwind without a spinnaker: wing-on-wing.
    a = SailAdvisor::advise(12, 172, 5.5, inv);
    QCOMPARE(a.sails.downwind, SailConfig::Downwind::None);
    QVERIFY(a.trim.headsail.contains("wing-on-wing"));
    QVERIFY(a.trim.boomDeg > 60);
    // … with one: spinnaker.
    SailInventory kite = inv;
    kite.spinnaker = true;
    QCOMPARE(SailAdvisor::advise(12, 172, 5.5, kite).sails.downwind,
             SailConfig::Downwind::Spinnaker);
    // Code 0 on a light reach.
    SailInventory code0 = inv;
    code0.code0 = true;
    QCOMPARE(SailAdvisor::advise(8, 80, 5, code0).sails.downwind,
             SailConfig::Downwind::Code0);
    // Calm.
    QVERIFY(!SailAdvisor::advise(2, 90, 1, inv).note.isEmpty());
    QVERIFY(!a.trim.lines().isEmpty());

    // Same sails: genoa furling in 10 % steps.
    SailAdvice x = SailAdvisor::advise(16, 60, 6, inv);
    SailAdvice y = x;
    y.sails.headFurlPct += 3;
    QVERIFY(x.sameSails(y));
    y.sails.headFurlPct -= 20;
    QVERIFY(!x.sameSails(y));

    // Stored inventory.
    kite.reef1Aws = 16;
    kite.save();
    const SailInventory r = SailInventory::load();
    QVERIFY(r.spinnaker);
    QCOMPARE(r.reef1Aws, 16.0);
    SailInventory().save();
  }

  void beatToWindward() {
    const PassageSettings s = settings(0, 12);  // wind from the north
    const SailingPlan p =
        SailPlanner::plan(north(), s, {}, SailInventory(), 1.0);
    QVERIFY(p.boards.size() >= 4);
    // Boards alternate between 045° and 315°.
    for (const SailBoard& b : p.boards) {
      QVERIFY(!b.motor);
      QCOMPARE(b.mode, PassageLeg::Mode::Beat);
      const double h = b.headingDeg;
      QVERIFY2(std::abs(h - 45) < 0.5 || std::abs(h - 315) < 0.5,
               qPrintable(QString::number(h)));
      QVERIFY(std::abs(std::abs(b.twa) - 45) < 0.5);
      QCOMPARE(b.advice.trim.pointOfSail, QStringLiteral("Close-hauled"));
    }
    for (int i = 1; i < p.boards.size(); ++i)
      QVERIFY(std::abs(p.boards[i].headingDeg - p.boards[i - 1].headingDeg) >
              80);
    // Tacks: one between each pair of boards, at the board ends.
    const auto tacks = p.eventsOf(SailEvent::Kind::Tack);
    QCOMPARE(p.tacks, int(p.boards.size()) - 1);
    QCOMPARE(int(tacks.size()), p.tacks);
    for (int i = 0; i < tacks.size(); ++i) {
      const QPointF a = tacks[i].pos, b = p.boards[i + 1].from;
      QVERIFY(geo::distanceNm(a.y(), a.x(), b.y(), b.x()) < 0.01);
      QCOMPARE(tacks[i].time, p.boards[i + 1].start);
    }
    // Within the lane, ending at the destination.
    for (const QPointF& pt : p.track)
      QVERIFY2(offsetNm(pt) < 1.05, qPrintable(QString::number(offsetNm(pt))));
    QCOMPARE(p.track.last(), north().last());
    // Sailed distance: √2 × 10 NM.
    QVERIFY2(std::abs(p.sailedNm - 10 * std::sqrt(2.0)) < 0.2,
             qPrintable(QString::number(p.sailedNm)));
    QVERIFY(p.arrival > p.departure);
    QVERIFY(std::abs(p.departure.secsTo(p.arrival) / 3600.0 - p.hours) < 0.01);
    QCOMPARE(p.events.first().kind, SailEvent::Kind::Start);
    QCOMPARE(p.events.last().kind, SailEvent::Kind::Arrival);
    // The first sail event names the sails.
    QCOMPARE(p.eventsOf(SailEvent::Kind::SailChange).size(), 1);
    QCOMPARE(p.sailChanges, 0);

    // A narrower lane needs more tacks.
    const SailingPlan narrow =
        SailPlanner::plan(north(), s, {}, SailInventory(), 0.4);
    QVERIFY(narrow.tacks > p.tacks);
    for (const QPointF& pt : narrow.track) QVERIFY(offsetNm(pt) < 0.45);
  }

  void gybeDownwind() {
    const PassageSettings s = settings(180, 14);  // wind from the south
    const SailingPlan p =
        SailPlanner::plan(north(), s, {}, SailInventory(), 1.0);
    QVERIFY(p.gybes >= 1);
    QCOMPARE(p.tacks, 0);
    QCOMPARE(int(p.eventsOf(SailEvent::Kind::Gybe).size()), p.gybes);
    for (const SailBoard& b : p.boards) {
      QCOMPARE(b.mode, PassageLeg::Mode::Run);
      QVERIFY(std::abs(std::abs(b.twa) - 150) < 0.5);
      QVERIFY(b.advice.trim.extra.contains("preventer"));
    }
  }

  void reachInOneBoard() {
    const PassageSettings s = settings(90, 12);  // wind from the east
    const SailingPlan p =
        SailPlanner::plan(north(), s, {}, SailInventory(), 1.0);
    QCOMPARE(p.boards.size(), 1);  // 2 NM steps merged
    QCOMPARE(p.tacks + p.gybes, 0);
    QVERIFY(std::abs(p.sailedNm - 10) < 0.01);
    QCOMPARE(p.boards.first().advice.trim.pointOfSail,
             QStringLiteral("Beam reach"));
    QVERIFY(p.boards.first().twa > 0);  // wind from starboard
  }

  void sailChangesWhenTheWindBuilds() {
    PassageSettings s = settings(90, 10);
    // 8 kn at the start, 26 kn after 20 NM.
    const auto wind = [](double lat, double, const QDateTime&, double& kn,
                         double& from) {
      kn = 8 + (lat - 54.5) * 60 * 0.9;
      from = 90;
      return true;
    };
    const SailingPlan p =
        SailPlanner::plan(north(20), s, wind, SailInventory(), 1.0);
    const auto changes = p.eventsOf(SailEvent::Kind::SailChange);
    QVERIFY2(changes.size() >= 3, qPrintable(QString::number(changes.size())));
    QCOMPARE(p.sailChanges, int(changes.size()) - 1);
    QCOMPARE(p.boards.first().advice.sails.main, SailConfig::Main::Full);
    QVERIFY(p.boards.last().advice.sails.main != SailConfig::Main::Full);
    for (int i = 1; i < changes.size(); ++i)
      QVERIFY(changes[i].time > changes[i - 1].time);
  }

  void landAndLane() {
    // Land east of 10.01 E: the boards stay west of it.
    const auto water = [](double, double lon) { return lon < 10.01; };
    const PassageSettings s = settings(0, 12);
    const SailingPlan p =
        SailPlanner::plan(north(), s, {}, SailInventory(), 1.0, water);
    QVERIFY(p.tacks > 3);
    for (const SailBoard& b : p.boards) {
      QVERIFY2(b.to.x() < 10.01, qPrintable(QString::number(b.to.x(), 'f', 4)));
      QVERIFY(b.from.x() < 10.01);
    }
    QVERIFY(p.warnings.isEmpty());
    // A channel too narrow to tack: motor with a warning.
    const auto channel = [](double, double lon) {
      return std::abs(lon - 10.0) < 0.0004;
    };
    const SailingPlan c =
        SailPlanner::plan(north(3), s, {}, SailInventory(), 1.0, channel);
    QVERIFY(!c.warnings.isEmpty());
    QVERIFY(c.motorHours > 0);
    QVERIFY(!c.eventsOf(SailEvent::Kind::MotorOn).isEmpty());
    QCOMPARE(c.track.last(), north(3).last());
  }

  void motorAndRoutes() {
    PassageSettings s = settings(0, 12);
    s.strategy = PassageSettings::Strategy::Motor;
    const SailingPlan m =
        SailPlanner::plan(north(), s, {}, SailInventory(), 1.0);
    QCOMPARE(m.boards.size(), 1);
    QVERIFY(m.boards.first().motor);
    QCOMPARE(m.eventsOf(SailEvent::Kind::MotorOn).size(), 1);
    QVERIFY(std::abs(m.hours - 10 / s.motorSpeedKn) < 0.01);

    // Two legs: the second one beats; a waypoint event between them.
    s.strategy = PassageSettings::Strategy::Sail;
    const QVector<QPointF> route = {QPointF(10.0, 54.5),
                                    QPointF(10.0 + 5 / 60.0 / 0.58, 54.5),
                                    QPointF(10.0 + 5 / 60.0 / 0.58, 54.6)};
    const SailingPlan p = SailPlanner::plan(route, s, {}, SailInventory(), 1.0);
    QCOMPARE(p.eventsOf(SailEvent::Kind::Waypoint).size(), 1);
    QCOMPARE(p.boards.first().leg, 0);
    QCOMPARE(p.boards.first().mode, PassageLeg::Mode::Sail);
    QCOMPARE(p.boards.last().leg, 1);
    QCOMPARE(p.boards.last().mode, PassageLeg::Mode::Beat);
    QVERIFY(p.tacks >= 1);
    // No tack is counted between the reach and the first board to windward.
    QCOMPARE(p.eventsOf(SailEvent::Kind::Tack).first().time >
                 p.eventsOf(SailEvent::Kind::Waypoint).first().time,
             true);
  }
};

QTEST_GUILESS_MAIN(SailPlanTest)
#include "test_sailplan.moc"
