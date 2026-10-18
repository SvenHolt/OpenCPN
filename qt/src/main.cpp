// OpenCPN Qt6 frontend entry point.
#include <QApplication>
#include <array>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include "core/AppSettings.h"
#include "core/Connections.h"
#include "core/GeoMath.h"
#include "core/NavStore.h"
#include "ui/MainWindow.h"
#include "ui/SideDrawer.h"
#include "ui/Theme.h"

namespace {

QString findDataDir() {
  QStringList candidates;
  const QString env = qEnvironmentVariable("OPENCPN_DATA_DIR");
  if (!env.isEmpty()) candidates << env;
  candidates << QCoreApplication::applicationDirPath() + "/../share/opencpn"
             << QStringLiteral(OCPN_QT_INSTALL_DATA_DIR)
             << QStringLiteral(OCPN_QT_SOURCE_DATA_DIR);
  for (const QString& c : candidates)
    if (QFileInfo::exists(c + "/basemap_shp/basemap_crude_10x10.shp"))
      return QDir(c).absolutePath();
  return {};
}

std::shared_ptr<const LandData> loadBasemap() {
  auto land = std::make_shared<LandData>();
  const QString dir = findDataDir();
  if (dir.isEmpty()) {
    qWarning("Basemap not found; set OPENCPN_DATA_DIR to OpenCPN's data dir");
    return land;
  }
  QElapsedTimer t;
  t.start();
  Shapefile::loadPolygons(dir + "/basemap_shp/basemap_crude_10x10.shp",
                          land->crude);
  Shapefile::loadPolygons(dir + "/basemap_shp/basemap_low.shp", land->detailed);
  qInfo("Basemap: %lld + %lld polygons loaded in %lld ms",
        qlonglong(land->crude.size()), qlonglong(land->detailed.size()),
        qlonglong(t.elapsed()));
  return land;
}

/** Sample route next to the simulated boat, used by --demo. Returns its
 *  bounding box. */
std::array<double, 4> createDemoRoute() {
  auto& store = NavStore::instance();
  const auto& own = store.ownShip();
  if (!own.valid) return {};
  Route r;
  r.name = QObject::tr("Kiel Bay tour");
  const double legs[][2] = {{25, 1.2}, {60, 1.6}, {15, 1.4}, {80, 1.8}};
  double lat = own.lat, lon = own.lon;
  Waypoint start;
  start.name = QObject::tr("Start");
  start.lat = lat;
  start.lon = lon;
  r.points << start;
  int i = 1;
  for (const auto& leg : legs) {
    geo::destination(lat, lon, leg[0], leg[1], lat, lon);
    Waypoint w;
    w.name = QStringLiteral("WP%1").arg(i++);
    w.lat = lat;
    w.lon = lon;
    r.points << w;
  }
  store.activateRoute(store.addRoute(r), 1);
  double a = 90, b = 180, c = -90, d = -180;
  for (const auto& p : r.points) {
    a = std::min(a, p.lat);
    b = std::min(b, p.lon);
    c = std::max(c, p.lat);
    d = std::max(d, p.lon);
  }
  Waypoint mark;
  geo::destination(own.lat, own.lon, 300, 0.9, mark.lat, mark.lon);
  mark.name = QObject::tr("Anchorage");
  store.addWaypoint(mark);
  return std::array<double, 4>{a, b, c, d};
}

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setOrganizationName("opencpn");
  QApplication::setApplicationName("opencpn-qt");
  QApplication::setApplicationDisplayName("OpenCPN");
  QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

  QCommandLineParser cli;
  cli.setApplicationDescription("OpenCPN chart plotter, Qt6 frontend");
  cli.addHelpOption();
  cli.addVersionOption();
  QCommandLineOption shot("screenshot", "Save a screenshot and quit.", "file");
  QCommandLineOption shotSize("size", "Window size for --screenshot.", "WxH",
                              "1600x1000");
  QCommandLineOption delay("delay", "Delay before the screenshot (ms).", "ms",
                           "4000");
  QCommandLineOption demo("demo", "Create and activate a demo route.");
  QCommandLineOption day("day", "Start with the day theme.");
  QCommandLineOption page("page", "Open a drawer page (0-4).", "index");
  QCommandLineOption fresh("fresh",
                           "Ignore stored settings and data (testing).");
  cli.addOptions({shot, shotSize, delay, demo, day, page, fresh});
  cli.process(app);

  if (cli.isSet(fresh)) {
    // Use throw-away settings and data so the user's setup stays untouched.
    QStandardPaths::setTestModeEnabled(true);
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir(dir).removeRecursively();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       dir + "/settings");
  }

  auto& settings = AppSettings::instance();
  settings.load();
  if (cli.isSet(day)) settings.theme = AppSettings::ThemeMode::Day;
  Theme::instance().apply();

  auto land = loadBasemap();
  NavStore::instance().loadFromDisk();

  MainWindow win(land);
  ConnectionManager::instance().load();

  if (cli.isSet(shot)) {
    const QStringList wh = cli.value(shotSize).split('x');
    win.resize(wh.value(0).toInt(), wh.value(1).toInt());
    win.show();
    if (cli.isSet(demo))
      QTimer::singleShot(1500, &win, [&win] {
        const auto box = createDemoRoute();
        win.chart()->fitGeoBounds(box[0], box[1], box[2], box[3]);
      });
    if (cli.isSet(page))
      QTimer::singleShot(1800, &win, [&win, &cli, &page] {
        win.drawer()->open(cli.value(page).toInt());
      });
    QTimer::singleShot(cli.value(delay).toInt(), &win, [&win, &cli, &shot] {
      win.grab().save(cli.value(shot));
      QApplication::quit();
    });
  } else {
    win.showMaximized();
    if (cli.isSet(demo))
      QTimer::singleShot(1500, &win, [] { createDemoRoute(); });
    if (cli.isSet(page))
      QTimer::singleShot(300, &win, [&win, &cli, &page] {
        win.drawer()->open(cli.value(page).toInt());
      });
  }
  const int rc = app.exec();
  for (auto* c : ConnectionManager::instance().connections()) c->stop();
  return rc;
}
