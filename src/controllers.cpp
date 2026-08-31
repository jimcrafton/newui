#include "newui/newui.h"
#include "newui/controllers.h"

#include <algorithm>

#include "newui/animation.h"
#include "newui/application.h"
#include "newui/items.h"
#include "newui/reflection.h"
#include "newui/runloop.h"
#include "newui/subview.h"
#include "newui/view.h"

namespace newui
{
    // ---------------------------------------------------------------------
    // Controller
    // ---------------------------------------------------------------------

    Controller::~Controller() {
        if (model_ != nullptr) {
            model_->onChanged.remove(modelChangedConnection_);
        }
    }

    void Controller::setModel(Model* val) {
        if (model_ != nullptr) {
            model_->onChanged.remove(modelChangedConnection_);
        }
        model_ = val;
        if (model_ != nullptr) {
            modelChangedConnection_ = model_->onChanged.add(this, &Controller::modelChanged);
        }
    }

    // ---------------------------------------------------------------------
    // ViewController
    // ---------------------------------------------------------------------

    ViewController::~ViewController() {
        // Unlink from a parent's childControllers_ if this is being
        // deleted directly (without dismiss()/removeFromParentController()
        // first) - otherwise the parent is left holding a dangling
        // pointer. Plain bookkeeping, not a virtual call, so it's safe to
        // do here unlike viewWillDisappear()/viewDidDisappear() (see this
        // destructor's own doc comment in controllers.h).
        if (parentController_ != nullptr) {
            auto& siblings = parentController_->childControllers_;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
        }

        if (view_ != nullptr) {
            if (container_ != nullptr) {
                container_->removeChild(view_);
            }
            view_->destroy();
            delete view_;
        }
    }

    SubView* ViewController::view() {
        if (view_ == nullptr) {
            view_ = loadView();
            viewDidLoad();
        }
        return view_;
    }

    void ViewController::present(View* container, ViewController* parent) {
        if (container == nullptr) {
            return;
        }

        container_ = container;
        parentController_ = parent;
        if (parent != nullptr) {
            parent->childControllers_.push_back(this);
        }

        container_->addChild(view());

        viewWillAppear();
        waitForAnimationThen(appearingAnimation(), &ViewController::finishAppearing);
    }

    void ViewController::dismiss() {
        if (container_ == nullptr) {
            return;
        }

        viewWillDisappear();
        waitForAnimationThen(disappearingAnimation(), &ViewController::finishDisappearing);
    }

    void ViewController::addChildController(ViewController* child) {
        if (child == nullptr) {
            return;
        }
        child->present(view(), this);
    }

    void ViewController::finishAppearing() {
        viewDidAppear();
    }

    void ViewController::finishDisappearing() {
        if (container_ != nullptr && view_ != nullptr) {
            container_->removeChild(view_);
        }

        if (parentController_ != nullptr) {
            auto& siblings = parentController_->childControllers_;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
            parentController_ = nullptr;
        }

        container_ = nullptr;
        viewDidDisappear();
    }

    void ViewController::waitForAnimationThen(Animation* animation, void (ViewController::* onComplete)()) {
        if (animation == nullptr) {
            (this->*onComplete)();
            return;
        }

        RunLoop& loop = RunLoop::current();
        if (loop) {
            loop.postIdle([this, animation, onComplete]() {
                if (AnimationManager::currentFrame() < animation->endTime()) {
                    return false;
                }
                (this->*onComplete)();
                return true;
                });
        }
        
    }

    // ---------------------------------------------------------------------
    // DocumentController
    // ---------------------------------------------------------------------

    DocumentController::~DocumentController() {
        for (Document* doc : documents_) {
            delete doc;
        }
    }

    void DocumentController::setActiveDocument(Document* document) {
        if (document != nullptr
                && std::find(documents_.begin(), documents_.end(), document) == documents_.end()) {
            return;
        }
        if (activeDocument_ == document) {
            return;
        }
        activeDocument_ = document;
        onActiveDocumentChanged(*this);
    }

    void DocumentController::addDocument(Document* document) {
        if (document == nullptr) {
            return;
        }
        if (std::find(documents_.begin(), documents_.end(), document) != documents_.end()) {
            return;
        }
        documents_.push_back(document);
        onDocumentAdded(*this, *document);
        setActiveDocument(document);
    }

    bool DocumentController::openDocument(Document* document, const std::string& path) {
        addDocument(document);
        return document != nullptr ? document->load(path) : false;
    }

    void DocumentController::closeDocument(Document* document) {
        if (document == nullptr) {
            return;
        }
        auto it = std::find(documents_.begin(), documents_.end(), document);
        if (it == documents_.end()) {
            return;
        }

        onDocumentWillClose(*this, *document);

        bool wasActive = (activeDocument_ == document);
        documents_.erase(it);
        delete document;

        if (wasActive) {
            setActiveDocument(documents_.empty() ? nullptr : documents_.back());
        }
    }

