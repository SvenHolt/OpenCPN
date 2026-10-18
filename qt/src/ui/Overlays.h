// Floating overlay widgets on top of the chart.
#pragma once

#include <QLabel>
#include <QTimer>
#include <QWidget>

#include "ui/GlassPanel.h"

class ChartView;
class QPushButton;

/** Large speed readout with course, heading and position (top left). */
class InstrumentCard : public GlassPanel {
  Q_OBJECT
public:
  explicit InstrumentCard(QWidget* parent = nullptr);
  QSize sizeHint() const override {
    return {300 + 2 * kShadow, 214 + 2 * kShadow};
  }

protected:
  void paintEvent(QPaintEvent*) override;
};

/** Turn-by-turn style card for the active route (top center). */
class GuidanceCard : public GlassPanel {
  Q_OBJECT
public:
  explicit GuidanceCard(QWidget* parent = nullptr);
  QSize sizeHint() const override {
    return {470 + 2 * kShadow, 124 + 2 * kShadow};
  }

protected:
  void paintEvent(QPaintEvent*) override;
  void resizeEvent(QResizeEvent*) override;

private:
  IconButton* m_stop;
};

/** Zoom, orientation and follow buttons (right edge). */
class MapControls : public QWidget {
  Q_OBJECT
public:
  MapControls(ChartView* chart, QWidget* parent = nullptr);

private:
  ChartView* m_chart;
  RoundButton* m_compass;
  RoundButton* m_follow;
};

/** Bottom dock with the main app sections. */
class BottomDock : public QWidget {
  Q_OBJECT
public:
  explicit BottomDock(QWidget* parent = nullptr);
  /** Adds a main menu button that opens drawer page `page`. */
  void addSection(const QString& icon, const QString& label, int page);
  /** 0 = cockpit, 1 = chart, 2 = boat systems */
  void setActiveView(int view);
  /** Highlights the button of drawer page `page` (-1: none). */
  void setActiveSection(int page);
  static constexpr int kHeight = 76;

signals:
  void sectionRequested(int page);
  void viewRequested(int view);
  void mobRequested();

protected:
  void paintEvent(QPaintEvent*) override;

private:
  void updateStatus();
  void resizeEvent(QResizeEvent*) override;
  /** Hides the sections that do not fit behind a "More" button. */
  void fitSections();
  QVector<DockButton*> m_buttons;
  QStringList m_icons, m_labels;
  QVector<int> m_pages;
  DockButton* m_more;
  int m_active = -1;
  DockButton* m_views[3];
  class QHBoxLayout* m_sectionLayout;
  DockButton* m_theme;
  QLabel* m_clock;
  QLabel* m_status;
  QTimer m_clockTimer;
};

/** Transient alarm/info banner. */
class Toast : public GlassPanel {
  Q_OBJECT
public:
  explicit Toast(QWidget* parent = nullptr);
  void showMessage(const QString& text, bool danger = true);

protected:
  void paintEvent(QPaintEvent*) override;

private:
  QString m_text;
  bool m_danger = true;
  QTimer m_hide;
};

/** Toolbar shown while a route is being drawn on the chart. */
class RouteEditBar : public GlassPanel {
  Q_OBJECT
public:
  explicit RouteEditBar(QWidget* parent = nullptr);
  void setPointCount(int n);

signals:
  void undo();
  void cancel();
  void done();

private:
  QLabel* m_label;
  QPushButton* m_done;
};

/** Details of the selected AIS target. */
class TargetCard : public GlassPanel {
  Q_OBJECT
public:
  explicit TargetCard(QWidget* parent = nullptr);
  void setTarget(quint32 mmsi);
  quint32 target() const { return m_mmsi; }
  QSize sizeHint() const override {
    return {320 + 2 * kShadow, 236 + 2 * kShadow};
  }

signals:
  void closed();

protected:
  void paintEvent(QPaintEvent*) override;
  void resizeEvent(QResizeEvent*) override;

private:
  quint32 m_mmsi = 0;
  IconButton* m_close;
};

QString aisShipTypeName(int type);
