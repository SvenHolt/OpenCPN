// Full screen vector chart canvas: basemap, graticule, routes, waypoints,
// AIS targets and own ship. Supports kinetic panning, animated zoom,
// pinch gestures, north-up / course-up and interactive route creation.
#pragma once

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QImage>
#include <QPointF>
#include <QTimer>
#include <QVariantAnimation>
#include <QVector>
#include <QWidget>
#include <memory>

#include "core/NavStore.h"
#include "core/Shapefile.h"
#include "core/charts/ChartDatabase.h"
#include "core/osm/Overpass.h"

class QGestureEvent;
class MapLayer;

/** Immutable basemap polygons at two levels of detail. */
struct LandData {
  QVector<MercatorPolygon> crude;
  QVector<MercatorPolygon> detailed;
  bool isEmpty() const { return crude.isEmpty() && detailed.isEmpty(); }
};

class ChartView : public QWidget {
  Q_OBJECT
public:
  enum class Mode { Browse, RouteCreate, AreaSelect };

  explicit ChartView(QWidget* parent = nullptr);
  ~ChartView() override;

  void setLandData(std::shared_ptr<const LandData> land);

  // View state ------------------------------------------------------------
  void centerOn(double lat, double lon, bool animated = true);
  void center(double& lat, double& lon) const;
  /** Pixels per normalized mercator unit. */
  double scale() const { return m_scale; }
  void setScale(double s);
  void zoomBy(double factor, bool animated = true);
  /** Animates the view so the geographic box fits the free screen area. */
  void fitGeoBounds(double lat1, double lon1, double lat2, double lon2);
  /** Approximate nautical miles per screen pixel at the view center. */
  double nmPerPixel() const;
  double rotation() const { return m_rotation; }

  bool follow() const { return m_follow; }
  void setFollow(bool on);
  void setCourseUp(bool on);
  bool courseUp() const { return m_courseUp; }

  /** Screen area covered by overlays; used to place the scale bar and the
   *  own ship in follow mode. */
  void setInsets(int left, int bottom) {
    m_insetLeft = left;
    m_insetBottom = bottom;
    update();
  }

  // Route creation --------------------------------------------------------
  Mode mode() const { return m_mode; }
  void startRouteCreate();
  void undoRoutePoint();
  /** Ends route creation; returns the drawn points. */
  QVector<Waypoint> finishRouteCreate();
  int pendingRoutePoints() const { return int(m_pending.size()); }

  // Area selection (offline regions) --------------------------------------
  /** Shows a resizable rectangle; an invalid box starts with the middle of
   *  the visible area. */
  void startAreaSelect(const GeoBox& initial = {});
  GeoBox selectedArea() const { return m_area; }
  void setSelectedArea(const GeoBox& box);
  /** Box of the visible chart area (without overlays). */
  GeoBox visibleArea() const;
  void finishAreaSelect();

  void setSelectedAis(quint32 mmsi) {
    m_selectedAis = mmsi;
    update();
  }

  QPointF screenPos(double lat, double lon) const;
  void geoPos(const QPointF& screen, double& lat, double& lon) const;
  /** Normalized mercator -> screen transform (wrap: world copy offset). */
  QTransform worldTransform(double wrap = 0) const {
    return worldToScreen(wrap);
  }
  /** Current display scale (1:x). */
  double displayScale() const;

  /** Overlay layers (weather, tides, tracks, plugins). Not owned. */
  void addLayer(MapLayer* layer);
  void removeLayer(MapLayer* layer);

signals:
  void viewChanged();
  /** The user asked to look at a place (center/fit), e.g. from a list. */
  void navigationRequested();
  void followChanged(bool on);
  void routeEditChanged(int points);
  void areaChanged(const GeoBox& box);
  void aisTargetClicked(quint32 mmsi);
  void waypointClicked(const QString& id);
  void contextRequested(double lat, double lon, const QPoint& globalPos);

protected:
  void paintEvent(QPaintEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void mouseDoubleClickEvent(QMouseEvent*) override;
  void wheelEvent(QWheelEvent*) override;
  void contextMenuEvent(QContextMenuEvent*) override;
  bool event(QEvent*) override;

private:
  struct ViewParams {
    QPointF center;  // mercator
    double scale = 1;
    double rotation = 0;
    QSize size;
    bool operator==(const ViewParams& o) const {
      return center == o.center && scale == o.scale && rotation == o.rotation &&
             size == o.size;
    }
  };
  ViewParams params() const;
  static QTransform worldToScreen(const ViewParams& v, double wrap = 0);
  QTransform worldToScreen(double wrap = 0) const {
    return worldToScreen(params(), wrap);
  }
  QPointF screenPosWorld(QPointF world) const;

  void viewModified();
  void scheduleLandRender();
  static QImage renderLand(std::shared_ptr<const LandData> land,
                           std::shared_ptr<const ChartList> charts,
                           ViewParams v, QColor color, ChartStyle style);
  void animateTo(QPointF center, double scale);
  void followShip();
  void handleGesture(QGestureEvent* e);
  void zoomAt(const QPointF& screen, double factor);

  void drawGraticule(QPainter& p);
  void drawRoutes(QPainter& p);
  void drawWaypoints(QPainter& p);
  void drawPendingRoute(QPainter& p);
  void drawAis(QPainter& p);
  void drawOwnShip(QPainter& p);
  void drawScaleBar(QPainter& p);
  void drawChartOutlines(QPainter& p);
  void drawLayers(QPainter& p, bool above);
  void drawAreaSelection(QPainter& p);
  /** Handle under a screen position: bit mask of kN/kS/kW/kE, kMove or 0. */
  int areaHitTest(const QPointF& pos) const;
  void updateAreaCursor(const QPointF& pos);

  std::shared_ptr<const LandData> m_land;
  std::shared_ptr<const ChartList> m_charts;
  QVector<MapLayer*> m_layers;
  QPointF m_center{0.5, 0.33};
  double m_scale = 4096;
  double m_rotation = 0;
  bool m_follow = true;
  bool m_courseUp = false;
  int m_insetLeft = 0, m_insetBottom = 0;

  // Asynchronous land rendering
  QImage m_landImage;
  ViewParams m_landParams;
  QFutureWatcher<QImage> m_landWatcher;
  ViewParams m_pendingParams;
  bool m_renderQueued = false;
  QTimer m_renderTimer;

  // Interaction
  QPointF m_pressPos, m_lastPos;
  bool m_dragging = false;
  QElapsedTimer m_moveClock;
  QPointF m_velocity;  // px/ms
  QVariantAnimation m_kinetic;
  QVariantAnimation m_viewAnim;
  QPointF m_animFromCenter, m_animToCenter;
  double m_animFromScale = 1, m_animToScale = 1;
  QPointF m_mousePos{-1, -1};

  // Smoothly interpolated own ship position
  QPointF m_shipFrom, m_shipTo, m_shipShown;
  QVariantAnimation m_shipAnim;
  QTimer m_pulseTimer;
  qreal m_pulse = 0;

  Mode m_mode = Mode::Browse;
  QVector<Waypoint> m_pending;
  quint32 m_selectedAis = 0;

  GeoBox m_area;
  GeoBox m_areaPressBox;
  int m_areaHit = 0;
  double m_areaPressLat = 0, m_areaPressLon = 0;
};
