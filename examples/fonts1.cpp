
#include "newui/newui.h"
#include "newui/delegate.h"
#include "newui/rootview.h"
#include "newui/runloop.h"
#include "newui/fontmanager.h"

#include <iostream>
#include <string>
#include <thread>

int main() {
    std::cout << "newui " << newui::version() << " - font examples\n";

    auto fonts = newui::FontManager::listFonts();
    for (const auto& font : fonts) {
        std::cout << " - " << font.name << " (" << font.filePath << ")\n";
    }   


    NONCLIENTMETRICSA ncm;
    memset(&ncm, 0, sizeof(ncm));

    ncm.cbSize = sizeof(ncm);

    auto defFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    LOGFONTA tmpLogFont = { 0 };
    GetObjectA(defFont, sizeof(LOGFONTA), &tmpLogFont);
    printf("lfFaceName: %s\n", tmpLogFont.lfFaceName);


    if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0)) {
        printf("lfFaceName: %s\n", ncm.lfMessageFont.lfFaceName);
    }



    return 0;
}