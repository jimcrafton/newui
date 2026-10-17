#pragma once

#include "manyrowsmodel.h"

#include <newui/controls.h>

#include <string>
#include <vector>

namespace manyrows {

    // Cells <-> the tab-separated text spreadsheets exchange on the clipboard (what Excel, LibreOffice and
    // Google Sheets write for a copied block, so a block moves between them and manyrows both ways).
    //
    // Rows end with a line break, cells are separated by a tab. A cell holding a tab, a line break or a
    // double quote is wrapped in double quotes with embedded quotes doubled - the same quoting CSV uses.
    // Pure functions over text: no clipboard, no UI, so they are unit-tested directly.
    // @reflect ignore=true
    class TableClipText {
    public:
        // The block as clipboard text: every row ends with "\r\n" (what Windows editors expect).
        // Throws std::runtime_error when the block is larger than maxCells (copying must stay bounded).
        static std::string encode(const ManyRowsModel& model, const newui::CellRange& range, std::size_t maxCells = 1000000);

        // Clipboard text -> rows of cells. Accepts "\r\n", "\n" or "\r" line ends; a single trailing line
        // break does not make an extra empty row (Excel ends every copied block with one). Empty text gives
        // no rows. Rows may differ in length; the caller treats missing cells as blank.
        static std::vector<std::vector<std::string>> parse(const std::string& text);
    };

}
