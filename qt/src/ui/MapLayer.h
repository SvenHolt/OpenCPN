// Overlay layer drawn on top of the chart (weather, tides, tracks, plugins).
#pragma once

#include <QObject>
#include <QString>

class ChartView;
class QPainter;

class MapLayer : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;

  virtual QString name() const = 0;
  /** Paints in screen coordinates. Use the view's screenPos() helpers. */
  virtual void paint(QPainter& p, const ChartView& view) = 0;
  /** < 0: below routes and AIS, >= 0: above the own ship. */
  virtual int zOrder() const { return -10; }

  bool isEnabled() const { return m_enabled; }
  void setEnabled(bool on) {
    if (on == m_enabled) return;
    m_enabled = on;
    emit changed();
  }

signals:
  void changed();

private:
  bool m_enabled = true;
};
