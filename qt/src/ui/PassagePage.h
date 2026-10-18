// "Passage" section of the side drawer: plan from harbour to harbour with
// the weather, the user's harbour and anchorage list (photos, nearby
// places, forecast) and the logbook. Plus the map layer showing harbours
// and the planned passage.
#pragma once

#include <QDateTime>
#include <QWidget>

#include "ui/MapLayer.h"

class ChartView;
class QButtonGroup;
class QComboBox;
class QDateTimeEdit;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
class ToggleSwitch;
namespace seachart {
struct Harbour;
}

/** Large light figure with a small caption, as on the instrument cards. */
class Figure : public QWidget {
  Q_OBJECT
public:
  explicit Figure(const QString& caption, QWidget* parent = nullptr);
  void setText(const QString& value);
  QSize sizeHint() const override { return {110, 58}; }

protected:
  void paintEvent(QPaintEvent*) override;

private:
  QString m_caption, m_value;
};

/** Harbours, anchorages and the planned passage on the chart. */
class PassageLayer : public MapLayer {
  Q_OBJECT
public:
  explicit PassageLayer(QObject* parent = nullptr);
  QString name() const override { return tr("Harbours and passage"); }
  void paint(QPainter& p, const ChartView& view) override;
  int zOrder() const override { return 1; }
};

/** Planning view: departure, destination, time, boat, result and legs. */
class PlanView : public QWidget {
  Q_OBJECT
public:
  explicit PlanView(ChartView* chart, QWidget* parent = nullptr);
  void setFrom(qint64 id);
  void setTo(qint64 id);

private:
  void fillHarbours();
  void calculate();
  void compare();
  void showPlan();
  void updateButtons();
  void storeBoat();
  qint64 selected(QComboBox* c) const;

  ChartView* m_chart;
  QComboBox *m_from, *m_to;
  QDateTimeEdit* m_departure;
  QWidget* m_boatBox;
  QDoubleSpinBox *m_hull, *m_designWind, *m_noGo, *m_draft, *m_motorSpeed,
      *m_motorBelow, *m_maxWind, *m_maxGust;
  ToggleSwitch* m_motor;
  QWidget* m_result;
  Figure *m_distance, *m_duration, *m_eta;
  QLabel *m_details, *m_weather;
  QVBoxLayout* m_legs;
  QPushButton *m_show, *m_route, *m_start;
};

/** List of the user's harbours and anchorages with an editor. */
class HarbourView : public QWidget {
  Q_OBJECT
public:
  explicit HarbourView(ChartView* chart, QWidget* parent = nullptr);
  /** Opens the editor for a new entry at the position. */
  void newAt(double lat, double lon);
  void edit(qint64 id);

signals:
  void planFrom(qint64 id);
  void planTo(qint64 id);

private:
  void rebuildList();
  void showDetail(const seachart::Harbour& h);
  void save();
  void fillPhotos();

  ChartView* m_chart;
  QStackedWidget* m_stack;
  QLineEdit* m_search;
  QVBoxLayout* m_rows;
  QLabel* m_empty;
  // editor
  qint64 m_id = 0;
  QLabel* m_title;
  QComboBox* m_kind;
  QLineEdit *m_name, *m_lat, *m_lon, *m_depth, *m_berths, *m_vhf, *m_phone,
      *m_website, *m_facilities;
  QButtonGroup* m_rating;
  QPlainTextEdit* m_notes;
  QVBoxLayout* m_photos;
  QVBoxLayout* m_nearby;
  QVBoxLayout* m_forecast;
  QPushButton* m_delete;
};

/** Passages with their entries, notes and export. */
class LogbookView : public QWidget {
  Q_OBJECT
public:
  explicit LogbookView(ChartView* chart, QWidget* parent = nullptr);

private:
  void rebuild();
  void showTrip(qint64 id);
  void exportAll();

  ChartView* m_chart;
  QStackedWidget* m_stack;
  QVBoxLayout* m_rows;
  QLabel* m_empty;
  qint64 m_trip = 0;
  QLabel* m_title;
  Figure *m_distance, *m_duration, *m_maxSog;
  QLabel* m_summary;
  QVBoxLayout* m_entries;
  QPlainTextEdit* m_notes;
};

/** The drawer page with the three views behind a segmented control. */
class PassagePage : public QWidget {
  Q_OBJECT
public:
  explicit PassagePage(ChartView* chart, QWidget* parent = nullptr);
  enum View { Plan, Harbours, Logbook };
  void showView(View v);
  HarbourView* harbours() const { return m_harbours; }
  PlanView* planner() const { return m_plan; }

private:
  QButtonGroup* m_tabs;
  QStackedWidget* m_stack;
  PlanView* m_plan;
  HarbourView* m_harbours;
  LogbookView* m_logbook;
};
