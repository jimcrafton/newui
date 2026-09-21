#pragma once

#include <any>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "newui/newui.h"
#include "newui/component.h"
#include "newui/delegate.h"


namespace newui {
    class View;

    // The M in MVC - deliberately not part of the View tree itself; a
    // Model is arbitrary application data, not a node in that tree.
    //
    // A basic Control (controls.h) doesn't need one of these at all - it's
    // just a View that reacts to its own mouse/keyboard input. Model (and
    // Controller, controllers.h) exist for the more complex case: a widget
    // (or a whole screen, via ViewController) whose content is driven by
    // real data rather than just its own visual state.
    class Model : public Component {
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

        // How many addressable items this Model holds - e.g. row count
        // for a list/table-shaped Model, consulted by a future ListView/
        // TableView (controls.h) via ItemController::itemCount()
        // (controllers.h) to know how many rows exist without needing to
        // know anything else about the concrete Model subclass. Default
        // 0, independent of empty() (not empty() == (size() == 0)) -
        // empty()'s own existing contract is untouched; a concrete Model
        // subclass overrides whichever of the two it actually needs.
        virtual std::size_t size() const {
            return 0;
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

    // A Model that's flat and 0-based-indexed - the shape ListController/
    // ListItem (controllers.h/items.h) are built around, per items-plan.md's
    // own "a ListController would be associated with ListModel type data."
    // Adds nothing new over plain Model's own value()/size() other than
    // valueAt()/setValueAt() below - type-safe convenience wrappers so a
    // caller (or a concrete subclass's own code) doesn't have to box/
    // unbox the std::size_t index into the std::any key value()/setValue()
    // already take, by hand, at every call site. A concrete subclass
    // still overrides value()/size() exactly as it would for a plain
    // Model - see examples/mvc1.cpp's StringListModel for a real one.
    //
    // ListController::model()/setModel() (controllers.h) require this
    // type specifically, not plain Model* - a real, compile-time-checked
    // guarantee that whatever's attached to a ListController is genuinely
    // list-shaped, not just "answers value()/size() and hopes for the
    // best."
    class ListModel : public Model {
    public:
        std::any valueAt(std::size_t index) { return value(index); }
        void setValueAt(std::size_t index, const std::any& newValue) { setValue(newValue, index); }
    };

    // A ready-made ListModel of plain strings. items() is a reflected collection, so a design
    // holding one saves and reloads its rows like any other property; addItem()/removeItem()/
    // setValue()/clear() also fire onChanged so an attached view repaints.
    class StringListModel : public ListModel {
    public:
        // Declared (not implicit) so reflectgen registers it - a design reload builds one through it.
        StringListModel() = default;

        // What reflection reads and restores the rows through - it bypasses onChanged, so use the
        // members below to change a live model.
        std::vector<std::string>& items() { return items_; }
        const std::vector<std::string>& items() const { return items_; }

        void addItem(const std::string& text);
        // A no-op for an index past the end.
        void removeItem(std::size_t index);

        void clear() override;
        bool empty() const override { return items_.empty(); }
        std::size_t size() const override { return items_.size(); }

        // key is a std::size_t row index; the value is a std::string. An unknown key gives an empty any.
        std::any value(const std::any& key = std::any()) override;
        void setValue(const std::any& newValue, const std::any& key = std::any()) override;

    private:
        std::vector<std::string> items_;
    };

    // A Model that's hierarchical, addressed by path - the sequence of
    // child indices from the root down to a given node (an empty path is
    // the root itself) - the shape TreeController/TreeItem (controllers.h/
    // items.h) are built around, same "a TreeController would be
    // associated with TreeModel type data" idea items-plan.md already
    // describes for ListController/ListModel.
    //
    // childCount() is the one piece of information a tree needs beyond
    // plain Model::value(path) that Model itself can't generically answer
    // - "how many direct children does this node have" - same reasoning
    // as ListModel's own size(). Default 0 (no children anywhere); a
    // concrete subclass overrides it against whatever real hierarchical
    // data structure backs it - see examples/mvc1.cpp's StringTreeModel
    // for a real one.
    //
    // TreeController::model()/setModel() (controllers.h) require this
    // type specifically, not plain Model* - same compile-time (plus
    // dynamic_cast-checked) guarantee ListController::model()/setModel()
    // already give for ListModel.
    class TreeModel : public Model {
    public:
        virtual std::size_t childCount(const std::vector<std::size_t>& path) const { return 0; }
        bool hasChildren(const std::vector<std::size_t>& path) const { return childCount(path) > 0; }
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

        // Back to a fresh, untitled, unmodified document (no filePath()) - e.g. a "New" that reuses
        // this same Document. Fires onModifiedChanged() if it was modified. Deliberately keeps
        // which paths were already backed up (see below), so New followed by reopening and saving
        // the same file doesn't replace the original backup with this app's own output. Doesn't
        // touch the document's own content - clearing that is the subclass's job.
        void reset();

        // Before save() first overwrites an existing file, the on-disk original is copied to
        // "<path>.bak" - once per path per Document instance, so the backup keeps the version from
        // before this app first touched the file (a rolling backup would lose it after two quick
        // saves). Protects whatever a serializer doesn't round-trip (hand-written comments,
        // formatting). On by default; best-effort - a failed backup never blocks the save.
        bool backupBeforeFirstOverwrite() const { return backupBeforeFirstOverwrite_; }
        void setBackupBeforeFirstOverwrite(bool value) { backupBeforeFirstOverwrite_ = value; }

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
        void backupIfFirstOverwrite(const std::string& target);
        std::string filePath_;
        bool backupBeforeFirstOverwrite_ = true;
        std::set<std::string> backupHandledPaths_;
        bool modified_ = false;
        bool loading_ = false;
    };
}
