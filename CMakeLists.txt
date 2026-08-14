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

# json5 - Dependency
# ==================
# Vendored under 3rdparty/ as a permanent part of the project (P-i-N/json5,
# MIT, header-only) - used by the UIComponent-based save/load format (see
# include/newui/serialization.h).

add_library(newui STATIC
    src/newui.cpp
    src/application.cpp
    src/frame.cpp
    "src/rootview.cpp"
    src/subview.cpp
    src/fontmanager.cpp
 "src/runloop.cpp" "include/newui/keyboard_constants.h" "include/newui/utils.h" "src/utils.cpp" "src/view.cpp" "include/newui/mouse_constants.h" "include/newui/color.h" "include/newui/color_constants.h" "include/newui/viewstyle.h" "include/newui/fontmanager.h" "include/newui/font.h" "include/newui/property.h" "src/property.cpp" "include/newui/custom_message_constants.h" "include/newui/animation.h" "src/animation.cpp" "include/newui/animationframe.h" "include/newui/curveinterpolation.h" "src/viewstyle.cpp" "include/newui/layout.h" "src/layout.cpp"
    "include/newui/uicomponent.h" "src/uicomponent.cpp"
    "include/newui/json5_helpers.h" "src/json5_helpers.cpp"
    "include/newui/serialization.h" "src/serialization.cpp"
    "include/newui/bundle.h" "src/bundle.cpp"
    "include/newui/reflection.h" "src/reflection.cpp"
    "include/newui/cursor.h" "src/cursor.cpp"
    "include/newui/graphics.h" "src/graphics.cpp"
    "include/newui/menus.h" "src/menus.cpp"
    "include/newui/tabcontrol.h" "src/tabcontrol.cpp"
    "include/newui/dialogs.h" "src/dialogs.cpp"
    "include/newui/uicolormanager.h" "src/uicolormanager.cpp")

add_dependencies(newui newui_version)

target_include_directories(newui PUBLIC
    include
    ${CMAKE_CURRENT_BINARY_DIR}/include
    3rdparty/json5/include
)

# Ole32/Uuid: COM (CoCreateInstance/CoInitializeEx and the CLSID_FileOpenDialog/
# CLSID_FileSaveDialog symbol storage) for dialogs.h's Common Item Dialog
# (IFileDialog) support. Shell32: SHCreateItemFromParsingName, used to turn
# an initial-directory path into the IShellItem SetFolder() wants. Advapi32:
# RegGetValueW, used by UIColorManager::isDarkMode() (uicolormanager.cpp)
# to read HKCU's AppsUseLightTheme value. Dwmapi: DwmGetColorizationColor,
# used by UIColorManager::colorFor() for the real Windows accent color.
target_link_libraries(newui PUBLIC blend2d::blend2d Comctl32.lib Uxtheme.lib Ole32.lib Uuid.lib Shell32.lib Advapi32.lib Dwmapi.lib)

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

add_executable(newui_app
    src/main.cpp
 "src/runloop.cpp" "include/newui/keyboard_constants.h" "include/newui/utils.h" "src/utils.cpp" "src/view.cpp" "include/newui/mouse_constants.h" "include/newui/color_constants.h" "include/newui/custom_message_constants.h" "src/viewstyle.cpp")

target_link_libraries(newui_app PRIVATE newui)

add_subdirectory(examples)

# GoogleTest - Testing Framework
# ==============================
# Vendored under 3rdparty/ as a permanent part of the project (release v1.17.0).

set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

add_subdirectory(3rdparty/googletest)

enable_testing()
add_subdirectory(unittests)
