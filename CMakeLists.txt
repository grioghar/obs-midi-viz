cmake_minimum_required(VERSION 3.24)

project(obs-midi-viz
  VERSION 0.1.4.4
  DESCRIPTION "MIDI visualizer plugin for OBS Studio"
  LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ─────────────────────────────────────────────────────────────────────────────
# OBS discovery — three-stage fallback
# ─────────────────────────────────────────────────────────────────────────────
if(APPLE)
  set(OBS_FRAMEWORK_PATH "/Applications/OBS.app/Contents/Frameworks"
    CACHE PATH "Path to OBS.app Frameworks directory")
  list(APPEND CMAKE_PREFIX_PATH "${OBS_FRAMEWORK_PATH}")
endif()

# 1st: CMake config package (Windows / macOS after OBS install)
find_package(libobs QUIET)

# 2nd: pkg-config (Linux with libobs-dev from OBS PPA)
if(NOT libobs_FOUND)
  find_package(PkgConfig QUIET)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(LIBOBS QUIET libobs)
    if(LIBOBS_FOUND)
      message(STATUS "Found libobs via pkg-config: ${LIBOBS_VERSION}")
      add_library(OBS::libobs INTERFACE IMPORTED)
      # OBS installs headers to /usr/include/obs/ — find_path with
      # PATH_SUFFIXES locates obs-module.h inside that subdirectory
      find_path(OBS_INCLUDE_DIR obs-module.h
        HINTS ${LIBOBS_INCLUDE_DIRS} /usr/include /usr/local/include
        PATH_SUFFIXES obs
        NO_DEFAULT_PATH
      )
      if(NOT OBS_INCLUDE_DIR)
        find_path(OBS_INCLUDE_DIR obs-module.h PATH_SUFFIXES obs)
      endif()
      if(OBS_INCLUDE_DIR)
        message(STATUS "OBS headers: ${OBS_INCLUDE_DIR}")
        target_include_directories(OBS::libobs INTERFACE "${OBS_INCLUDE_DIR}")
      else()
        message(WARNING "libobs found via pkg-config but obs-module.h not located")
      endif()
      target_link_libraries(OBS::libobs INTERFACE ${LIBOBS_LINK_LIBRARIES})
      set(libobs_FOUND TRUE)
    endif()
  endif()
endif()

# 3rd: header-only stub (local dev without OBS installed)
if(NOT libobs_FOUND)
  message(STATUS "libobs not found — using header-only stub (set OBS_INCLUDE_DIR manually)")
  set(OBS_INCLUDE_DIR "" CACHE PATH "Path to libobs headers")
  add_library(OBS::libobs INTERFACE IMPORTED)
  target_include_directories(OBS::libobs INTERFACE "${OBS_INCLUDE_DIR}")
endif()

# ─────────────────────────────────────────────────────────────────────────────
# RtMidi via FetchContent
# ─────────────────────────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
  rtmidi
  GIT_REPOSITORY https://github.com/thestk/rtmidi.git
  GIT_TAG        6.0.0
  GIT_SHALLOW    TRUE
)
set(RTMIDI_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(rtmidi)

# Force -fPIC on the rtmidi target directly — setting CMAKE_POSITION_INDEPENDENT_CODE
# in parent scope does not reliably propagate into FetchContent subprojects
set_target_properties(rtmidi PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Don't let cmake --install pull in RtMidi headers/cmake files — it's a
# private build dependency, not something users of the plugin need.
set_target_properties(rtmidi PROPERTIES EXPORT_NAME "rtmidi_internal")

# ─────────────────────────────────────────────────────────────────────────────
# Plugin target
# NOTE: plugin-support.c is intentionally excluded — OBS_DECLARE_MODULE() and
# OBS_MODULE_USE_DEFAULT_LOCALE() are already in plugin-main.cpp; having them
# in a second translation unit causes multiple-definition linker errors.
# ─────────────────────────────────────────────────────────────────────────────
set(PLUGIN_SOURCES
  src/plugin-main.cpp
  src/midi-engine.cpp
  src/sources/piano-source.cpp
  src/sources/drum-source.cpp
  src/sources/cc-source.cpp
  src/sources/dj-source.cpp
)

add_library(obs-midi-viz MODULE ${PLUGIN_SOURCES})

target_include_directories(obs-midi-viz PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(obs-midi-viz PRIVATE
  OBS::libobs
  rtmidi
)

if(WIN32)
  target_link_libraries(obs-midi-viz PRIVATE winmm)
elseif(APPLE)
  find_library(COREMIDI_LIB  CoreMIDI  REQUIRED)
  find_library(COREAUDIO_LIB CoreAudio REQUIRED)
  find_library(COREFOUNDATION_LIB CoreFoundation REQUIRED)
  target_link_libraries(obs-midi-viz PRIVATE
    ${COREMIDI_LIB} ${COREAUDIO_LIB} ${COREFOUNDATION_LIB})
else()
  target_link_libraries(obs-midi-viz PRIVATE asound pthread)
endif()

target_compile_definitions(obs-midi-viz PRIVATE
  PLUGIN_NAME="obs-midi-viz"
  PLUGIN_VERSION="${PROJECT_VERSION}"
)

# Strip the "lib" prefix on all platforms (obs-midi-viz.so / .dll not libobs-midi-viz.so).
set_target_properties(obs-midi-viz PROPERTIES PREFIX "")

# On macOS, OBS loads plugins as .plugin bundles (same as its own bundled plugins).
# cmake BUNDLE TRUE produces obs-midi-viz.plugin/Contents/MacOS/obs-midi-viz.
if(APPLE)
  set_target_properties(obs-midi-viz PROPERTIES
    BUNDLE           TRUE
    BUNDLE_EXTENSION plugin
  )
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Install — layout matches OBS's plugin search paths so the zip/tar can be
# dropped directly into the right place with no manual file shuffling.
#
#   Windows : extract zip  → C:\Program Files\obs-studio\
#             (obs-plugins\64bit\  +  data\obs-plugins\obs-midi-viz\)
#
#   macOS   : copy obs-midi-viz/ folder
#             → ~/Library/Application Support/obs-studio/plugins/
#             (obs-midi-viz/bin/  +  obs-midi-viz/data/)
#
#   Linux   : copy obs-midi-viz/ folder
#             → ~/.config/obs-studio/plugins/
#             (obs-midi-viz/bin/64bit/  +  obs-midi-viz/data/)
# ─────────────────────────────────────────────────────────────────────────────
if(WIN32)
  install(TARGETS obs-midi-viz
    LIBRARY DESTINATION "obs-plugins/64bit"
    COMPONENT obs-midi-viz
  )
  install(DIRECTORY data/
    DESTINATION "data/obs-plugins/obs-midi-viz"
    COMPONENT obs-midi-viz
  )
elseif(APPLE)
  # Install the .plugin bundle to the root — user extracts tar then copies
  # obs-midi-viz.plugin/ into ~/Library/Application Support/obs-studio/plugins/
  install(TARGETS obs-midi-viz
    LIBRARY DESTINATION "."
    COMPONENT obs-midi-viz
  )
  # Data lives inside the bundle where OBS expects it (Contents/Resources)
  install(DIRECTORY data/
    DESTINATION "obs-midi-viz.plugin/Contents/Resources"
    COMPONENT obs-midi-viz
  )
else()
  install(TARGETS obs-midi-viz
    LIBRARY DESTINATION "obs-midi-viz/bin/64bit"
    COMPONENT obs-midi-viz
  )
  install(DIRECTORY data/
    DESTINATION "obs-midi-viz/data"
    COMPONENT obs-midi-viz
  )
endif()
