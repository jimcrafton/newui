#include "newui/bundle.h"
#include "newui/application.h"

#include <json5/json5.hpp>
#include <json5/json5_input.hpp>

#include <fstream>
#include <sstream>
#include <vector>

namespace {

    // GetModuleFileNameA's buffer-too-small signal is "returned length ==
    // buffer size" (and, since Vista, ERROR_INSUFFICIENT_BUFFER) - grow
    // and retry rather than guessing a size upfront.
    std::string getExecutablePath() {
        std::vector<char> buffer(MAX_PATH);

        for (;;) {
            DWORD len = ::GetModuleFileNameA(nullptr, buffer.data(), DWORD(buffer.size()));
            if (len == 0) {
                return std::string();
            }
            if (len < buffer.size()) {
                return std::string(buffer.data(), len);
            }
            if (buffer.size() > 32768) {
                return std::string();  // pathological - bail rather than growing forever
            }
            buffer.resize(buffer.size() * 2);
        }
    }

    std::string directoryOf(const std::string& path) {
        size_t pos = path.find_last_of("\\/");
        return pos == std::string::npos ? std::string() : path.substr(0, pos);
    }

    bool fileExists(const std::string& path) {
        DWORD attrs = ::GetFileAttributesA(path.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
    }

}

namespace newui {

    Bundle::Bundle() {
        executableDir_ = directoryOf(getExecutablePath());
        resourcesDir_ = executableDir_ + "\\Resources";
    }

    Bundle& Bundle::instance() {
        static Bundle bundle;
        return bundle;
    }

    std::string Bundle::resourcePath(const std::string& relativePath) const {
        std::string path = resourcesDir_ + "\\" + relativePath;
        return fileExists(path) ? path : std::string();
    }

    bool Bundle::loadImage(const std::string& relativePath, BLImage& outImage) const {
        std::string path = resourcePath(relativePath);
        if (path.empty()) {
            return false;
        }
        return outImage.read_from_file(path.c_str()) == BL_SUCCESS;
    }

    std::string Bundle::loadTextFile(const std::string& relativePath) const {
        std::string path = resourcePath(relativePath);
        if (path.empty()) {
            return std::string();
        }

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return std::string();
        }

        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }

    void Bundle::ensureInfoLoaded() const {
        if (infoLoaded_) {
            return;
        }
        infoLoaded_ = true;

        json5::document doc;
        if (json5::from_file(executableDir_ + "\\Info.json", doc)) {
            return;  // missing or unparseable - appName_/appVersion_ stay ""
        }

        appName_ = doc["name"].get_c_str("");
        appVersion_ = doc["version"].get_c_str("");
    }

    const std::string& Bundle::appName() const {
        ensureInfoLoaded();
        if (!appName_.empty()) {
            return appName_;
        }
        // Application::getName() returns by value, and appName_ must stay
        // untouched (it's the "did Info.json provide one" signal) - so the
        // live fallback goes into its own scratch member instead, purely
        // to give this by-reference return something to point at that
        // outlives the call.
        resolvedAppName_ = Application::instance().getName();
        return resolvedAppName_;
    }

    const std::string& Bundle::appVersion() const {
        ensureInfoLoaded();
        return appVersion_;
    }

}
