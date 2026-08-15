#include "newui/models.h"

#include <algorithm>

#include "newui/view.h"

namespace newui {

    void Model::setValue(const std::any& newValue, const std::any& key) {
        onChanged(*this);
    }

    void Model::addView(View* view) {
        if (view == nullptr) {
            return;
        }
        if (std::find(views_.begin(), views_.end(), view) != views_.end()) {
            return;
        }
        views_.push_back(view);
        viewDestroyedConnections_.push_back(view->onDestroyed.add(this, &Model::handleViewDestroyed));
    }

    void Model::removeView(View* view) {
        auto it = std::find(views_.begin(), views_.end(), view);
        if (it == views_.end()) {
            return;
        }
        std::size_t index = static_cast<std::size_t>(it - views_.begin());

        // The view whose onDestroyed we're unsubscribing from is still
        // alive here (removeView() called directly, not via
        // handleViewDestroyed()) - unlike the destroyed case, where
        // removing the now-dangling subscription would be pointless (the
        // view, and its Delegate, are already gone).
        if (view != nullptr) {
            view->onDestroyed.remove(viewDestroyedConnections_[index]);
        }

        views_.erase(it);
        viewDestroyedConnections_.erase(viewDestroyedConnections_.begin() + static_cast<std::ptrdiff_t>(index));
    }

    void Model::updateAllViews() {
        for (View* view : views_) {
            view->style().markDirty();
        }
    }

    SyncReturn Model::handleViewDestroyed(View& view) {
        auto it = std::find(views_.begin(), views_.end(), &view);
        if (it == views_.end()) {
            return SyncReturn::Ignored;
        }
        std::size_t index = static_cast<std::size_t>(it - views_.begin());

        // view is already being destroyed - its own onDestroyed Delegate
        // is going away along with it, so there's nothing to unsubscribe
        // here (unlike removeView()'s still-alive case above).
        views_.erase(it);
        viewDestroyedConnections_.erase(viewDestroyedConnections_.begin() + static_cast<std::ptrdiff_t>(index));
        return SyncReturn::Handled;
    }

    // -----------------------------------------------------------------
    // Document
    // -----------------------------------------------------------------

    void Document::markModified() {
        if (loading_) {
            return;
        }
        setModifiedFlag(true);
    }

    bool Document::load(const std::string& path) {
        loading_ = true;
        bool ok = readFromFile(path);
        loading_ = false;

        if (ok) {
            filePath_ = path;
            setModifiedFlag(false);
        }
        return ok;
    }

    bool Document::save(const std::string& path) {
        const std::string& target = path.empty() ? filePath_ : path;
        if (target.empty()) {
            return false;
        }

        bool ok = writeToFile(target);
        if (ok) {
            filePath_ = target;
            setModifiedFlag(false);
        }
        return ok;
    }

    void Document::setValue(const std::any& newValue, const std::any& key) {
        Model::setValue(newValue, key);
        markModified();
    }

    void Document::setModifiedFlag(bool value) {
        if (modified_ == value) {
            return;
        }
        modified_ = value;
        onModifiedChanged(*this);
    }

}