    // ---------------------------------------------------------------------
    // ItemController
    // ---------------------------------------------------------------------

    void ItemController::releaseItem(Item* item) {
        if (item == nullptr) {
            return;
        }
        pool_.push_back(std::unique_ptr<Item>(item));
    }

    Item* ItemController::instantiateItem(const std::string& className) const {
        const reflection::Class* cls = reflection::classinfo(className);
        if (cls == nullptr) {
            throw std::runtime_error(
                "ItemController::instantiateItem: unknown Item class \"" + className
                + "\" - not registered (did registerReflectionData() run before this?)");
        }
        void* raw = nullptr;
        cls->createInstance(&raw);
        if (raw == nullptr) {
            throw std::runtime_error(
                "ItemController::instantiateItem: Class::createInstance() failed for \"" + className
                + "\" (no registered default constructor, or an abstract class?)");
        }
        return static_cast<Item*>(raw);
    }

    Item* ItemController::takeFromPoolOrInstantiate(const std::string& className) {
        if (!pool_.empty()) {
            std::unique_ptr<Item> item = std::move(pool_.back());
            pool_.pop_back();
            return item.release();
        }
        return instantiateItem(className);
    }

    // ---------------------------------------------------------------------
    // ListController
    // ---------------------------------------------------------------------

    ListController::ListController() {
        setDefaultItemClassName("ListItem");
    }

    ListItem* ListController::createItem(std::size_t /*index*/) {
        return static_cast<ListItem*>(takeFromPoolOrInstantiate(defaultItemClassName()));
    }

    float ListController::totalHeight() const {
        float total = 0.0f;
        std::size_t count = itemCount();
        for (std::size_t i = 0; i < count; ++i) {
            total += itemHeight(i);
        }
        return total;
    }

    float ListController::itemOffset(std::size_t index) const {
        float offset = 0.0f;
        std::size_t count = itemCount();
        std::size_t last = index < count ? index : count;
        for (std::size_t i = 0; i < last; ++i) {
            offset += itemHeight(i);
        }
        return offset;
    }

    std::size_t ListController::indexAt(float contentY) const {
        std::size_t count = itemCount();
        float offset = 0.0f;
        for (std::size_t i = 0; i < count; ++i) {
            float height = itemHeight(i);
            if (contentY < offset + height) {
                return i;
            }
            offset += height;
        }
        return count;
    }

    // ---------------------------------------------------------------------
    // TreeController
    // ---------------------------------------------------------------------

    TreeController::TreeController() {
        setDefaultItemClassName("TreeItem");
    }

    TreeItem* TreeController::createItem(const std::vector<std::size_t>& /*path*/) {
        return static_cast<TreeItem*>(takeFromPoolOrInstantiate(defaultItemClassName()));
    }

    float TreeController::totalHeight() const {
        float total = 0.0f;
        std::size_t count = visibleCount();
        for (std::size_t i = 0; i < count; ++i) {
            total += itemHeight(i);
        }
        return total;
    }

    float TreeController::itemOffset(std::size_t visibleIndex) const {
        float offset = 0.0f;
        std::size_t count = visibleCount();
        std::size_t last = visibleIndex < count ? visibleIndex : count;
        for (std::size_t i = 0; i < last; ++i) {
            offset += itemHeight(i);
        }
        return offset;
    }

    std::size_t TreeController::indexAt(float contentY) const {
        std::size_t count = visibleCount();
        float offset = 0.0f;
        for (std::size_t i = 0; i < count; ++i) {
            float height = itemHeight(i);
            if (contentY < offset + height) {
                return i;
            }
            offset += height;
        }
        return count;
    }

    void TreeController::rebuildVisibleListIfNeeded() const {
        if (!visibleListDirty_) {
            return;
        }
        visiblePaths_.clear();
        appendVisibleChildren(std::vector<std::size_t>());
        visibleListDirty_ = false;
    }

    void TreeController::appendVisibleChildren(const std::vector<std::size_t>& parentPath) const {
        const TreeModel* treeModel = model();
        if (treeModel == nullptr) {
            return;
        }
        std::size_t count = treeModel->childCount(parentPath);
        for (std::size_t i = 0; i < count; ++i) {
            std::vector<std::size_t> childPath = parentPath;
            childPath.push_back(i);
            visiblePaths_.push_back(childPath);
            if (isExpanded(childPath)) {
                appendVisibleChildren(childPath);
            }
        }
    }

    // ---------------------------------------------------------------------
    // TableController
    // ---------------------------------------------------------------------

    TableController::TableController() {
        setDefaultItemClassName("TableItem");
    }

    TableItem* TableController::createItem(std::size_t /*row*/, std::size_t /*col*/) {
        return static_cast<TableItem*>(takeFromPoolOrInstantiate(defaultItemClassName()));
    }

} // namespace newui
