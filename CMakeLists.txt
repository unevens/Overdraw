cmake_minimum_required(VERSION 3.22)
project(Overdraw VERSION 1.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# macOS arm64 toolchain preflight + auto-config.
#
# Apple Clang (Xcode 17, CLT 21) and pre-22 upstream Clang have a
# NEON-intrinsic codegen bug that triggers a frontend bus error in
# Source/OverdrawDsp.cpp (the heavy SIMD spline TU). The fix shipped in
# upstream LLVM 22, so we require Homebrew Clang >= 22 here. Verified
# working: Homebrew clang 22.1.4 at -j8 with LTO enabled.
#
# We also auto-derive llvm-ar / llvm-ranlib from the chosen compiler's
# toolchain dir. LTO is on (juce_recommended_lto_flags), so the compiler
# emits LLVM bitcode object files. Apple's /usr/bin/ar can't archive those
# and the link step fails with "file too small to be an archive" if you
# leave CMake's auto-detected /usr/bin/ar in place.
if(APPLE AND (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64"
              OR CMAKE_OSX_ARCHITECTURES MATCHES "arm64"))
    if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang"
       OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
           AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 22))
        set(_overdraw_brew_clang "")
        foreach(_p /opt/homebrew/opt/llvm/bin/clang++
                   /usr/local/opt/llvm/bin/clang++)
            if(EXISTS "${_p}")
                set(_overdraw_brew_clang "${_p}")
                break()
            endif()
        endforeach()
        if(_overdraw_brew_clang)
            string(REPLACE "++" "" _overdraw_brew_cc "${_overdraw_brew_clang}")
            message(FATAL_ERROR
                "Overdraw on macOS arm64 requires Homebrew Clang >= 22 "
                "(detected: ${CMAKE_CXX_COMPILER_ID} "
                "${CMAKE_CXX_COMPILER_VERSION}). Apple Clang and pre-22 "
                "upstream Clang hit a NEON codegen bus error in "
                "Source/OverdrawDsp.cpp. You already have a suitable "
                "compiler installed; reconfigure with:\n"
                "  rm -rf ${CMAKE_BINARY_DIR}\n"
                "  cmake -S ${CMAKE_SOURCE_DIR} -B ${CMAKE_BINARY_DIR} \\\n"
                "    -DCMAKE_C_COMPILER=${_overdraw_brew_cc} \\\n"
                "    -DCMAKE_CXX_COMPILER=${_overdraw_brew_clang}")
        else()
            message(FATAL_ERROR
                "Overdraw on macOS arm64 requires Homebrew Clang >= 22 "
                "(detected: ${CMAKE_CXX_COMPILER_ID} "
                "${CMAKE_CXX_COMPILER_VERSION}). Apple Clang and pre-22 "
                "upstream Clang hit a NEON codegen bus error in "
                "Source/OverdrawDsp.cpp. Install with:\n"
                "  brew install llvm\n"
                "and reconfigure with:\n"
                "  rm -rf ${CMAKE_BINARY_DIR}\n"
                "  cmake -S ${CMAKE_SOURCE_DIR} -B ${CMAKE_BINARY_DIR} \\\n"
                "    -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \\\n"
                "    -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++")
        endif()
    endif()

    get_filename_component(_overdraw_clang_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    if(EXISTS "${_overdraw_clang_dir}/llvm-ar"
       AND EXISTS "${_overdraw_clang_dir}/llvm-ranlib")
        set(CMAKE_AR     "${_overdraw_clang_dir}/llvm-ar"     CACHE FILEPATH "" FORCE)
        set(CMAKE_RANLIB "${_overdraw_clang_dir}/llvm-ranlib" CACHE FILEPATH "" FORCE)
        set(CMAKE_AR     "${_overdraw_clang_dir}/llvm-ar")
        set(CMAKE_RANLIB "${_overdraw_clang_dir}/llvm-ranlib")
        message(STATUS
            "Overdraw: using ${CMAKE_AR} / ${CMAKE_RANLIB} (required for LTO bitcode archives).")
    else()
        message(FATAL_ERROR
            "Could not find llvm-ar / llvm-ranlib next to the chosen C++ "
            "compiler (${CMAKE_CXX_COMPILER}). LTO is enabled, so the "
            "compiler emits LLVM bitcode object files that Apple's "
            "/usr/bin/ar cannot archive. Either install a self-contained "
            "homebrew llvm (`brew install llvm`) or, if your toolchain "
            "stores them elsewhere, pass them explicitly:\n"
            "  -DCMAKE_AR=<path/to/llvm-ar> -DCMAKE_RANLIB=<path/to/llvm-ranlib>")
    endif()
endif()

# Apple Silicon native; Universal binary support is opt-in via -DUNIVERSAL=ON.
# For release zips you almost certainly want -DUNIVERSAL=ON so end users on
# both Apple Silicon and Intel can use the same bundles.
if(APPLE)
    option(UNIVERSAL "Build a universal arm64+x86_64 binary" OFF)
    if(UNIVERSAL)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    endif()
endif()

# After-build install into the user's macOS plug-in folders.
# ON by default so the dev workflow is build → reload in DAW with no manual
# step. Turn OFF (-DINSTALL_TO_USER_PLUGINS=OFF) for CI or release builds, or
# any time you don't want the build to touch ~/Library/Audio/Plug-Ins/*.
option(INSTALL_TO_USER_PLUGINS
    "After build, copy AU/VST3 to ~/Library/Audio/Plug-Ins/* (macOS)" ON)

set(JUCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../JUCE" CACHE PATH "Path to JUCE")
add_subdirectory(${JUCE_DIR} JUCE EXCLUDE_FROM_ALL)

juce_add_plugin(Overdraw
    PRODUCT_NAME              "Overdraw 1.2"
    COMPANY_NAME              "unevens"
    COMPANY_COPYRIGHT         "Copyright Dario Mambro 2020-2026"
    COMPANY_WEBSITE           "https://www.unevens.net"
    COMPANY_EMAIL             "hi@unevens.net"
    BUNDLE_ID                 "net.unevens.Overdraw"
    PLUGIN_MANUFACTURER_CODE  UnEv
    PLUGIN_CODE               Odrw
    FORMATS                   AU VST3 Standalone
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   ${INSTALL_TO_USER_PLUGINS})

juce_generate_juce_header(Overdraw)

juce_add_binary_data(OverdrawBinaryData
    SOURCES ${CMAKE_CURRENT_LIST_DIR}/Images/background.png)

target_sources(Overdraw PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Processing.cpp
    Source/OverdrawDsp.cpp)

target_sources(Overdraw PRIVATE

    juicy/GainVuMeter.cpp
    juicy/SimpleLookAndFeel.cpp
    juicy/SplineEditor.cpp
    juicy/SplineParameters.cpp

    oversimple/oversimple/FirOversampling.cpp
    oversimple/r8brain/pffft.cpp
    oversimple/r8brain/r8bbase.cpp
    oversimple/r8brain/pffft_double/pffft_double.c)

if(NOT (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64"
        OR (APPLE AND CMAKE_OSX_ARCHITECTURES MATCHES "arm64")))
    target_sources(Overdraw PRIVATE
        oversimple/avec/vectorclass/instrset_detect.cpp)
endif()

target_include_directories(Overdraw PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/Source
    ${CMAKE_CURRENT_LIST_DIR}/audio-dsp
    ${CMAKE_CURRENT_LIST_DIR}/juicy
    ${CMAKE_CURRENT_LIST_DIR}/oversimple
    ${CMAKE_CURRENT_LIST_DIR}/oversimple/avec
    ${CMAKE_CURRENT_LIST_DIR}/oversimple/avec/vectorclass
    ${CMAKE_CURRENT_LIST_DIR}/oversimple/r8brain
    ${CMAKE_CURRENT_LIST_DIR}/oversimple/hiir)

target_compile_definitions(Overdraw PUBLIC
    PFFFT_ENABLE_DOUBLE=1
    R8B_PFFFT_DOUBLE=1
    NOMINMAX=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(Overdraw
    PRIVATE
        OverdrawBinaryData
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_cryptography
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_opengl
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Release-zip staging.
#
# `cmake --build build --target package-zip` populates
# build/release-zip/Overdraw 1.2 (macOS)/ with everything end users need:
# the AU bundle, the VST3 bundle, the legal/ folder, and README.txt with
# install + xattr instructions. Right-click that folder in Finder and
# "Compress" to produce the distributable zip.
#
# For a real release, configure with -DUNIVERSAL=ON so the bundles run on
# both Apple Silicon and Intel.
set(_overdraw_zip_dir
    "${CMAKE_BINARY_DIR}/release-zip/Overdraw 1.2 (macOS)")

add_custom_target(package-zip
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${_overdraw_zip_dir}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_overdraw_zip_dir}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "$<TARGET_BUNDLE_DIR:Overdraw_AU>"
        "${_overdraw_zip_dir}/Overdraw 1.2.component"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "$<TARGET_BUNDLE_DIR:Overdraw_VST3>"
        "${_overdraw_zip_dir}/Overdraw 1.2.vst3"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_LIST_DIR}/legal"
        "${_overdraw_zip_dir}/legal"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_LIST_DIR}/README.txt"
        "${_overdraw_zip_dir}/README.txt"
    DEPENDS Overdraw_AU Overdraw_VST3
    COMMENT "Staging release zip contents at ${_overdraw_zip_dir}"
    VERBATIM)
