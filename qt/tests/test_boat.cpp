// On-board systems: light scenes, switching, Webasto heater and the
// Victron energy balance of the simulator.
#include <QSignalSpy>
#include <QtTest>

#include "core/BoatSystems.h"

namespace {
const BoatLight* light(const QString& id) {
  for (const auto& l : BoatSystems::instance().lights())
    if (l.id == id) return &l;
  return nullptr;
}
const BoatConsumer* consumer(const QString& id) {
  for (const auto& c : BoatSystems::instance().consumers())
    if (c.id == id) return &c;
  return nullptr;
}
}  // namespace

class BoatTest : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("opencpn-qt-tests");
    QStandardPaths::setTestModeEnabled(true);
  }

  void layoutAndPaths() {
    auto& b = BoatSystems::instance();
    for (const char* z :
         {"outside", "forward", "saloon", "galley", "heads", "aft"})
      QVERIFY2(b.zone(z), z);
    QVERIFY(b.zone("saloon")->signalKPath.startsWith("environment.inside."));
    QCOMPARE(b.zone("outside")->signalKPath,
             QStringLiteral("environment.outside.temperature"));
    QCOMPARE(b.tanks().size(), 3);
    for (const auto& l : b.lights())
      QVERIFY(l.signalKPath.startsWith("electrical.switches."));
  }

  void nightSailing() {
    auto& b = BoatSystems::instance();
    QSignalSpy spy(&b, &BoatSystems::changed);
    b.setLight("nav_lights", true);
    b.applyScene("night");
    QCOMPARE(b.activeScene(), QStringLiteral("night"));
    QVERIFY(spy.size() >= 2);
    // Red and dim where the crew moves, sleeping cabins dark.
    for (const char* id : {"saloon_ceiling", "chart_table", "courtesy"}) {
      const BoatLight* l = light(id);
      QVERIFY2(l->on, id);
      QVERIFY(l->brightness <= 0.15);
      QVERIFY(l->color.red() > 200 && l->color.green() < 60 &&
              l->color.blue() < 30);
    }
    QVERIFY(!light("fwd_ceiling")->on);
    QVERIFY(!light("aft_read")->on);
    // Navigation lights are not part of scenes.
    QVERIFY(light("nav_lights")->on);
    // A dim red LED draws little.
    QVERIFY(light("saloon_ceiling")->powerW() < 1);
  }

  void otherScenes() {
    auto& b = BoatSystems::instance();
    b.applyScene("anchor");
    QVERIFY(light("anchor_light")->on);
    QVERIFY(!light("nav_lights")->on);
    QVERIFY(light("saloon_indirect")->on);
    b.applyScene("bright");
    for (const auto& l : b.lights())
      if (!l.navigation) QVERIFY(l.on && l.brightness == 1.0);
    b.applyScene("off");
    for (const auto& l : b.lights())
      if (!l.navigation) QVERIFY(!l.on);
    // A manual change leaves the scene.
    b.setLight("galley_spot", true);
    QVERIFY(light("galley_spot")->on);
    QVERIFY(b.activeScene().isEmpty());
    b.setLightLevel("galley_spot", 0.3, QColor(0, 0, 255));
    QCOMPARE(light("galley_spot")->brightness, 0.3);
    QCOMPARE(light("galley_spot")->color, QColor(0, 0, 255));
  }

  void consumers() {
    auto& b = BoatSystems::instance();
    b.setConsumer("stereo", true);
    QVERIFY(consumer("stereo")->on);
    b.setConsumer("stereo", false);
    QVERIFY(!consumer("stereo")->on);
    // The automatic bilge pump cannot be switched off from the display.
    b.setConsumer("bilge", false);
    QVERIFY(consumer("bilge")->on);
  }

  void heater() {
    auto& b = BoatSystems::instance();
    b.setHeaterSetpoint(40);
    QCOMPARE(b.heater().setpointC, 30.0);  // clamped
    b.setHeaterSetpoint(21);
    const double before = b.zone("saloon")->temperatureC;
    b.setHeaterOn(true);
    QCOMPARE(b.heater().state, Heater::State::Starting);
    for (int i = 0; i < 61; ++i) b.simulate(1);
    QCOMPARE(b.heater().state, Heater::State::Heating);
    QVERIFY(b.heater().fuelLph > 0.1 && b.heater().fuelLph < 0.3);
    QVERIFY(b.heater().outputPct > 0);
    // Half an hour of heating warms the saloon more than the heads.
    const double heads = b.zone("heads")->temperatureC;
    for (int i = 0; i < 180; ++i) b.simulate(10);
    QVERIFY(b.zone("saloon")->temperatureC > before + 1);
    QVERIFY(b.zone("saloon")->temperatureC - before >
            b.zone("heads")->temperatureC - heads);
    // Off: cool down first, then off.
    b.setHeaterOn(false);
    QCOMPARE(b.heater().state, Heater::State::CoolDown);
    QCOMPARE(b.heater().fuelLph, 0.0);
    for (int i = 0; i < 13; ++i) b.simulate(10);
    QCOMPARE(b.heater().state, Heater::State::Off);
    QVERIFY(!Heater::stateName(Heater::State::Heating).isEmpty());
  }

  void energy() {
    auto& b = BoatSystems::instance();
    b.setShoreConnected(false);
    b.simulate(1);
    // Loads are the sum of lights and consumers.
    double expected = 0;
    for (const auto& l : b.lights()) expected += l.powerW();
    for (const auto& c : b.consumers())
      if (c.on) expected += c.powerW;
    QVERIFY2(std::fabs(b.energy().loadsW - expected) < 2,
             qPrintable(QStringLiteral("%1 vs %2 heater %3")
                            .arg(b.energy().loadsW)
                            .arg(expected)
                            .arg(int(b.heater().state))));
    // Shore power charges the battery.
    b.setShoreConnected(true);
    b.simulate(60);
    const EnergySystem& e = b.energy();
    QVERIFY(e.shoreW > 0 && e.shoreV > 220);
    QVERIFY(e.current > 0);
    QVERIFY(std::isnan(e.timeToGoH));
    const double soc = e.soc;
    b.simulate(600);
    QVERIFY(b.energy().soc > soc);
    // Without shore power at night (no solar) the bank discharges.
    b.setShoreConnected(false);
    b.applyScene("bright");
    for (const auto& c : b.consumers()) b.setConsumer(c.id, true);
    b.simulate(1);
    if (b.energy().solarW < b.energy().loadsW) {
      QVERIFY(b.energy().current < 0);
      QVERIFY(b.energy().timeToGoH > 0);
    }
    QVERIFY(b.energy().soc >= 0 && b.energy().soc <= 1);
  }
};

QTEST_GUILESS_MAIN(BoatTest)
#include "test_boat.moc"
