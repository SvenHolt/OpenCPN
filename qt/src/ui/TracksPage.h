// Track recording: drawer page and chart layer.
#pragma once

#include "ui/DrawerPages.h"
#include "ui/MapLayer.h"

class QPushButton;

class TrackLayer : public MapLayer {
  Q_OBJECT
public:
  explicit TrackLayer(QObject* parent = nullptr);
  QString name() const override { return tr("Tracks"); }
  void paint(QPainter& p, const ChartView& view) override;
  int zOrder() const override { return -1; }
};

class TracksPage : public ListPage {
  Q_OBJECT
public:
  explicit TracksPage(ChartView* chart, QWidget* parent = nullptr);

private:
  void rebuild();
  void updateRecordButton();
  ChartView* m_chart;
  QPushButton* m_record;
  QLabel* m_stats;
};
