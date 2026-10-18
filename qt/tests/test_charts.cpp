// Tests for chart readers (BSB/KAP raster, S-57 vector).
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

#include "core/GeoMath.h"
#include "core/charts/BsbChart.h"
#include "core/charts/ChartDatabase.h"
#include "core/charts/S57Chart.h"

class ChartTest : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    S57Chart::setRegistryDir(QStringLiteral(OCPN_QT_SOURCE_DATA_DIR) +
                             "/s57data");
  }

  void bsbDecodeGdalSample() {
    QFile f(QStringLiteral(OCPN_QT_TEST_DATA_DIR) + "/rgbsmall.kap");
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray data = f.readAll();
    BsbHeader h;
    qsizetype start = 0;
    QVERIFY(BsbChart::parseHeader(data, h, &start, nullptr));
    QCOMPARE(h.width, 50);
    QCOMPARE(h.height, 50);
    QCOMPARE(h.scale, 25000);
    QCOMPARE(h.palette.size(), 128);
    const QImage img = BsbChart::decodeRaster(data, start, h, nullptr);
    QCOMPARE(img.size(), QSize(50, 50));
    int zeros = 0;
    for (int y = 0; y < 50; ++y)
      for (int x = 0; x < 50; ++x) zeros += img.pixelIndex(x, y) == 0;
    QCOMPARE(zeros, 0);  // every pixel got a palette entry
  }

  void bsbRoundTripAndGeoref() {
    // Synthetic 300x200 chart with long runs (multi byte run lengths).
    QImage img(300, 200, QImage::Format_Indexed8);
    QVector<QRgb> pal(256, qRgb(0, 0, 0));
    for (int i = 1; i < 8; ++i) pal[i] = qRgb(i * 30, 255 - i * 30, 128);
    img.setColorTable(pal);
    for (int y = 0; y < 200; ++y)
      for (int x = 0; x < 300; ++x)
        img.setPixel(x, y, 1 + ((x / 150 + y / 40) % 7));
    BsbHeader h;
    h.name = "TEST";
    h.scale = 50000;
    h.palette = pal.mid(0, 8);
    // Mercator georeference: corners of a 0.3 x 0.2 degree box.
    const double lat0 = 54.5, lon0 = 10.0, lat1 = 54.3, lon1 = 10.3;
    const QPointF m0 = geo::toMercator(lat0, lon0);
    const QPointF m1 = geo::toMercator(lat1, lon1);
    auto ref = [&](double x, double y) {
      const QPointF m(m0.x() + (m1.x() - m0.x()) * x / 300,
                      m0.y() + (m1.y() - m0.y()) * y / 200);
      double lat, lon;
      geo::fromMercator(m, lat, lon);
      return BsbHeader::Ref{x, y, lat, lon};
    };
    h.refs = {ref(0, 0), ref(300, 0), ref(300, 200), ref(0, 200)};

    for (int depth : {3, 7}) {
      const QByteArray data = BsbChart::encode(h, img, depth);
      BsbHeader back;
      qsizetype start = 0;
      QVERIFY(BsbChart::parseHeader(data, back, &start, nullptr));
      QCOMPARE(back.refs.size(), 4);
      const QImage dec = BsbChart::decodeRaster(data, start, back, nullptr);
      QCOMPARE(dec.size(), img.size());
      for (int y = 0; y < 200; ++y)
        QCOMPARE(memcmp(dec.constScanLine(y), img.constScanLine(y), 300), 0);
    }

    QTemporaryDir dir;
    const QString path = dir.filePath("test.kap");
    QFile out(path);
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(BsbChart::encode(h, img));
    out.close();
    BsbChart chart(path);
    QVERIFY2(chart.isValid(), qPrintable(chart.error()));
    QCOMPARE(chart.nativeScale(), 50000);
    const QPointF c00 = chart.pixelToWorld().map(QPointF(0, 0));
    const QPointF c11 = chart.pixelToWorld().map(QPointF(300, 200));
    QVERIFY(std::hypot(c00.x() - m0.x(), c00.y() - m0.y()) < 1e-9);
    QVERIFY(std::hypot(c11.x() - m1.x(), c11.y() - m1.y()) < 1e-9);
    QCOMPARE(chart.image().size(), QSize(300, 200));

    // Database scan finds it.
    const ChartList list = ChartDatabase::scan({dir.path()});
    QCOMPARE(list.size(), 1);
    QCOMPARE(list[0]->type(), Chart::Type::Raster);
  }

  void s57Cell() {
    S57Chart chart(QStringLiteral(OCPN_QT_TEST_DATA_DIR) + "/1B5X02NE.000");
    QVERIFY2(chart.isValid(), qPrintable(chart.error()));
    QCOMPARE(chart.nativeScale(), 20000);
    QSet<QByteArray> classes;
    int soundings = 0;
    for (const auto& f : chart.features()) {
      classes << f.cls;
      soundings += int(f.soundPts.size());
    }
    for (const char* cls : {"DEPARE", "DEPCNT", "LNDARE", "COALNE", "SOUNDG"})
      QVERIFY2(classes.contains(cls), cls);
    QVERIFY(soundings > 0);
    double lat, lon;
    geo::fromMercator(chart.bounds().center(), lat, lon);
    QVERIFY(qAbs(lat + 32.496) < 0.01);
    QVERIFY(qAbs(lon - 60.98) < 0.01);

    // Render smoke test: depth areas must change pixels.
    QImage img(400, 400, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    ChartRenderContext ctx;
    const QRectF b = chart.bounds();
    ctx.scale = 400 / std::max(b.width(), b.height());
    ctx.worldToImage =
        QTransform().scale(ctx.scale, ctx.scale).translate(-b.left(), -b.top());
    ctx.visibleWorld = b;
    ctx.displayScale = 10000;
    ctx.style.depthDeep = Qt::blue;
    ctx.style.depthMedium = Qt::blue;
    ctx.style.depthShallow = Qt::cyan;
    ctx.style.depthVeryShallow = Qt::cyan;
    ctx.style.land = Qt::yellow;
    {
      QPainter p(&img);
      chart.render(p, ctx);
    }
    int painted = 0;
    for (int y = 0; y < 400; y += 10)
      for (int x = 0; x < 400; x += 10) painted += qAlpha(img.pixel(x, y)) > 0;
    QVERIFY(painted > 100);
  }
};

QTEST_MAIN(ChartTest)
#include "test_charts.moc"
