cmake_minimum_required(VERSION 3.24)
project(newui VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Version header (Major.Minor.Release.Build)
# ============================================
# Build advances by 1 every time the project is *built*, not just
# configured - so this can't be a plain configure_file() (which only runs
# once, at `cmake` configure time). Instead newui_version is a custom
# target with no declared OUTPUT, so every generator (Ninja, MSBuild, Make)
# treats it as always out-of-date and reruns it on every build.
#
# Build rolls over at 9999 into Release, Release rolls over at 999 into
# Minor, Minor rolls over at 99 into Major (see GenerateVersion.cmake) -
# so Major/Minor/Release aren't fixed at configure time either; they're
# state that can change purely from accumulated builds. That state is
# persisted in VERSION.state at the repo root - deliberately a *tracked*
# source-tree file, not a build-tree one, so version progression survives
# a `build/` wipe-and-reconfigure. project(VERSION) above only seeds
# VERSION.state the very first time it's created.
add_custom_target(newui_version ALL
    COMMAND ${CMAKE_COMMAND}
        -DINITIAL_MAJOR=${newui_VERSION_MAJOR}
        -DINITIAL_MINOR=${newui_VERSION_MINOR}
        -DINITIAL_RELEASE=${newui_VERSION_PATCH}
        -DSTATE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/VERSION.state
        -DIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/include/newui/version.h.in
        -DOUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/include/newui/version.h
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateVersion.cmake
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/include/newui/version.h
    COMMENT "Generating newui/version.h (incrementing build number)"
    VERBATIM
)

# Blend2D - Dependency
# ====================
# Vendored under 3rdparty/ as a permanent part of the project (commit
# 6dbc2cefbc996379e07104e34519a440b49b15d7). asmjit is Blend2D's required
# JIT dependency, vendored alongside it (commit
# 0bd5787b54b575ed94bf32ac452153b34385c514); Blend2D's own CMakeLists.txt
# add_subdirectory()s whatever ASMJIT_DIR points to.

set(BLEND2D_STATIC ON CACHE BOOL "" FORCE)
set(ASMJIT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/asmjit CACHE PATH "" FORCE)

add_subdirectory(3rdparty/blend2d)

# zlib - Dependency
# =================
# Vendored under 3rdparty/ as a permanent part of the project (madler/zlib
# v1.3.1, zlib license, C) - inflate and deflate. Used by the manyrows demo to read
# .xlsx (ZIP) files and by the sketch demo for .svgz (gzip) files; linked by those
# targets alone, not by the newui library.
add_subdirectory(3rdparty/zlib)

# WIL - Dependency
# =================
# Vendored under 3rdparty/ as a permanent part of the project (microsoft/wil,
# tag v1.0.260126.7, MIT, header-only) - RAII wrappers (wil::unique_hglobal,
# wil::com_ptr, etc.) used by the OLE drag-and-drop implementation (see
# include/newui/dragndrop.h).

# json5 - Dependency
# ==================
# Vendored under 3rdparty/ as a permanent part of the project (P-i-N/json5,
# MIT, header-only) - used by the reflection-driven save/load format (see
# include/newui/reflection.h/reflectionio.h).

# svgandme - Dependency
# =====================
# Vendored under 3rdparty/ as a permanent part of the project (wiladams/
# svgandme, commit 2bacca9c4a75de90481c28ac88d58d6c01d4f939, MIT,
# header-only) - only the svg/ subdirectory (the reusable SVG parser +
# Blend2D renderer, namespace waavs); the rest of upstream (app/, testy/,
# ...) is Win32/GDI-specific example-app scaffolding we don't need. Pinned
# to an exact commit rather than tracking upstream - it's active but
# pre-1.0 (only tag is v0.1.0-alpha) and its own example projects have
# already drifted out of sync with its headers once. Used by
# include/newui/svgimage.h/src/svgimage.cpp only - kept out of
# include/newui/graphics.h so its ~150 headers don't end up as a transitive
# include of every graphics.h consumer.

# reflectgen build integration - see cmake/ReflectGen.cmake's own header
# comment for the full two-stage (configure-time discovery, build-time
# generation) design. include()'d before add_library(newui ...) below
# purely so newui_add_reflectgen_output(newui) reads top-to-bottom next to
# the target it's registering generated output for; the discovery/
# execute_process() work this include() triggers doesn't depend on
# `newui` existing yet either way.
include(cmake/ReflectGen.cmake)

# reflectgen_sizeof_probe (tools/reflectgen/src) - built before
# newui_add_reflectgen_output(newui) below so its DEPENDS on the probe
# target resolves - see reflectgen.py's --sizeof-probe / that target's own
# CMakeLists.txt for what it's for.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_subdirectory(tools/reflectgen/src)
endif()

# Shared core: compiles on every platform. A file moves here from the Windows list below once it no longer
# needs <windows.h> (or its platform part has moved to src/win32, src/macos, ...).
add_library(newui STATIC
    src/newui.cpp
    "include/newui/pen.h" "src/pen.cpp"
    "include/newui/presentsurface.h"
    src/subview.cpp
    "include/newui/keyboard_constants.h"
    "include/newui/utils.h" "src/stringutils.cpp"
    "include/newui/displayunit.h" "src/displayunit.cpp"
    "src/compositing.cpp"
    "include/newui/mouse_constants.h"
    "include/newui/color.h" "src/color.cpp"
    "include/newui/color_constants.h"
    "include/newui/viewstyle.h" "src/viewstyle.cpp" "include/newui/fontmanager.h"
    "src/view.cpp"
    "include/newui/font.h"
    "include/newui/property.h"
    "src/property.cpp"
    "include/newui/custom_message_constants.h"
    "include/newui/animation.h"
    "src/animation.cpp"
    "include/newui/animationframe.h"
    "include/newui/curveinterpolation.h"
    "include/newui/layout.h"
    "src/layout.cpp"
    "include/newui/bundle.h"
    "include/newui/viewpath.h" "src/viewpath.cpp"
    "include/newui/namemanager.h" "src/namemanager.cpp"
    "include/newui/filewatcher.h"
    "include/newui/reflection.h" "src/reflection.cpp"
    "include/newui/cursor.h" "src/cursor.cpp"
    "include/newui/graphics.h" "src/graphics.cpp"
    "include/newui/menus.h"
    "include/newui/dialogs.h"
    "include/newui/popupframe.h"
    "include/newui/uicolormanager.h"
    "include/newui/models.h" "src/models.cpp"
    "include/newui/items.h" "src/items.cpp"
    "include/newui/controllers.h"
    "include/newui/groupedlist.h" "src/groupedlist.cpp"
    "include/newui/rootcontroller.h"
    "include/newui/controls.h"
    "include/newui/shapes.h" "src/shapes.cpp"
    "include/newui/themedata.h"
    "include/newui/reflectionio.h"
    "include/newui/text.h"
    "include/newui/piecetree.h" "src/piecetree.cpp"
    "include/newui/texthistory.h" "src/texthistory.cpp"
    "include/newui/textstyle.h" "src/textstyle.cpp"
    "include/newui/textfolding.h"
    "include/newui/overlay.h" "src/overlay.cpp"
    "include/newui/underlay.h" "src/underlay.cpp"
    "include/newui/popuptool.h"
    "include/newui/action.h"
    "include/newui/command.h" "src/command.cpp"
    "include/newui/clipboardmgr.h"
    "include/newui/dibimage.h"
    "include/newui/dragndrop.h"
    "include/newui/component.h"
    "include/newui/componentsettings.h" "src/componentsettings.cpp"
    "include/newui/binding.h" "src/binding.cpp"
    "include/newui/undostack.h" "src/undostack.cpp"
    "include/newui/viewbuilder.h"
    "include/newui/frameproxy.h" "src/frameproxy.cpp"
    "include/newui/rootviewproxy.h" "src/rootviewproxy.cpp"
    "include/newui/splitter.h" "src/splitter.cpp"
    "include/newui/toolframe.h"
    "include/newui/segmentedcontrol.h" "src/segmentedcontrol.cpp"
    "include/newui/svgimage.h" "src/svgimage.cpp"
    "include/newui/uiinputmanager.h"
    ${REFLECTION_GENERATED_CPP})

if(WIN32)
    target_sources(newui PRIVATE
        src/application.cpp
        src/frame.cpp
        "src/rootview.cpp"
        "include/newui/gdipresentsurface.h" "src/gdipresentsurface.cpp"
        "include/newui/dxgipresentsurface.h" "src/dxgipresentsurface.cpp"
        "src/presentsurface.cpp"
        src/fontmanager.cpp
        "src/runloop.cpp"
        "src/utils.cpp"
        "src/win32/view_win32.cpp"
        "src/win32/displaymetrics_win32.cpp"
        "src/win32/themedstyles.cpp"
        "src/bundle.cpp"
        "src/filewatcher.cpp"
        "src/win32/cursor_win32.cpp"
        "src/win32/graphics_win32.cpp"
        "src/menus.cpp"
        "src/dialogs.cpp"
        "src/popupframe.cpp"
        "src/uicolormanager.cpp"
        "src/controllers.cpp"
        "src/controls.cpp"
        "src/themedata.cpp"
        "src/text.cpp"
        "src/textfolding.cpp"
        "src/popuptool.cpp"
        "src/clipboardmgr.cpp"
        "src/dibimage.cpp"
        "src/dragndrop.cpp"
        "include/newui/win32/enum_format_etc.h" "include/newui/win32/dragndrop_com.h" "src/enum_format_etc.cpp"
        "src/toolframe.cpp"
        "src/uiinputmanager.cpp")
endif()

# Placeholders for what has no Apple implementation yet.
if(APPLE)
    target_sources(newui PRIVATE
        "src/apple/cursor_stub.cpp"
        "src/apple/view_stub.cpp")
endif()

add_dependencies(newui newui_version)

# Adds this build's combined reflectgen-generated registration .cpp (see
# cmake/ReflectGen.cmake) to newui's own sources - scan-all by default
# (NEWUI_REFLECTGEN_REQUIRE_MARKER OFF), so this reflects every real class
# under include/newui unless it opts out. Nothing calls the generated
# register_*Reflection() functions yet - examples/reflection2.cpp still
# registers its own small example classes by hand to demonstrate that path
# deliberately - see reflection.md's "Automatic CMake integration".
newui_add_reflectgen_output(newui)

target_include_directories(newui PUBLIC
    include
    ${CMAKE_CURRENT_BINARY_DIR}/include
    3rdparty/json5/include
    3rdparty/wil/include
)

# PRIVATE, not PUBLIC like json5/wil above - svgandme is only ever included
# from src/svgimage.cpp, never from a public newui/ header, so there's no
# reason for it to leak into consumers' include paths.
target_include_directories(newui PRIVATE
    3rdparty/svgandme/svg
)

# Ole32/Uuid: COM (CoCreateInstance/CoInitializeEx and the CLSID_FileOpenDialog/
# CLSID_FileSaveDialog symbol storage) for dialogs.h's Common Item Dialog
# (IFileDialog) support. Shell32: SHCreateItemFromParsingName, used to turn
# an initial-directory path into the IShellItem SetFolder() wants. Advapi32:
# RegGetValueW, used by UIColorManager::isDarkMode() (uicolormanager.cpp)
# to read HKCU's AppsUseLightTheme value. Dwmapi: DwmGetColorizationColor,
# used by UIColorManager::colorFor() for the real Windows accent color.
# Shlwapi: SHCreateMemStream, used by VirtualFileDataObject (dragndrop.cpp)
# to hand Explorer an in-memory CFSTR_FILECONTENTS IStream per virtual file.
target_link_libraries(newui PUBLIC blend2d::blend2d)
if(WIN32)
    target_link_libraries(newui PUBLIC Comctl32.lib Uxtheme.lib Ole32.lib Uuid.lib Shell32.lib Advapi32.lib Dwmapi.lib Shlwapi.lib d3d11.lib dxgi.lib dxguid.lib)
endif()

# Precompiled header
# ===================
# Only stable, externally-owned headers live in src/newuicommon_precomp.h
# (stdlib, vendored blend2d, and newui/newui.h itself, which hasn't changed
# since the initial commit) - see that file's header comment for why the
# actively-developed local headers (view.h, subview.h, rootview.h, etc.)
# are deliberately left out of it. PRIVATE so PCH usage doesn't leak into
# targets that link against newui.
# The precompiled header pulls in <windows.h>, so it is Windows-only.
if(WIN32)
    target_precompile_headers(newui PRIVATE src/newuicommon_precomp.h)
endif()

# Resource bundles
# =================
# Copies resourceSourceDir next to target's built .exe as "Resources/" -
# see include/newui/bundle.h, which only ever looks in
# <executable directory>/Resources (no dev-vs-deployed search-path
# fallback), so this keeps a dev build and a deployed one identical in
# structure. Call this for any target that has its own resourceSourceDir
# (e.g. newui_add_resources(newui_app "${CMAKE_CURRENT_SOURCE_DIR}/Resources")) -
# not called anywhere by default, since no target has resources of its own yet.
function(newui_add_resources target resourceSourceDir)
    if(EXISTS "${resourceSourceDir}")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${resourceSourceDir}" "$<TARGET_FILE_DIR:${target}>/Resources"
            COMMENT "Copying resources for ${target}")
    endif()
