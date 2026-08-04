cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)
add_subdirectory(deps/libuiohook)

find_package(libobs REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Svg Widgets)
find_library(APPLICATION_SERVICES_FRAMEWORK ApplicationServices REQUIRED)
find_library(APPKIT_FRAMEWORK AppKit REQUIRED)

# Qt's 2025-07 prebuilt package still advertises the obsolete AGL framework. macOS 26.5 no longer ships it.
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
  set_property(TARGET WrapOpenGL::WrapOpenGL PROPERTY INTERFACE_LINK_LIBRARIES OpenGL::GL)
endif()

target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    OBS::libobs
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Svg
    Qt6::Widgets
    uiohook
    ${APPLICATION_SERVICES_FRAMEWORK}
    ${APPKIT_FRAMEWORK}
)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE src deps/libuiohook/include)
target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/hook/uiohook_helper.hpp
    src/hook/uiohook_helper_macos.mm
    src/input/input_data.hpp
    src/input/input_data.cpp
    src/input/input_broker.hpp
    src/input/input_broker.cpp
    src/input/keycodes.h
    src/sources/activity_sources.hpp
    src/sources/activity_sources.cpp
    src/sources/league_safe_area_layout.hpp
    src/sources/league_safe_area_layout.cpp
    src/sources/league_safe_area_source.hpp
    src/sources/league_safe_area_source.cpp
    src/sources/league_capture_switcher.hpp
    src/sources/league_capture_switcher.cpp
    src/sources/lol_game_report_types.hpp
    src/sources/lol_game_report_types.cpp
    src/sources/lol_game_report_diagnostics.hpp
    src/sources/lol_game_report_diagnostics.cpp
    src/sources/lol_game_report_store.hpp
    src/sources/lol_game_report_store.cpp
    src/sources/lol_game_report_web.hpp
    src/sources/lol_game_report_web.cpp
    src/sources/lol_game_report_web_assets.hpp
    src/sources/lol_game_report_web_assets.cpp
    src/sources/lol_game_report_riot_api.hpp
    src/sources/lol_game_report_riot_api.cpp
    src/sources/lol_game_report_collector.hpp
    src/sources/lol_game_report_collector.cpp
    src/sources/lol_game_report_source.hpp
    src/sources/lol_game_report_source.cpp
    src/sources/lol_performance_dashboard_source.cpp
    src/sources/lol_performance_dashboard_camera_visibility.hpp
    src/sources/lol_performance_dashboard_camera_visibility.cpp
    src/sources/lol_dashboard_game_config_watcher.hpp
    src/sources/lol_dashboard_game_config_watcher.cpp
    src/sources/lol_performance_dashboard_properties.inc
    src/sources/lol_performance_dashboard_layout.hpp
    src/sources/lol_performance_dashboard_layout.cpp
    src/sources/lol_performance_dashboard_visuals.hpp
    src/sources/lol_performance_dashboard_visuals.cpp
    src/sources/lol_performance_dashboard_heatmap.cpp
    src/sources/activity/core.inc
    src/sources/activity/key_labels.inc
    src/sources/activity/live_keys.inc
    src/sources/activity/mouse_events.inc
    src/sources/activity/mouse_render.inc
    src/sources/activity/statistics.inc
    src/sources/activity/intensity_events.inc
    src/sources/activity/intensity_render.inc
    src/sources/activity/unified.inc
    src/sources/activity/common_properties.inc
    src/sources/activity/live_keys_properties.inc
    src/sources/activity/mouse_properties.inc
    src/sources/activity/intensity_properties.inc
    src/sources/activity/unified_properties.inc
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

if(APPLE)
  cmake_path(GET Qt6_DIR PARENT_PATH _qt6_cmake_dir)
  cmake_path(GET _qt6_cmake_dir PARENT_PATH _qt6_lib_dir)
  cmake_path(GET _qt6_lib_dir PARENT_PATH _qt6_root_dir)
  set(_qt_tls_backend "${_qt6_root_dir}/plugins/tls/libqsecuretransportbackend.dylib")
  if(NOT EXISTS "${_qt_tls_backend}")
    message(FATAL_ERROR "Qt Secure Transport TLS backend is missing: ${_qt_tls_backend}")
  endif()
  add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_DIR:${CMAKE_PROJECT_NAME}>/Contents/Resources/tls"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_qt_tls_backend}"
            "$<TARGET_BUNDLE_DIR:${CMAKE_PROJECT_NAME}>/Contents/Resources/tls/libqsecuretransportbackend.dylib"
    VERBATIM
  )
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(
    league-safe-area-layout-test
    tests/league_safe_area_layout_test.cpp
    src/sources/league_safe_area_layout.cpp
    src/sources/lol_performance_dashboard_layout.cpp
  )
  target_include_directories(league-safe-area-layout-test PRIVATE src)
  target_compile_features(league-safe-area-layout-test PRIVATE cxx_std_17)
  add_test(NAME league-safe-area-layout COMMAND league-safe-area-layout-test)
  add_executable(
    lol-game-report-test
    tests/lol_game_report_test.cpp
    src/sources/lol_game_report_types.cpp
    src/sources/lol_game_report_diagnostics.cpp
  )
  target_link_libraries(lol-game-report-test PRIVATE Qt6::Core)
  target_include_directories(lol-game-report-test PRIVATE src)
  target_compile_features(lol-game-report-test PRIVATE cxx_std_17)
  add_test(NAME lol-game-report COMMAND lol-game-report-test)
  if(APPLE)
    target_link_options(lol-game-report-test PRIVATE "-Wl,-rpath,${_qt6_lib_dir}")
    set_target_properties(lol-game-report-test PROPERTIES XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO")
  endif()
endif()
