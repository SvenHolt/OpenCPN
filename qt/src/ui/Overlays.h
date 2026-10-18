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
  enum Section { Routes, Marks, Ais, Connections, Settings, SectionCount };
  explicit BottomDock(QWidget* parent = nullptr);
  void setActiveSection(int s);
  static constexpr int kHeight = 76;

signals:
  void sectionRequested(int section);
  void mobRequested();

protected:
  void paintEvent(QPaintEvent*) override;

private:
  void updateStatus();
  DockButton* m_buttons[SectionCount];
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
