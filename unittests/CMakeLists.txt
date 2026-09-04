find_package(Threads REQUIRED)

add_executable(newui_tests
    test_version.cpp
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
    test_cursor.cpp
    test_graphics.cpp
    test_menus.cpp
    test_tabcontrol.cpp
    test_rootview.cpp
    test_layout.cpp
    test_bundle.cpp
    test_viewpath.cpp
    test_namemanager.cpp
    test_reflection.cpp
    test_reflectionio.cpp
    test_delegateserialization.cpp
    test_enumserialization.cpp
    test_dialogs.cpp
    test_controllers.cpp
    test_documents.cpp
    test_controls.cpp
    test_items.cpp
    test_themedata.cpp
    test_shapes.cpp
    test_text.cpp
    test_command.cpp
    test_clipboard.cpp
    test_dragndrop.cpp
    test_undostack.cpp
    test_viewbuilder.cpp
)

target_link_libraries(newui_tests PRIVATE newui GTest::gtest_main Threads::Threads)

include(GoogleTest)
gtest_discover_tests(newui_tests)
