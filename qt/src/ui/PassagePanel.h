// Passage planning for a planned route: departure and time window,
// propulsion, wind (GRIB or manual) and the results: duration, arrival,
// sail/motor share, tacks and gybes, fuel, daylight and a leg list.
#pragma once

#include <QTimer>
#include <QWidget>

#include "core/PassagePlanner.h"

class QButtonGroup;
class QComboBox;
class QDateTimeEdit;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QSpinBox;
class QVBoxLayout;

/** Bars for the evaluated departures of a time window (height = duration,
 *  color = sail share); a click picks a departure. */
class DepartureTimeline : public QWidget {
  Q_OBJECT
public:
  explicit DepartureTimeline(QWidget* parent = nullptr);
  void setOptions(const QVector<DepartureOption>& options,
                  const QDateTime& selected);
  QSize sizeHint() const override { return {360, 86}; }

signals:
  void departureChosen(const QDateTime& departure);

protected:
  void paintEvent(QPaintEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;

private:
  QRectF barRect(int i) const;
  QVector<DepartureOption> m_options;
  QDateTime m_selected;
};

class PassagePanel : public QWidget {
  Q_OBJECT
public:
  explicit PassagePanel(QWidget* parent = nullptr);
  /** Route as x = lon, y = lat; an empty route hides the panel. */
  void setRoute(const QVector<QPointF>& route);
  const PassagePlan& plan() const { return m_plan; }
  PassageSettings settings() const;

  /** Wind from the loaded GRIB file, interpolated in time. */
  static bool gribWind(double lat, double lon, const QDateTime& t, double& kn,
                       double& fromDeg);
  static QString modeName(PassageLeg::Mode m);

signals:
  void planChanged();

private:
  void recalculate();
  void showResults(const QVector<DepartureOption>& options);
  void saveSettings() const;

  QVector<QPointF> m_route;
  PassagePlan m_plan;
  QDateTime m_chosen;  // departure picked in the timeline
  QTimer m_debounce;

  QDateTimeEdit* m_departure;
  QComboBox* m_window;
  QComboBox* m_optimize;
  QButtonGroup* m_strategy;
  QDoubleSpinBox* m_motorSpeed;
  QDoubleSpinBox* m_hullSpeed;
  QSpinBox* m_windDir;
  QDoubleSpinBox* m_windKn;
  QLabel* m_windInfo;

  QWidget* m_results;
  QGridLayout* m_tiles;
  QLabel* m_bestInfo;
  DepartureTimeline* m_timeline;
  QLabel* m_warnings;
  QVBoxLayout* m_legs;
};
