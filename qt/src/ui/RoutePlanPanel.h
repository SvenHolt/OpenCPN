// Harbour to harbour route planning: start and destination search, route
// calculation over water and a preview on the chart.
#pragma once

#include <QFutureWatcher>
#include <QTimer>
#include <QWidget>
#include <functional>

#include "core/RoutePlanner.h"
#include "ui/MapLayer.h"

class ChartView;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;
class PassagePanel;

/** A start or destination: harbour, marina, mark or position. */
struct Place {
  QString name, kind;
  double lat = 0, lon = 0;
  bool valid = false;
  QString detail;  // e.g. the region of an online result
};

/** Dashed preview of the calculated route. */
class PlannedRouteLayer : public MapLayer {
  Q_OBJECT
public:
  using MapLayer::MapLayer;
  QString name() const override { return tr("Route preview"); }
  void paint(QPainter& p, const ChartView& view) override;
  int zOrder() const override { return -1; }
  void setPoints(const QVector<QPointF>& lonLat) {
    m_points = lonLat;
    m_modes.clear();
    emit changed();
  }
  /** PassageLeg::Mode per leg, for coloring. */
  void setLegModes(const QVector<int>& modes) {
    m_modes = modes;
    emit changed();
  }

private:
  QVector<QPointF> m_points;
  QVector<int> m_modes;
};

/** Search field with suggestions for a Place. */
class PlaceField : public QWidget {
  Q_OBJECT
public:
  PlaceField(const QString& icon, const QString& placeholder,
             QWidget* parent = nullptr);
  Place place() const { return m_place; }
  void setPlace(const Place& p);
  /** Where to look first (x = lon, y = lat), e.g. the chart center. */
  void setNearProvider(std::function<QPointF()> f) { m_near = std::move(f); }

signals:
  void placeChanged();

protected:
  bool eventFilter(QObject* o, QEvent* e) override;

private:
  void search();
  void showSuggestions(const QVector<Place>& places);

  QLineEdit* m_edit;
  QVBoxLayout* m_list;
  QWidget* m_listBox;
  QTimer m_debounce;
  Place m_place;
  int m_searchId = 0;
  QVector<Place> m_results;
  std::function<QPointF()> m_near;
};

class RoutePlanPanel : public QWidget {
  Q_OBJECT
public:
  explicit RoutePlanPanel(ChartView* chart, QWidget* parent = nullptr);
  void setFrom(const Place& p);
  void setTo(const Place& p);

private:
  void calculate();
  void finished();
  void save(bool navigate);
  void clearResult();

  ChartView* m_chart;
  PlannedRouteLayer* m_layer;
  PlaceField* m_from;
  PlaceField* m_to;
  QPushButton* m_calc;
  QLabel* m_result;
  QWidget* m_actions;
  PassagePanel* m_passage;
  QFutureWatcher<RoutePlanResult> m_watcher;
  RoutePlanResult m_plan;
  bool m_osmPending = false;
};
