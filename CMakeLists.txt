cmake_minimum_required(VERSION 3.24)
project(newui VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

configure_file(
    include/newui/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/newui/version.h
    @ONLY
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

add_library(newui STATIC
    src/newui.cpp
    src/application.cpp
    src/frame.cpp
    "src/rootview.cpp"
    src/subview.cpp
    src/fontmanager.cpp
 "src/runloop.cpp" "include/newui/keyboard_constants.h" "include/newui/utils.h" "src/utils.cpp" "src/view.cpp" "include/newui/mouse_constants.h" "include/newui/color.h" "include/newui/color_constants.h" "include/newui/viewstyle.h" "include/newui/fontmanager.h" "include/newui/font.h" "include/newui/property.h" "src/property.cpp" "include/newui/custom_message_constants.h" "include/newui/animation.h" "src/animation.cpp" "include/newui/animationframe.h" "include/newui/curveinterpolation.h" "src/viewstyle.cpp")

target_include_directories(newui PUBLIC
    include
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

target_link_libraries(newui PUBLIC blend2d::blend2d Comctl32.lib)

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
