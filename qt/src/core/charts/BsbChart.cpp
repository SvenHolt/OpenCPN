#include "core/charts/BsbChart.h"

#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QRegularExpression>
#include <cmath>

#include "core/GeoMath.h"

namespace {

/** Solves the 3x3 normal equations for x = a*u + b*v + c. */
bool fitAffine(const QVector<QPointF>& uv, const QVector<double>& x, double& a,
               double& b, double& c) {
  double s[3][4] = {};
  for (int i = 0; i < uv.size(); ++i) {
    const double r[3] = {uv[i].x(), uv[i].y(), 1.0};
    for (int j = 0; j < 3; ++j) {
      for (int k = 0; k < 3; ++k) s[j][k] += r[j] * r[k];
      s[j][3] += r[j] * x[i];
    }
  }
  for (int col = 0; col < 3; ++col) {  // Gauss-Jordan with pivoting
    int piv = col;
    for (int r = col + 1; r < 3; ++r)
      if (std::fabs(s[r][col]) > std::fabs(s[piv][col])) piv = r;
    if (std::fabs(s[piv][col]) < 1e-30) return false;
    for (int k = 0; k < 4; ++k) std::swap(s[col][k], s[piv][k]);
    for (int r = 0; r < 3; ++r) {
      if (r == col) continue;
      const double f = s[r][col] / s[col][col];
      for (int k = col; k < 4; ++k) s[r][k] -= f * s[col][k];
    }
  }
  a = s[0][3] / s[0][0];
  b = s[1][3] / s[1][1];
  c = s[2][3] / s[2][2];
  return true;
}

QString value(const QString& line, const QString& key) {
  const QRegularExpression re(QStringLiteral("(?:^|[,/])%1=([^,]*)").arg(key));
  const auto m = re.match(line);
  return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

}  // namespace

bool BsbChart::parseHeader(const QByteArray& data, BsbHeader& h,
                           qsizetype* rasterStart, QString* error) {
  const qsizetype end = data.indexOf('\x1a');
  if (end < 0) {
    if (error) *error = QStringLiteral("No BSB header terminator");
    return false;
  }
  // Join continuation lines (starting with whitespace) to their record.
  QStringList records;
  for (const QByteArray& raw : data.left(end).split('\n')) {
    QString line = QString::fromLatin1(raw).remove('\r');
    if (line.isEmpty()) continue;
    if ((line[0] == ' ' || line[0] == '\t') && !records.isEmpty())
      records.last() += ',' + line.trimmed();
    else if (line.size() > 4 && line[3] == '/')
      records << line.trimmed();
    else if (!records.isEmpty() && !line.startsWith('!'))
      records.last() += line.trimmed();  // broken line
  }
  for (const QString& r : records) {
    const QString tag = r.left(3);
    const QString body = r.mid(4);
    if (tag == "BSB" || tag == "NOS") {
      h.name = value(body, "NA");
      const QStringList ra = value(body + ",", "RA").isEmpty()
                                 ? QStringList()
                                 : body.mid(body.indexOf("RA=") + 3).split(',');
      if (ra.size() >= 2) {
        h.width = ra[0].toInt();
        h.height = ra[1].toInt();
      }
    } else if (tag == "KNP") {
      h.scale = value(body, "SC").toInt();
      h.projection = value(body, "PR");
    } else if (tag == "RGB" || tag == "NGT") {
      const QStringList f = body.split(',');
      if (f.size() < 4) continue;
      const int idx = f[0].toInt();
      if (idx < 0 || idx > 255) continue;
      QVector<QRgb>& pal = tag == "RGB" ? h.palette : h.nightPalette;
      if (pal.size() <= idx) pal.resize(idx + 1, qRgb(0, 0, 0));
      pal[idx] = qRgb(f[1].toInt(), f[2].toInt(), f[3].toInt());
    } else if (tag == "REF") {
      const QStringList f = body.split(',');
      if (f.size() < 5) continue;
      h.refs.push_back(
          {f[1].toDouble(), f[2].toDouble(), f[3].toDouble(), f[4].toDouble()});
    } else if (tag == "PLY") {
      const QStringList f = body.split(',');
      if (f.size() < 3) continue;
      h.ply << QPointF(f[2].toDouble(), f[1].toDouble());
    }
  }
  if (h.width <= 0 || h.height <= 0) {
    if (error) *error = QStringLiteral("Missing raster size (RA=)");
    return false;
  }
  // Raster data: 0x1A, optional 0x00, then the bit depth byte.
  qsizetype p = end + 1;
  if (p < data.size() && data[p] == 0) ++p;
  if (rasterStart) *rasterStart = p;
  return true;
}

QImage BsbChart::decodeRaster(const QByteArray& data, qsizetype start,
                              const BsbHeader& h, QString* error) {
  const auto* d = reinterpret_cast<const uchar*>(data.constData());
  const qsizetype n = data.size();
  if (start >= n) return {};
  const int depth = d[start];
  if (depth < 1 || depth > 7) {
    if (error) *error = QStringLiteral("Invalid bit depth %1").arg(depth);
    return {};
  }
  const int shift = 7 - depth;
  const uchar valueMask = uchar(((1 << depth) - 1) << shift);
  const uchar countMask = uchar((1 << shift) - 1);

  QImage img(h.width, h.height, QImage::Format_Indexed8);
  img.fill(0);
  QVector<QRgb> table = h.palette;
  if (table.size() < 256) table.resize(256, qRgb(0, 0, 0));
  img.setColorTable(table);

  qsizetype p = start + 1;
  int rowsRead = 0;
  while (p < n && rowsRead < h.height) {
    // Row number (1 based), 7 bits per byte, high bit = continuation.
    int row = 0;
    while (p < n) {
      const uchar b = d[p++];
      row = (row << 7) | (b & 0x7f);
      if (!(b & 0x80)) break;
    }
    const int y = row - 1;
    uchar* line = (y >= 0 && y < h.height) ? img.scanLine(y) : nullptr;
    int x = 0;
    while (p < n) {
      uchar b = d[p++];
      if (b == 0) break;  // end of row
      const uchar pix = uchar((b & valueMask) >> shift);
      int run = b & countMask;
      while ((b & 0x80) && p < n) {
        b = d[p++];
        run = (run << 7) | (b & 0x7f);
      }
      ++run;
      if (line) {
        const int len = std::min(run, h.width - x);
        if (len > 0) std::memset(line + x, pix, size_t(len));
      }
      x += run;
    }
    ++rowsRead;
  }
  return img;
}

QByteArray BsbChart::encode(const BsbHeader& h, const QImage& img, int depth) {
  QByteArray out;
  out += "!Generated by OpenCPN Qt\r\nVER/3.0\r\n";
  out += QStringLiteral("BSB/NA=%1,NU=0,RA=%2,%3,DU=254\r\n")
             .arg(h.name)
             .arg(img.width())
             .arg(img.height())
             .toLatin1();
  out += QStringLiteral("KNP/SC=%1,GD=WGS84,PR=%2\r\n")
             .arg(h.scale)
             .arg(h.projection.isEmpty() ? "MERCATOR" : h.projection)
             .toLatin1();
  for (int i = 1; i < h.palette.size(); ++i)
    out += QStringLiteral("RGB/%1,%2,%3,%4\r\n")
               .arg(i)
               .arg(qRed(h.palette[i]))
               .arg(qGreen(h.palette[i]))
               .arg(qBlue(h.palette[i]))
               .toLatin1();
  for (int i = 0; i < h.refs.size(); ++i)
    out += QStringLiteral("REF/%1,%2,%3,%4,%5\r\n")
               .arg(i + 1)
               .arg(h.refs[i].x)
               .arg(h.refs[i].y)
               .arg(h.refs[i].lat, 0, 'f', 8)
               .arg(h.refs[i].lon, 0, 'f', 8)
               .toLatin1();
  for (int i = 0; i < h.ply.size(); ++i)
    out += QStringLiteral("PLY/%1,%2,%3\r\n")
               .arg(i + 1)
               .arg(h.ply[i].y(), 0, 'f', 8)
               .arg(h.ply[i].x(), 0, 'f', 8)
               .toLatin1();
  out += '\x1a';
  out += '\0';
  out += char(depth);
  const int shift = 7 - depth;
  const int maxShort = (1 << shift) - 1;
  for (int y = 0; y < img.height(); ++y) {
    // Row number varint
    int row = y + 1;
    QByteArray rn;
    rn.prepend(char(row & 0x7f));
    row >>= 7;
    while (row) {
      rn.prepend(char(0x80 | (row & 0x7f)));
      row >>= 7;
    }
    out += rn;
    const uchar* line = img.constScanLine(y);
    int x = 0;
    while (x < img.width()) {
      const uchar v = line[x];
      int run = 1;
      while (x + run < img.width() && line[x + run] == v) ++run;
      x += run;
      // The run length minus one is split into the low bits of the first
      // byte (most significant part) and 7 bit continuation bytes.
      int rest = run - 1;
      QVector<int> digits;
      while (rest > maxShort) {
        digits.prepend(rest & 0x7f);
        rest >>= 7;
      }
      const int firstBits = rest;
      QByteArray tail;
      for (int i = 0; i < digits.size(); ++i)
        tail += char((i + 1 < digits.size() ? 0x80 : 0) | digits[i]);
      uchar b = uchar((v << shift) | firstBits);
      if (!tail.isEmpty()) b |= 0x80;
      out += char(b);
      out += tail;
    }
    out += '\0';
  }
  return out;
}

BsbChart::BsbChart(const QString& path) {
  m_path = path;
  m_name = QFileInfo(path).completeBaseName();
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    m_error = f.errorString();
    return;
  }
  // The header is small; read just enough of the file.
  QByteArray head = f.read(256 * 1024);
  if (!parseHeader(head, m_header, nullptr, &m_error)) return;
  if (!m_header.name.isEmpty()) m_name = m_header.name;
  m_scale = m_header.scale;
  if (m_header.refs.size() < 3) {
    m_error = QStringLiteral("Chart has fewer than 3 REF points");
    return;
  }
  // Fit pixel -> mercator.
  QVector<QPointF> px;
  QVector<double> mx, my;
  for (const auto& r : m_header.refs) {
    px << QPointF(r.x, r.y);
    const QPointF m = geo::toMercator(r.lat, r.lon);
    mx << m.x();
    my << m.y();
  }
  double a, b, c, d, e, g;
  if (!fitAffine(px, mx, a, b, c) || !fitAffine(px, my, d, e, g)) {
    m_error = QStringLiteral("Degenerate REF points");
    return;
  }
  m_pixelToWorld = QTransform(a, d, b, e, c, g);
  if (m_header.ply.size() >= 3) {
    QPolygonF poly;
    for (const QPointF& ll : m_header.ply)
      poly << geo::toMercator(ll.y(), ll.x());
    poly << poly.first();
    m_coverage.addPolygon(poly);
  } else {
    m_coverage.addPolygon(m_pixelToWorld.map(
        QPolygonF(QRectF(0, 0, m_header.width, m_header.height))));
  }
  m_bounds = m_coverage.boundingRect();
  m_valid = true;
}

