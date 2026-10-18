// Chart directories, scanning and the set of available charts.
#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QStringList>
#include <QVector>
#include <memory>

#include "core/charts/Chart.h"

using ChartList = QVector<ChartPtr>;

class ChartDatabase : public QObject {
  Q_OBJECT
public:
  static ChartDatabase& instance();

  QStringList directories() const { return m_dirs; }
  void addDirectory(const QString& dir);
  void removeDirectory(const QString& dir);

  /** Immutable snapshot of all charts, safe to pass to worker threads. */
  std::shared_ptr<const ChartList> charts() const { return m_charts; }
  bool isScanning() const { return m_watcher.isRunning(); }

  void load();  // directories from settings, then rescan
  void rescan();

  /** Opens a single chart file by extension (nullptr if unsupported). */
  static ChartPtr open(const QString& path);
  static bool isChartFile(const QString& path);
  /** Scans the directories synchronously (used by the background job). */
  static ChartList scan(const QStringList& dirs);

signals:
  void chartsChanged();
  void scanStarted();

private:
  ChartDatabase();
  void save() const;

  QStringList m_dirs;
  std::shared_ptr<const ChartList> m_charts;
  QFutureWatcher<ChartList> m_watcher;
  bool m_rescanPending = false;
};
