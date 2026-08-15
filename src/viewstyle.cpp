





#include "newui/viewstyle.h"
#include "newui/view.h"
#include "newui/rootview.h"
#include "newui/json5_helpers.h"
#include "newui/color.h"
#include "newui/uicolormanager.h"

#include <json5/json5.hpp>
#include <json5/json5_builder.hpp>

namespace {

	// Approximates a dark-mode look for a theme part uxtheme has no real
	// dark visual for at all (see RootView::refreshThemes()'s doc comment)
	// by inverting each pixel's HSL lightness in place - the same "smart
	// invert" heuristic some browsers use to dark-ify content with no real
	// dark asset. Hue and alpha are left untouched, so e.g. a red glyph
	// stays recognizably red, just lighter against a lighter background
	// instead of darker against a darker one - not a real dark-mode asset,
	// just a fake approximation of one.
	//
	// Unpremultiplies each pixel before doing the HSL math, then
	// re-premultiplies the result - these bytes are premultiplied BGRA
	// (see the call site), so px.rgbRed/Green/Blue already equal
	// straightRGB * alpha. An earlier version of this function skipped
	// that step, inverting the premultiplied bytes directly on the
	// (mistaken) assumption that only the thin anti-aliased edge of a
	// partially-transparent part (a glow, a rounded corner) would ever
	// see a nonzero-RGB/low-alpha pixel. That's wrong for a part whose
	// "normal" state is *entirely* transparent - e.g. MENU_BARITEM's
	// MBI_NORMAL (ThemedMenuBarItemStyle) draws nothing at all for an
	// unhighlighted top-level menu item - where inverting straight black
	// (alpha=0) in premultiplied space produced RGB=white still tagged
	// alpha=0, an invalid premultiplied pixel. blend2d's premultiplied
	// "over" compositing (Result = SrcRGB + DstRGB*(1-SrcA)) then adds
	// that leftover white directly on top of whatever's behind it
	// regardless of alpha, showing up as a solid opaque white box exactly
	// where nothing should have been drawn - reported live as the
	// MenuBar's own top-level items looking wrong in Dark mode (their
	// label text, itself correctly dark-mode-colored via UIColorManager,
	// then invisible against that same-color white ghost box).
	//
	// highlighted (threaded straight through from paint()'s own parameter)
	// applies a much stronger boost specifically for a hot/pressed state's
	// own bitmap. Measured live against MENU_BARITEM's MBI_HOT
	// (ThemedMenuBarItemStyle, hovering "File"): the *plain* kGammaNormal
	// boost above only reached RGB(10,10,10) against an unhovered
	// RGB(0,0,0) background - because this visual style's native hot fill
	// is itself only barely lighter than its own normal-state white
	// background (l ~ 0.995 vs. 1.0) to begin with. A gamma curve can only
	// re-shape an *existing* numeric gap, not manufacture one that was
	// never really there in relative terms - kGammaNormal's 0.6 simply
	// isn't steep enough near zero to pull a ~0.005 gap up to anything
	// visible. kGammaHighlighted is deliberately much smaller (steeper
	// near 0) so that same near-invisible gap lands somewhere clearly
	// perceptible instead - reserved for the highlighted case specifically
	// (rather than lowering kGammaNormal for everything) so static chrome
	// elsewhere doesn't get the same aggressive, washed-out treatment.
	void invertLightnessInPlace(RGBQUAD* bits, int width, int height, int strideInPixels, bool highlighted) {
		for (int y = 0; y < height; ++y) {
			RGBQUAD* row = bits + intptr_t(y) * strideInPixels;
			for (int x = 0; x < width; ++x) {
				RGBQUAD& px = row[x];
				if (px.rgbReserved == 0) {
					continue;  // fully transparent - no straight-alpha color to unpremultiply/invert, and premultiplied RGB must stay 0 too
				}

				float alpha = float(px.rgbReserved) / 255.0f;
				newui::Color color(
					float(px.rgbRed) / 255.0f / alpha,
					float(px.rgbGreen) / 255.0f / alpha,
					float(px.rgbBlue) / 255.0f / alpha,
					alpha);

				newui::HSLColor hsl = color.toHSL();

				// Plain complement (1-l) preserves the *numeric* lightness
				// gap between two colors, but not the *perceived* one: a
				// hot/pressed highlight that's normally much lighter than
				// its surrounding chrome (e.g. MBI_HOT vs. MBI_NORMAL,
				// ThemedMenuBarItemStyle) inverts to something only
				// slightly less black than that same chrome's own
				// inverted-to-black background - human contrast
				// sensitivity is much lower down near black than up near
				// white, so the same 0.15-ish delta reads as "barely
				// highlighted" post-invert even though it was clearly
				// visible pre-invert. kGamma<1 counters this by expanding
				// the low end of the inverted range while leaving both
				// ends fixed (0^kGamma=0, 1^kGamma=1) - a true white
				// background still inverts to true black, but anything
				// that inverts to only-slightly-off-black gets pulled
				// noticeably lighter instead.
				constexpr float kGammaNormal = 0.6f;
				constexpr float kGammaHighlighted = 0.2f;
				hsl.l = std::pow(1.0f - hsl.l, highlighted ? kGammaHighlighted : kGammaNormal);

				// toBGRA32() writes exactly RGBQUAD's own B,G,R,A byte
				// order (see its doc comment in color.h), clamping/
				// rounding each channel the same way every other Color
				// byte-packing call in this codebase does - straightBytes
				// is therefore still straight (unpremultiplied) alpha,
				// same as color/hsl above; re-premultiply by its own
				// (unchanged) alpha byte before writing back to px.
				uint8_t straightBytes[4];
				newui::Color::fromHSL(hsl).toBGRA32(straightBytes);
				float straightAlpha = float(straightBytes[3]) / 255.0f;
				px.rgbBlue = uint8_t(std::lround(straightBytes[0] * straightAlpha));
				px.rgbGreen = uint8_t(std::lround(straightBytes[1] * straightAlpha));
				px.rgbRed = uint8_t(std::lround(straightBytes[2] * straightAlpha));
				px.rgbReserved = straightBytes[3];
			}
		}
	}

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

