# sketch - vector editor demo (see ../PLAN.md).

add_executable(sketch
    main.cpp
    canvasview.h canvasview.cpp
    gradientbar.h gradientbar.cpp
    swatchgrid.h swatchgrid.cpp
    pagepreview.h pagepreview.cpp
)

target_link_libraries(sketch PRIVATE newui)
target_include_directories(sketch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# sketch's own reflected classes (CanvasView, GradientBar, SwatchGrid, PagePreview) so sketch.newui can name
# them as "type" - generates registerSketchReflection(), which main() calls after newui's own
# registerReflectionData(). See cmake/ReflectGen.cmake.
newui_add_reflectgen_output(sketch
    SCAN_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    REGISTER_FUNCTION registerSketchReflection)

# Resources/ (sketch.newui, icons/*.svg, cursors/*.svg) is copied next to sketch.exe, where
# Bundle looks for it.
newui_add_resources(sketch "${CMAKE_CURRENT_SOURCE_DIR}/Resources")
