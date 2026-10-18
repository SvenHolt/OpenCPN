// Range analysis (consumption log, learned rates, projection with the
// temperature forecast) and provisions (stock, use, check, shopping list).
#include <QSignalSpy>
#include <QtTest>

#include "core/BoatSystems.h"
#include "core/Consumption.h"
#include "core/Provisions.h"
#include "core/Range.h"

namespace {
/** A day of half-hour samples. */
QVector<ConsumptionSample> day(const QDate& date, BoatMode mode, double tempC,
                               double freshL, double heaterFuelL,
                               double engineFuelL = 0, double solarWh = 600,
                               double loadsWh = 1200) {
  QVector<ConsumptionSample> out;
  for (int i = 1; i <= 48; ++i) {
    ConsumptionSample s;
    s.end = QDateTime(date, QTime(0, 0)).addSecs(i * 1800).toUTC();
    // Stay inside the local day.
    s.end = s.end.addSecs(-60);
    s.hours = 0.5;
    s.mode = mode;
    s.outsideC = tempC;
    s.freshL = freshL / 48;
    s.wasteL = freshL / 48 / 3;
    s.heaterFuelL = heaterFuelL / 48;
    s.engineFuelL = engineFuelL / 48;
    s.engineH = engineFuelL > 0 ? 3.0 / 48 : 0;
    s.loadsWh = loadsWh / 48;
    s.solarWh = solarWh / 48;
    out << s;
  }
  return out;
}
}  // namespace

