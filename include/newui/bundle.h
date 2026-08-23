#pragma once

#include <newui/newui.h>

#include <blend2d/blend2d.h>

#include <string>

namespace newui {

    class Frame;
    class Dialog;
    class RootView;
    class View;

    namespace reflection {
        class ObjectReader;
    }

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

        // Loads "<frame.getName()>.newui" (resolved via resourcePath(),
        // same failure contract as loadTextFile()) into frame in place -
        // every property the file describes, including everything under
        // its nested "rootView" property (recursively, down through
        // childViews), is applied directly onto the already-live frame/
        // rootView objects; nothing here is freshly constructed. Returns
        // false if frame.getName() is empty, the file doesn't resolve, or
        // it isn't valid JSON5.
        bool loadFrame(Frame& frame) const;

        // Delegates to loadFrame(dialog.frame()) - see Dialog::frame().
        bool loadDialog(Dialog& dialog) const;

        // Loads just the "rootView" node of
        // "<rootView.getFrame()->getName()>.newui" into rootView in place -
        // the owning Frame's own properties are untouched. rootView must
        // already be attached to a live Frame (rootView.getFrame() !=
        // nullptr); returns false otherwise, or for any of loadFrame()'s
        // own failure reasons.
        bool loadRootView(RootView& rootView) const;

        // Loads "<name>.newui" as a freshly Class::createInstance()'d
        // View - the concrete type is whatever the file's own root "type"
        // tag names (a registered, default-constructible View subclass -
        // View itself, SubView, or any real widget), not something the
        // caller picks. Returns nullptr if name is empty, the file doesn't
        // resolve, isn't valid JSON5, or its "type" can't be resolved/
        // constructed this way. Caller owns the returned View.
        View* loadView(const std::string& name) const;

        // Writes frame.getName() + ".newui" under resourcesDir() (created
        // first if it doesn't exist yet) via ObjectWriter - frame's own
        // registered properties (title, bounds, name, ...; its real
        // runtime class if it's ever a Frame subclass, e.g. PopupFrame),
        // plus its rootView (and rootView's own childViews subtree)
        // nested under a "rootView" key - see Frame::rootView()'s own
        // comment for why that link needs writeNested() rather than an
        // ordinary registered property. Returns false if frame.getName()
        // is empty or the file couldn't be written.
        bool writeFrame(Frame& frame) const;

        // Delegates to writeFrame(dialog.frame()) - see Dialog::frame().
        bool writeDialog(Dialog& dialog) const;

        // Writes "<name>.newui" under resourcesDir() from view - the
        // written "type" tag (and full property set) comes from view's
        // own real runtime class (classinfo(typeid(view))), not whatever
        // static type the caller happens to hold it as, so a SubView (or
        // any concrete widget) round-trips as itself, symmetric with
        // loadView()'s own polymorphic reconstruction. Returns false if
        // name is empty, view's runtime class isn't registered, or the
        // file couldn't be written.
        bool writeView(View& view, const std::string& name) const;

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

        // Shared by loadFrame()/loadRootView()/loadView() - see its own
        // comment in bundle.cpp.
        bool parseNewuiFile(const std::string& name, reflection::ObjectReader& outReader) const;

        // Shared by writeFrame()/writeView() - see its own comment in
        // bundle.cpp.
        bool writeTextFile(const std::string& relativePath, const std::string& contents) const;

        std::string executableDir_;
        std::string resourcesDir_;

        mutable bool infoLoaded_ = false;
        mutable std::string appName_;      // from Info.json only ("" if absent) - never overwritten by the fallback
        mutable std::string appVersion_;
        mutable std::string resolvedAppName_;  // scratch buffer for appName()'s live Application fallback
    };

}
