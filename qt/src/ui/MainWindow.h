// Top level window: full screen chart with floating automotive style
// overlays, a bottom dock and a slide-in drawer.
#pragma once

#include <QPropertyAnimation>
#include <QWidget>
#include <memory>

#include "ui/ChartView.h"

class InstrumentCard;
class GuidanceCard;
class MapControls;
class BottomDock;
class Toast;
class RouteEditBar;
class TargetCard;
class SideDrawer;

class MainWindow : public QWidget {
  Q_OBJECT
public:
  explicit MainWindow(std::shared_ptr<const LandData> land,
                      QWidget* parent = nullptr);

  ChartView* chart() const { return m_chart; }
  SideDrawer* drawer() const { return m_drawer; }
  void selectAisTarget(quint32 mmsi);

protected:
  void resizeEvent(QResizeEvent*) override;
  void keyPressEvent(QKeyEvent*) override;

private:
  void layoutOverlays(bool animate = false);
  void showContextMenu(double lat, double lon, const QPoint& global);
  void beginRouteCreate();
  void endRouteCreate(bool save);

  ChartView* m_chart;
  InstrumentCard* m_instruments;
  GuidanceCard* m_guidance;
  MapControls* m_controls;
  BottomDock* m_dock;
  Toast* m_toast;
  RouteEditBar* m_routeBar;
  TargetCard* m_target;
  SideDrawer* m_drawer;
  QPropertyAnimation m_instrumentsAnim;
};