class SuppliesTest : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("opencpn-qt-tests");
    QStandardPaths::setTestModeEnabled(true);
    const QString data =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    for (const char* f :
         {"consumption.json", "provisions.json", "temperature-forecast.json"})
      QFile::remove(data + "/" + f);
  }

  void learnRates() {
    const QDate d0(2026, 10, 1);
    QVector<ConsumptionSample> log;
    // Three days at anchor at 8 °C (10 degree days): 20 l water, 1.5 l
    // heater diesel a day.
    for (int i = 0; i < 3; ++i)
      log += day(d0.addDays(i), BoatMode::Anchor, 8, 20, 1.5);
    // A passage day: four hours underway, engine 9 l.
    auto passage = day(d0.addDays(3), BoatMode::Anchor, 13, 15, 0.5, 9);
    for (int i = 18; i < 26; ++i) passage[i].mode = BoatMode::Underway;
    log += passage;
    // A marina night.
    log += day(d0.addDays(4), BoatMode::Marina, 10, 30, 1);

    const UsageRates anchor = range::learn(log, BoatMode::Anchor);
    QVERIFY(anchor.learned);
    QCOMPARE(anchor.days, 3.0);
    QVERIFY(std::fabs(anchor.freshLpd - 20) < 1e-6);
    QVERIFY(std::fabs(anchor.wasteLpd - 20.0 / 3) < 1e-6);
    QVERIFY(std::fabs(anchor.degreeDaysPerDay - 10) < 1e-6);
    QVERIFY(std::fabs(anchor.heaterFuelPerDd - 0.15) < 1e-6);
    QCOMPARE(anchor.engineFuelLpd, 0.0);
    QVERIFY(std::fabs(anchor.meanTempC - 8) < 1e-6);

    const UsageRates underway = range::learn(log, BoatMode::Underway);
    QVERIFY(underway.learned);
    QCOMPARE(underway.days, 1.0);
    QVERIFY(std::fabs(underway.engineFuelLpd - 9) < 1e-6);
    QVERIFY(std::fabs(underway.freshLpd - 15) < 1e-6);

    // Too little data: typical values.
    const UsageRates none = range::learn({}, BoatMode::Underway);
    QVERIFY(!none.learned);
    QVERIFY(none.freshLpd > 10 && none.engineFuelLpd > 0);
  }

  void projection() {
    UsageRates r = UsageRates::defaults(BoatMode::Anchor);
    r.freshLpd = 20;
    r.wasteLpd = 8;
    r.heaterFuelPerDd = 0.15;
    r.engineFuelLpd = 0;
    r.baseWhPerDay = 1200;
    r.heaterWhPerDd = 10;
    r.solarWhPerDay = 600;
    r.meanTempC = 15;
    r.degreeDaysPerDay = 3;
    RangeState s;
    s.freshL = 100;
    s.wasteL = 16;
    s.wasteCapacityL = 40;
    s.fuelL = 40;
    s.fuelReserveL = 10;
    s.soc = 0.8;
    s.minSoc = 0.2;
    s.capacityAh = 200;
    s.solarPeakW = 400;
    const QDate today(2026, 10, 18);

    // No forecast: the logged mean.
    RangeResult res = range::project(r, s, {}, today);
    QCOMPARE(res.resource(RangeResource::FreshWater).days, 5.0);
    QCOMPARE(res.resource(RangeResource::WasteWater).days, 3.0);
    // 30 l above the reserve at 0.45 l/day: more than 30 days.
    QVERIFY(res.resource(RangeResource::Diesel).unlimited());
    // Battery: 0.6 × 200 Ah × 12.8 V = 1536 Wh, net 630 Wh/day.
    QVERIFY(std::fabs(res.resource(RangeResource::Energy).days - 1536.0 / 630) <
            1e-6);
    QCOMPARE(res.limit, RangeResource::Energy);
    QVERIFY(std::fabs(res.days - 1536.0 / 630) < 1e-6);
    QCOMPARE(res.projection.size(), 30);
    QVERIFY(!res.projection.first().forecast);

    // A cold forecast (0/4 °C: 16 degree days) burns more diesel.
    QVector<DayForecast> cold;
    for (int i = 0; i < 10; ++i)
      cold << DayForecast{today.addDays(i), 0, 4,
                          std::numeric_limits<double>::quiet_NaN()};
    const RangeResult c = range::project(r, s, cold, today);
    QVERIFY(c.projection.first().forecast);
    QCOMPARE(c.projection.first().degreeDays, 16.0);
    QVERIFY(std::fabs(c.projection.first().heaterFuelL - 2.4) < 1e-9);
    QVERIFY(std::fabs(c.resource(RangeResource::Diesel).days - 30 / 2.4) <
            1e-6);
    QVERIFY(c.resource(RangeResource::Energy).days <
            res.resource(RangeResource::Energy).days);
    // Beyond the forecast its mean is used.
    QCOMPARE(c.projection[15].tempC, 2.0);
    QVERIFY(!c.projection[15].forecast);

    // A sunny, warm forecast: solar covers the use, the battery lasts.
    QVector<DayForecast> sunny;
    for (int i = 0; i < 16; ++i)
      sunny << DayForecast{today.addDays(i), 16, 26, 20};  // 20 MJ/m²
    r.solarWhPerDay = 1500;  // after the forecast as well
    const RangeResult w = range::project(r, s, sunny, today);
    QVERIFY(w.projection.first().solarWh > 1200);
    QVERIFY(w.resource(RangeResource::Energy).unlimited());
    QVERIFY(w.projection.last().batteryPct <= 100.0 + 1e-9);
    QCOMPARE(w.limit, RangeResource::WasteWater);

    // Underway: the engine burns diesel and the alternator charges.
    UsageRates u = r;
    u.mode = BoatMode::Underway;
    u.engineFuelLpd = 10;
    u.alternatorWhPerDay = 2000;
    const RangeResult uw = range::project(u, s, {}, today);
    QVERIFY(uw.resource(RangeResource::Diesel).days < 3.5);
    QVERIFY(uw.resource(RangeResource::Energy).unlimited());
  }

  void trips() {
    QVector<ConsumptionSample> log;
    const QDate d0(2026, 10, 1);
    log += day(d0, BoatMode::Marina, 10, 10, 0);
    log += day(d0.addDays(1), BoatMode::Anchor, 10, 10, 0);
    log += day(d0.addDays(2), BoatMode::Anchor, 10, 10, 0);
    log += day(d0.addDays(3), BoatMode::Marina, 10, 10, 0);
    const auto trip = range::lastTrip(log);
    QVERIFY(trip.first.isValid());
    QCOMPARE(trip.first, log[48].start());
    QCOMPARE(trip.second, log[48 * 3 - 1].end);
    const auto w = range::window(log, RangeWindow::LastTrip, QDateTime());
    QCOMPARE(w, trip);
    const QDateTime now = log.last().end;
    QCOMPARE(range::window(log, RangeWindow::Days3, now).first,
             now.addDays(-3));
    QVERIFY(!range::window({}, RangeWindow::Days7, now).first.isValid());
  }

  void forecastParse() {
    const QByteArray json = R"({"daily": {
      "time": ["2026-10-18", "2026-10-19", "2026-10-20"],
      "temperature_2m_min": [6.1, null, 4.0],
      "temperature_2m_max": [12.5, 11.0, 9.5],
      "shortwave_radiation_sum": [7.2, 5.0, null]}})";
    QString err;
    const auto days = TemperatureForecast::parse(json, &err);
    QCOMPARE(days.size(), 2);  // the day without a minimum is skipped
    QCOMPARE(days[0].date, QDate(2026, 10, 18));
    QCOMPARE(days[0].minC, 6.1);
    QCOMPARE(days[0].radiationMJ, 7.2);
    QVERIFY(std::isnan(days[1].radiationMJ));
    QVERIFY(std::fabs(days[1].degreeDays() - (14 + 8.5) / 2) < 1e-9);
    QVERIFY(TemperatureForecast::parse("{}", &err).isEmpty());
    QVERIFY(!err.isEmpty());

    // Stored for offline use.
    auto& fc = TemperatureForecast::instance();
    QSignalSpy spy(&fc, &TemperatureForecast::updated);
    fc.setDays(days, QPointF(10.3, 54.5), QDateTime::currentDateTimeUtc());
    QCOMPARE(spy.size(), 1);
    QCOMPARE(fc.upcoming(QDate(2026, 10, 19)).size(), 1);
  }

  void consumptionLog() {
    auto& log = ConsumptionLog::instance();
    // The simulator starts with generated days.
    QVERIFY(log.samples().size() >= 9 * 48);
    QVERIFY(log.simulatedHistory());
    const UsageRates a = range::learn(log.samples(), BoatMode::Anchor);
    QVERIFY(a.learned);
    QVERIFY2(a.freshLpd > 10 && a.freshLpd < 40,
             qPrintable(QString::number(a.freshLpd)));
    QVERIFY(a.heaterFuelPerDd > 0);
    const UsageRates u = range::learn(log.samples(), BoatMode::Underway);
    QVERIFY(u.learned);
    QVERIFY(u.engineFuelLpd > 1);
    QVERIFY(u.alternatorWhPerDay > 0);
    // Marina days are no anchor days.
    QVERIFY(range::learn(log.samples(), BoatMode::Marina).learned);

    // Samples from the counters of the boat systems.
    log.clear();
    QVERIFY(log.samples().isEmpty());
    QVERIFY(!log.simulatedHistory());
    auto& b = BoatSystems::instance();
    const BoatCounters before = b.counters();
    b.setConsumer("water_pump", true);
    for (int i = 0; i < 60; ++i) b.simulate(60);
    QVERIFY(b.counters().loadsWh > before.loadsWh);
    QVERIFY(b.counters().freshL > before.freshL);
    log.sampleNow();
    QCOMPARE(log.samples().size(), 1);
    const ConsumptionSample s = log.samples().first();
    QVERIFY(s.loadsWh > 0);
    QVERIFY(s.freshL > 0);
    QVERIFY(s.freshLevelL > 0);

    // Persisted; older than 30 days is dropped.
    ConsumptionSample old = s;
    old.end = s.end.addDays(-31);
    log.add(old);
    QCOMPARE(log.samples().size(), 1);
    ConsumptionSample next = s;
    next.end = s.end.addSecs(1800);
    next.mode = BoatMode::Underway;
    log.add(next);
    log.reload();
    QCOMPARE(log.samples().size(), 2);
    QCOMPARE(log.samples().last().mode, BoatMode::Underway);
    QCOMPARE(log.between(s.end, next.end.addSecs(1)).size(), 1);
  }

  void provisions() {
    auto& store = ProvisionStore::instance();
    store.clear();
    store.setTrip(2, 7);
    const QDate today(2026, 10, 18);
    ProvisionItem apples;
    apples.name = "Apples";
    apples.category = ProvisionCategory::Fruit;
    apples.quantity = 10;
    apples.unit = "pcs";
    apples.added = today.addDays(-4);
    apples.bestBefore = today.addDays(10);
    apples.plannedPerPersonDay = 0.5;
    const QString id = store.save(apples);
    // Planned: 0.5 × 2 = 1 a day → 10 days.
    QCOMPARE(store.usePerDay(*store.find(id), today), 1.0);
    QCOMPARE(store.daysLeft(*store.find(id), today), 10.0);
    // Taken: 2 + 2 + 2 over the four days since bought → 1.5/day measured.
    store.take(id, 2, today.addDays(-2));
    store.take(id, 2, today.addDays(-1));
    store.take(id, 2, today);
    QCOMPARE(store.find(id)->quantity, 4.0);
    QCOMPARE(ProvisionStore::measuredPerDay(*store.find(id), today), 6.0 / 5);
    QVERIFY(std::fabs(store.daysLeft(*store.find(id), today) - 4 / 1.2) < 1e-9);
    // Stock count: 3 left → one more used.
    store.count(id, 3, today);
    QCOMPARE(store.find(id)->quantity, 3.0);
    QCOMPARE(store.find(id)->usage.size(), 4);
    // Taking more than there is takes what is left.
    store.take(id, 99, today);
    QCOMPARE(store.find(id)->quantity, 0.0);
    store.count(id, 6, today);  // restock
    QCOMPARE(store.find(id)->quantity, 6.0);
    QCOMPARE(store.find(id)->usage.size(), 5);

    // Milk: expires in two days, plenty left → will spoil; tuna below the
    // minimum; yoghurt past its date; bread runs out before the trip ends.
    const auto add = [&](const QString& name, ProvisionCategory c, double q,
                         int bestBefore, double planned, double minQ = 0) {
      ProvisionItem i;
      i.name = name;
      i.category = c;
      i.quantity = q;
      i.added = today;
      i.bestBefore = today.addDays(bestBefore);
      i.plannedPerPersonDay = planned;
      i.minQuantity = minQ;
      return store.save(i);
    };
    const QString milk = add("Milk", ProvisionCategory::Fridge, 4, 2, 0.25);
    const QString tuna = add("Tuna", ProvisionCategory::Cans, 1, 600, 0, 3);
    const QString yog = add("Yoghurt", ProvisionCategory::Fridge, 2, -1, 0.5);
    const QString bread = add("Bread", ProvisionCategory::Freezer, 4, 60, 1);
    const auto issues = store.check(today);
    const auto has = [&](const QString& item, ProvisionIssue::Kind k) {
      for (const auto& i : issues)
        if (i.itemId == item && i.kind == k) return true;
      return false;
    };
    using K = ProvisionIssue::Kind;
    QVERIFY(has(milk, K::WillSpoil));
    QVERIFY(has(tuna, K::BelowMin));
    QVERIFY(has(yog, K::Expired));
    QVERIFY(has(bread, K::RunsOut));
    // Apples: 10 used in 5 days, 6 left = 3 days.
    QVERIFY(has(id, K::RunsOut));
    QVERIFY(issues.first().kind == K::Expired);

    // Shopping list for 7 days: bread 2 × 7 = 14, 4 in stock → 10.
    const auto list = store.shoppingList(today);
    const auto need = [&](const QString& item) {
      for (const auto& e : list)
        if (e.itemId == item) return e.quantity;
      return 0.0;
    };
    QCOMPARE(need(bread), 10.0);
    QCOMPARE(need(tuna), 2.0);
    QCOMPARE(need(yog), 7.0);  // expired stock does not count
    // Milk: 0.5 a day for 7 days, only 2 days usable (1): 2.5, rounded
    // up to whole pieces.
    QCOMPARE(need(milk), 3.0);
    QCOMPARE(need(id), 8.0);

    const CategorySummary fridge =
        store.summary(ProvisionCategory::Fridge, today);
    QCOMPARE(fridge.items, 2);
    QCOMPARE(fridge.earliestBestBefore, today.addDays(-1));

    // Persisted.
    store.reload();
    QCOMPARE(store.items().size(), 5);
    QCOMPARE(store.find(id)->usage.size(), 5);
    QCOMPARE(store.tripDays(), 7);
    QCOMPARE(store.find(milk)->category, ProvisionCategory::Fridge);
    store.remove(milk);
    QVERIFY(!store.find(milk));

    // Examples cover all categories.
    store.loadExamples(today);
    for (auto c : ProvisionStore::categories())
      QVERIFY(!store.inCategory(c).isEmpty());
    QVERIFY(!store.check(today).isEmpty());
  }
};

QTEST_GUILESS_MAIN(SuppliesTest)
#include "test_supplies.moc"
