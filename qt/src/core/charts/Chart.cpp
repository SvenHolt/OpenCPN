#include "core/charts/Chart.h"

bool Chart::ensureLoaded() const {
  std::lock_guard<std::mutex> lock(m_loadMutex);
  if (m_loaded) return true;
  m_loaded = loadData();
  return m_loaded;
}
