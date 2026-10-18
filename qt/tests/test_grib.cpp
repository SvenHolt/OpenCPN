// GRIB decoder tests. Reference values were produced with ECMWF eccodes
// 2.50 from the same files (non-missing count, sum, min, max).
#include <QtTest>
#include <cmath>

#include "core/Grib.h"
#include "core/GribDownload.h"

namespace {
struct Ref {
  qsizetype count;
  double sum, min, max;
};

void check(const GribField& f, const Ref& r) {
  qsizetype n = 0;
  double sum = 0, mn = 1e300, mx = -1e300;
  for (float v : f.values) {
    if (std::isnan(v)) continue;
    ++n;
    sum += v;
    mn = std::min<double>(mn, v);
    mx = std::max<double>(mx, v);
  }
  QCOMPARE(n, r.count);
  QVERIFY2(
      std::fabs(sum - r.sum) < 1e-3 * std::max(1.0, std::fabs(r.sum)) + 0.05,
      qPrintable(QString("sum %1 vs %2").arg(sum).arg(r.sum)));
  QVERIFY(std::fabs(mn - r.min) < 1e-3 + 1e-7 * std::fabs(r.min));
  QVERIFY(std::fabs(mx - r.max) < 1e-3 + 1e-7 * std::fabs(r.max));
}

GribFile loadFile(const char* name) {
  GribFile g;
  QString err;
  const bool ok =
      g.load(QStringLiteral(OCPN_QT_TEST_DATA_DIR "/") + name, &err);
  if (!ok) qWarning("%s: %s", name, qPrintable(err));
  return g;
}
}  // namespace

class GribTest : public QObject {
  Q_OBJECT
private slots:
  void grib2SimplePacking() {
    const GribFile g = loadFile("gfs_simple.grb2");
    QCOMPARE(g.fields().size(), 3);
    const GribField& p = g.fields()[0];
    QCOMPARE(p.param, GribField::Param::Pressure);
    QCOMPARE(p.ni, 21);
    QCOMPARE(p.nj, 11);
    QCOMPARE(p.lat1, 50.0);
    QCOMPARE(p.dlat, 1.0);  // south to north
    QCOMPARE(p.refTime.date(), QDate(2026, 10, 17));
    QCOMPARE(p.refTime.secsTo(p.validTime), 6 * 3600);
    check(p, {231, 23640855.47, 101640.45, 103441.09});
    QVERIFY(std::fabs(p.values[100] - 101683.49) < 0.01);
    check(g.fields()[1], {231, 97.8939, -5.9919, 6.5581});
    check(g.fields()[2], {231, -1188.7199, -15.1519, 6.6681});
    QCOMPARE(g.fields()[1].param, GribField::Param::WindU);
    QCOMPARE(g.fields()[2].param, GribField::Param::WindV);

    QCOMPARE(g.steps().size(), 1);
    double speed, dir;
    QVERIFY(g.wind(0, 55.0, 10.0, speed, dir));
    QVERIFY(speed > 0 && dir >= 0 && dir < 360);
    // Grid point exactly: lat 50 + 4, lon 0 + 16 -> index 4*21+16 = 100.
    QVERIFY(std::fabs(p.sample(54.0, 16.0) - 101683.49) < 0.01);
  }

  void grib2ComplexPackingSpatialDifferencing() {
    const GribFile g = loadFile("gfs_complex.grb2");
    QCOMPARE(g.fields().size(), 1);
    const GribField& u = g.fields()[0];
    QCOMPARE(u.ni, 360);
    QCOMPARE(u.nj, 181);
    QCOMPARE(u.lat1, 90.0);
    QCOMPARE(u.dlat, -1.0);  // north to south
    check(u, {65160, 26945.2461, -31.9019, 28.8081});
    QVERIFY(std::fabs(u.values[100] + 4.5919) < 1e-3);
    // Global grid wraps around the date line.
    QVERIFY(!std::isnan(u.sample(10.0, 359.5)));
    QVERIFY(!std::isnan(u.sample(10.0, -0.5)));
  }

  void grib1WithBitmap() {
    const GribFile g = loadFile("quikscat.grb");
    QCOMPARE(g.fields().size(), 4);
    const Ref refs[] = {{2194, 47.0, 0.0, 1.0},
                        {2194, -12175.681, -16.415, 6.299},
                        {2194, 5406.101, -11.679, 13.66},
                        {2194, 51058549.0, 19598.0, 25986.0}};
    for (int i = 0; i < 4; ++i) check(g.fields()[i], refs[i]);
    QCOMPARE(g.fields()[1].ni, 66);
    QCOMPARE(g.fields()[1].nj, 74);
    QVERIFY(std::fabs(g.fields()[1].lat1 - 31.575) < 1e-6);
    QVERIFY(std::isnan(g.fields()[1].values[100]));  // masked by bitmap
  }

  void gfsUrl() {
    const QDateTime run(QDate(2026, 10, 17), QTime(6, 0), Qt::UTC);
    const QString u =
        GribDownload::url(run, 9, 50.2, 2.5, 58.7, 14.1).toString();
    QVERIFY(u.contains("dir=/gfs.20261017/06/atmos"));
    QVERIFY(u.contains("file=gfs.t06z.pgrb2.0p25.f009"));
    QVERIFY(u.contains("leftlon=2&rightlon=15&toplat=59&bottomlat=50"));
    const auto runs = GribDownload::candidateRuns(
        QDateTime(QDate(2026, 10, 17), QTime(11, 0), Qt::UTC));
    QCOMPARE(runs.first(),
             QDateTime(QDate(2026, 10, 17), QTime(6, 0), Qt::UTC));
  }

  /** Live download from NOAA; enabled with OCPN_QT_NETWORK_TESTS=1. */
  void gfsDownloadLive() {
    if (qEnvironmentVariableIsEmpty("OCPN_QT_NETWORK_TESTS"))
      QSKIP("set OCPN_QT_NETWORK_TESTS=1 to run");
    QStandardPaths::setTestModeEnabled(true);
    GribDownload dl;
    QSignalSpy done(&dl, &GribDownload::finished);
    QSignalSpy failed(&dl, &GribDownload::failed);
    dl.start(53, 7, 56, 11, 6, 3);
    QTRY_VERIFY_WITH_TIMEOUT(done.count() + failed.count() > 0, 120000);
    QVERIFY2(failed.isEmpty(),
             failed.isEmpty() ? "" : qPrintable(failed[0][0].toString()));
    GribFile g;
    QVERIFY(g.load(done[0][0].toString()));
    QCOMPARE(g.steps().size(), 3);
    double kn, dir;
    QVERIFY(g.wind(1, 54.5, 9.0, kn, dir));
    qInfo("GFS wind at 54.5N 9E: %.1f kn from %.0f", kn, dir);
  }
};

QTEST_GUILESS_MAIN(GribTest)
#include "test_grib.moc"
