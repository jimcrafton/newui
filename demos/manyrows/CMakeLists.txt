# manyrows - million-row spreadsheet-style table demo (see ../PLAN.md).

add_executable(manyrows
    main.cpp
    manyrowsmodel.h manyrowsmodel.cpp
    timedtableview.h timedtableview.cpp
    cellitem.h cellitem.cpp
    spreadsheeteditor.h spreadsheeteditor.cpp
    io/tableformat.h
    io/csvformat.h io/csvformat.cpp
    io/compoundfile.h io/compoundfile.cpp
    io/xlsformat.h io/xlsformat.cpp
    io/zipfile.h io/zipfile.cpp
    io/xmlparser.h io/xmlparser.cpp
    io/xlsxformat.h io/xlsxformat.cpp
    io/rowassembler.h
    io/formatregistry.h io/formatregistry.cpp
    manyrowsloader.h manyrowsloader.cpp
    manyrowssaver.h manyrowssaver.cpp
    manyrowsdocument.h manyrowsdocument.cpp
    manyrowscontroller.h manyrowscontroller.cpp
)

target_link_libraries(manyrows PRIVATE newui zlib_inflate)
target_include_directories(manyrows PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# TimedTableView is reflected so manyrows.newui can name it as "type" - generates
# registerManyRowsReflection(), which main() calls after newui's own registerReflectionData().
newui_add_reflectgen_output(manyrows
    SCAN_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    REGISTER_FUNCTION registerManyRowsReflection)

# Resources/ (manyrows.newui) is copied next to manyrows.exe, where Bundle looks for it.
newui_add_resources(manyrows "${CMAKE_CURRENT_SOURCE_DIR}/Resources")

# Headless tests of the model. Run: manyrows_tests.exe
add_executable(manyrows_tests
    tests/test_manyrows_model.cpp
    tests/test_spreadsheet_editor.cpp
    tests/test_csv_format.cpp
    tests/test_xls_format.cpp
    tests/test_xlsx_format.cpp
    tests/test_manyrows_document.cpp
    tests/test_manyrows_loader.cpp
    tests/test_manyrows_saver.cpp
    io/csvformat.cpp
    io/compoundfile.cpp
    io/xlsformat.cpp
    io/zipfile.cpp
    io/xmlparser.cpp
    io/xlsxformat.cpp
    io/formatregistry.cpp
    manyrowsloader.cpp
    manyrowssaver.cpp
    manyrowsdocument.cpp
    manyrowsmodel.cpp
    spreadsheeteditor.cpp
)
target_link_libraries(manyrows_tests PRIVATE newui zlib_inflate GTest::gtest_main)
target_include_directories(manyrows_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
