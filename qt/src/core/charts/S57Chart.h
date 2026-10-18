// S-57 electronic navigational charts (ENC, *.000 + updates). Reading uses
// OpenCPN's ISO 8211 / S-57 reader; rendering is a simplified, theme aware
// interpretation of the S-52 presentation library.
#pragma once

#include <QHash>
#include <QPointF>
#include <QVector>

#include "core/charts/Chart.h"

struct S57Feature {
  enum class Geom { Point, Line, Area, Soundings };
  QByteArray cls;  // object class acronym, e.g. "DEPARE"
  Geom geom = Geom::Point;
  QPainterPath path;           // lines and areas, mercator
  QPointF point;               // points, mercator
  QVector<QPointF> soundPts;   // soundings, mercator
  QVector<float> soundDepths;  // meters
  QHash<QByteArray, QString> attrs;

  double num(const char* key, double fallback = qQNaN()) const;
  QVector<int> list(const char* key) const;
};

class S57Chart : public Chart {
public:
  /** Path of the S-57 class registry (s57objectclasses.csv etc.). */
  static void setRegistryDir(const QString& dir);
  static QString registryDir();

  explicit S57Chart(const QString& path);
  Type type() const override { return Type::Vector; }
  void render(QPainter& p, const ChartRenderContext& ctx) const override;
  void unload() const override;

  const QVector<S57Feature>& features() const;
  /** Features at a mercator position (pick report). */
  QVector<const S57Feature*> featuresAt(const QPointF& world,
                                        double toleranceWorld) const;

protected:
  bool loadData() const override;

private:
  bool read(bool metadataOnly) const;
  void drawSymbol(QPainter& p, const S57Feature& f, const QPointF& pos,
                  const ChartRenderContext& ctx) const;

  mutable QVector<S57Feature> m_features;
};
