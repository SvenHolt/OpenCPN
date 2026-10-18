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
)

add_library(ocpnqt_core STATIC ${OCPN_QT_CORE_SOURCES})
target_include_directories(ocpnqt_core PUBLIC src)
target_link_libraries(ocpnqt_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network)
if(Qt6SerialPort_FOUND)
  target_link_libraries(ocpnqt_core PUBLIC Qt6::SerialPort)
  target_compile_definitions(ocpnqt_core PUBLIC OCPN_QT_HAVE_SERIAL)
endif()

add_library(ocpnqt_ui STATIC
  src/ui/Theme.cpp
  src/ui/Icons.cpp
  src/ui/GlassPanel.cpp
  src/ui/ChartView.cpp
  src/ui/Overlays.cpp
  src/ui/SideDrawer.cpp
  src/ui/DrawerPages.cpp
  src/ui/MainWindow.cpp
)
target_link_libraries(ocpnqt_ui PUBLIC ocpnqt_core Qt6::Widgets Qt6::Svg
  Qt6::Concurrent)

add_executable(opencpn-qt src/main.cpp)
target_link_libraries(opencpn-qt PRIVATE ocpnqt_ui)

# Location of the basemap shapefiles shipped in the OpenCPN source tree.
target_compile_definitions(opencpn-qt PRIVATE
  OCPN_QT_SOURCE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data"
  OCPN_QT_INSTALL_DATA_DIR="${CMAKE_INSTALL_PREFIX}/share/opencpn")

install(TARGETS opencpn-qt RUNTIME DESTINATION bin)
install(DIRECTORY ../data/basemap_shp DESTINATION share/opencpn)

option(OCPN_QT_BUILD_TESTS "Build opencpn-qt unit tests" ON)
if(OCPN_QT_BUILD_TESTS)
  find_package(Qt6 QUIET COMPONENTS Test)
  if(Qt6Test_FOUND)
    enable_testing()
    add_executable(ocpnqt_tests tests/test_core.cpp)
    target_link_libraries(ocpnqt_tests PRIVATE ocpnqt_core Qt6::Test)
    add_test(NAME ocpnqt_tests COMMAND ocpnqt_tests)
    add_executable(ocpnqt_ui_tests tests/test_ui.cpp)
    target_link_libraries(ocpnqt_ui_tests PRIVATE ocpnqt_ui Qt6::Test)
    add_test(NAME ocpnqt_ui_tests COMMAND ocpnqt_ui_tests)
    set_tests_properties(ocpnqt_ui_tests PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()
endif()
