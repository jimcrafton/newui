#pragma once

#include <newui/menus.h>

#include <functional>

namespace manyrows {

    // Menus are declared in the .newui file; code finds an item by the menu's and the item's text and gives
    // it a handler. A layout may leave an item out (the lite app has no menus at all) - then nothing is
    // bound and nothing fails, which is what lets one controller serve several layouts.

    // The item `itemText` of the menu `menuText`; null when the bar, the menu or the item is not there.
    inline newui::MenuItem* findMenuItem(newui::MenuBar* menuBar, const char* menuText, const char* itemText) {
        if (menuBar == nullptr) {
            return nullptr;
        }
        for (newui::MenuItem* menu : menuBar->menus()) {
            if (menu->text() != menuText) {
                continue;
            }
            for (newui::MenuItem* item : menu->children()) {
                if (item->text() == itemText) {
                    return item;
                }
            }
        }
        return nullptr;
    }

    // Runs `action` when that item is clicked (no-op when the item does not exist).
    inline void bindMenuItem(newui::MenuBar* menuBar, const char* menuText, const char* itemText, std::function<void(newui::MenuItem&)> action) {
        if (newui::MenuItem* item = findMenuItem(menuBar, menuText, itemText)) {
            item->onClick += [action](newui::MenuItem& clicked) {
                action(clicked);
                return newui::SyncReturn::Handled;
            };
        }
    }

}
