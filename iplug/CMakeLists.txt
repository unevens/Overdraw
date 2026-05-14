cmake_minimum_required(VERSION 3.25)
project(Overdraw VERSION 0.1.0)

if(NOT DEFINED IPLUG2_DIR)
  set(IPLUG2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../iPlug2" CACHE PATH "iPlug2 root directory")
endif()

include(${IPLUG2_DIR}/iPlug2.cmake)

find_package(iPlug2 REQUIRED)

# Shared header-only helpers for Curvessor + Overdraw iPlug2 ports
# (palette, light-marker meter control, layout helpers, etc.). Default
# is the in-tree submodule at <plugin_root>/iplug-helpers; the
# IPLUG_HELPERS_DIR cache var lets a dev with a sibling-tree layout
# (e.g. ~/dev/iplug-helpers/) point at that copy instead.
if(NOT DEFINED IPLUG_HELPERS_DIR)
  set(IPLUG_HELPERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../iplug-helpers"
      CACHE PATH "Path to iplug-helpers")
endif()
if(NOT TARGET iplug-helpers)
  add_subdirectory(${IPLUG_HELPERS_DIR} iplug-helpers)
endif()

# Roots of the JUCE-free pieces of Overdraw that we vendor in here. The
# JUCE-bound files in Source/PluginProcessor.{h,cpp}, Source/PluginEditor.{h,cpp}
# and the whole juicy/ tree are deliberately NOT compiled — Overdraw.cpp
# reimplements them against iPlug2.
set(OVERDRAW_ROOT  "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(AUDIO_DSP_DIR  "${OVERDRAW_ROOT}/audio-dsp")
set(OVERSIMPLE_DIR "${OVERDRAW_ROOT}/oversimple")
set(OVERDRAW_SRC   "${OVERDRAW_ROOT}/Source")
# Only juicy/SplineEditorDsp.hpp (a JUCE-free scalar spline evaluator) is
# used from here. The rest of juicy/ depends on JUCE and must NOT be
# included from the iPlug2 build.
set(JUICY_DIR      "${OVERDRAW_ROOT}/juicy")

iplug_add_plugin(${PROJECT_NAME}
  SOURCES
    Overdraw.cpp
    Overdraw.h
    resources/resource.h
    # Overdraw JUCE-free DSP TU. Heavy SIMD/spline NEON codegen lives here;
    # the Homebrew Clang >= 22 requirement in the top-level CMakeLists.txt
    # exists because of this file.
    ${OVERDRAW_SRC}/OverdrawDsp.cpp
    ${OVERDRAW_SRC}/OverdrawDsp.h
    # oversimple oversampler
    ${OVERSIMPLE_DIR}/oversimple/FirOversampling.cpp
    ${OVERSIMPLE_DIR}/r8brain/pffft.cpp
    ${OVERSIMPLE_DIR}/r8brain/r8bbase.cpp
    ${OVERSIMPLE_DIR}/r8brain/pffft_double/pffft_double.c
  RESOURCES
    resources/fonts/Roboto-Regular.ttf
    resources/fonts/Roboto-Bold.ttf
  FORMATS
    MINIMAL_PLUGINS
)

# vectorclass's runtime CPU-detect TU is x86-only; on arm64 avec routes
# through its own NEON shims and never references it.
set(_overdraw_extra_sources "")
if(NOT (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64"
        OR (APPLE AND CMAKE_OSX_ARCHITECTURES MATCHES "arm64")))
  list(APPEND _overdraw_extra_sources
    ${OVERSIMPLE_DIR}/avec/vectorclass/instrset_detect.cpp)
endif()

# Apply Overdraw's DSP include dirs and compile defs to every format target
# that iplug_add_plugin produced.
set(_overdraw_format_targets
  ${PROJECT_NAME}-vst3
  ${PROJECT_NAME}-au
  ${PROJECT_NAME}-clap)

foreach(_t IN LISTS _overdraw_format_targets)
  if(TARGET ${_t})
    if(_overdraw_extra_sources)
      target_sources(${_t} PRIVATE ${_overdraw_extra_sources})
    endif()
    target_include_directories(${_t} PRIVATE
      ${OVERDRAW_SRC}
      ${AUDIO_DSP_DIR}
      ${OVERSIMPLE_DIR}
      ${OVERSIMPLE_DIR}/avec
      ${OVERSIMPLE_DIR}/avec/vectorclass
      ${OVERSIMPLE_DIR}/r8brain
      ${OVERSIMPLE_DIR}/hiir
      ${JUICY_DIR})
    target_compile_definitions(${_t} PRIVATE
      PFFFT_ENABLE_DOUBLE=1
      R8B_PFFFT_DOUBLE=1
      NOMINMAX=1)
    target_link_libraries(${_t} PRIVATE iplug-helpers)
    # iPlug2's FindiPlug2.cmake forces CXX_STANDARD 17 on every format target
    # via iplug_configure_target; override here so Overdraw's C++20 DSP code
    # compiles (and the framework code along with it).
    set_target_properties(${_t} PROPERTIES
      CXX_STANDARD 20
      CXX_STANDARD_REQUIRED ON
      CXX_EXTENSIONS OFF)
  endif()
endforeach()
