// Common interface of all chart types (S-57 vector, BSB raster). Charts are
// immutable after loading and rendered from a background thread.
#pragma once

#include <QColor>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QTransform>
#include <memory>
#include <mutex>

/** Colors used for chart rendering, derived from the UI theme. */
struct ChartStyle {
  bool dark = true;
  QColor land, landEdge, coast;
  QColor depthDeep, depthMedium, depthShallow, depthVeryShallow,
      depthIntertidal;
  QColor contour, safetyContour;
  QColor soundingShallow, soundingDeep;
  QColor text, textHalo;
  QColor magenta, red, green, yellow, white, black, grey;
  QColor built, structure;
  double shallowDepth = 2, safetyDepth = 5, deepDepth = 20;  // meters
};

struct ChartRenderContext {
  QTransform worldToImage;  // normalized mercator -> image pixels
  QRectF visibleWorld;      // visible mercator rect (same wrap as transform)
  double scale = 1;         // pixels per mercator unit
  double displayScale = 1;  // 1:x scale of the current view
  double rotation = 0;      // view rotation, degrees (course up)
  ChartStyle style;
};

class Chart {
public:
  enum class Type { Vector, Raster };

  virtual ~Chart() = default;

  virtual Type type() const = 0;
  const QString& path() const { return m_path; }
  const QString& name() const { return m_name; }
  /** Compilation scale (1:x). */
  int nativeScale() const { return m_scale; }
  /** Coverage bounds in normalized mercator coordinates. */
  const QRectF& bounds() const { return m_bounds; }
  /** Coverage outline in normalized mercator coordinates. */
  const QPainterPath& coverage() const { return m_coverage; }
  bool isValid() const { return m_valid; }
  QString error() const { return m_error; }

  /** Loads the full chart data if not done yet; thread-safe. */
  bool ensureLoaded() const;
  /** Releases the bulk data (reloaded on demand). */
  virtual void unload() const {}
  bool isLoaded() const { return m_loaded; }

  /** Renders the chart. Called from a worker thread, must be const-safe. */
  virtual void render(QPainter& p, const ChartRenderContext& ctx) const = 0;

  /** Display scale (1:x) of a view with the given meters per pixel. */
  static double displayScaleFor(double metersPerPixel) {
    return metersPerPixel / 0.00028;  // 0.28 mm standard pixel (S-52)
  }

protected:
  virtual bool loadData() const = 0;

  QString m_path, m_name;
  int m_scale = 0;
  QRectF m_bounds;
  QPainterPath m_coverage;
  bool m_valid = false;
  QString m_error;
  mutable bool m_loaded = false;
  mutable std::mutex m_loadMutex;
};

using ChartPtr = std::shared_ptr<const Chart>;
