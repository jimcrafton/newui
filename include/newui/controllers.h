#pragma once
#include <vector>

#include "newui/newui.h"
#include "newui/models.h"

namespace newui {
    class View;
    class SubView;
    class Animation;

    // The C in MVC - owns/observes a Model, reacting to its onChanged via
    // modelChanged(). Deliberately not View-aware at all: a basic Control
    // (controls.h) doesn't need one of these - it's just a View reacting to
    // its own mouse/keyboard input, no real data behind it. Controller (and
    // ViewController below, which extends it) are for the more complex
    // case: a widget (e.g. a future ListView/TreeView) or a whole screen
    // whose content is driven by real data.
    //
    // A data-driven Control is expected to *own a Controller as a member*
    // (composition), not inherit from it - keeps the View-hierarchy class
    // (presentation) and the Controller (data mediation) as separate
    // collaborators rather than one class doing both jobs. ViewController
    // is the one place this codebase does inherit from Controller directly:
    // a screen-level controller genuinely *is* one, not just "has one."
    class Controller {
    public:
        Controller() = default;
        virtual ~Controller();

        // Non-owning - same convention Model::views_ already uses for the
        // reverse relationship (a View doesn't own its Model either).
        void setModel(Model* val);

        const Model* model() const { return model_; }
        Model* model() { return model_; }

        // Fired whenever model()'s onChanged fires, once a Model is set
        // (see setModel()). Override to react; returning
        // SyncReturn::Handled vs Ignored only matters if something else is
        // also subscribed to the same Model's onChanged directly and cares
        // about early-out - see Delegate::syncCall()'s doc comment in
        // delegate.h. Default does nothing and reports Ignored.
        virtual SyncReturn modelChanged(Model&) { return SyncReturn::Ignored; }

    private:
        Model* model_ = nullptr;
        Connection modelChangedConnection_;
    };

    // The VC in MVC - owns exactly one root SubView (view()), lazily built
    // via loadView()/viewDidLoad(), plus an appear/disappear lifecycle and
    // parent/child containment - modeled on UIKit's UIViewController, sized
    // down to what this toolkit actually needs (no navigation stack, no
    // storyboard-equivalent - loadView() is the *only* way a view gets
    // built here, not a rarely-overridden fallback the way it is in real
    // UIKit).
    //
    // present()/dismiss() (and the addChildController()/
    // removeFromParentController() convenience wrappers around them) do
    // three things together as one operation, unlike UIKit's more granular
    // addChild()+view.addSubview()+didMove(toParent:) split: attach the
    // view into its container, run containment bookkeeping, and drive the
    // appear/disappear lifecycle - including, optionally, a real animated
    // transition (see appearingAnimation()/disappearingAnimation()).
    // Combining them is a deliberate simplification for this toolkit: with
    // nothing else to coordinate multiple independent container APIs
    // against (UIKit keeps its steps separate partly for that flexibility),
    // one call that can't be misused (forgetting the view-attach half, or
    // firing lifecycle callbacks without ever attaching the view) is more
    // valuable here than matching UIKit's exact call shape.
    class ViewController : public Controller {
    public:
        ViewController() = default;

        // Does NOT fire viewWillDisappear()/viewDidDisappear() - calling a
        // virtual function from a destructor never reaches an overriding
        // subclass's version (its part of the object is already gone by
        // the time ~ViewController runs), so this only does the non-
        // virtual cleanup: detach view() from its container if still
        // attached, then destroy()+delete it. Call dismiss() first for a
        // clean lifecycle-callback-driven teardown.
        virtual ~ViewController();

        // Lazily builds view() on first access (loadView(), then
        // viewDidLoad() - see those). Never rebuilt afterward, even across
        // present()/dismiss() cycles.
        SubView* view();

        bool isViewLoaded() const { return view_ != nullptr; }

