#include "core/charts/ChartDatabase.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QSettings>
#include <QtConcurrent>
#include <algorithm>

#include "core/charts/BsbChart.h"
#include "core/charts/S57Chart.h"

ChartDatabase& ChartDatabase::instance() {
  static ChartDatabase db;
  return db;
}

ChartDatabase::ChartDatabase() : m_charts(std::make_shared<ChartList>()) {
  connect(&m_watcher, &QFutureWatcher<ChartList>::finished, this, [this] {
    m_charts = std::make_shared<const ChartList>(m_watcher.result());
    emit chartsChanged();
    if (m_rescanPending) {
      m_rescanPending = false;
      rescan();
    }
  });
}

bool ChartDatabase::isChartFile(const QString& path) {
  const QString suffix = QFileInfo(path).suffix().toLower();
  return suffix == "000" || suffix == "kap";
}

ChartPtr ChartDatabase::open(const QString& path) {
  const QString suffix = QFileInfo(path).suffix().toLower();
  std::shared_ptr<Chart> c;
  if (suffix == "000")
    c = std::make_shared<S57Chart>(path);
  else if (suffix == "kap")
    c = std::make_shared<BsbChart>(path);
  if (!c || !c->isValid()) {
    if (c) qWarning("Chart %s: %s", qPrintable(path), qPrintable(c->error()));
    return nullptr;
  }
  // Keep memory low: bulk data is reloaded when the chart becomes visible.
  c->unload();
  return c;
}

ChartList ChartDatabase::scan(const QStringList& dirs) {
  ChartList out;
  QStringList files;
  for (const QString& dir : dirs) {
    if (isChartFile(dir)) {
      files << dir;
      continue;
    }
    QDirIterator it(dir, {"*.000", "*.kap", "*.KAP"}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) files << it.next();
  }
  files.removeDuplicates();
  for (const QString& f : files)
    if (ChartPtr c = open(f)) out << c;
  // Coarse charts first so detailed ones are drawn on top.
  std::sort(out.begin(), out.end(), [](const ChartPtr& a, const ChartPtr& b) {
    return a->nativeScale() > b->nativeScale();
  });
  return out;
}

void ChartDatabase::rescan() {
  if (m_watcher.isRunning()) {
    m_rescanPending = true;
    return;
  }
  emit scanStarted();
  m_watcher.setFuture(QtConcurrent::run(&ChartDatabase::scan, m_dirs));
}

void ChartDatabase::addDirectory(const QString& dir) {
  if (dir.isEmpty() || m_dirs.contains(dir)) return;
  m_dirs << dir;
  save();
  rescan();
}

void ChartDatabase::removeDirectory(const QString& dir) {
  m_dirs.removeAll(dir);
  save();
  rescan();
}

void ChartDatabase::load() {
  QSettings s;
  m_dirs = s.value("Charts/directories").toStringList();
  if (!m_dirs.isEmpty()) rescan();
}

void ChartDatabase::save() const {
  QSettings s;
  s.setValue("Charts/directories", m_dirs);
}
