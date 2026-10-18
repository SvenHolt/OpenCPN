// Legs (recording, summary, storage), heel from NMEA and the logbook.
#include <QSignalSpy>
#include <QtTest>

#include "core/GeoMath.h"
#include "core/Legs.h"
#include "core/Logbook.h"
#include "core/NavStore.h"
#include "core/Nmea.h"

namespace {
/** A sample `minutes` after `t0`, `nm` north of 54.5 N 10.3 E. */
LegSample at(qint64 t0, double minutes, double nm, double sog, double rpm = 0) {
  LegSample s;
  s.t = t0 + qint64(minutes * 60);
  s.lat = 54.5 + nm / 60;
  s.lon = 10.3;
  s.sog = sog;
  s.cog = s.hdg = 0;
  s.heel = rpm > 0 ? 0 : -12;
  s.tws = 14;
  s.twd = 270;
  s.tempC = 11;
  s.loadsW = 100;
  s.chargeW = rpm > 0 ? 600 : 0;
  s.fuelLph = rpm > 0 ? 3 : 0;
  s.rpm = rpm;
  s.soc = 0.8;
  return s;
}
}  // namespace

class LegsTest : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("opencpn-qt-tests");
    QStandardPaths::setTestModeEnabled(true);
    const QString data =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir(data + "/legs").removeRecursively();
    QFile::remove(data + "/logbook.json");
  }

  void heelFromNmea() {
    NmeaParser parser;
    NavUpdate u;
    connect(&parser, &NmeaParser::navUpdate, this,
            [&](const NavUpdate& x) { u = x; });
    QVERIFY(parser.parseLine(NmeaParser::makeSentence("IIXDR,A,-12.5,D,ROLL")));
    QVERIFY(u.heel);
    QCOMPARE(*u.heel, -12.5);
    NavStore::instance().applyNavUpdate(u);
    QCOMPARE(NavStore::instance().environment().heel, -12.5);
  }

  void sampleJson() {
    LegSample s = at(1'700'000'000, 0, 1, 6.25);
    s.depth = LegSample::kLegNaN;
    const QJsonObject o = s.toJson();
    QVERIFY(!o.contains("depth"));  // missing values are left out
    const LegSample r = LegSample::fromJson(o);
    QCOMPARE(r.t, s.t);
    QCOMPARE(r.sog, 6.25);
    QCOMPARE(r.heel, -12.0);
    QVERIFY(std::isnan(r.depth));
    QVERIFY(std::fabs(r.lat - s.lat) < 1e-6);
  }

  void summary() {
    // One hour sailing at 6 kn, one hour motoring at 5 kn, 30 s steps.
    Leg leg;
    const qint64 t0 = 1'700'000'000;
    double nm = 0;
    for (int i = 0; i <= 240; ++i) {
      const bool motor = i > 120;
      leg.samples << at(t0, i * 0.5, nm, motor ? 5 : 6, motor ? 2000 : 0);
      nm += (motor ? 5.0 : 6.0) / 120;
    }
    const LegSummary s = leg.summary();
    QVERIFY(std::fabs(s.hours - 2) < 1e-9);
    QVERIFY2(std::fabs(s.distanceNm - 11) < 0.05,
             qPrintable(QString::number(s.distanceNm)));
    QVERIFY(std::fabs(s.avgSog - 5.5) < 0.01);
    QCOMPARE(s.maxSog, 6.0);
    QVERIFY(std::fabs(s.engineHours - 1) < 1e-9);
    QVERIFY(std::fabs(s.sailingShare - 0.5) < 1e-9);
    QVERIFY(std::fabs(s.fuelL - 3) < 1e-9);
    QCOMPARE(s.maxHeel, 12.0);
    QVERIFY(std::fabs(s.energyWh - 200) < 1e-6);
    QVERIFY(std::fabs(s.chargedWh - 600) < 1e-6);
    QCOMPARE(s.maxTws, 14.0);
    QCOMPARE(leg.indexAt(t0 + 31 * 60), 62);
    QCOMPARE(leg.indexAt(t0 - 100), 0);
    QCOMPARE(leg.indexAt(t0 + 99999), 240);
    // A gap in the recording does not count as distance.
    Leg gap;
    gap.samples << at(t0, 0, 0, 5) << at(t0, 60, 5, 5);
    QCOMPARE(gap.summary().distanceNm, 0.0);
  }

  void seeded() {
    auto& store = LegStore::instance();
    // The simulator starts with four legs, newest first.
    QCOMPARE(store.legs().size(), 4);
    QVERIFY(store.legs().first().start() > store.legs().last().start());
    for (const Leg& l : store.legs()) {
      QVERIFY(l.simulated);
      const LegSummary s = l.summary();
      QVERIFY2(s.distanceNm > 15 && s.distanceNm < 45,
               qPrintable(l.name + QString::number(s.distanceNm)));
      QVERIFY(s.maxHeel > 5);
      QVERIFY(s.engineHours > 0.1);
      QVERIFY(s.sailingShare > 0.1);
      QVERIFY(!l.from.isEmpty() && !l.to.isEmpty());
    }
    // Stored on disk.
    store.reload();
    QCOMPARE(store.legs().size(), 4);
    const Leg& last = store.legs().first();
    QCOMPARE(store.legsOn(last.start().toLocalTime().date()).size(), 1);
  }

  void recording() {
    auto& store = LegStore::instance();
    const int before = int(store.legs().size());
    for (const auto& [name, lat] :
         {std::pair<QString, double>{"Harbour A", 54.5},
          {"Harbour B", 54.5 + 4.0 / 60}}) {
      Waypoint w;
      w.name = name;
      w.lat = lat;
      w.lon = 10.3;
      NavStore::instance().addWaypoint(w);
    }
    const qint64 t0 = QDateTime::currentSecsSinceEpoch() - 4 * 3600;
    // At rest: nothing.
    store.addSample(at(t0, 0, 0, 0.2), false);
    QVERIFY(!store.recording());
    // Underway for 48 minutes at 5 kn.
    double nm = 0;
    int i = 1;
    for (; i <= 96; ++i) {
      store.addSample(at(t0, i * 0.5, nm, 5), i < 10);
      nm += 5.0 / 120;
    }
    QVERIFY(store.recording());
    QVERIFY(store.current().samples.size() >= 96);
    // 25 minutes at rest close the leg; the rest is cut off.
    for (int k = 0; k < 50; ++k, ++i)
      store.addSample(at(t0, i * 0.5, nm, 0.1), false);
    QVERIFY(!store.recording());
    QCOMPARE(int(store.legs().size()), before + 1);
    const Leg& leg = store.legs().first();
    QVERIFY(leg.samples.last().sog > 1);
    QCOMPARE(leg.from, QStringLiteral("Harbour A"));
    QCOMPARE(leg.to, QStringLiteral("Harbour B"));
    QCOMPARE(leg.name, QStringLiteral("Harbour A – Harbour B"));
    QVERIFY(std::fabs(leg.summary().distanceNm - 4) < 0.1);

    // A short move in the harbour is not a leg.
    const qint64 t1 = t0 + 3 * 3600;
    for (int k = 0; k < 6; ++k)
      store.addSample(at(t1, k * 0.5, k * 0.01, 2), false);
    for (int k = 6; k < 60; ++k)
      store.addSample(at(t1, k * 0.5, 0.06, 0), false);
    QVERIFY(!store.recording());
    QCOMPARE(int(store.legs().size()), before + 1);

    // Rename and remove.
    const QString id = leg.id;
    store.rename(id, "Test leg", "A", "B");
    store.reload();
    QCOMPARE(store.leg(id)->name, QStringLiteral("Test leg"));
    QCOMPARE(store.leg(id)->to, QStringLiteral("B"));
    store.remove(id);
    store.reload();
    QVERIFY(!store.leg(id));
  }

  void logbook() {
    auto& book = Logbook::instance();
    const Leg& leg = LegStore::instance().legs().first();
    const QDate day = leg.start().toLocalTime().date();
    // Facts from the recorded legs.
    const DayFacts f = DayFacts::of(day);
    QCOMPARE(f.legs, 1);
    QVERIFY(f.distanceNm > 15);
    QCOMPARE(f.places.first(), leg.from);
    QCOMPARE(f.places.last(), leg.to);
    QVERIFY(!f.windDir.isEmpty());
    QVERIFY(f.text().contains("NM"));
    QVERIFY(DayFacts::of(day.addDays(1)).text().contains("No leg"));

    LogEntry e;
    e.date = day;
    e.crew = "Sven, Anna";
    e.weather = "SW 4, sunny";
    e.report = "Departed 09:15, reefed at noon.";
    QSignalSpy spy(&book, &Logbook::changed);
    book.save(e);
    QCOMPARE(spy.size(), 1);
    QCOMPARE(book.entry(day).crew, QStringLiteral("Sven, Anna"));
    QVERIFY(book.entry(day).updated.isValid());
    // A day without an entry is empty.
    QVERIFY(book.entry(day.addDays(-1)).isEmpty());
    // Days: with entries and with legs, newest first.
    const auto days = book.days();
    QVERIFY(days.contains(day));
    for (const Leg& l : LegStore::instance().legs())
      QVERIFY(days.contains(l.start().toLocalTime().date()));
    QVERIFY(days.first() >= days.last());
    // Markdown.
    const QString md = book.markdown();
    QVERIFY(md.contains("Departed 09:15, reefed at noon."));
    QVERIFY(md.contains("Sven, Anna"));
    QVERIFY(md.contains(leg.to));
    // Persisted; an empty entry removes the day.
    book.reload();
    QCOMPARE(book.entry(day).report, e.report);
    LogEntry empty;
    empty.date = day;
    book.save(empty);
    book.reload();
    QVERIFY(!book.entries().contains(day));
  }
};

QTEST_GUILESS_MAIN(LegsTest)
#include "test_legs.moc"