        ViewController* parentController() const { return parentController_; }
        const std::vector<ViewController*>& childControllers() const { return childControllers_; }

        // Attaches view() into container, firing viewWillAppear() then -
        // once appearingAnimation() (if any) finishes playing -
        // viewDidAppear(). If parent is non-null, also registers this as
        // one of parent's childControllers() (parentController()). Pass
        // parent=nullptr for a top-level ViewController with no parent
        // controller (e.g. the app's main screen, presented straight into
        // a RootView).
        void present(View* container, ViewController* parent = nullptr);

        // Reverse of present(): fires viewWillDisappear(), then - once
        // disappearingAnimation() (if any) finishes playing - detaches
        // view() from its container, clears containment, and fires
        // viewDidDisappear(). A no-op if not currently presented. Safe to
        // present() again afterward - view() itself isn't destroyed here.
        void dismiss();

        // Convenience for the common case: presenting into a parent
        // controller's own view(). Equivalent to
        // child->present(view(), this).
        void addChildController(ViewController* child);

        // Convenience matching addChildController() above - equivalent to
        // dismiss().
        void removeFromParentController() { dismiss(); }

    protected:
        // Builds and returns this controller's root view. Called at most
        // once, the first time view() is accessed - required override,
        // unlike UIKit's loadView() (which can be skipped entirely when a
        // storyboard/XIB builds the view instead; this toolkit has no such
        // alternative).
        virtual SubView* loadView() = 0;

        // One-time setup after loadView() - the view exists but isn't
        // necessarily attached to anything live yet (rootView() may still
        // be null). Default does nothing.
        virtual void viewDidLoad() {}

        // view() has just been attached to container (see present()) -
        // rootView()/getBounds() etc. are valid, but if appearingAnimation()
        // provided a transition, it hasn't played yet (the view may be at
        // its pre-transition state, e.g. transparent/off-screen).
        virtual void viewWillAppear() {}

        // view() is fully "in" - attached, and appearingAnimation() (if
        // any) has finished playing.
        virtual void viewDidAppear() {}

        // view() is about to disappear - still fully attached/valid;
        // disappearingAnimation() (if any) hasn't played yet. The usual
        // place to stop anything viewWillAppear()/viewDidAppear() started
        // (timers, subscriptions) - matches the common real-world UIKit
        // advice to pair start/stop in the "will" hooks rather than
        // waiting for "did", so cleanup doesn't depend on a transition
        // actually finishing.
        virtual void viewWillDisappear() {}

        // view() has just been detached from its container (see
        // dismiss()) - disappearingAnimation() (if any) has already
        // played.
        virtual void viewDidDisappear() {}

        // Optional real transition, built via AnimationManager::addAnimation()
        // and returned here - e.g. a fade/slide keyed on one of view()'s
        // fields (registered through PropertyManager, see animation.h) or
        // any other ObservableProperty. Default nullptr: viewDidAppear()/
        // the actual detach+viewDidDisappear() then follow viewWillAppear()/
        // viewWillDisappear() immediately, with no gap - the common case
        // costs nothing. When provided, present()/dismiss() defer until
        // AnimationManager::currentFrame() reaches the returned Animation's
        // endTime().
        virtual Animation* appearingAnimation() { return nullptr; }
        virtual Animation* disappearingAnimation() { return nullptr; }

    private:
        void finishAppearing();
        void finishDisappearing();
        void waitForAnimationThen(Animation* animation, void (ViewController::* onComplete)());

        SubView* view_ = nullptr;
        View* container_ = nullptr;
        ViewController* parentController_ = nullptr;
        std::vector<ViewController*> childControllers_;
    };

