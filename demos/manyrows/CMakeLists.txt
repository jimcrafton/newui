# manyrows - million-row spreadsheet-style table demo (see ../PLAN.md).
#
# Three layers, so the two apps share exactly what they use:
#   manyrows_core   the table: ManyRowsModel, TimedTableView, CellItem, SpreadsheetEditor and
#                   SpreadsheetController (cells, undo, clipboard, fill, readouts). No files.
#   manyrows_files  the file side: io formats (CSV / XLS / XLSX), loader, saver, ManyRowsDocument and
#                   ManyRowsController (the DocumentController).
#   manyrows_lite   core only: a bare table over 1,000,000 generated rows.
#   manyrows        core + files + ManyRowsWindow: the full app.

add_library(manyrows_core STATIC
    manyrowsmodel.h manyrowsmodel.cpp
    timedtableview.h timedtableview.cpp
    cellitem.h cellitem.cpp
    spreadsheeteditor.h spreadsheeteditor.cpp
    spreadsheetcontroller.h spreadsheetcontroller.cpp
    tablecliptext.h tablecliptext.cpp
    filterrule.h filterrule.cpp
    rowindex.h rowindex.cpp
    tablequery.h tablequery.cpp
    menubinding.h
    textformat.h
)
target_link_libraries(manyrows_core PUBLIC newui)
target_include_directories(manyrows_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# TimedTableView and CellItem are reflected so the .newui files can name them as "type" - generates
# registerManyRowsReflection(), which main() calls after newui's own registerReflectionData().
newui_add_reflectgen_output(manyrows_core
    SCAN_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    REGISTER_FUNCTION registerManyRowsReflection)

add_library(manyrows_files STATIC
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
target_link_libraries(manyrows_files PUBLIC manyrows_core zlib_static)

# The full app. Resources/ (manyrows.newui) is copied next to manyrows.exe, where Bundle looks for it.
add_executable(manyrows
    main.cpp
    manyrowswindow.h manyrowswindow.cpp
    filterstrip.h filterstrip.cpp
    manyrowstoolbar.h manyrowstoolbar.cpp
)
target_link_libraries(manyrows PRIVATE manyrows_files)
newui_add_resources(manyrows "${CMAKE_CURRENT_SOURCE_DIR}/Resources")

# The lite app: Resources/manyrows_lite.newui is copied beside it (both apps share one Resources folder).
add_executable(manyrows_lite
    lite/main.cpp
)
target_link_libraries(manyrows_lite PRIVATE manyrows_core)
newui_add_resources(manyrows_lite "${CMAKE_CURRENT_SOURCE_DIR}/Resources")

# Headless tests. Run: manyrows_tests.exe
add_executable(manyrows_tests
    tests/test_manyrows_model.cpp
    tests/test_spreadsheet_editor.cpp
    tests/test_clipboard_fill.cpp
    tests/test_sort_filter.cpp
    tests/test_spreadsheet_controller.cpp
    tests/test_csv_format.cpp
    tests/test_xls_format.cpp
    tests/test_xlsx_format.cpp
    tests/test_manyrows_document.cpp
    tests/test_manyrows_loader.cpp
    tests/test_manyrows_saver.cpp
)
target_link_libraries(manyrows_tests PRIVATE manyrows_files GTest::gtest_main)
