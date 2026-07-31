cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)
add_subdirectory(deps/libuiohook)

find_package(libobs REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Svg Widgets)
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
  PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
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
