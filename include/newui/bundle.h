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
        // computed once via GetModuleFileNameA() and cached - unless
        // overridden, see setExecutableDirOverride() below.
        const std::string& executableDir() const {
            return executableDir_;
        }

        // Overrides executableDir() (and, via it, resourcesDir()) for a
        // host where the running process's own exe directory isn't the
        // right root - e.g. NativeEditControls.dll hosted inside
        // devenv.exe, whose own exe directory has nothing to do with
        // whichever project's .newui files are actually being edited.
        // Pass "" to reset to the real, OS-derived value. Mutates this
        // shared singleton, so it isn't safe for two callers needing
        // different roots active at the same time - a real limitation
        // for two Designer documents from different projects open at
        // once, accepted for now rather than making Bundle non-singleton.
        // Also invalidates the cached Info.json read (appName()/
        // appVersion()), since that resolves against executableDir() too.
        void setExecutableDirOverride(const std::string& dir);

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
        // codecs in unconditionally. A ".svg" path is instead rasterized
        // via renderSvgFile() (svgimage.h) at a fixed kDefaultSvgRasterSize
        // x kDefaultSvgRasterSize - svgandme has no reliable way to ask an
        // SVG for its own "natural" size (see renderSvgFile()'s own
        // comment). Returns false, leaving outImage untouched, if the path
        // doesn't resolve or isn't a loadable image.
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

        // Loads just the "rootView" node of "<bundleName>.newui" into
        // target in place - no Frame required, for a standalone RootView
        // (see RootView's Frame-less constructor). Returns false for any
        // of loadFrame()'s own failure reasons (bundleName empty, file
        // missing/invalid).
        //
        // Templated (defined + explicitly instantiated in bundle.cpp, not
        // inline here - same "declare in the header, define + explicitly
        // instantiate in the .cpp" shape as any templated member that
        // doesn't need to be usable for every possible T at every call
        // site) so a registered proxy class (Class::proxy()/proxyFor(),
        // reflection.h - e.g. RootViewProxy, cpp_codetools' own
        // design-time stand-in for a real RootView, which can't be loaded
        // through this as a real RootView& at all since it's a genuinely
        // different, unrelated type - see RootViewProxy's own class
        // comment) can be loaded the same way a real RootView is, reusing
        // this method's own file-resolution/parsing, not a hand-rolled
        // duplicate. Add a new explicit instantiation in bundle.cpp for
        // any future T that needs this.
        //
        // designMode, when true, propagates Component::setDesignTime(true)
        // (via Class::trySetDesignTime()) onto every freshly-constructed
        // child read while rebuilding target's own childViews - readNested()
        // itself never consults a "type" tag for target's own top-level
        // node (it always trusts T, this call's own static type - see
        // reflection.h's readNested()), so designMode only ever affects
        // target's *descendants*, not target itself. Defaults to false
        // (ordinary, non-design-time load) - a real running app loading its
        // own RootView normally should never mark its own content as
        // design-time; a design-time caller (e.g. cpp_codetools' own
        // DesignerEditor, loading into a RootViewProxy) passes true
        // explicitly. Bundle has no built-in notion of which T "means"
        // design-time - that's the caller's call, not something to infer
        // from T here.
        //
        // IMPORTANT, real trap already hit once: this only actually
        // matters for a target tree that stays *unattached* to any
        // RootView (Component::isDesignTime() is what reads the flag this
        // sets). The moment a loaded child is addChild()'d onto a real
        // RootView, View::isDesignTime() takes over (it hides Component's
        // own method by name, not override) and unconditionally defers to
        // rootView_->isDesignTime() instead - so designMode's per-child
        // propagation is silently moot for anything actually hosted in a
        // window. For that case (e.g. DesignerEditor's own RootViewProxy,
        // itself a child of a real, hosting RootView), call
        // rootView->setDesignTime(true) once on that outer, real RootView
        // instead - every attached descendant already defers to it, no
        // per-load propagation needed at all.
        template<typename T>
        bool loadRootView(T& target, const std::string& bundleName, bool designMode = false) const;

        // Loads just the "rootView" node of
        // "<rootView.getFrame()->getName()>.newui" into rootView in place -
        // the owning Frame's own properties are untouched. rootView must
        // already be attached to a live Frame (rootView.getFrame() !=
        // nullptr); returns false otherwise, or for any of loadFrame()'s
        // own failure reasons. Thin wrapper over the bundleName overload
        // above.
        bool loadRootView(RootView& rootView) const;

        // Loads just the "animations" block of "<frame.getName()>.newui"
        // into AnimationManager::instance() (via the real addAnimation()/
        // addKey() calls, resolving each saved target against frame's own,
        // already-live rootView tree) - unlike loadFrame(), frame's own
        // properties (including rootView) are left completely untouched,
        // so this is safe to call on a Frame whose UI was already built
        // some other way (by hand, or via an earlier loadFrame() call) -
        // calling loadFrame() again afterward would duplicate every child
        // in rootView's own childViews (its add-only reconstruction has
        // no way to know a child it's about to add already exists - see
        // ObjectReader::read()'s own comment on that). Returns false for
        // any of loadFrame()'s own failure reasons, or if the file has no
        // "animations" block at all.
        bool loadAnimations(Frame& frame) const;

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

        // Write-side counterpart to loadRootView(T&, bundleName) - for a
        // standalone RootView (or registered proxy - see loadRootView()'s
        // own comment) with no owning Frame. Unlike writeFrame() (which
        // always writes the whole Frame fresh), this preserves every
        // other top-level key an existing "<bundleName>.newui" already
        // has (title, bounds, animations, ...) - only "rootView" is
        // replaced - so editing a Frame-less view of a file a real Frame
        // elsewhere still loadFrame()s from doesn't silently drop that
        // Frame's own data. Returns false if bundleName is empty or the
        // file couldn't be written. Same templated/explicit-instantiation
        // shape as loadRootView() - see its own comment.
        //
        // designMode, when true, writes target's real Class::proxyFor()
        // name (e.g. "RootView") as the "rootView" node's own "type" tag
        // instead of target's actual class name (e.g. "RootViewProxy") -
        // see ObjectWriter::isDesignMode()/Class::proxyFor()'s own
        // comments - so a file written from a design-time proxy tree still
        // reads as an ordinary RootView-shaped document. A no-op for a T
        // with no registered proxyFor() (RootView itself, in particular),
        // so callers that never use proxies don't need to pass this at
        // all. Defaults to false, same reasoning as loadRootView()'s own
        // designMode parameter.
        template<typename T>
        bool writeRootView(T& target, const std::string& bundleName, bool designMode = false) const;

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
