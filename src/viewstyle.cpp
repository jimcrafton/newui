





#include "newui/viewstyle.h"
#include "newui/view.h"
#include "newui/rootview.h"
#include "newui/json5_helpers.h"

#include <json5/json5.hpp>
#include <json5/json5_builder.hpp>

namespace {

	const char* edge3DStyleToString(newui::Edge3DStyle s) {
		switch (s) {
			case newui::Edge3DStyle::Raised: return "Raised";
			case newui::Edge3DStyle::Sunken: return "Sunken";
			case newui::Edge3DStyle::Etched: return "Etched";
			case newui::Edge3DStyle::Bump: return "Bump";
		}
		return "Raised";
	}

	newui::Edge3DStyle edge3DStyleFromString(const std::string& s, newui::Edge3DStyle defaultValue) {
		if (s == "Raised") return newui::Edge3DStyle::Raised;
		if (s == "Sunken") return newui::Edge3DStyle::Sunken;
		if (s == "Etched") return newui::Edge3DStyle::Etched;
		if (s == "Bump") return newui::Edge3DStyle::Bump;
		return defaultValue;
	}

}

namespace newui {

	void ViewStyle::markDirty()
	{
		if (nullptr != view_) {
			view_->rootView()->markDirty();
		}
	}

	void ViewStyle::writeFields(json5::builder& w) const {
		writeColor(w, "backgroundFill", backgroundFill);
		writeColor(w, "borderFill", borderFill);
		w["borderWidth"] = borderWidth;
		writeColor(w, "highlightFill", highlightFill);
		w["opacity"] = opacity;
		// Raw int, not a name table: BLCompOp has ~30 blend2d composite-op
		// values and this field is essentially always left at its
		// SRC_OVER default - not worth a full string table for v1.
		w["compositingOp"] = int(compositingOp);
		writeFont(w, "font", font);
	}

	void ViewStyle::readFields(const json5::value& obj) {
		readColor(obj, "backgroundFill", backgroundFill);
		readColor(obj, "borderFill", borderFill);
		borderWidth = obj["borderWidth"].get<float>(borderWidth);
		readColor(obj, "highlightFill", highlightFill);
		opacity = obj["opacity"].get<float>(opacity);
		compositingOp = BLCompOp(obj["compositingOp"].get<int>(int(compositingOp)));
		font = readFont(obj["font"], font);
	}

