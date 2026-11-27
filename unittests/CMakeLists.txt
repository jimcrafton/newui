find_package(Threads REQUIRED)

# Off Windows only the tests whose code is already in the shared core are built (TODO(macport): add files here as
# their sources move out of the Windows-only list in the top-level CMakeLists.txt).
if(NOT WIN32)
    add_executable(newui_tests
        test_version.cpp
        test_delegate.cpp
        test_curveinterpolation.cpp
        test_namemanager.cpp
        test_command.cpp
        test_undostack.cpp
    )
    target_link_libraries(newui_tests PRIVATE newui GTest::gtest_main Threads::Threads)
    include(GoogleTest)
    # PRE_TEST: Xcode code-signs after post-build rules, and Apple Silicon kills an unsigned binary, so discovery
    # can't run as part of the build.
    gtest_discover_tests(newui_tests DISCOVERY_MODE PRE_TEST)
    return()
endif()

add_executable(newui_tests
    test_version.cpp
    test_automation.cpp
    test_application.cpp
    test_delegate.cpp
    test_runloop.cpp
    test_color.cpp
    test_viewstyle.cpp
    test_fontmanager.cpp
    test_font.cpp
    test_property.cpp
    test_animation.cpp
    test_curveinterpolation.cpp
    test_view.cpp
    test_displayunit.cpp
    test_cursor.cpp
    test_graphics.cpp
    test_menus.cpp
    test_tabcontrol.cpp
    test_rootview.cpp
    test_pen.cpp
    test_gdipresentsurface.cpp
    test_dxgipresentsurface.cpp
    test_layout.cpp
    test_bundle.cpp
    test_componentsettings.cpp
    test_binding.cpp
    test_viewpath.cpp
    test_namemanager.cpp
    test_filewatcher.cpp
    test_utils.cpp
    test_reflection.cpp
    test_reflectionio.cpp
    test_delegateserialization.cpp
    test_enumserialization.cpp
    test_dialogs.cpp
    test_popuptool.cpp
    test_controllers.cpp
    test_documents.cpp
    test_controls.cpp
    test_table.cpp
    test_items.cpp
    test_themedata.cpp
    test_shapes.cpp
    test_text.cpp
    test_piecetree.cpp
    test_texthistory.cpp
    test_textfolding.cpp
    test_command.cpp
    test_clipboard.cpp
    test_dragndrop.cpp
    test_undostack.cpp
    test_viewbuilder.cpp
    test_rootviewproxy.cpp
    test_frameproxy.cpp
    test_splitter.cpp
    test_segmentedcontrol.cpp
    test_svgimage.cpp
)

target_link_libraries(newui_tests PRIVATE newui newui_automation GTest::gtest_main Threads::Threads)

include(GoogleTest)
gtest_discover_tests(newui_tests)
