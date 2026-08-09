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
    test_layout.cpp
)

target_link_libraries(newui_tests PRIVATE newui GTest::gtest_main Threads::Threads)

target_compile_definitions(newui_tests PRIVATE
    NEWUI_EXPECTED_VERSION="${PROJECT_VERSION}"
)

include(GoogleTest)
gtest_discover_tests(newui_tests)