	const char* tabItemPositionToString(newui::ThemedTabItemStyle::Position p) {
		switch (p) {
			case newui::ThemedTabItemStyle::Position::Middle: return "Middle";
			case newui::ThemedTabItemStyle::Position::Left: return "Left";
			case newui::ThemedTabItemStyle::Position::Right: return "Right";
			case newui::ThemedTabItemStyle::Position::Only: return "Only";
		}
		return "Middle";
	}

	newui::ThemedTabItemStyle::Position tabItemPositionFromString(const std::string& s, newui::ThemedTabItemStyle::Position defaultValue) {
		if (s == "Middle") return newui::ThemedTabItemStyle::Position::Middle;
		if (s == "Left") return newui::ThemedTabItemStyle::Position::Left;
		if (s == "Right") return newui::ThemedTabItemStyle::Position::Right;
		if (s == "Only") return newui::ThemedTabItemStyle::Position::Only;
		return defaultValue;
	}

	const char* tabItemAlignmentToString(newui::ThemedTabItemStyle::TabAlignment a) {
		switch (a) {
			case newui::ThemedTabItemStyle::TabAlignment::Top: return "Top";
			case newui::ThemedTabItemStyle::TabAlignment::Bottom: return "Bottom";
			case newui::ThemedTabItemStyle::TabAlignment::Left: return "Left";
			case newui::ThemedTabItemStyle::TabAlignment::Right: return "Right";
		}
		return "Top";
	}

	newui::ThemedTabItemStyle::TabAlignment tabItemAlignmentFromString(const std::string& s, newui::ThemedTabItemStyle::TabAlignment defaultValue) {
		if (s == "Top") return newui::ThemedTabItemStyle::TabAlignment::Top;
		if (s == "Bottom") return newui::ThemedTabItemStyle::TabAlignment::Bottom;
		if (s == "Left") return newui::ThemedTabItemStyle::TabAlignment::Left;
		if (s == "Right") return newui::ThemedTabItemStyle::TabAlignment::Right;
		return defaultValue;
	}

