cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core QUIET)
  if(Qt6_FOUND)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE -DQT_VERSION=6)
  else()
    find_package(Qt5 COMPONENTS Widgets Core REQUIRED)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt5::Core Qt5::Widgets)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE -DQT_VERSION=5)
  endif()
  
  # Crucial for finding <QtWidgets/...> on some platforms
  target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${Qt6Widgets_INCLUDE_DIRS} ${Qt5Widgets_INCLUDE_DIRS})
  
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.cpp src/obs-mpv-source.cpp src/mpv-dock.cpp src/mpv-sub-dialog.cpp src/playlist-table-widget.cpp)

if(WIN32)
  find_path(MPV_INCLUDE_DIR mpv/client.h HINTS ${MPV_INCLUDE_DIRS} $ENV{MPV_INCLUDE_DIRS})
  find_library(MPV_LIBRARY NAMES mpv libmpv HINTS ${MPV_LIBRARY_DIRS} $ENV{MPV_LIBRARY_DIRS})
  target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${MPV_INCLUDE_DIR})
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${MPV_LIBRARY})
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(MPV REQUIRED mpv)
  find_library(MPV_LIBRARY NAMES mpv HINTS ${MPV_LIBRARY_DIRS})
  target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${MPV_INCLUDE_DIRS})
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${MPV_LIBRARY})
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