	void ButtonStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["edgeStyle"] = w.new_string(edge3DStyleToString(edgeStyle));
		w["edgeWidth"] = edgeWidth;
		writeColor(w, "edgeHighlightColor", edgeHighlightColor);
		writeColor(w, "edgeShadowColor", edgeShadowColor);
	}

	void ButtonStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		edgeStyle = edge3DStyleFromString(obj["edgeStyle"].get_c_str(""), edgeStyle);
		edgeWidth = obj["edgeWidth"].get<float>(edgeWidth);
		readColor(obj, "edgeHighlightColor", edgeHighlightColor);
		readColor(obj, "edgeShadowColor", edgeShadowColor);
	}

	void LabelStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["text"] = w.new_string(text);
		writeColor(w, "textColor", textColor);
	}

	void LabelStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		text = obj["text"].get_c_str(text.c_str());
		readColor(obj, "textColor", textColor);
	}

	void CheckBoxStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["checked"] = checked;
		w["boxSize"] = boxSize;
		w["boxEdgeStyle"] = w.new_string(edge3DStyleToString(boxEdgeStyle));
		w["boxEdgeWidth"] = boxEdgeWidth;
		writeColor(w, "boxFill", boxFill);
		writeColor(w, "boxEdgeHighlightColor", boxEdgeHighlightColor);
		writeColor(w, "boxEdgeShadowColor", boxEdgeShadowColor);
		writeColor(w, "checkColor", checkColor);
		w["checkWidth"] = checkWidth;
		w["boxLabelSpacing"] = boxLabelSpacing;
	}

	void CheckBoxStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		checked = obj["checked"].get_bool(checked);
		boxSize = obj["boxSize"].get<float>(boxSize);
		boxEdgeStyle = edge3DStyleFromString(obj["boxEdgeStyle"].get_c_str(""), boxEdgeStyle);
		boxEdgeWidth = obj["boxEdgeWidth"].get<float>(boxEdgeWidth);
		readColor(obj, "boxFill", boxFill);
		readColor(obj, "boxEdgeHighlightColor", boxEdgeHighlightColor);
		readColor(obj, "boxEdgeShadowColor", boxEdgeShadowColor);
		readColor(obj, "checkColor", checkColor);
		checkWidth = obj["checkWidth"].get<float>(checkWidth);
		boxLabelSpacing = obj["boxLabelSpacing"].get<float>(boxLabelSpacing);
	}

	ThemedViewStyle::~ThemedViewStyle() {
		closeTheme();
	}

	void ThemedViewStyle::closeTheme() {
		if (theme_ != nullptr) {
			::CloseThemeData(theme_);
			theme_ = nullptr;
		}
	}

	Rect ThemedViewStyle::computeClientBounds(const Size& size) const {
		Rect fullRect(0.0f, 0.0f, size.width, size.height);

		// No theme cached yet - nothing has painted this style through a
		// live HWND, so there's no HTHEME to query GetThemeBackgroundContentRect()
		// against. See the class comment: a paint-free caller (e.g.
		// Layout::arrange() before this View has ever been painted) can
		// see this too-generous answer the very first time; resolves
		// itself once paint() has run once.
		if (theme_ == nullptr) {
			return fullRect;
		}

		RECT boundsRect{0, 0, static_cast<int>(size.width), static_cast<int>(size.height)};
		RECT contentRect{};
		// Neutral/normal state - content-rect deflation generally doesn't
		// vary by hover/pressed state for standard theme parts, and
		// computeClientBounds() (unlike paint()) has no "highlighted" of
		// its own to pass through (see ViewStyle::computeClientBounds()'s
		// own signature).
		if (FAILED(::GetThemeBackgroundContentRect(theme_, nullptr, partId(), stateId(false), &boundsRect, &contentRect))) {
			return fullRect;
		}

		return Rect(float(contentRect.left), float(contentRect.top),
			float(contentRect.right - contentRect.left), float(contentRect.bottom - contentRect.top));
	}

	void ThemedViewStyle::paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const {
		clientBounds = computeClientBounds(size);

		if (size.width <= 0.0f || size.height <= 0.0f) {
			return;
		}

		HWND hwnd = (view() != nullptr && view()->rootView() != nullptr)
			? view()->rootView()->windowHandle() : nullptr;
		if (hwnd == nullptr) {
			return;  // not attached to a live (initialize()'d) window yet
		}

		if (theme_ == nullptr) {
			theme_ = ::OpenThemeData(hwnd, themeClassName_.c_str());
			if (theme_ == nullptr) {
				return;  // theming unavailable (no comctl32 v6 manifest, ...) - draw nothing rather than guess a fallback
			}
		}

		const int width = static_cast<int>(size.width);
		const int height = static_cast<int>(size.height);
		RECT rect{0, 0, width, height};

		// A memory DC, not hwnd's own screen DC (e.g. via GetDC()) - this
		// paint() runs off-screen into an in-memory BLImage (see
		// RootView::repaint()), never during a real WM_PAINT/BeginPaint,
		// and BeginBufferedPaint() needs a real memory DC as its
		// destination target to actually draw real content into the
		// buffer it creates - a plain window screen DC silently produces
		// an all-transparent, undrawn buffer instead (confirmed by
		// inspecting the returned bits directly while diagnosing this).
		HDC targetDC = ::CreateCompatibleDC(nullptr);
		if (targetDC == nullptr) {
			return;
		}

		BP_PAINTPARAMS params{};
		params.cbSize = sizeof(BP_PAINTPARAMS);
		params.dwFlags = BPPF_ERASE;

		HDC bufferDC = nullptr;
		HPAINTBUFFER paintBuffer = ::BeginBufferedPaint(targetDC, &rect, BPBF_TOPDOWNDIB, &params, &bufferDC);
		if (paintBuffer != nullptr && bufferDC != nullptr) {
			::DrawThemeBackground(theme_, bufferDC, partId(), stateId(highlighted), &rect, nullptr);

			// DrawThemeBackground() draws via plain GDI under the hood for
			// most parts, which has no alpha-channel concept at all and
			// leaves the buffer's alpha at 0 throughout - except for parts
			// IsThemeBackgroundPartiallyTransparent() reports as genuinely
			// alpha-blended (rounded corners, glows, ...), where the theme
			// engine populates real per-pixel alpha itself via its own
			// alpha-aware rendering path. Force the whole buffer opaque
			// only for the former case - forcing it for the latter would
			// turn transparent rounded corners into solid black squares
			// (the RGB there is 0,0,0/uninitialized, only meant to stay
			// invisible under its real, low alpha).
			if (!::IsThemeBackgroundPartiallyTransparent(theme_, partId(), stateId(highlighted))) {
				::BufferedPaintSetAlpha(paintBuffer, nullptr, 255);
			}

			// GDI batches drawing calls - reading the buffer's raw pixel
			// memory directly (via GetBufferedPaintBits(), bypassing GDI's
			// own read APIs) can see stale/incomplete data unless any
			// pending GDI operations are flushed first.
			::GdiFlush();

			RGBQUAD* bits = nullptr;
			int rowWidthPixels = 0;  // GetBufferedPaintBits() only reports the row
			                          // stride (pcxRow) - which may exceed the
			                          // requested width due to internal padding,
			                          // hence using it (not size.width) for the
			                          // stride below - not the buffer's actual
			                          // width/height, which are just rect's.
			if (SUCCEEDED(::GetBufferedPaintBits(paintBuffer, &bits, &rowWidthPixels)) && bits != nullptr) {
				// Premultiplied, top-down BGRA - exactly BL_FORMAT_PRGB32,
				// so no channel/premultiplication conversion is needed
				// before wrapping these bits directly as a BLImage.
				BLImage themedImage;
				themedImage.create_from_data(width, height, BL_FORMAT_PRGB32, bits, intptr_t(rowWidthPixels) * 4);

				ctx.save();
				ctx.set_comp_op(compositingOp);
				ctx.blit_image(BLPoint(0, 0), themedImage);
				ctx.restore();
			}

			::EndBufferedPaint(paintBuffer, TRUE);
		}

		::DeleteDC(targetDC);
	}

	void ThemedButtonStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["pressed"] = pressed;
		w["enabled"] = enabled;
	}

	void ThemedButtonStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		pressed = obj["pressed"].get_bool(pressed);
		enabled = obj["enabled"].get_bool(enabled);
	}

	void ThemedCheckBoxStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["checked"] = checked;
		w["pressed"] = pressed;
		w["enabled"] = enabled;
	}

	void ThemedCheckBoxStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		checked = obj["checked"].get_bool(checked);
		pressed = obj["pressed"].get_bool(pressed);
		enabled = obj["enabled"].get_bool(enabled);
	}

}