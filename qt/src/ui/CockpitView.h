// Cockpit home screen: an animated top-down view of the boat with its sails
// inside a heading-up compass rose, surrounded by cards for speed, course,
// wind, the next waypoint and (when running) the engine.
#pragma once

#include <QElapsedTimer>
#include <QPointF>
#include <QTimer>
#include <QVector>
#include <QWidget>

class CockpitView : public QWidget {
  Q_OBJECT
public:
  explicit CockpitView(QWidget* parent = nullptr);

  /** Space on the left covered by the drawer (cards move aside). */
  void setLeftInset(int px) {
    m_leftInset = px;
    update();
  }

signals:
  void sailConfigRequested();
  void routesRequested();

protected:
  void paintEvent(QPaintEvent*) override;
  void showEvent(QShowEvent*) override;
  void hideEvent(QHideEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void resizeEvent(QResizeEvent*) override;

private:
  struct Particle {
    QPointF pos;  // screen
    double life;  // 0..1
  };
  void step();
  void layout();
  void drawBackground(QPainter& p);
  void drawWind(QPainter& p);
  void drawRose(QPainter& p);
  void drawBoat(QPainter& p);
  void drawSails(QPainter& p, double length);
  void drawCards(QPainter& p);
  void drawEngine(QPainter& p, const QRectF& r);

  QTimer m_timer;
  QElapsedTimer m_clock;
  double m_time = 0;  // seconds
  // Smoothed values for the animation
  double m_hdg = 0, m_awa = 45, m_sog = 0, m_heel = 0;
  bool m_first = true;
  QVector<Particle> m_wind;
  QVector<QPointF> m_foam;

  QPointF m_center;
  double m_radius = 200;
  QRectF m_sailPill, m_wpCard;
  int m_leftInset = 0;
};
