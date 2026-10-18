// On-board systems: light scenes, switching, Webasto heater and the
// Victron energy balance of the simulator.
#include <QSignalSpy>
#include <QtTest>

#include "core/Automation.h"
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
    // Start with the factory scenes and automations.
    const QString data =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QFile::remove(data + "/light-scenes.json");
    QFile::remove(data + "/automations.json");
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

  void ownScenes() {
    auto& b = BoatSystems::instance();
    QVERIFY(b.scene("shower"));
    QVERIFY(b.scene("night")->builtIn);
    // A scene from the current lights, with one LED changed.
    b.applyScene("off");
    b.setLightLevel("saloon_indirect", 0.4, QColor(40, 110, 255));
    LightScene s = b.captureScene("Blue evening");
    QVERIFY(s.id.isEmpty());
    QVERIFY(!s.find("nav_lights"));  // navigation lights are not captured
    QVERIFY(s.find("saloon_indirect")->on);
    // Only a part of the lights: the rest keeps its state.
    s.lights.removeIf([](const SceneLight& l) {
      return l.lightId != "saloon_indirect" && l.lightId != "heads_ceiling";
    });
    QSignalSpy spy(&b, &BoatSystems::scenesChanged);
    b.saveScene(s);
    QCOMPARE(spy.size(), 1);
    const QString id = b.scenes().last().id;
    QVERIFY(!id.isEmpty());
    QVERIFY(!b.scenes().last().builtIn);
    b.setLight("galley_spot", true);
    b.applyScene(id);
    QCOMPARE(b.activeScene(), id);
    QVERIFY(light("galley_spot")->on);  // not part of the scene
    QCOMPARE(light("saloon_indirect")->color, QColor(40, 110, 255));
    QCOMPARE(light("saloon_indirect")->brightness, 0.4);
    // Stored on disk, JSON round trip.
    QFile f(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
            "/light-scenes.json");
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    f.close();
    bool found = false;
    for (const auto& v : arr) {
      const LightScene r = LightScene::fromJson(v.toObject());
      if (r.id != id) continue;
      found = true;
      QCOMPARE(r.name, QStringLiteral("Blue evening"));
      QCOMPARE(r.lights.size(), 2);
      QCOMPARE(r.find("saloon_indirect")->color, QColor(40, 110, 255));
    }
    QVERIFY(found);
    // Built-in scenes can be changed and reset.
    LightScene night = *b.scene("night");
    night.lights[0].color = QColor(0, 0, 255);
    b.saveScene(night);
    QCOMPARE(b.scene("night")->lights[0].color, QColor(0, 0, 255));
    QVERIFY(b.scene("night")->builtIn);
    b.removeScene("night");
    QVERIFY(b.scene("night"));
    QVERIFY(b.scene("night")->lights[0].color.red() > 200);
    b.removeScene(id);
    QVERIFY(!b.scene(id));
  }

  void roomTargetsAndHotWater() {
    auto& b = BoatSystems::instance();
    b.setShoreConnected(true);
    b.setZoneSetpoint("saloon", 21);
    QCOMPARE(b.heater().setpointC, 21.0);
    b.setZoneSetpoint("heads", 23);
    QCOMPARE(b.heater().setpointFor("heads"), 23.0);
    QVERIFY(std::isnan(b.heater().setpointFor("forward")));
    b.setZoneSetpoint("heads", 40);
    QCOMPARE(b.heater().setpointFor("heads"), 30.0);  // clamped
    b.setZoneSetpoint("heads", 23);
    b.setHeaterOn(true);
    b.setWaterHeaterOn(true);
    QVERIFY(b.waterHeater().on);
    const double water = b.waterHeater().tempC;
    // Two hours: both rooms reach their targets, the water gets hot.
    for (int i = 0; i < 720; ++i) b.simulate(10);
    QVERIFY2(b.zone("saloon")->temperatureC > 20.3,
             qPrintable(QString::number(b.zone("saloon")->temperatureC)));
    QVERIFY2(b.zone("heads")->temperatureC > 22.3,
             qPrintable(QString::number(b.zone("heads")->temperatureC)));
    // The flaps keep the heads from overheating far beyond its target.
    QVERIFY(b.zone("heads")->temperatureC < 25);
    QVERIFY(b.waterHeater().tempC > water + 20);
    // The boiler runs on shore power, not from the battery.
    b.setWaterHeaterOn(false);
    QVERIFY(!b.waterHeater().heating);
    b.setZoneSetpoint("heads", std::numeric_limits<double>::quiet_NaN());
    QVERIFY(!b.heater().zoneSetpointC.contains("heads"));
    b.setHeaterOn(false);
    for (int i = 0; i < 13; ++i) b.simulate(10);
  }

  void automations() {
    auto& store = AutomationStore::instance();
    const Automation* shower = store.find("winter_shower");
    QVERIFY(shower);
    QCOMPARE(shower->months, Automation::kWinter);
    QVERIFY(shower->allowedOn(QDate(2026, 1, 10)));
    QVERIFY(!shower->allowedOn(QDate(2026, 7, 10)));
    QVERIFY(shower->schedule().contains("Winter"));
    QCOMPARE(shower->actions.size(), 5);
    QVERIFY(shower->actions[2].describe().contains("23.0"));

    auto& b = BoatSystems::instance();
    b.applyScene("off");
    QSignalSpy ran(&store, &AutomationStore::ran);
    store.run("winter_shower");
    QCOMPARE(ran.size(), 1);
    QVERIFY(b.heater().running());
    QCOMPARE(b.heater().setpointC, 21.0);
    QCOMPARE(b.heater().setpointFor("heads"), 23.0);
    QVERIFY(b.waterHeater().on);
    QCOMPARE(b.activeScene(), QStringLiteral("shower"));
    QVERIFY(light("heads_ceiling")->on);
    QVERIFY(store.find("winter_shower")->lastRun.isValid());

    store.run("leave_boat");
    QVERIFY(!b.heater().running());
    QVERIFY(!b.waterHeater().on);
    QVERIFY(!consumer("stereo")->on);

    // Own automation at a time of day, Mondays only.
    Automation a;
    a.name = "Monday light";
    a.trigger = Automation::Trigger::Time;
    a.time = QTime(18, 30);
    a.weekdays = 0x01;
    AutomationAction act;
    act.type = AutomationAction::Type::Light;
    act.target = "galley_spot";
    act.on = true;
    a.actions << act;
    const QString id = store.save(a);
    QVERIFY(!id.isEmpty());
    const QDate monday(2026, 10, 19);
    QCOMPARE(monday.dayOfWeek(), 1);
    b.setLight("galley_spot", false);
    QVERIFY(store.check(QDateTime(monday, QTime(18, 29))).isEmpty());
    QCOMPARE(store.check(QDateTime(monday, QTime(18, 31))), QStringList{id});
    QVERIFY(light("galley_spot")->on);
    // Once per day only, and not when far too late or on Tuesday.
    QVERIFY(store.check(QDateTime(monday, QTime(18, 33))).isEmpty());
    QVERIFY(store.check(QDateTime(monday.addDays(1), QTime(18, 31))).isEmpty());
    QVERIFY(store.check(QDateTime(monday.addDays(7), QTime(19, 0))).isEmpty());
    QCOMPARE(store.check(QDateTime(monday.addDays(7), QTime(18, 35))),
             QStringList{id});
    // Disabled: never.
    Automation off = *store.find(id);
    off.enabled = false;
    store.save(off);
    QVERIFY(
        store.check(QDateTime(monday.addDays(14), QTime(18, 31))).isEmpty());

    // Persisted.
    store.reload();
    QVERIFY(store.find(id));
    QCOMPARE(store.find(id)->actions.size(), 1);
    QCOMPARE(store.find(id)->actions[0].target, QStringLiteral("galley_spot"));
    QCOMPARE(store.find(id)->time, QTime(18, 30));
    QVERIFY(!store.find(id)->enabled);
    store.remove(id);
    QVERIFY(!store.find(id));
    store.reload();
    QVERIFY(!store.find(id));
    QVERIFY(store.find("winter_shower"));
  }
};

QTEST_GUILESS_MAIN(BoatTest)
#include "test_boat.moc"
