// Top level window: full screen chart with floating automotive style
// overlays, a bottom dock and a slide-in drawer.
#pragma once

#include <QHash>
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
class AreaSelectBar;
class TargetCard;
class SideDrawer;
class CockpitView;
class BoatView;
class LogView;
class RadarLayer;
class RadarView;
class SonarView;
class RoutesPage;
class LightScenesPage;
class AutomationsPage;
class RangePage;
class ProvisionsPage;

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
  enum class View { Cockpit, Chart, Boat, Log, Radar, Sonar };
  void setView(View v);
  View view() const { return m_view; }
  /** Lets the skipper frame an area for offline OSM data. */
  void beginAreaSelect();
  /** Drawer page by key ("settings", "routes", "general", "charts", "data",
   *  "offline", "sails", "scenes", "automations") or number; -1 if unknown. */
  int pageIndex(const QString& key) const;
  LightScenesPage* scenesPage() const { return m_scenesPage; }
  AutomationsPage* automationsPage() const { return m_automationsPage; }
  RangePage* rangePage() const { return m_rangePage; }
  LogView* logView() const { return m_log; }
  RadarView* radarView() const { return m_radar; }
  SonarView* sonarView() const { return m_sonar; }
  RadarLayer* radarLayer() const { return m_radarLayer; }
  ProvisionsPage* provisionsPage() const { return m_provisionsPage; }

protected:
  void resizeEvent(QResizeEvent*) override;
  void keyPressEvent(QKeyEvent*) override;
  bool eventFilter(QObject* o, QEvent* e) override;

private:
  void layoutOverlays(bool animate = false);
  void showContextMenu(double lat, double lon, const QPoint& global);
  void beginRouteCreate();
  void endRouteCreate(bool save);
  void applyAreaPreset(int preset);
  void endAreaSelect(bool accept);

  ChartView* m_chart;
  InstrumentCard* m_instruments;
  GuidanceCard* m_guidance;
  MapControls* m_controls;
  BottomDock* m_dock;
  Toast* m_toast;
  RouteEditBar* m_routeBar;
  AreaSelectBar* m_areaBar;
  TargetCard* m_target;
  SideDrawer* m_drawer;
  QPropertyAnimation m_instrumentsAnim;
  QVector<QPair<QWidget*, Qt::Corner>> m_overlays;
  int m_routesSection = -1;
  RoutesPage* m_routes = nullptr;
  int m_sailSection = -1;
  int m_offlineSection = -1;
  int m_settingsSection = -1;
  int m_scenesSection = -1;
  int m_automationsSection = -1;
  LightScenesPage* m_scenesPage = nullptr;
  AutomationsPage* m_automationsPage = nullptr;
  QHash<QString, int> m_pageKeys;
  int m_suppliesSection = -1;
  int m_rangeSection = -1;
  int m_provisionsSection = -1;
  RangePage* m_rangePage = nullptr;
  ProvisionsPage* m_provisionsPage = nullptr;
  CockpitView* m_cockpit;
  BoatView* m_boat;
  LogView* m_log;
  RadarView* m_radar;
  SonarView* m_sonar;
  RadarLayer* m_radarLayer;
  View m_view = View::Chart;
};
