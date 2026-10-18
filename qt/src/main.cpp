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

#include "core/Provisions.h"
#include "core/AppSettings.h"
#include "core/Connections.h"
#include "core/GeoMath.h"
#include "core/BoatConfig.h"
#include "core/Grib.h"
#include "core/NavStore.h"
#include "core/osm/Overpass.h"
#include "core/Tides.h"
#include "core/TrackRecorder.h"
#include "core/charts/ChartDatabase.h"
#include "core/charts/S57Chart.h"
#include "ui/MainWindow.h"
#include "ui/PluginManager.h"
#include "ui/SideDrawer.h"
#include "ui/Theme.h"

#ifdef OCPN_QT_STATIC_MEDIA_PLUGIN
#include <QtPlugin>
Q_IMPORT_PLUGIN(MediaPlugin)
#endif

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
  QCommandLineOption page(
      "page",
      "Open a drawer page: settings, routes, general, charts, data, offline, "
      "sails, scenes, automations or its number.",
      "page");
  QCommandLineOption charts("charts", "Add a chart directory or file.", "path");
  QCommandLineOption center(
      "center", "Center the chart: lat,lon[,display scale].", "lat,lon,scale");
  QCommandLineOption view("view", "Start view: cockpit, chart or boat.",
                          "view");
  QCommandLineOption grib("grib", "Open a GRIB file.", "file");
  QCommandLineOption fresh("fresh",
                           "Ignore stored settings and data (testing).");
  QCommandLineOption importOsm(
      "import-osm", "Import an OSM XML file as offline region.", "file");
  QCommandLineOption selectArea("select-area",
                                "Start selecting an offline area.");
  cli.addOptions({shot, shotSize, delay, demo, day, page, charts, center, grib,
                  view, fresh, importOsm, selectArea});
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

  BoatConfig::instance().load();
  auto land = loadBasemap();
  S57Chart::setRegistryDir(findDataDir() + "/s57data");
  ChartDatabase::instance().load();
  TideDatabase::instance().loadAsync(findDataDir() + "/tcdata");
  for (const QString& dir : cli.values(charts))
    ChartDatabase::instance().addDirectory(QFileInfo(dir).absoluteFilePath());
  NavStore::instance().loadFromDisk();
  TrackRecorder::instance().loadFromDisk();
  if (settings.trackRecording) TrackRecorder::instance().start();

  MainWindow win(land);
  PluginManager::instance().loadAll(&win);
  if (cli.isSet(view))
    win.setView(cli.value(view) == "chart"  ? MainWindow::View::Chart
                : cli.value(view) == "boat" ? MainWindow::View::Boat
                                            : MainWindow::View::Cockpit);
  if (cli.isSet(grib)) {
    QString err;
    if (!GribManager::instance().open(cli.value(grib), &err))
      qWarning("GRIB: %s", qPrintable(err));
  }
  if (cli.isSet(center)) {
    const QStringList v = cli.value(center).split(',');
    QTimer::singleShot(0, &win, [&win, v] {
      win.chart()->centerOn(v.value(0).toDouble(), v.value(1).toDouble(),
                            false);
      if (v.size() > 2)
        win.chart()->zoomBy(win.chart()->displayScale() / v.value(2).toDouble(),
                            false);
    });
  }
  ConnectionManager::instance().load();
  // The demo shows a stocked boat.
  if (cli.isSet(demo) && ProvisionStore::instance().items().isEmpty())
    ProvisionStore::instance().loadExamples(QDate::currentDate());
  for (const QString& file : cli.values(importOsm)) {
    QString err;
    if (!OverpassService::instance().importFile(file, QString(), &err))
      qWarning("OSM import: %s", qPrintable(err));
  }
  if (cli.isSet(selectArea))
    QTimer::singleShot(2000, &win, [&win] { win.beginAreaSelect(); });

  if (cli.isSet(shot)) {
    const QStringList wh = cli.value(shotSize).split('x');
    win.resize(wh.value(0).toInt(), wh.value(1).toInt());
    win.show();
    if (cli.isSet(demo))
      QTimer::singleShot(1500, &win, [&win, &cli, &view] {
        const auto box = createDemoRoute();
        win.chart()->fitGeoBounds(box[0], box[1], box[2], box[3]);
        if (!cli.isSet(view)) win.setView(MainWindow::View::Cockpit);
      });
    if (cli.isSet(page))
      QTimer::singleShot(1800, &win, [&win, &cli, &page] {
        if (const int p = win.pageIndex(cli.value(page)); p >= 0)
          win.drawer()->open(p);
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
        if (const int p = win.pageIndex(cli.value(page)); p >= 0)
          win.drawer()->open(p);
      });
  }
  const int rc = app.exec();
  PluginManager::instance().shutdownAll();
  TrackRecorder::instance().saveToDisk();
  for (auto* c : ConnectionManager::instance().connections()) c->stop();
  return rc;
}
