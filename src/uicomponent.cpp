#include "newui/uicomponent.h"

#include <cstring>

namespace newui {

    std::string demangleTypeName(const std::type_info& info) {
        std::string name = info.name();

        static const char* kPrefixes[] = { "class ", "struct ", "enum ", "union " };
        for (const char* prefix : kPrefixes) {
            size_t len = std::strlen(prefix);
            if (name.compare(0, len, prefix) == 0) {
                name.erase(0, len);
                break;
            }
        }

        size_t pos = name.rfind("::");
        if (pos != std::string::npos) {
            name.erase(0, pos + 2);
        }

        return name;
    }

}
