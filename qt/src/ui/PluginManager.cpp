#include "ui/PluginManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QLibrary>
#include <QPluginLoader>
#include <QSettings>
#include <QStandardPaths>

PluginManager& PluginManager::instance() {
  static PluginManager m;
  return m;
}

QStringList PluginManager::searchPaths() {
  QStringList paths;
  const QString env = qEnvironmentVariable("OPENCPN_QT_PLUGIN_PATH");
  if (!env.isEmpty()) paths << env.split(QDir::listSeparator());
  const QString app = QCoreApplication::applicationDirPath();
  paths << app + "/plugins" << app + "/../lib/opencpn-qt/plugins"
        << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
               "/plugins";
  return paths;
}

bool PluginManager::isEnabled(const QString& id) const {
  return !QSettings().value("Plugins/disabled").toStringList().contains(id);
}

void PluginManager::setEnabled(const QString& id, bool enabled) {
  QSettings s;
  QStringList disabled = s.value("Plugins/disabled").toStringList();
  disabled.removeAll(id);
  if (!enabled) disabled << id;
  s.setValue("Plugins/disabled", disabled);
  for (auto& info : m_infos)
    if (info.id == id) info.enabled = enabled;
  emit pluginsChanged();
}

void PluginManager::loadAll(PluginHost* host) {
  QStringList seen;
  for (const QString& dir : searchPaths()) {
    const QDir d(dir);
    if (!d.exists()) continue;
    for (const QString& file : d.entryList(QDir::Files)) {
      const QString path = d.absoluteFilePath(file);
      if (!QLibrary::isLibrary(path)) continue;
      auto* loader = new QPluginLoader(path, this);
      const QJsonObject meta = loader->metaData().value("MetaData").toObject();
      PluginInfo info;
      info.file = path;
      info.id = meta.value("id").toString(QFileInfo(file).baseName());
      info.name = meta.value("name").toString(info.id);
      info.description = meta.value("description").toString();
      info.version = meta.value("version").toString();
      if (loader->metaData().value("IID").toString() != OcpnPlugin_iid ||
          seen.contains(info.id)) {
        delete loader;
        continue;
      }
      seen << info.id;
      info.enabled = isEnabled(info.id);
      if (info.enabled) {
        auto* plugin = qobject_cast<OcpnPlugin*>(loader->instance());
        if (plugin) {
          plugin->init(host);
          m_active << plugin;
          m_loaders << loader;
          info.loaded = true;
          info.name = plugin->name();
          info.description = plugin->description();
          info.version = plugin->version();
        } else {
          info.error = loader->errorString();
          qWarning("Plugin %s: %s", qPrintable(path), qPrintable(info.error));
          delete loader;
        }
      } else {
        delete loader;
      }
      m_infos << info;
    }
  }
  emit pluginsChanged();
}

void PluginManager::shutdownAll() {
  for (OcpnPlugin* p : m_active) p->shutdown();
  m_active.clear();
}
