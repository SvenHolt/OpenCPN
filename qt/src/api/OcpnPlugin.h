// Public plugin API of the OpenCPN Qt frontend.
//
// A plugin is a Qt plugin (QPluginLoader) implementing OcpnPlugin. It gets a
// PluginHost in init() through which it can add a dock section with a drawer
// page, floating overlay cards and chart layers, and access the navigation
// model. Plugins link against the opencpn-qt core/ui shared libraries so
// they share the theme and the navigation data with the application.
#pragma once

#include <QString>
#include <QtPlugin>

class QWidget;
class ChartView;
class MapLayer;
class NavStore;

class PluginHost {
public:
  virtual ~PluginHost() = default;

  /** Adds a dock button and drawer page; returns the section index. */
  virtual int addSection(const QString& icon, const QString& title,
                         QWidget* page) = 0;
  /** Floating card above the chart, in the given corner (BottomLeft,
   *  BottomRight or TopRight). The host takes ownership. */
  virtual void addOverlay(QWidget* widget, Qt::Corner corner) = 0;
  /** Chart overlay layer; the plugin keeps ownership. */
  virtual void addMapLayer(MapLayer* layer) = 0;
  /** Shows a toast message. */
  virtual void showMessage(const QString& text, bool alarm = false) = 0;
  /** Opens the drawer at a section returned by addSection(). */
  virtual void openSection(int section) = 0;

  virtual ChartView* chartView() const = 0;
  virtual NavStore& navigation() const = 0;
  virtual QWidget* mainWindow() const = 0;
};

class OcpnPlugin {
public:
  virtual ~OcpnPlugin() = default;

  virtual QString id() const = 0;
  virtual QString name() const = 0;
  virtual QString description() const = 0;
  virtual QString version() const = 0;

  virtual void init(PluginHost* host) = 0;
  virtual void shutdown() {}
};

#define OcpnPlugin_iid "org.opencpn.qt.Plugin/1.0"
Q_DECLARE_INTERFACE(OcpnPlugin, OcpnPlugin_iid)
