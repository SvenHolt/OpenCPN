// Passage planning for a planned route: departure and time window,
// propulsion, wind (GRIB or manual) and the results: duration, arrival,
// sail/motor share, tacks and gybes, fuel, daylight and a leg list.
#pragma once

#include <QJsonObject>
#include <QTimer>
#include <QWidget>

#include "core/Currents.h"
#include "core/PassagePlanner.h"
#include "core/SailPlan.h"
#include "core/WeatherModels.h"

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

/** One wind source (model, GRIB, manual) evaluated over the departures. */
struct SourceResult {
  QString id, name;
  QVector<DepartureOption> options;
  PassagePlan best;
  bool usable = true;  // false if the source does not cover the passage
};

/** Matrix: wind sources × departure times; cell color = sail share,
 *  text = duration. A click picks the source and the departure. */
class ModelMatrix : public QWidget {
  Q_OBJECT
public:
  explicit ModelMatrix(QWidget* parent = nullptr);
  void setResults(const QVector<SourceResult>& rows, const QString& selected,
                  const QDateTime& departure);
  QSize sizeHint() const override;

signals:
  void picked(const QString& source, const QDateTime& departure);

protected:
  void paintEvent(QPaintEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;

private:
  QRectF cell(int row, int col) const;
  int columns() const;
  QVector<SourceResult> m_rows;
  QString m_selected;
  QDateTime m_departure;
};

class PassagePanel : public QWidget {
  Q_OBJECT
public:
  explicit PassagePanel(QWidget* parent = nullptr);
  /** Route as x = lon, y = lat; an empty route hides the panel. */
  void setRoute(const QVector<QPointF>& route);
  const QVector<QPointF>& route() const { return m_route; }
  const PassagePlan& plan() const { return m_plan; }
  /** Boards, tacks, gybes and sail changes of the shown departure. */
  const SailingPlan& sailingPlan() const { return m_sail; }
  /** Land check for the boards (basemap); none: lane only. */
  void setWaterTest(WaterTest water) { m_water = std::move(water); }
  /** Half width of the lane around the rhumb line for tacking. */
  double laneNm() const;
  PassageSettings settings() const;

  /** Model forecasts (online or from a stored voyage). */
  const QHash<QString, ModelForecast>& forecasts() const { return m_forecasts; }
  void setForecasts(const QHash<QString, ModelForecast>& f, bool offline);
  /** Inputs as JSON, for stored voyages. */
  QJsonObject saveState() const;
  void restoreState(const QJsonObject& o);
  /** Fetches the model forecasts for the current route. */
  void fetchForecasts();
  QString source() const { return m_sourceId; }
  /** Fetches the marine forecast, the rivers and the gauges of the route. */
  void fetchCurrents();
  /** Tides and currents used for the plan. */
  const CurrentModel& currents() const { return m_currents; }
  void setCurrents(const CurrentModel& m);

  /** Wind from the loaded GRIB file, interpolated in time. */
  static bool gribWind(double lat, double lon, const QDateTime& t, double& kn,
                       double& fromDeg);
  static QString modeName(PassageLeg::Mode m);

signals:
  void planChanged();

private:
  void recalculate();
  WindSource windFor(const QString& id) const;
  QString sourceName(const QString& id) const;
  void showResults(const QVector<DepartureOption>& options);
  void showSailPlan();
  void saveSettings() const;
  void showCurrents();
  QString levelText(const QString& title, const QPointF& pos,
                    const QDateTime& t, const QVector<Gauge>& gauges) const;

  QVector<QPointF> m_route;
  PassagePlan m_plan;
  QDateTime m_chosen;  // departure picked in the timeline
  QTimer m_debounce;

  QDateTimeEdit* m_departure;
  QComboBox* m_window;
  QComboBox* m_optimize;
  QButtonGroup* m_strategy;
  QDoubleSpinBox* m_motorSpeed;
  QLabel* m_polarInfo;
  QHash<QString, ModelForecast> m_forecasts;
  bool m_forecastsOffline = false;
  QString m_sourceId = "manual";
  QLabel* m_modelInfo;
  ModelMatrix* m_matrix;
  QSpinBox* m_windDir;
  QDoubleSpinBox* m_windKn;
  QLabel* m_windInfo;

  QWidget* m_results;
  QGridLayout* m_tiles;
  QLabel* m_bestInfo;
  DepartureTimeline* m_timeline;
  QLabel* m_warnings;
  QVBoxLayout* m_legs;
  SailingPlan m_sail;
  WaterTest m_water;
  QDoubleSpinBox* m_lane;
  QLabel* m_sailSummary;
  QVBoxLayout* m_sailList;
  class QPushButton* m_applySails;

  // Tides and currents
  CurrentModel m_currents;
  QVector<Gauge> m_gaugesFrom, m_gaugesTo;
  QComboBox* m_currentMode;
  QWidget* m_manualCurrent;
  QSpinBox* m_currentSet;
  QDoubleSpinBox* m_currentDrift;
  QLabel* m_currentInfo;
  QVBoxLayout* m_riverList;
  QDoubleSpinBox* m_minDepLevel;
  QDoubleSpinBox* m_minArrLevel;
  QLabel* m_levelInfo;
};
