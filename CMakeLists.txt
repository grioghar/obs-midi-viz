cmake_minimum_required(VERSION 3.24)

# ─────────────────────────────────────────────────────────────────────────────
# Project metadata (bump version here → flows through to plugin + CI release)
# ─────────────────────────────────────────────────────────────────────────────
project(obs-midi-viz
  VERSION 0.1.0
  DESCRIPTION "MIDI visualizer plugin for OBS Studio"
  LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clangd / IDE tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─────────────────────────────────────────────────────────────────────────────
# CMake module path — our custom finders live in cmake/
# ─────────────────────────────────────────────────────────────────────────────
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ─────────────────────────────────────────────────────────────────────────────
# Platform-specific OBS discovery
# On Windows + macOS, OBS ships CMake config packages.
# On Linux, libobs-dev provides pkg-config; we also support header-only builds.
# ─────────────────────────────────────────────────────────────────────────────
if(APPLE)
  # OBS.app bundles its framework — look there first
  set(OBS_FRAMEWORK_PATH "/Applications/OBS.app/Contents/Frameworks"
    CACHE PATH "Path to OBS.app Frameworks directory")
  list(APPEND CMAKE_PREFIX_PATH "${OBS_FRAMEWORK_PATH}")
endif()

find_package(libobs QUIET)
if(NOT libobs_FOUND)
  # Fallback: header-only target (useful for CI before OBS libs are present)
  message(STATUS "libobs CMake package not found — using header-only stub")
  set(OBS_INCLUDE_DIR "" CACHE PATH "Path to libobs headers (fallback)")
  add_library(OBS::libobs INTERFACE IMPORTED)
  target_include_directories(OBS::libobs INTERFACE "${OBS_INCLUDE_DIR}")
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Qt6 — required for Properties panels
# ─────────────────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
set_target_properties(Qt6::Core Qt6::Widgets PROPERTIES
  INTERFACE_COMPILE_FEATURES "cxx_std_17")

# ─────────────────────────────────────────────────────────────────────────────
# RtMidi — cross-platform MIDI I/O (WinMM / CoreMIDI / ALSA)
# Pulled at configure time via FetchContent; no submodule needed.
# ─────────────────────────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
  rtmidi
  GIT_REPOSITORY https://github.com/thestk/rtmidi.git
  GIT_TAG        6.0.0
  GIT_SHALLOW    TRUE
)
set(RTMIDI_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS    OFF CACHE BOOL "" FORCE)  # link statically
FetchContent_MakeAvailable(rtmidi)

# ─────────────────────────────────────────────────────────────────────────────
# Plugin target
# ─────────────────────────────────────────────────────────────────────────────
set(PLUGIN_SOURCES
  src/plugin-main.cpp
  src/plugin-support.c
  src/midi-engine.cpp
  src/sources/piano-source.cpp
  src/sources/drum-source.cpp
  src/sources/cc-source.cpp
)

add_library(obs-midi-viz MODULE ${PLUGIN_SOURCES})

target_include_directories(obs-midi-viz PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(obs-midi-viz PRIVATE
  OBS::libobs
  Qt6::Core
  Qt6::Widgets
  rtmidi
)

# Platform-specific MIDI backend linkage (RtMidi needs these)
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

# Tell OBS the plugin API version we were built against
target_compile_definitions(obs-midi-viz PRIVATE
  PLUGIN_NAME="obs-midi-viz"
  PLUGIN_VERSION="${PROJECT_VERSION}"
)

# macOS: plugin output suffix is .plugin; other platforms .so / .dll
if(APPLE)
  set_target_properties(obs-midi-viz PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION plugin
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in"
  )
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Install rules (cmake --install → consumed by CI packaging step)
# ─────────────────────────────────────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS obs-midi-viz
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/obs-plugins"
  BUNDLE  DESTINATION "obs-plugins"
)

install(DIRECTORY data/
  DESTINATION "${CMAKE_INSTALL_DATADIR}/obs/obs-plugins/obs-midi-viz"
)
