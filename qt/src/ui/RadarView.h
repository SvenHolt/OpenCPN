// Radar view: the plan position indicator of a Signal K radar (or the
// simulator) with range rings, heading line, AIS targets and an electronic
// bearing line / variable range marker; and the radar overlay on the chart.
#pragma once

#include <QImage>
#include <QWidget>

#include "core/Radar.h"
#include "ui/MapLayer.h"

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class ToggleSwitch;

/** Display settings shared by the view and the chart overlay. */
struct RadarDisplay {
  double rangeNm = 2;
  bool northUp = false;
  double gain = 1.0;
  int threshold = 1;
  bool rings = true, ais = true, overlay = false;
  static RadarDisplay load();
  void save() const;
  RadarImage::Style style() const;
};

class RadarScope : public QWidget {
  Q_OBJECT
public:
  explicit RadarScope(QWidget* parent = nullptr);
  void setDisplay(const RadarDisplay& d);
  /** EBL/VRM at a bearing (deg true or relative) and range (NM); NaN off. */
  void setMarker(double bearingRel, double rangeNm);
  double markerBearingRel() const { return m_markBrg; }
  double markerRangeNm() const { return m_markNm; }
  /** Heading used for north up (deg), NaN if unknown. */
  double heading() const;
  QSize sizeHint() const override { return {640, 640}; }
  QSize minimumSizeHint() const override { return {320, 320}; }

signals:
  void markerChanged();
  void zoomRequested(int steps);

protected:
  void paintEvent(QPaintEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void wheelEvent(QWheelEvent*) override;

private:
  QRectF scopeRect() const;
  void refresh();
  RadarDisplay m_display;
  QImage m_image;
  double m_markBrg = std::numeric_limits<double>::quiet_NaN();
  double m_markNm = std::numeric_limits<double>::quiet_NaN();
};

class RadarView : public QWidget {
  Q_OBJECT
public:
  explicit RadarView(QWidget* parent = nullptr);
  void setLeftInset(int px);
  RadarScope* scope() const { return m_scope; }
  /** Display ranges in NM. */
  static const QVector<double>& ranges();
  static QString rangeText(double nm);
  void setRangeNm(double nm);
  const RadarDisplay& display() const { return m_display; }

signals:
  void displayChanged();

protected:
  void paintEvent(QPaintEvent*) override;
  void showEvent(QShowEvent*) override;
  void hideEvent(QHideEvent*) override;

private:
  void apply();
  void rebuildSources();
  void updateInfo();

  RadarDisplay m_display;
  RadarScope* m_scope;
  QComboBox* m_source;
  QComboBox* m_range;
  QPushButton* m_orient[2];
  QSlider* m_gain;
  QSlider* m_threshold;
  ToggleSwitch* m_rings;
  ToggleSwitch* m_ais;
  ToggleSwitch* m_overlay;
  QLabel* m_status;
  QLabel* m_info;
  QLabel* m_marker;
  QLabel* m_hint;
};

/** The radar image on the chart, north up around the own ship. */
class RadarLayer : public MapLayer {
  Q_OBJECT
public:
  explicit RadarLayer(QObject* parent = nullptr);
  QString name() const override { return tr("Radar"); }
  void paint(QPainter& p, const ChartView& view) override;
  int zOrder() const override { return -8; }
  void setDisplay(const RadarDisplay& d);

private:
  RadarDisplay m_display;
  QImage m_cache;
  int m_cacheSize = 0;
  double m_cacheHdg = 0;
  bool m_dirty = true;
};
