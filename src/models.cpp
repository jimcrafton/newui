#include "newui/models.h"

#include <algorithm>
#include <filesystem>

#include "newui/view.h"

namespace newui {

    void Model::setValue(const std::any& newValue, const std::any& key) {
        onChanged(*this);
    }

    void StringListModel::addItem(const std::string& text) {
        items_.push_back(text);
        onChanged(*this);
    }

    void StringListModel::removeItem(std::size_t index) {
        if (index >= items_.size()) {
            return;
        }
        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
        onChanged(*this);
    }

    void StringListModel::clear() {
        items_.clear();
        Model::clear();
        onChanged(*this);
    }

    std::any StringListModel::value(const std::any& key) {
        if (const std::size_t* index = std::any_cast<std::size_t>(&key)) {
            if (*index < items_.size()) {
                return items_[*index];
            }
        }
        return std::any();
    }

    void StringListModel::setValue(const std::any& newValue, const std::any& key) {
        const std::size_t* index = std::any_cast<std::size_t>(&key);
        const std::string* text = std::any_cast<std::string>(&newValue);
        if (index != nullptr && text != nullptr && *index < items_.size()) {
            items_[*index] = *text;
        }
        Model::setValue(newValue, key);
    }

    std::vector<std::size_t> StringTreeModel::childRowIndices(std::size_t parentIndex, std::size_t parentDepth) const {
        std::vector<std::size_t> children;
        std::size_t start = parentIndex == rows_.size() ? 0 : parentIndex + 1;
        for (std::size_t i = start; i < rows_.size(); ++i) {
            if (parentIndex != rows_.size() && rows_[i].depth <= parentDepth) {
                break;
            }
            if (rows_[i].depth == (parentIndex == rows_.size() ? 0 : parentDepth + 1)) {
                children.push_back(i);
            }
        }
        return children;
    }

    std::size_t StringTreeModel::rowIndexForPath(const std::vector<std::size_t>& path) const {
        std::size_t current = rows_.size();   // the root, per childRowIndices()'s own convention
        std::size_t depth = 0;
        for (std::size_t index : path) {
            std::vector<std::size_t> children = childRowIndices(current, depth);
            if (index >= children.size()) {
                return rows_.size();
            }
            current = children[index];
            depth = rows_[current].depth;
        }
        return current;
    }

    void StringTreeModel::addItem(const std::string& text) {
        rows_.push_back(TreeRow{ 0, text });
        onChanged(*this);
    }

    void StringTreeModel::removeLastItem() {
        if (rows_.empty()) {
            return;
        }
        rows_.pop_back();
        onChanged(*this);
    }

    void StringTreeModel::clear() {
        rows_.clear();
        Model::clear();
        onChanged(*this);
    }

    std::size_t StringTreeModel::childCount(const std::vector<std::size_t>& path) const {
        std::size_t parent = rowIndexForPath(path);
        if (parent == rows_.size() && !path.empty()) {
            return 0;   // path doesn't resolve to a real row
        }
        std::size_t depth = parent == rows_.size() ? 0 : rows_[parent].depth;
        return childRowIndices(parent, depth).size();
    }

    std::any StringTreeModel::value(const std::any& key) {
        if (const std::vector<std::size_t>* path = std::any_cast<std::vector<std::size_t>>(&key)) {
            std::size_t row = rowIndexForPath(*path);
            if (row < rows_.size()) {
                return rows_[row].text;
            }
        }
        return std::any();
    }

    void StringTreeModel::setValue(const std::any& newValue, const std::any& key) {
        const std::vector<std::size_t>* path = std::any_cast<std::vector<std::size_t>>(&key);
        const std::string* text = std::any_cast<std::string>(&newValue);
        if (path != nullptr && text != nullptr) {
            std::size_t row = rowIndexForPath(*path);
            if (row < rows_.size()) {
                rows_[row].text = *text;
            }
        }
        Model::setValue(newValue, key);
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

        backupIfFirstOverwrite(target);

        bool ok = writeToFile(target);
        if (ok) {
            filePath_ = target;
            setModifiedFlag(false);
        }
        return ok;
    }

    void Document::reset() {
        filePath_.clear();
        setModifiedFlag(false);
    }

    void Document::backupIfFirstOverwrite(const std::string& target) {
        if (!backupBeforeFirstOverwrite_ || backupHandledPaths_.count(target) != 0) {
            return;
        }
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path source = fs::u8path(target);
        if (!fs::exists(source, ec)) {
            backupHandledPaths_.insert(target);  // nothing to preserve; later saves overwrite our own output
            return;
        }
        fs::path backup = source;
        backup += ".bak";
        fs::copy_file(source, backup, fs::copy_options::overwrite_existing, ec);
        if (!ec) {
            backupHandledPaths_.insert(target);  // on failure, try again next save
        }
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
