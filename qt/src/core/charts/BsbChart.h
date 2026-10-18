// BSB/KAP raster charts (NOAA RNC format, version 1-3).
#pragma once

#include <QImage>
#include <QPolygonF>
#include <QVector>

#include "core/charts/Chart.h"

struct BsbHeader {
  QString name;
  int width = 0, height = 0;
  int scale = 0;
  QString projection;
  QVector<QRgb> palette;       // RGB/ entries, index 0 unused
  QVector<QRgb> nightPalette;  // NGT/ entries if present
  struct Ref {
    double x, y, lat, lon;
  };
  QVector<Ref> refs;
  QPolygonF ply;  // lat (y) / lon (x) coverage polygon
};

class BsbChart : public Chart {
public:
  explicit BsbChart(const QString& path);
  Type type() const override { return Type::Raster; }
  void render(QPainter& p, const ChartRenderContext& ctx) const override;
  void unload() const override;

  const BsbHeader& header() const { return m_header; }
  /** Decoded raster (Indexed8), loads on demand. */
  QImage image() const;

  /** Parses only the text header. */
  static bool parseHeader(const QByteArray& data, BsbHeader& h,
                          qsizetype* rasterStart, QString* error);
  /** Decodes the run-length encoded raster. */
  static QImage decodeRaster(const QByteArray& data, qsizetype start,
                             const BsbHeader& h, QString* error);
  /** Encodes a BSB file (used by tests and tools). */
  static QByteArray encode(const BsbHeader& h, const QImage& indexed,
                           int depth = 7);

  /** Pixel <-> normalized mercator affine transforms (least squares fit of
   *  the REF/ points; exact for Mercator charts). */
  const QTransform& pixelToWorld() const { return m_pixelToWorld; }

protected:
  bool loadData() const override;

private:
  BsbHeader m_header;
  QTransform m_pixelToWorld;
  mutable QImage m_image;
};