	const char* scrollbarArrowDirectionToString(newui::ThemedScrollbarArrowStyle::Direction d) {
		switch (d) {
			case newui::ThemedScrollbarArrowStyle::Direction::Up: return "Up";
			case newui::ThemedScrollbarArrowStyle::Direction::Down: return "Down";
			case newui::ThemedScrollbarArrowStyle::Direction::Left: return "Left";
			case newui::ThemedScrollbarArrowStyle::Direction::Right: return "Right";
		}
		return "Up";
	}

	newui::ThemedScrollbarArrowStyle::Direction scrollbarArrowDirectionFromString(const std::string& s, newui::ThemedScrollbarArrowStyle::Direction defaultValue) {
		if (s == "Up") return newui::ThemedScrollbarArrowStyle::Direction::Up;
		if (s == "Down") return newui::ThemedScrollbarArrowStyle::Direction::Down;
		if (s == "Left") return newui::ThemedScrollbarArrowStyle::Direction::Left;
		if (s == "Right") return newui::ThemedScrollbarArrowStyle::Direction::Right;
		return defaultValue;
	}

	const char* scrollbarTrackPositionToString(newui::ThemedScrollbarTrackStyle::Position p) {
		switch (p) {
			case newui::ThemedScrollbarTrackStyle::Position::Lower: return "Lower";
			case newui::ThemedScrollbarTrackStyle::Position::Upper: return "Upper";
		}
		return "Lower";
	}

	newui::ThemedScrollbarTrackStyle::Position scrollbarTrackPositionFromString(const std::string& s, newui::ThemedScrollbarTrackStyle::Position defaultValue) {
		if (s == "Lower") return newui::ThemedScrollbarTrackStyle::Position::Lower;
		if (s == "Upper") return newui::ThemedScrollbarTrackStyle::Position::Upper;
		return defaultValue;
	}

	const char* progressBarFillStateToString(newui::ThemedProgressBarFillStyle::FillState s) {
		switch (s) {
			case newui::ThemedProgressBarFillStyle::FillState::Normal: return "Normal";
			case newui::ThemedProgressBarFillStyle::FillState::Error: return "Error";
			case newui::ThemedProgressBarFillStyle::FillState::Paused: return "Paused";
		}
		return "Normal";
	}

	newui::ThemedProgressBarFillStyle::FillState progressBarFillStateFromString(const std::string& s, newui::ThemedProgressBarFillStyle::FillState defaultValue) {
		if (s == "Normal") return newui::ThemedProgressBarFillStyle::FillState::Normal;
		if (s == "Error") return newui::ThemedProgressBarFillStyle::FillState::Error;
		if (s == "Paused") return newui::ThemedProgressBarFillStyle::FillState::Paused;
		return defaultValue;
	}

}

namespace newui {

	void ViewStyle::markDirty()
	{
		// view_->rootView() is null for any View not yet attached to a
		// live window tree (e.g. a freshly-constructed TabControl/MenuBar
		// in a headless test, or any composite still being assembled
		// before addChild() into a real RootView-rooted tree) - every
		// pre-existing caller only ever ran from a live message pump
		// (RootView::updateHoveredSubView(), Part 9), where a RootView is
		// always already present, so this went unnoticed until
		// TabControl::selectTab() called it from a headless test. No-op
		// gracefully instead of crashing, same "do nothing without a live
		// window" convention ThemedViewStyle::paint() already uses.
		if (nullptr != view_ && nullptr != view_->rootView()) {
			view_->rootView()->markDirty();
		}
	}

	bool ViewStyle::setBackgroundImage(const std::string& path) {
		BLImage image;
		if (image.read_from_file(path.c_str()) != BL_SUCCESS) {
			return false;
		}

		setBackgroundImage(image);
		return true;
	}

