#pragma once

#include <newui/newui.h>

#include <blend2d/blend2d.h>

#include <string>

namespace newui {

    // Resolves app resources (fonts, images, saved UI trees, ...) relative
    // to the running .exe's own directory - a flattened, Windows-idiomatic
    // analog of a macOS .app bundle's Contents/Resources/ lookup, without
    // replicating the Contents/MacOS+Info.plist split (that split exists
    // for Finder's double-clickable-package semantics, which Windows has
    // no equivalent of). Expected layout, next to the .exe:
    //
    //   MyApp.exe
    //   Info.json          (optional - "name"/"version")
    //   Resources/
    //     Fonts/
    //     Images/
    //
    // Sibling to Application, not a member of it - Application owns Win32
    // app/window lifecycle, Bundle is purely a path resolver. Simpler
    // singleton than Application's (no need for its std::atomic
    // single-instantiation guard - that exists because Application wraps a
    // genuinely unique-per-process Win32 registration; Bundle has no such
    // constraint) - a plain Meyer's-singleton instead.
    class Bundle {
    public:
        static Bundle& instance();

        Bundle(const Bundle&) = delete;
        Bundle& operator=(const Bundle&) = delete;

        // Directory containing the running .exe (no trailing slash),
        // computed once via GetModuleFileNameA() and cached.
        const std::string& executableDir() const {
            return executableDir_;
        }

        // executableDir() + "\Resources".
        const std::string& resourcesDir() const {
            return resourcesDir_;
        }

        // resourcesDir() + "\" + relativePath, if that file exists - else
        // "". Fails fast rather than throwing, matching this codebase's
        // bool/empty-return error handling elsewhere rather than
        // exceptions.
        std::string resourcePath(const std::string& relativePath) const;

        // Loads relativePath (resolved via resourcePath()) as a BLImage.
        // BMP/JPEG/PNG/QOI all work out of the box - blend2d builds those
        // codecs in unconditionally. Returns false, leaving outImage
        // untouched, if the path doesn't resolve or isn't a loadable image.
        bool loadImage(const std::string& relativePath, BLImage& outImage) const;

        // Reads relativePath (resolved via resourcePath()) as text.
        // Returns "" if the path doesn't resolve or can't be read - not
        // distinguishable from a genuinely empty file; callers that need
        // to tell those apart should use resourcePath() directly instead.
        std::string loadTextFile(const std::string& relativePath) const;

        // Lazily parsed once from executableDir() + "\Info.json" (a plain
        // JSON5 read - Bundle isn't part of the View hierarchy).
        // appName() falls back to
        // Application::instance().getName() if Info.json is missing or
        // has no "name" key - re-read live on every such call (not cached
        // itself, unlike the Info.json-provided value), so it tracks a
        // later Application::setName() rather than freezing on whatever
        // name was current the first time appName() happened to be
        // called. appVersion() falls back to "" (Application has no
        // version concept to fall back to).
        const std::string& appName() const;
        const std::string& appVersion() const;

    private:
        Bundle();

        void ensureInfoLoaded() const;

        std::string executableDir_;
        std::string resourcesDir_;

        mutable bool infoLoaded_ = false;
        mutable std::string appName_;      // from Info.json only ("" if absent) - never overwritten by the fallback
        mutable std::string appVersion_;
        mutable std::string resolvedAppName_;  // scratch buffer for appName()'s live Application fallback
    };

}
