# Media plugin: Spotify / YouTube Music "now playing" card and controls.
find_package(Qt6 QUIET COMPONENTS DBus)

add_library(media_pi_core STATIC
  MediaSource.h
  MediaController.cpp
  SpotifyWebSource.cpp
  MprisSource.cpp
)
set_target_properties(media_pi_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(media_pi_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(media_pi_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network)
if(Qt6DBus_FOUND)
  target_link_libraries(media_pi_core PUBLIC Qt6::DBus)
  target_compile_definitions(media_pi_core PUBLIC OCPN_MEDIA_HAVE_DBUS)
endif()

add_library(media_pi MODULE MediaPlugin.cpp media.json)
target_link_libraries(media_pi PRIVATE media_pi_core ocpnqt_ui)
set_target_properties(media_pi PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
  INSTALL_RPATH "$ORIGIN/../..")
install(TARGETS media_pi
  LIBRARY DESTINATION ${OCPN_QT_PLUGIN_DIR}
  RUNTIME DESTINATION ${OCPN_QT_PLUGIN_DIR})
