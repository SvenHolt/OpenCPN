// Discovers, loads and initializes plugins.
#pragma once

#include <QObject>
#include <QStringList>
#include <QVector>

#include "api/OcpnPlugin.h"

class QPluginLoader;

struct PluginInfo {
  QString id, name, description, version, file;
  bool enabled = true;
  bool loaded = false;
  QString error;
};

class PluginManager : public QObject {
  Q_OBJECT
public:
  static PluginManager& instance();

  /** Directories searched for plugins, in order. */
  static QStringList searchPaths();
  /** Loads all enabled plugins and initializes them with the host. */
  void loadAll(PluginHost* host);
  void shutdownAll();

  const QVector<PluginInfo>& plugins() const { return m_infos; }
  /** Takes effect on the next start. */
  void setEnabled(const QString& id, bool enabled);
  bool isEnabled(const QString& id) const;

signals:
  void pluginsChanged();

private:
  PluginManager() = default;
  QVector<PluginInfo> m_infos;
  QVector<OcpnPlugin*> m_active;
  QVector<QPluginLoader*> m_loaders;
};
