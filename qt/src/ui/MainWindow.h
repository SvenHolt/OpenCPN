// Top level window: full screen chart with floating automotive style
// overlays, a bottom dock and a slide-in drawer.
#pragma once

#include <QPropertyAnimation>
#include <QWidget>
#include <memory>

#include "api/OcpnPlugin.h"
#include "ui/ChartView.h"

class InstrumentCard;
class GuidanceCard;
class MapControls;
class BottomDock;
class Toast;
class RouteEditBar;
class TargetCard;
class SideDrawer;
class CockpitView;
class PassagePage;

class MainWindow : public QWidget, public PluginHost {
  Q_OBJECT
public:
  explicit MainWindow(std::shared_ptr<const LandData> land,
                      QWidget* parent = nullptr);
  ~MainWindow() override;

  // PluginHost
  int addSection(const QString& icon, const QString& title,
                 QWidget* page) override;
  void addOverlay(QWidget* widget, Qt::Corner corner) override;
  void addMapLayer(MapLayer* layer) override;
  void showMessage(const QString& text, bool alarm = false) override;
  void openSection(int section) override;
  ChartView* chartView() const override { return m_chart; }
  NavStore& navigation() const override;
  QWidget* mainWindow() const override { return const_cast<MainWindow*>(this); }

  ChartView* chart() const { return m_chart; }
  SideDrawer* drawer() const { return m_drawer; }
  void selectAisTarget(quint32 mmsi);
  enum class View { Cockpit, Chart };
  void setView(View v);
  View view() const { return m_view; }

protected:
  void resizeEvent(QResizeEvent*) override;
  void keyPressEvent(QKeyEvent*) override;
  bool eventFilter(QObject* o, QEvent* e) override;

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
  QVector<QPair<QWidget*, Qt::Corner>> m_overlays;
  int m_routesSection = -1;
  int m_passageSection = -1;
  PassagePage* m_passage = nullptr;
  int m_sailSection = -1;
  CockpitView* m_cockpit;
  View m_view = View::Chart;
};
