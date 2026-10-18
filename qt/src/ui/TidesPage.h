// Tides: station markers on the chart and a drawer page with a tide curve.
#pragma once

#include <QTimer>

#include "core/Tides.h"
#include "ui/DrawerPages.h"
#include "ui/MapLayer.h"

class TideLayer : public MapLayer {
  Q_OBJECT
public:
  explicit TideLayer(QObject* parent = nullptr);
  QString name() const override { return tr("Tides"); }
  void paint(QPainter& p, const ChartView& view) override;
  int zOrder() const override { return -5; }

private:
  QTimer m_refresh;
};

/** 24 hour tide curve of one station. */
class TideCurve : public QWidget {
  Q_OBJECT
public:
  explicit TideCurve(QWidget* parent = nullptr);
  void setStation(const TideStation* s);
  QSize sizeHint() const override { return {380, 230}; }

protected:
  void paintEvent(QPaintEvent*) override;

private:
  const TideStation* m_station = nullptr;
};

class TidesPage : public ListPage {
  Q_OBJECT
public:
  explicit TidesPage(ChartView* chart, QWidget* parent = nullptr);

private:
  void rebuild();
  ChartView* m_chart;
  TideCurve* m_curve;
  QString m_selected;
  QTimer m_refresh;
};
