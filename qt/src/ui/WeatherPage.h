// GRIB weather: chart layer (wind colors, arrows, isobars), a time scrubber
// overlay and the drawer page (open/download, layers, readout).
#pragma once

#include <QTimer>

#include "core/GribDownload.h"
#include "ui/GlassPanel.h"
#include "ui/MapLayer.h"

class ChartView;
class QLabel;
class QProgressBar;
class QPushButton;
class ToggleSwitch;

/** Wind speed color scale (knots). */
QColor windColor(double knots, int alpha = 255);

class GribLayer : public MapLayer {
  Q_OBJECT
public:
  explicit GribLayer(QObject* parent = nullptr);
  QString name() const override { return tr("Weather"); }
  void paint(QPainter& p, const ChartView& view) override;
  int zOrder() const override { return -20; }
};

/** Media-player style time scrubber for the forecast steps. */
class WeatherTimeBar : public GlassPanel {
  Q_OBJECT
public:
  explicit WeatherTimeBar(QWidget* parent = nullptr);
  QSize sizeHint() const override {
    return {440 + 2 * kShadow, 64 + 2 * kShadow};
  }

protected:
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void resizeEvent(QResizeEvent*) override;

private:
  QRectF trackRect() const;
  void seek(const QPointF& pos);
  RoundButton* m_play;
  QTimer m_timer;
};

class WeatherPage : public QWidget {
  Q_OBJECT
public:
  explicit WeatherPage(ChartView* chart, QWidget* parent = nullptr);

private:
  void refresh();
  void download();
  ChartView* m_chart;
  GribDownload m_download;
  QPushButton* m_downloadButton;
  QProgressBar* m_progress;
  QLabel* m_status;
  QLabel* m_info;
  QLabel* m_readout;
  QWidget* m_layers;
  QPushButton* m_close;
};
