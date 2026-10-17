# sketch - vector editor demo (see ../PLAN.md).

add_executable(sketch
    main.cpp
    canvasview.h canvasview.cpp
    canvastools.cpp
    texttool.cpp
    cuttools.cpp
    artboardtool.cpp
    transformtools.cpp
    guides.cpp
    cursorset.h cursorset.cpp
    shapetools.h shapetools.cpp
    pathops.h pathops.cpp
    textoutline.h textoutline.cpp
    gzip.h gzip.cpp
    sketchbin.h sketchbin.cpp
    gradientbar.h gradientbar.cpp
    swatchgrid.h swatchgrid.cpp
    pagepreview.h pagepreview.cpp
    stylepanel.h stylepanel.cpp
    transformpanel.h transformpanel.cpp
    layertree.h layertree.cpp
    layerspanel.h layerspanel.cpp
    artboardpanel.h artboardpanel.cpp
    librarypanel.h librarypanel.cpp
    brushpanel.h brushpanel.cpp
    brush.h brush.cpp
    pathflatten.h pathflatten.cpp
    imagedata.h imagedata.cpp
    textwarp.h textwarp.cpp
    documenttextpath.cpp
    documentimages.cpp
    documentexpand.cpp
    pdfexport.h pdfexport.cpp
    documentbrushes.cpp
    textpanel.h textpanel.cpp
    numberscrubber.h numberscrubber.cpp
    sketchcolor.h sketchcolor.cpp
    sketchobject.h sketchobject.cpp
    sketchdocument.h sketchdocument.cpp
    documentcutting.cpp
    documentpaths.cpp
    documenttransform.cpp
    documentguides.cpp
    documentlayers.cpp
    documentswatches.cpp
    documentselect.cpp
    documentclip.cpp
    documentsymbols.cpp
    documentartboards.cpp
    documenttrace.cpp
    imagetrace.h imagetrace.cpp
    sketchsvg.h sketchsvg.cpp
    sketchexport.h sketchexport.cpp
    sketchcontroller.h sketchcontroller.cpp
    sketchcommands.h sketchcommands.cpp
    sketchwindow.h sketchwindow.cpp
    sketchchecks.h sketchchecks.cpp
    newdocumentmodel.h newdocumentmodel.cpp
    newdocumentdialog.h newdocumentdialog.cpp
    tracedialog.h tracedialog.cpp
)

target_link_libraries(sketch PRIVATE newui zlib_static windowscodecs ole32)
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

# Headless tests of the model layer (color, objects, document). Run: sketch_tests.exe
add_executable(sketch_tests
    tests/test_sketch_model.cpp
    sketchcolor.cpp
    sketchobject.cpp
    sketchdocument.cpp
    documentcutting.cpp
    documentpaths.cpp
    documenttransform.cpp
    documentguides.cpp
    documentlayers.cpp
    documentswatches.cpp
    documentselect.cpp
    documentclip.cpp
    documentsymbols.cpp
    documentbrushes.cpp
    brush.cpp
    pathflatten.cpp
    textwarp.cpp
    imagedata.cpp
    documenttextpath.cpp
    documentimages.cpp
    documentexpand.cpp
    pdfexport.cpp
    documentartboards.cpp
    documenttrace.cpp
    imagetrace.cpp
    sketchsvg.cpp
    sketchexport.cpp
    shapetools.cpp
    pathops.cpp
    textoutline.cpp
    gzip.cpp
    numberscrubber.cpp
    sketchbin.cpp
    cursorset.cpp
    newdocumentmodel.cpp
)
target_link_libraries(sketch_tests PRIVATE newui zlib_static windowscodecs ole32 GTest::gtest_main)
target_include_directories(sketch_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
