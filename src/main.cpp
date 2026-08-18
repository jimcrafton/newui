#include <iostream>

#include "newui/newui.h"
#include "newui/reflection.h"
#include "newui/geometry.h"

extern void registerReflectionData();


int main() {
    std::cout << "newui " << newui::version() << "\n";

    registerReflectionData();

    auto clazz = newui::reflection::classinfo(typeid(newui::Rect));

    std::cout << "class: " << clazz->name() << "\n";


    return 0;
}
