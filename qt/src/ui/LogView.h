// Log view: past legs (overview and details with the track on a map and
// charts of speed, heel, wind, energy, diesel, temperature and depth) and
// the logbook with a report for every day.
#pragma once

#include <QDate>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <memory>

#include "core/Legs.h"
#include "core/Logbook.h"

struct LandData;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
class QGridLayout;

/** The track of a leg on the basemap, colored by speed, with wind arrows
 *  and a marker for the time under the mouse in the charts. */
class TrackMap : public QWidget {
  Q_OBJECT
public:
  explicit TrackMap(QWidget* parent = nullptr);
  void setLand(std::shared_ptr<const LandData> land) { m_land = land; }
  void setLeg(const Leg* leg);
  void setMarker(qint64 t);
  QSize sizeHint() const override { return {600, 360}; }
  /** Screen position of a sample (tests). */
  QPointF screenPos(int index) const;

signals:
  void timePicked(qint64 t);

protected:
  void paintEvent(QPaintEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void resizeEvent(QResizeEvent*) override;

private:
  void fit();
  QPointF toScreen(const QPointF& merc) const;
  std::shared_ptr<const LandData> m_land;
  QVector<QPointF> m_merc;  // track in normalized Mercator
  QVector<LegSample> m_samples;
  QString m_from, m_to;
  QRectF m_view;  // Mercator rectangle shown
  double m_scale = 1;
  QPointF m_offset;
  qint64 m_marker = 0;
};

/** One chart over the time of a leg: one or two series, motoring periods
 *  shaded, a crosshair that follows the other charts. */
class TimeChart : public QWidget {
  Q_OBJECT
public:
  struct Series {
    QString name;
    QColor color;
    std::function<double(const LegSample&)> value;
    bool fill = false;
  };
  TimeChart(const QString& title, const QString& unit, QVector<Series> series,
            QWidget* parent = nullptr);
  void setLeg(const Leg* leg);
  void setMarker(qint64 t);
  /** Symmetric range around zero (heel). */
  void setSymmetric(bool on) { m_symmetric = on; }
  void setRange(double min, double max) {
    m_fixedMin = min;
    m_fixedMax = max;
  }
  QSize sizeHint() const override { return {600, 150}; }
  QString title() const { return m_title; }
  /** Values (NaN skipped) of series `i` (tests). */
  int pointCount(int i) const;

signals:
  void hovered(qint64 t);

protected:
  void paintEvent(QPaintEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void leaveEvent(QEvent*) override;

private:
  QRectF plotRect() const;
  QString m_title, m_unit;
  QVector<Series> m_series;
  QVector<QVector<QPointF>> m_points;  // t, value
  QVector<QPair<qint64, qint64>> m_motoring;
  qint64 m_t0 = 0, m_t1 = 1;
  double m_min = 0, m_max = 1;
  double m_fixedMin = 0, m_fixedMax = 0;
  bool m_symmetric = false;
  qint64 m_marker = 0;
};

class LegDetail : public QWidget {
  Q_OBJECT
public:
  explicit LegDetail(QWidget* parent = nullptr);
  void setLand(std::shared_ptr<const LandData> land);
  void showLeg(const QString& id);
  QString legId() const { return m_id; }
  TrackMap* map() const { return m_map; }
  const QVector<TimeChart*>& charts() const { return m_charts; }

signals:
  void logbookRequested(const QDate& date);

private:
  void setMarker(qint64 t);
  QString m_id;
  QLabel* m_title;
  QLabel* m_when;
  QGridLayout* m_tiles;
  TrackMap* m_map;
  QLabel* m_hover;
  QVector<TimeChart*> m_charts;
  QPushButton* m_logbook;
};

class LogbookEditor : public QWidget {
  Q_OBJECT
public:
  explicit LogbookEditor(QWidget* parent = nullptr);
  void showDay(const QDate& date);
  QDate date() const { return m_date; }
  /** Writes pending edits now. */
  void flush();
  QLineEdit* crew() const { return m_crew; }
  QLineEdit* weather() const { return m_weather; }
  QPlainTextEdit* report() const { return m_report; }
  /** Fills the weather from the recorded data of the day. */
  void insertWeather();

signals:
  void legRequested(const QString& id);

private:
  QDate m_date;
  QLabel* m_title;
  QLabel* m_facts;
  QVBoxLayout* m_legs;
  QLineEdit* m_crew;
  QLineEdit* m_weather;
  QPlainTextEdit* m_report;
  QLabel* m_saved;
  QTimer m_save;
  bool m_loading = false;
};

class LogView : public QWidget {
  Q_OBJECT
public:
  explicit LogView(QWidget* parent = nullptr);
  void setLand(std::shared_ptr<const LandData> land);
  void setLeftInset(int px);
  void showLegs();
  void showLogbook(const QDate& date = {});
  void openLeg(const QString& id);
  LegDetail* detail() const { return m_detail; }
  LogbookEditor* editor() const { return m_editor; }
  bool logbookShown() const;

protected:
  void paintEvent(QPaintEvent*) override;

private:
  void rebuildLegs();
  void rebuildDays();
  QPushButton* m_tabs[2];
  QStackedWidget* m_stack;
  QVBoxLayout* m_legList;
  QLabel* m_totals;
  LegDetail* m_detail;
  QVBoxLayout* m_dayList;
  LogbookEditor* m_editor;
};
