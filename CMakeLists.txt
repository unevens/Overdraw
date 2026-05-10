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

# Universal arm64 + x86_64 on macOS so a single zip serves both Apple
# Silicon and Intel users. ON by default; flip to OFF
# (-DUNIVERSAL=OFF) for fast single-arch dev iteration when you don't
# need the x86_64 slice. Cost when ON: ~2x build time, ~2x binary size.
if(APPLE)
    option(UNIVERSAL "Build a universal arm64+x86_64 binary" ON)
endif()
if(APPLE AND UNIVERSAL)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)

    # Universal-build lipo shim.
    #
    # When building -arch arm64 -arch x86_64 with LTO on, clang compiles
    # each TU once per arch and internally invokes `lipo` to combine the
    # per-arch LLVM bitcode object files. Apple's bundled lipo
    # (CommandLineTools and /usr/bin/lipo) is too old to read modern LLVM
    # bitcode and fails with "Unknown attribute kind (105)". Homebrew llvm
    # ships `llvm-lipo` which can, but clang's driver looks up the literal
    # name `lipo` via getProgramPath, so we expose llvm-lipo under that
    # name in a build-dir-local toolchain dir and add it to clang's program
    # search path via -B. Same shape as the llvm-ar / llvm-ranlib
    # auto-config above.
    get_filename_component(_overdraw_clang_dir
        "${CMAKE_CXX_COMPILER}" DIRECTORY)
    if(EXISTS "${_overdraw_clang_dir}/llvm-lipo")
        set(_overdraw_lipo_shim_dir "${CMAKE_BINARY_DIR}/_lipo_shim")
        file(MAKE_DIRECTORY "${_overdraw_lipo_shim_dir}")
        if(NOT EXISTS "${_overdraw_lipo_shim_dir}/lipo")
            file(CREATE_LINK
                "${_overdraw_clang_dir}/llvm-lipo"
                "${_overdraw_lipo_shim_dir}/lipo"
                SYMBOLIC)
        endif()
        add_compile_options("-B${_overdraw_lipo_shim_dir}")
        add_link_options("-B${_overdraw_lipo_shim_dir}")
        message(STATUS
            "Overdraw: using ${_overdraw_lipo_shim_dir}/lipo -> "
            "llvm-lipo (required for LTO bitcode in universal builds).")
    else()
        message(FATAL_ERROR
            "llvm-lipo not found at ${_overdraw_clang_dir}/llvm-lipo. "
            "Apple's lipo cannot read LLVM 22 bitcode object files. "
            "Install Homebrew llvm (`brew install llvm`).")
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

# Release-zip staging + zipping.
#
# `cmake --build build --target package-zip` produces, in build/release-zip/:
#   Overdraw1.2_<platform>/      — AU bundle, VST3 bundle, legal/, README.txt
#   Overdraw1.2_<platform>.zip   — the same folder, zipped, ready to upload
# where <platform> is mac / win / linux depending on the build host.
#
# For a real release, configure with -DUNIVERSAL=ON so the bundles run on
# both Apple Silicon and Intel.
if(APPLE)
    set(_overdraw_platform_suffix "mac")
elseif(WIN32)
    set(_overdraw_platform_suffix "win")
elseif(UNIX)
    set(_overdraw_platform_suffix "linux")
else()
    set(_overdraw_platform_suffix "unknown")
endif()

set(_overdraw_zip_basename "Overdraw1.2_${_overdraw_platform_suffix}")
set(_overdraw_zip_stage
    "${CMAKE_BINARY_DIR}/release-zip/${_overdraw_zip_basename}")

add_custom_target(package-zip
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/release-zip"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_overdraw_zip_stage}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "$<TARGET_BUNDLE_DIR:Overdraw_AU>"
        "${_overdraw_zip_stage}/Overdraw 1.2.component"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "$<TARGET_BUNDLE_DIR:Overdraw_VST3>"
        "${_overdraw_zip_stage}/Overdraw 1.2.vst3"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_LIST_DIR}/legal"
        "${_overdraw_zip_stage}/legal"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_LIST_DIR}/README.txt"
        "${_overdraw_zip_stage}/README.txt"
    COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_BINARY_DIR}/release-zip"
        zip -rq "${_overdraw_zip_basename}.zip" "${_overdraw_zip_basename}"
    DEPENDS Overdraw_AU Overdraw_VST3
    COMMENT "Building ${_overdraw_zip_basename}.zip in ${CMAKE_BINARY_DIR}/release-zip"
    VERBATIM)
