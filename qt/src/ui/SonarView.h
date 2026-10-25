// Sonar view: a scrolling echogram (newest on the right) with depth scale,
// bottom line and safety depth, the depth, depth below keel and water
// temperature, and the Signal K path for echo samples.
#pragma once

#include <QImage>
#include <QWidget>

#include "core/Sonar.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

class Echogram : public QWidget {
  Q_OBJECT
public:
  enum class Colors { Color, Gray };
  explicit Echogram(QWidget* parent = nullptr);
  void setSpanSecs(int secs);
  int spanSecs() const { return m_span; }
  /** 0: automatic from the depths shown. */
  void setRangeM(double m);
  /** The depth range in use (automatic or fixed). */
  double rangeM() const { return m_usedRange; }
  void setColors(Colors c);
  /** Echo strength 0..255 of a ping at a depth (also drawn from the depth
   *  when the ping has no samples). */
  static int echoAt(const SonarPing& p, double depthM);
  static QRgb color(int value, Colors c);
  QSize sizeHint() const override { return {700, 520}; }

protected:
  void paintEvent(QPaintEvent*) override;
  void resizeEvent(QResizeEvent*) override;

private:
  QRectF plotRect() const;
  void rebuild();

  int m_span = 300;
  double m_fixedRange = 0, m_usedRange = 10;
  Colors m_colors = Colors::Color;
  QImage m_image;
};

class SonarView : public QWidget {
  Q_OBJECT
public:
  explicit SonarView(QWidget* parent = nullptr);
  void setLeftInset(int px);
  Echogram* echogram() const { return m_echo; }

protected:
  void paintEvent(QPaintEvent*) override;
  void showEvent(QShowEvent*) override;

private:
  void updateReadout();

  Echogram* m_echo;
  QLabel* m_status;
  QLabel* m_depth;
  QLabel* m_details;
  QPushButton* m_span[4];
  QComboBox* m_range;
  QComboBox* m_colors;
  QLineEdit* m_path;
};
