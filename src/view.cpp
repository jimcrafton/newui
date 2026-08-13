#include "newui/view.h"
#include "newui/subview.h"
#include "newui/json5_helpers.h"

#include <json5/json5.hpp>
#include <json5/json5_builder.hpp>

namespace newui {
	bool View::initialize()
	{
		return true;
	}

	void View::destroy()
	{
		// Deliberately re-reads childViews_.front() each pass rather than
		// holding an iterator across the loop: child->destroy() removes
		// itself from its parent's childViews_ (see SubView::destroy()'s
		// parent_->removeChild(this)) - when that parent is *this* (the
		// common case, destroying a whole subtree top-down), a live
		// range-based-for iterator would be invalidated out from under it
		// mid-walk. count is a sanity check, not the loop's termination
		// condition (that's childViews_.empty()) - it catches "still had
		// N-original-children left over after N iterations" (e.g.
		// something re-added a child mid-destroy), not "a child failed to
		// remove itself" (that fails earlier and louder: the next
		// iteration's front() would still be the just-deleted child,
		// crashing on the use-after-free before this check is ever
		// reached).
		std::size_t count = childViews_.size();
		while (!childViews_.empty()) {
			SubView* child = childViews_.front();
			child->destroy();  // removes itself from childViews_ - see above
			delete child;
			--count;
			if (count == 0 && !childViews_.empty()) {
				throw std::runtime_error("removal logic busted in this view");
			}
		}

		onDestroyed(*this);
	}

	void View::addChild(SubView* child)
	{
		childViews_.push_back(child);
		
		updateLayout();
	}

	void View::removeChild(SubView* child) {
		childViews_.erase(std::remove(childViews_.begin(), childViews_.end(), child), childViews_.end());
		updateLayout();
	}

	void View::setLayout(std::unique_ptr<Layout> layout) {
		layout_ = std::move(layout);
		updateLayout();
	}

	void View::updateLayout() {
		if (layout_) {
			layout_->arrange(*this);
		}
	}

	void View::propagateRootView(RootView* root) {
		setRootView(root);
		for (SubView* child : childViews_) {
			child->propagateRootView(root);
		}
	}

	void View::paintChildren(BLContext& ctx) {
		for (SubView* child : childViews_) {
			if (!child->isVisible()) {
				continue;
			}

			const Rect& bounds = child->getBounds();

			ctx.save();
			ctx.translate(bounds.left(), bounds.top());
			ctx.clip_to_rect(BLRect(0, 0, bounds.size().width, bounds.size().height));

			child->paintStyle(ctx);
			child->paint(ctx);
			child->paintChildren(ctx);

			ctx.restore();
		}
	}

	SubView* View::hitTestChildren(const Point& localPt, Point& outLocalPt) const {
		for (auto it = childViews_.rbegin(); it != childViews_.rend(); ++it) {
			SubView* child = *it;
			if (!child->isVisible()) {
				continue;
			}

			const Rect& bounds = child->getBounds();
			if (!bounds.contains(localPt)) {
				continue;
			}

			Point childLocalPt(localPt.x - bounds.left(), localPt.y - bounds.top());

			Point deeperLocalPt;
			if (SubView* deeper = child->hitTestChildren(childLocalPt, deeperLocalPt)) {
				outLocalPt = deeperLocalPt;
				return deeper;
			}

			outLocalPt = childLocalPt;
			return child;
		}

		return nullptr;
	}

	void View::paintStyle(BLContext& ctx) {
		if (style_) {
			Rect unused;
			style_->paint(ctx, bounds_.size(), highlighted_, unused);
		}
	}

	void View::writeFields(json5::builder& w) const {
		w["name"] = w.new_string(name_);
		w["visible"] = visible_;
		writeRect(w, "bounds", bounds_);
		if (desiredSizeOverride_.has_value()) {
			writeSize(w, "desiredSize", *desiredSizeOverride_);
		}
		// A Custom cursor with no path() (built via cursor().setImage() -
		// an in-memory image has no stable, file-portable representation,
		// same reasoning as BLPattern/BLGradient fills - viewstyle.cpp/
		// HANDOFF.md) is skipped entirely. A Custom cursor loaded from a
		// file (setPath()) *is* portable via that same path, so it
		// round-trips as both fields below instead of being skipped.
		if (cursor_.kind() != CursorKind::Custom) {
			w["cursor"] = w.new_string(Cursor::cursorKindToString(cursor_.kind()));
		} else if (!cursor_.path().empty()) {
			w["cursor"] = w.new_string(Cursor::cursorKindToString(CursorKind::Custom));
			w["cursorPath"] = w.new_string(cursor_.path());
		}
	}

	void View::readFields(const json5::value& obj) {
		name_ = obj["name"].get_c_str(name_.c_str());
		visible_ = obj["visible"].get_bool(visible_);
		bounds_ = readRect(obj["bounds"], bounds_);
		if (json5::value v = obj["desiredSize"]; v.is_object()) {
			desiredSizeOverride_ = readSize(v);
		} else {
			desiredSizeOverride_.reset();
		}

		std::string cursorKindStr = obj["cursor"].get_c_str("");
		if (!cursorKindStr.empty()) {
			CursorKind kind = Cursor::cursorKindFromString(cursorKindStr, cursor_.kind());
			if (kind == CursorKind::Custom) {
				std::string cursorPath = obj["cursorPath"].get_c_str("");
				// setPath() failing (file missing/unreadable at load time)
				// leaves cursor_ untouched, same "current cursor left as-is
				// on failure" contract setPath() always has.
				if (!cursorPath.empty()) {
					cursor_.setPath(cursorPath);
				}
			} else {
				cursor_.setCursorKind(kind);
			}
		}
	}
}