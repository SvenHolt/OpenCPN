#include "core/Shapefile.h"

#include <QFile>
#include <QtEndian>
#include <cstring>

#include "core/GeoMath.h"

namespace {
qint32 be32(const char* p) { return qFromBigEndian<qint32>(p); }
qint32 le32(const char* p) { return qFromLittleEndian<qint32>(p); }
double le64(const char* p) {
  quint64 v = qFromLittleEndian<quint64>(p);
  double d;
  std::memcpy(&d, &v, sizeof d);
  return d;
}
}  // namespace

namespace Shapefile {

bool loadPolygons(const QString& path, QVector<MercatorPolygon>& out,
                  QString* error) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) *error = f.errorString();
    return false;
  }
  const QByteArray data = f.readAll();
  const char* d = data.constData();
  const qsizetype n = data.size();
  if (n < 100 || be32(d) != 9994) {
    if (error) *error = QStringLiteral("Not a shapefile: %1").arg(path);
    return false;
  }
  qsizetype pos = 100;
  while (pos + 8 <= n) {
    const qsizetype len = qsizetype(be32(d + pos + 4)) * 2;
    const char* rec = d + pos + 8;
    pos += 8 + len;
    if (pos > n || len < 44) continue;
    const qint32 type = le32(rec);
    if (type != 5 && type != 15 && type != 25) continue;  // Polygon(Z/M)
    const qint32 numParts = le32(rec + 36);
    const qint32 numPoints = le32(rec + 40);
    const qsizetype ptsOff = 44 + qsizetype(numParts) * 4;
    if (numParts <= 0 || numPoints <= 0 ||
        ptsOff + qsizetype(numPoints) * 16 > len)
      continue;
    MercatorPolygon poly;
    poly.path.setFillRule(Qt::OddEvenFill);
    for (qint32 part = 0; part < numParts; ++part) {
      const qint32 first = le32(rec + 44 + part * 4);
      const qint32 last =
          part + 1 < numParts ? le32(rec + 44 + (part + 1) * 4) : numPoints;
      if (first < 0 || last > numPoints || last - first < 3) continue;
      QPolygonF ring;
      ring.reserve(last - first);
      for (qint32 i = first; i < last; ++i) {
        const char* p = rec + ptsOff + qsizetype(i) * 16;
        ring.append(geo::toMercator(le64(p + 8), le64(p)));
      }
      poly.path.addPolygon(ring);
      poly.pointCount += ring.size();
    }
    if (poly.pointCount == 0) continue;
    poly.bounds = poly.path.boundingRect();
    out.push_back(std::move(poly));
  }
  return true;
}

}  // namespace Shapefile