bool BsbChart::loadData() const {
  QFile f(m_path);
  if (!f.open(QIODevice::ReadOnly)) return false;
  const QByteArray data = f.readAll();
  BsbHeader h;
  qsizetype start = 0;
  if (!parseHeader(data, h, &start, nullptr)) return false;
  m_image = decodeRaster(data, start, h, nullptr);
  return !m_image.isNull();
}

void BsbChart::unload() const {
  std::lock_guard<std::mutex> lock(m_loadMutex);
  m_image = QImage();
  m_loaded = false;
}

QImage BsbChart::image() const {
  ensureLoaded();
  return m_image;
}

void BsbChart::render(QPainter& p, const ChartRenderContext& ctx) const {
  if (!m_valid || !ensureLoaded()) return;
  p.save();
  // Clip to the coverage polygon (in mercator, with the view transform).
  p.setTransform(ctx.worldToImage);
  p.setClipPath(m_coverage);
  p.setTransform(m_pixelToWorld * ctx.worldToImage);
  // Smooth when zoomed out, crisp pixels when overzoomed.
  const double pixelsPerChartPixel =
      std::hypot(p.transform().m11(), p.transform().m12());
  p.setRenderHint(QPainter::SmoothPixmapTransform, pixelsPerChartPixel < 1.5);
  p.drawImage(0, 0, m_image);
  if (ctx.style.dark) {
    // Dim paper charts for night use.
    p.setCompositionMode(QPainter::CompositionMode_Multiply);
    p.fillRect(QRectF(0, 0, m_image.width(), m_image.height()),
               QColor(70, 72, 92));
  }
  p.restore();
}
