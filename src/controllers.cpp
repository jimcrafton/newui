#include "newui/controllers.h"

#include <algorithm>

#include "newui/animation.h"
#include "newui/application.h"
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

        Application::instance().runLoop().postIdle([this, animation, onComplete]() {
            if (AnimationManager::currentFrame() < animation->endTime()) {
                return false;
            }
            (this->*onComplete)();
            return true;
        });
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

} // namespace newui
