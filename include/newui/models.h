#pragma once

#include <any>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "newui/newui.h"
#include "newui/delegate.h"


namespace newui {
    class View;

    // The M in MVC - deliberately not a UIComponent/not part of the View
    // tree's own serialization system (serialization.h covers View/
    // ViewStyle/Layout/LayoutParams/Frame/Application specifically - a
    // Model is arbitrary application data, not a node in that tree).
    //
    // A basic Control (controls.h) doesn't need one of these at all - it's
    // just a View that reacts to its own mouse/keyboard input. Model (and
    // Controller, controllers.h) exist for the more complex case: a widget
    // (or a whole screen, via ViewController) whose content is driven by
    // real data rather than just its own visual state.
    class Model {
    public:
        enum UpdateFlags {
            NoFlags = 0x00,
            RequiresValidation = 0x001,
            DisplayErrorIfInvalid = 0x002,
        };

        typedef Delegate<Model> ModelDelegate;
        typedef Delegate<Model, const std::any&, const std::any&> ModelKeyValueDelegate;

        //returns bool, takes key, value, and outResult
        //using ValidatorFuncPtr = bool(*)(const std::any&, const std::any&, std::any& );

        virtual ~Model() = default;

        ModelDelegate onChanged;
        ModelDelegate onCleared;
        ModelKeyValueDelegate onKeyValueChanged;

        virtual void clear() {
            onCleared(*this);
        }

        virtual bool empty() const {
            return true;
        }

        virtual std::any value(const std::any& key = std::any()) {
            return std::any();
        }

        virtual void setValue(const std::any& newValue, const std::any& key = std::any());

        // Registers view to receive updateAllViews() and be dropped
        // automatically (see removeView()) once it's destroyed - so a
        // caller doesn't have to remember to unregister a view it's about
        // to delete. Adding the same view twice is a no-op (checked by
        // pointer identity); this doesn't take ownership - see views_'s
        // own comment.
        void addView(View* view);

        // Unregisters view (already-removed or unknown views are a no-op)
        // - also called automatically when a registered view fires its own
        // onDestroyed, so views_ never holds a dangling pointer past that
        // point.
        void removeView(View* view);

        std::size_t viewCount() const { return views_.size(); }

        // Marks every registered view's style dirty (ViewStyle::markDirty()),
        // so each one repaints reflecting this Model's current state on its
        // next paint pass. Doesn't itself call onChanged() - a subclass
        // whose setValue()/clear() already fires onChanged() typically also
        // wants to call this right alongside it (or subscribe onChanged()
        // to it - see Model::Model()), not instead of it: onChanged() is
        // for arbitrary listeners (e.g. a Controller), this is specifically
        // "and now repaint every view showing me."
        void updateAllViews();

    private:
        SyncReturn handleViewDestroyed(View& view);

        // Views are not owned by Model. When a view is added, the model
        // subscribes to that view's own onDestroyed delegate so it's
        // removed automatically if the view goes away first - views_ never
        // holds a dangling pointer past that, without every caller having
        // to remember to call removeView() themselves before deleting a
        // view. viewDestroyedConnections_ is kept in lockstep with views_
        // (same index) purely so removeView() can drop that subscription
        // too when a view is unregistered while still alive - otherwise a
        // harmless but wasteful dead subscription would sit on that view's
        // onDestroyed for the rest of its life.
        std::vector<View*> views_;
        std::vector<Connection> viewDestroyedConnections_;
        std::uint32_t updateFlags_ = UpdateFlags::NoFlags;
    };

    // A Model that represents one open file - a text document, an image,
    // anything with real load/save semantics and a dirty flag. Adds
    // exactly what plain Model doesn't have: filePath()/isModified(), and
    // the load()/save() <-> readFromFile()/writeToFile() split (the
    // public pair handles path/modified bookkeeping; the protected pair,
    // implemented per concrete Document subclass, does the actual I/O -
    // same "public orchestration, protected/virtual per-subclass work"
    // shape as ViewController's loadView()).
    //
    // Abstract (readFromFile()/writeToFile() are pure virtual) - a
    // concrete subclass (e.g. a text document wrapping a std::string, an
    // image wrapping decoded pixels) provides the actual file format.
    //
    // See DocumentController (controllers.h) for owning/tracking a set of
    // open Documents - Document itself only knows about its own file, not
    // about any others that might be open alongside it.
    class Document : public Model {
    public:
        Document() = default;

        typedef Delegate<Document> DocumentDelegate;

        // Fired whenever isModified() actually flips, either direction -
        // e.g. to update a window title's/tab's "unsaved changes"
        // indicator without polling.
        DocumentDelegate onModifiedChanged;

        const std::string& filePath() const { return filePath_; }
        bool hasFilePath() const { return !filePath_.empty(); }

        bool isModified() const { return modified_; }

        // Marks this Document as having unsaved changes, firing
        // onModifiedChanged() if isModified() wasn't already true. Called
        // automatically by setValue() (see the override below) - a
        // subclass whose own mutators don't go through setValue() should
        // call this directly instead. A no-op while load() is running
        // readFromFile() - a document populating its own fields while
        // loading isn't "modified", it's just now equal to what's on disk.
        void markModified();

        // Loads via readFromFile(path); on success (only), adopts path as
        // filePath() and clears isModified(). Returns readFromFile()'s
        // result - on failure, filePath()/isModified() are left exactly
        // as they were before the call.
        bool load(const std::string& path);

        // Saves via writeToFile(path, or filePath() if path is empty);
        // on success, adopts the target as the new filePath() (so a "Save
        // As" naturally becomes this Document's new identity) and clears
        // isModified(). Returns false without calling writeToFile() at
        // all if path is empty and hasFilePath() is also false - nowhere
        // to save to.
        bool save(const std::string& path = std::string());

        // Model: chains to Model::setValue() first (still fires
        // onChanged()), then markModified() - a Document counts as
        // "modified" any time its value changes through the normal
        // Model API. A subclass with its own additional mutators should
        // call markModified() from those directly, the same as it would
        // fire onChanged() itself for a plain Model.
        void setValue(const std::any& newValue, const std::any& key = std::any()) override;

    protected:
        // Reads this Document's content from path - format-specific,
        // provided per concrete subclass. Return false on failure without
        // leaving this Document half-loaded if avoidable (load() only
        // adopts path/clears isModified() on true).
        virtual bool readFromFile(const std::string& path) = 0;

        // Writes this Document's content to path - see readFromFile().
        virtual bool writeToFile(const std::string& path) = 0;

    private:
        void setModifiedFlag(bool value);

        std::string filePath_;
        bool modified_ = false;
        bool loading_ = false;
    };
}