	void ViewStyle::setBackgroundImage(const BLImage& image) {
		backgroundFill = BLPattern(image);
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
				if (newui::UIColorManager::isDarkMode()) {
					invertLightnessInPlace(bits, width, height, rowWidthPixels, highlighted);
				}

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

	void ThemedRadioButtonStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["checked"] = checked;
		w["pressed"] = pressed;
		w["enabled"] = enabled;
	}

	void ThemedRadioButtonStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		checked = obj["checked"].get_bool(checked);
		pressed = obj["pressed"].get_bool(pressed);
		enabled = obj["enabled"].get_bool(enabled);
	}

	void ThemedGroupBoxStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["enabled"] = enabled;
	}

	void ThemedGroupBoxStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		enabled = obj["enabled"].get_bool(enabled);
	}

	void ThemedToolbarButtonStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["checked"] = checked;
		w["pressed"] = pressed;
		w["enabled"] = enabled;
	}

	void ThemedToolbarButtonStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		checked = obj["checked"].get_bool(checked);
		pressed = obj["pressed"].get_bool(pressed);
		enabled = obj["enabled"].get_bool(enabled);
	}

	void ThemedToolbarDropDownButtonStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["checked"] = checked;
		w["pressed"] = pressed;
		w["enabled"] = enabled;
	}

	void ThemedToolbarDropDownButtonStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		checked = obj["checked"].get_bool(checked);
		pressed = obj["pressed"].get_bool(pressed);
		enabled = obj["enabled"].get_bool(enabled);
	}

	void ThemedToolbarDropDownButtonGlyphStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["pressed"] = pressed;
		w["enabled"] = enabled;
	}

	void ThemedToolbarDropDownButtonGlyphStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		pressed = obj["pressed"].get_bool(pressed);
		enabled = obj["enabled"].get_bool(enabled);
	}

	void ThemedToolbarSplitButtonStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["checked"] = checked;
		w["pressed"] = pressed;
		w["enabled"] = enabled;
	}

	void ThemedToolbarSplitButtonStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		checked = obj["checked"].get_bool(checked);
		pressed = obj["pressed"].get_bool(pressed);
		enabled = obj["enabled"].get_bool(enabled);
	}

	void ThemedToolbarSplitButtonDropDownStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["checked"] = checked;
		w["pressed"] = pressed;
		w["enabled"] = enabled;
	}

	void ThemedToolbarSplitButtonDropDownStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		checked = obj["checked"].get_bool(checked);
		pressed = obj["pressed"].get_bool(pressed);
		enabled = obj["enabled"].get_bool(enabled);
	}

	void ThemedToolbarSeparatorStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["horizontal"] = horizontal;
	}

	void ThemedToolbarSeparatorStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		horizontal = obj["horizontal"].get_bool(horizontal);
	}

	void ThemedTooltipStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["linked"] = linked;
	}

	void ThemedTooltipStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		linked = obj["linked"].get_bool(linked);
	}

	void ThemedSpinButtonStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["isUpButton"] = isUpButton;
		w["pressed"] = pressed;
		w["enabled"] = enabled;
	}

	void ThemedSpinButtonStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		isUpButton = obj["isUpButton"].get_bool(isUpButton);
		pressed = obj["pressed"].get_bool(pressed);
		enabled = obj["enabled"].get_bool(enabled);
	}

	void ThemedEditStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["focused"] = focused;
		w["readOnly"] = readOnly;
		w["enabled"] = enabled;
	}

	void ThemedEditStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		focused = obj["focused"].get_bool(focused);
		readOnly = obj["readOnly"].get_bool(readOnly);
		enabled = obj["enabled"].get_bool(enabled);
	}

	void ThemedListItemStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["selected"] = selected;
		w["enabled"] = enabled;
	}

	void ThemedListItemStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		selected = obj["selected"].get_bool(selected);
		enabled = obj["enabled"].get_bool(enabled);
	}

	void ThemedHeaderItemStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["pressed"] = pressed;
		w["sorted"] = sorted;
	}

	void ThemedHeaderItemStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		pressed = obj["pressed"].get_bool(pressed);
		sorted = obj["sorted"].get_bool(sorted);
	}

	void ThemedHeaderSortArrowStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["sortedAscending"] = sortedAscending;
	}

	void ThemedHeaderSortArrowStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		sortedAscending = obj["sortedAscending"].get_bool(sortedAscending);
	}

	void ThemedTreeItemStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["selected"] = selected;
		w["enabled"] = enabled;
	}

	void ThemedTreeItemStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		selected = obj["selected"].get_bool(selected);
		enabled = obj["enabled"].get_bool(enabled);
	}

	void ThemedTreeGlyphStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["expanded"] = expanded;
	}

	void ThemedTreeGlyphStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		expanded = obj["expanded"].get_bool(expanded);
	}

	void ThemedTabItemStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["alignment"] = w.new_string(tabItemAlignmentToString(alignment));
		w["position"] = w.new_string(tabItemPositionToString(position));
		w["selected"] = selected;
		w["enabled"] = enabled;
	}

	void ThemedTabItemStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		alignment = tabItemAlignmentFromString(obj["alignment"].get_c_str(""), alignment);
		position = tabItemPositionFromString(obj["position"].get_c_str(""), position);
		selected = obj["selected"].get_bool(selected);
		enabled = obj["enabled"].get_bool(enabled);
	}

	void ThemedTrackbarTrackStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["horizontal"] = horizontal;
	}

	void ThemedTrackbarTrackStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		horizontal = obj["horizontal"].get_bool(horizontal);
	}

	void ThemedTrackbarThumbStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["horizontal"] = horizontal;
		w["pressed"] = pressed;
		w["enabled"] = enabled;
	}

	void ThemedTrackbarThumbStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		horizontal = obj["horizontal"].get_bool(horizontal);
		pressed = obj["pressed"].get_bool(pressed);
		enabled = obj["enabled"].get_bool(enabled);
	}

	void ThemedTrackbarTicksStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["horizontal"] = horizontal;
	}

	void ThemedTrackbarTicksStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		horizontal = obj["horizontal"].get_bool(horizontal);
	}

	void ThemedProgressBarTrackStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["horizontal"] = horizontal;
	}

	void ThemedProgressBarTrackStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		horizontal = obj["horizontal"].get_bool(horizontal);
	}

	void ThemedProgressBarFillStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["horizontal"] = horizontal;
		w["state"] = w.new_string(progressBarFillStateToString(state));
	}

	void ThemedProgressBarFillStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		horizontal = obj["horizontal"].get_bool(horizontal);
		state = progressBarFillStateFromString(obj["state"].get_c_str(""), state);
	}

	void ThemedScrollbarThumbStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["horizontal"] = horizontal;
		w["pressed"] = pressed;
		w["enabled"] = enabled;
	}

	void ThemedScrollbarThumbStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		horizontal = obj["horizontal"].get_bool(horizontal);
		pressed = obj["pressed"].get_bool(pressed);
		enabled = obj["enabled"].get_bool(enabled);
	}

	void ThemedScrollbarArrowStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["direction"] = w.new_string(scrollbarArrowDirectionToString(direction));
		w["pressed"] = pressed;
		w["enabled"] = enabled;
	}

	void ThemedScrollbarArrowStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		direction = scrollbarArrowDirectionFromString(obj["direction"].get_c_str(""), direction);
		pressed = obj["pressed"].get_bool(pressed);
		enabled = obj["enabled"].get_bool(enabled);
	}

	void ThemedScrollbarTrackStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["horizontal"] = horizontal;
		w["position"] = w.new_string(scrollbarTrackPositionToString(position));
		w["pressed"] = pressed;
		w["enabled"] = enabled;
	}

	void ThemedScrollbarTrackStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		horizontal = obj["horizontal"].get_bool(horizontal);
		position = scrollbarTrackPositionFromString(obj["position"].get_c_str(""), position);
		pressed = obj["pressed"].get_bool(pressed);
		enabled = obj["enabled"].get_bool(enabled);
	}

	void ThemedMenuBarItemStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["pressed"] = pressed;
		w["enabled"] = enabled;
		w["highlightInset"] = highlightInset;
		w["highlightCornerRadius"] = highlightCornerRadius;
	}

	void ThemedMenuBarItemStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		pressed = obj["pressed"].get_bool(pressed);
		enabled = obj["enabled"].get_bool(enabled);
		highlightInset = obj["highlightInset"].get<float>(highlightInset);
		highlightCornerRadius = obj["highlightCornerRadius"].get<float>(highlightCornerRadius);
	}

	// Hand-drawn rounded highlight for MBI_HOT/MBI_PUSHED, replacing
	// DrawThemeBackground entirely for this style (unlike every other
	// ThemedViewStyle subclass in this file, which all rely on the base
	// class's native rendering) - the classic uxtheme skin's MENU_BARITEM
	// part has no rounded-corner variant at all, just a plain square fill,
	// so there's no theme part to ask for the look a modern Fluent-style
	// menu-bar hover highlight has (a rounded "pill" inset a couple pixels
	// from the item's own edge) - reported live: "the highlight should be
	// drawn with a rectangle with rounded edges, inset by 1 or 2 pixels".
	// MBI_NORMAL/MBI_DISABLED both draw nothing (see
	// invertLightnessInPlace()'s own doc comment above for why that's the
	// correct native look too), so paint() only ever needs to draw
	// anything for the pressed/highlighted case - no live HWND, no HTHEME,
	// no dark-mode invert-lightness approximation needed at all: unlike
	// DrawThemeBackground's flat classic-skin fill, UIColorManager's own
	// accent color (colorFor(UIColorRole::HighlightBackground)) already
	// tracks the user's real Windows accent color and Light/Dark mode
	// directly. theme_ (ThemedViewStyle's own cached HTHEME) is therefore
	// never opened by this style at all - computeClientBounds() always
	// falls back to the full rect (see its own "no theme cached yet"
	// comment), which is exactly right here: MENU_BARITEM has no border
	// content-rect deflation of its own for a client to need anyway.
	void ThemedMenuBarItemStyle::paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const {
		clientBounds = computeClientBounds(size);

		if (size.width <= 0.0f || size.height <= 0.0f || !enabled || (!pressed && !highlighted)) {
			return;
		}

		Rect r = Rect(0.0f, 0.0f, size.width, size.height).deflated(highlightInset);
		if (r.size().width <= 0.0f || r.size().height <= 0.0f) {
			return;
		}

		Color accent = UIColorManager::colorFor(UIColorRole::HighlightBackground);

		ctx.save();
		ctx.set_comp_op(compositingOp);
		ctx.set_fill_style(accent.toBLRgba32());
		// Pressed reads as a stronger fill than a plain hover - same
		// "pressed is more emphatic than hot" precedence stateId() already
		// encodes for the native part this replaces.
		ctx.set_fill_alpha(opacity * (pressed ? 0.55f : 0.30f));
		ctx.fill_round_rect(double(r.left()), double(r.top()),
			double(r.size().width), double(r.size().height), double(highlightCornerRadius));
		ctx.restore();
	}

	void ThemedRebarChevronStyle::writeFields(json5::builder& w) const {
		ViewStyle::writeFields(w);
		w["horizontal"] = horizontal;
		w["pressed"] = pressed;
	}

	void ThemedRebarChevronStyle::readFields(const json5::value& obj) {
		ViewStyle::readFields(obj);
		horizontal = obj["horizontal"].get_bool(horizontal);
		pressed = obj["pressed"].get_bool(pressed);
	}

}