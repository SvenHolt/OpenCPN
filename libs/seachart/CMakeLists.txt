# seachart: offline harbour/weather database (written by tools/seachart)
# and the weather dependent passage router.  Plain C++17, used by the wx
# GUI and the Qt6 frontend.

cmake_minimum_required(VERSION 3.10)

if (TARGET ocpn::seachart)
  return ()
endif ()

if (NOT TARGET ocpn::sqlite_cpp)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../SQLiteCpp SQLiteCpp)
endif ()
if (NOT TARGET ocpn::filesystem)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../std_filesystem std_filesystem)
endif ()

add_library(SEACHART STATIC
  src/passage_router.cpp
  src/seachart_db.cpp
  include/passage_router.h
  include/seachart_db.h
)
add_library(ocpn::seachart ALIAS SEACHART)
target_include_directories(SEACHART PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(SEACHART PUBLIC ocpn::sqlite_cpp ocpn::filesystem)
set_target_properties(SEACHART PROPERTIES POSITION_INDEPENDENT_CODE ON)