endfunction()

# Copies infoJsonPath next to target's built .exe as "Info.json" - see
# include/newui/bundle.h's Bundle::appName()/appVersion(), which reads it
# from <executable directory>/Info.json the same way newui_add_resources()
# above expects Resources/ there. Independent of that call - a target may
# want one, the other, both (e.g. via tools/bundlegen, which scaffolds
# both an Info.json and a Resources/ tree together), or neither - not
# called anywhere by default, since no target has an Info.json of its own
# yet.
function(newui_add_bundle_info target infoJsonPath)
    if(EXISTS "${infoJsonPath}")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                "${infoJsonPath}" "$<TARGET_FILE_DIR:${target}>/Info.json"
            COMMENT "Copying Info.json for ${target}")
    endif()
endfunction()

# The app, examples, demos, automation and unit tests are still Windows-only (TODO(macport): Step 4.4 brings the
# platform-neutral tests over).
if(WIN32)

add_executable(newui_app
    src/main.cpp
 "src/runloop.cpp" "include/newui/keyboard_constants.h" "include/newui/utils.h" "src/utils.cpp" "src/view.cpp" "include/newui/mouse_constants.h" "include/newui/color_constants.h" "include/newui/custom_message_constants.h" "src/viewstyle.cpp" "include/newui/reflectionio.h")

target_link_libraries(newui_app PRIVATE newui)

add_subdirectory(examples)
add_subdirectory(demos)

endif() # WIN32

# GoogleTest - Testing Framework
# ==============================
# Vendored under 3rdparty/ as a permanent part of the project (release v1.17.0).

set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

add_subdirectory(3rdparty/googletest)

enable_testing()
if(WIN32)
# Opt-in GUI-test automation (see include/newui/automation.h): only test builds link this.
add_library(newui_automation STATIC include/newui/automation.h src/automation.cpp)
target_link_libraries(newui_automation PUBLIC newui)
endif() # WIN32

add_subdirectory(unittests)