    // Owns/tracks a set of open Document instances (models.h) - the app-
    // wide coordinator half of this toolkit's document-based-app support,
    // modeled on (a deliberately scoped-down version of) AppKit's
    // NSDocumentController: tracks documents(), which one is
    // activeDocument(), and add/open/close. There's no per-document
    // "window controller" class here the way AppKit has NSWindowController -
    // a screen showing one Document is just an ordinary ViewController
    // with setModel(document) called on it (Controller::setModel(), Document
    // being a Model), so nothing new is needed for that half.
    //
    // No document-type-to-class factory/registry (NSDocumentController's
    // "which NSDocument subclass handles this file extension" mapping) -
    // out of scope until there's more than one concrete Document subclass
    // to map between; a caller constructs the right concrete Document
    // itself (`new MyTextDocument()`) and hands it to addDocument()/
    // openDocument().
    //
    // Deliberately does *not* prompt about unsaved changes itself -
    // closeDocument() just closes. Checking document->isModified() and
    // showing whatever confirmation UI is appropriate (a Dialog, a custom
    // View, ...) is the caller's job, same "give hooks, don't force a
    // specific UI policy" principle as Model::updateAllViews() not
    // dictating what "repaint to reflect new data" looks like either.
    //
    // Not itself wired to any particular View (e.g. a TabControl showing
    // one tab per document) - subscribe onDocumentAdded()/
    // onDocumentWillClose()/onActiveDocumentChanged() to drive whatever
    // widget represents "which documents are open" in a real app; keeping
    // DocumentController ignorant of which widget that is is what lets a
    // single-window (tabbed) app and a future multi-window app share it
    // unchanged.
    class DocumentController : public Controller {
    public:
        DocumentController() = default;

        // Deletes every still-open Document without notifying anything
        // (no onDocumentWillClose()) - same "quiet, non-virtual cleanup"
        // reasoning as ~ViewController(): by process/object teardown time,
        // subscribers driving UI from these events may already be gone
        // themselves, so firing more events here is more likely to cause
        // harm than good. Call closeDocument() on each first for a normal,
        // notified shutdown sequence.
        virtual ~DocumentController();

        typedef Delegate<DocumentController, Document&> DocumentEventDelegate;
        typedef Delegate<DocumentController> ActiveDocumentChangedDelegate;

        // Fired right after document is added to documents() (see
        // addDocument()/openDocument()) - e.g. to build the tab/window
        // representing it.
        DocumentEventDelegate onDocumentAdded;

        // Fired right before document is removed from documents() and
        // deleted (see closeDocument()) - document is still fully valid
        // here (isModified()/filePath() etc. all still readable).
        DocumentEventDelegate onDocumentWillClose;

        // Fired whenever activeDocument() changes, including to nullptr
        // (the last open document was just closed).
        ActiveDocumentChangedDelegate onActiveDocumentChanged;

        const std::vector<Document*>& documents() const { return documents_; }

        Document* activeDocument() const { return activeDocument_; }

        // A no-op if document isn't in documents() (including nullptr,
        // for "no active document") other than nullptr itself.
        void setActiveDocument(Document* document);

        // Takes ownership of document (already constructed by the caller)
        // - registers it, fires onDocumentAdded(), and makes it
        // activeDocument(). A no-op for a null or already-registered
        // document.
        void addDocument(Document* document);

        // Convenience: addDocument(document) then document->load(path).
        // document is added either way (even if the load fails) - the
        // caller decides whether to closeDocument() it or leave it as an
        // empty/unsaved document the user can retry Open or Save As on.
        // Returns load()'s result.
        bool openDocument(Document* document, const std::string& path);

        // Removes document from documents() and deletes it - see this
        // class's own comment on why no unsaved-changes prompt happens
        // here. Fires onDocumentWillClose() first, then - if document was
        // activeDocument() - moves to whichever document is now last in
        // documents() (or nullptr, if none remain), firing
        // onActiveDocumentChanged(). A no-op if document isn't in
        // documents() (including nullptr).
        void closeDocument(Document* document);

    private:
        std::vector<Document*> documents_;
        Document* activeDocument_ = nullptr;
    };

} // namespace newui
