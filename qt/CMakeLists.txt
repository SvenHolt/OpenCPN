# OpenCPN Qt6 frontend ("opencpn-qt")
#
# Standalone CMake project, independent of the wxWidgets build. Build with:
#   cmake -S qt -B build-qt && cmake --build build-qt
cmake_minimum_required(VERSION 3.16)
project(opencpn-qt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Svg Concurrent)
find_package(Qt6 QUIET COMPONENTS SerialPort)

# S-57 support reuses OpenCPN's wx-free ISO 8211 / OGR / S-57 reader libs.
set(OCPN_LIBS ${CMAKE_CURRENT_SOURCE_DIR}/../libs)
file(GLOB OCPN_GDAL_SRC ${OCPN_LIBS}/gdal/src/*.cpp)
file(GLOB OCPN_ISO8211_SRC ${OCPN_LIBS}/iso8211/src/*.cpp)
add_library(ocpnqt_s57reader STATIC
  ${OCPN_GDAL_SRC}
  ${OCPN_ISO8211_SRC}
  ${OCPN_LIBS}/s57-charts/src/ogrs57datasource.cpp
  ${OCPN_LIBS}/s57-charts/src/ogrs57layer.cpp
  ${OCPN_LIBS}/s57-charts/src/s57classregistrar.cpp
  ${OCPN_LIBS}/s57-charts/src/s57featuredefns.cpp
  ${OCPN_LIBS}/s57-charts/src/s57reader.cpp
)
# s57-charts must come first: iso8211 ships an older s57.h.
target_include_directories(ocpnqt_s57reader PUBLIC
  ${OCPN_LIBS}/s57-charts/include
  ${OCPN_LIBS}/gdal/include
  ${OCPN_LIBS}/gdal/include/gdal
  ${OCPN_LIBS}/iso8211/include)
set_target_properties(ocpnqt_s57reader PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(ocpnqt_s57reader PRIVATE -w)
endif()

set(OCPN_QT_CORE_SOURCES
  src/core/GeoMath.cpp
  src/core/Nmea.cpp
  src/core/Ais.cpp
  src/core/NavStore.cpp
  src/core/GpxIO.cpp
  src/core/Shapefile.cpp
  src/core/Connections.cpp
  src/core/Simulator.cpp
  src/core/AppSettings.cpp
  src/core/TrackRecorder.cpp
  src/core/Tides.cpp
  src/core/Grib.cpp
  src/core/GribDownload.cpp
  src/core/charts/Chart.cpp
  src/core/charts/BsbChart.cpp
  src/core/charts/S57Chart.cpp
  src/core/charts/ChartDatabase.cpp
)

# Core and UI are shared libraries so that plugins share the application's
# singletons (theme, navigation data) with the executable.
include(GNUInstallDirs)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
set(CMAKE_INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
set(OCPN_QT_PLUGIN_DIR ${CMAKE_INSTALL_LIBDIR}/opencpn-qt/plugins)

add_library(ocpnqt_core SHARED ${OCPN_QT_CORE_SOURCES})
target_include_directories(ocpnqt_core PUBLIC src)
target_link_libraries(ocpnqt_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network
  Qt6::Concurrent PRIVATE ocpnqt_s57reader)
if(Qt6SerialPort_FOUND)
  target_link_libraries(ocpnqt_core PUBLIC Qt6::SerialPort)
  target_compile_definitions(ocpnqt_core PUBLIC OCPN_QT_HAVE_SERIAL)
endif()

add_library(ocpnqt_ui SHARED
  src/ui/Theme.cpp
  src/ui/Icons.cpp
  src/ui/GlassPanel.cpp
  src/ui/ChartView.cpp
  src/ui/Overlays.cpp
  src/ui/SideDrawer.cpp
  src/ui/DrawerPages.cpp
  src/ui/MainWindow.cpp
  src/ui/MapLayer.h
  src/ui/PluginManager.cpp
  src/ui/TracksPage.cpp
  src/ui/TidesPage.cpp
  src/ui/WeatherPage.cpp
  src/api/OcpnPlugin.h
)
target_link_libraries(ocpnqt_ui PUBLIC ocpnqt_core Qt6::Widgets Qt6::Svg
  Qt6::Concurrent)

add_executable(opencpn-qt src/main.cpp)
target_link_libraries(opencpn-qt PRIVATE ocpnqt_ui)

# Location of the basemap shapefiles shipped in the OpenCPN source tree.
target_compile_definitions(opencpn-qt PRIVATE
  OCPN_QT_SOURCE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data"
  OCPN_QT_INSTALL_DATA_DIR="${CMAKE_INSTALL_PREFIX}/share/opencpn")

install(TARGETS opencpn-qt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS ocpnqt_core ocpnqt_ui
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY src/api src/ui src/core
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/opencpn-qt
  FILES_MATCHING PATTERN "*.h")
install(DIRECTORY ../data/s57data ../data/tcdata DESTINATION share/opencpn)
install(DIRECTORY ../data/basemap_shp DESTINATION share/opencpn)

# Plugins are built into <build>/plugins, where the application finds them.
add_subdirectory(plugins/media)

option(OCPN_QT_BUILD_TESTS "Build opencpn-qt unit tests" ON)
if(OCPN_QT_BUILD_TESTS)
  find_package(Qt6 QUIET COMPONENTS Test)
  if(Qt6Test_FOUND)
    enable_testing()
    add_executable(ocpnqt_tests tests/test_core.cpp)
    target_link_libraries(ocpnqt_tests PRIVATE ocpnqt_core Qt6::Test)
    target_compile_definitions(ocpnqt_tests PRIVATE
      OCPN_QT_SOURCE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data")
    add_test(NAME ocpnqt_tests COMMAND ocpnqt_tests)
    add_executable(ocpnqt_chart_tests tests/test_charts.cpp)
    target_link_libraries(ocpnqt_chart_tests PRIVATE ocpnqt_core Qt6::Test)
    target_compile_definitions(ocpnqt_chart_tests PRIVATE
      OCPN_QT_SOURCE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data"
      OCPN_QT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    add_test(NAME ocpnqt_chart_tests COMMAND ocpnqt_chart_tests)
    set_tests_properties(ocpnqt_chart_tests PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(ocpnqt_grib_tests tests/test_grib.cpp)
    target_link_libraries(ocpnqt_grib_tests PRIVATE ocpnqt_core Qt6::Test
      Qt6::Network)
    target_compile_definitions(ocpnqt_grib_tests PRIVATE
      OCPN_QT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    add_test(NAME ocpnqt_grib_tests COMMAND ocpnqt_grib_tests)
    add_executable(ocpnqt_media_tests tests/test_media.cpp)
    target_link_libraries(ocpnqt_media_tests PRIVATE media_pi_core Qt6::Test)
    if(TARGET Qt6::DBus)
      add_executable(fake_mpris_player tests/fake_mpris_player.cpp)
      target_link_libraries(fake_mpris_player PRIVATE Qt6::DBus)
    endif()
    find_program(DBUS_RUN_SESSION dbus-run-session)
    if(DBUS_RUN_SESSION)
      add_test(NAME ocpnqt_media_tests
        COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:ocpnqt_media_tests>)
    else()
      add_test(NAME ocpnqt_media_tests COMMAND ocpnqt_media_tests)
    endif()
    set_tests_properties(ocpnqt_media_tests PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(ocpnqt_ui_tests tests/test_ui.cpp)
    target_link_libraries(ocpnqt_ui_tests PRIVATE ocpnqt_ui Qt6::Test)
    add_test(NAME ocpnqt_ui_tests COMMAND ocpnqt_ui_tests)
    set_tests_properties(ocpnqt_ui_tests PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()
endif()
