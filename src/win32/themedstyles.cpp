// The uxtheme-backed Themed*/Fluent* styles declared in newui/win32/themedstyles.h.
#include "newui/viewstyle.h"
#include "newui/win32/themedstyles.h"
#include "newui/win32/nativehandles.h"
#include "newui/view.h"
#include "newui/rootview.h"
#include "newui/color.h"
#include "newui/themedata.h"
#include "newui/utils.h"
#include "newui/uicolormanager.h"

#include "newui/shapes.h"

#include <array>
#include <cassert>
#include <cmath>
#include <vector>

namespace {

	// Fluent's own "state layer" convention (see FluentButtonStyle::paint()'s
	// own comment, below) - a translucent WindowText overlay whose alpha
	// alone signals hover/pressed, shared across every Fluent* style that
	// needs the same two-level feedback (FluentButtonStyle/
	// FluentCheckBoxStyle/FluentRadioButtonStyle/FluentToolbarButtonStyle).
	float fluentStateOverlayAlpha(bool pressed, bool highlighted) {
		return pressed ? 0.15f : (highlighted ? 0.08f : 0.0f);
	}

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
	constexpr float kGammaNormal = 0.6f;
	constexpr float kGammaHighlighted = 0.2f;

	// pow(x/255, gamma) precomputed for every possible byte input - avoids
	// a std::pow() call (profiled hotspot: invertLightnessInPlace() runs
	// this per pixel of every themed control's paint() call in dark mode,
	// and a themed control can repaint at 30-60fps - see Progress's "Live"
	// demo row) for each of the (at most, in practice exactly two -
	// kGammaNormal/kGammaHighlighted) gamma values actually used. Safe to
	// quantize to byte precision: the input is itself derived from 8-bit
	// RGB channels via toHSL(), so it never had more than byte-level
	// precision to begin with - a lookup can't lose anything std::pow()
	// was actually preserving.
	const std::array<float, 256>& gammaLUT(bool highlighted) {
		static const std::array<float, 256> normalTable = [] {
			std::array<float, 256> t{};
			for (int i = 0; i < 256; ++i) {
				t[i] = std::pow(float(i) / 255.0f, kGammaNormal);
			}
			return t;
		}();
		static const std::array<float, 256> highlightedTable = [] {
			std::array<float, 256> t{};
			for (int i = 0; i < 256; ++i) {
				t[i] = std::pow(float(i) / 255.0f, kGammaHighlighted);
			}
			return t;
		}();
		return highlighted ? highlightedTable : normalTable;
	}

	// `floor`, when the host has a palette of its own (UIColorManager::setColorOverride): inverting white gives pure
	// black, which is not what the host's dark panels look like, so the inverted range is remapped to run from `floor`
	// (instead of black) up to white - a themed control's background then matches the host's.
	void invertLightnessInPlace(RGBQUAD* bits, int width, int height, int strideInPixels, bool highlighted,
	                            const std::optional<newui::Color>& floor = std::nullopt) {
		const std::array<float, 256>& lut = gammaLUT(highlighted);

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
				// noticeably lighter instead. Table lookup instead of a
				// direct std::pow() call - see gammaLUT()'s own comment.
				int gammaIndex = int(std::lround((1.0f - hsl.l) * 255.0f));
				gammaIndex = gammaIndex < 0 ? 0 : (gammaIndex > 255 ? 255 : gammaIndex);
				hsl.l = lut[gammaIndex];

				// toBGRA32() writes exactly RGBQUAD's own B,G,R,A byte
				// order (see its doc comment in color.h), clamping/
				// rounding each channel the same way every other Color
				// byte-packing call in this codebase does - straightBytes
				// is therefore still straight (unpremultiplied) alpha,
				// same as color/hsl above; re-premultiply by its own
				// (unchanged) alpha byte before writing back to px.
				uint8_t straightBytes[4];
				newui::Color inverted = newui::Color::fromHSL(hsl);
					if (floor.has_value()) {
						inverted.r = floor->r + inverted.r * (1.0f - floor->r);
						inverted.g = floor->g + inverted.g * (1.0f - floor->g);
						inverted.b = floor->b + inverted.b * (1.0f - floor->b);
					}
					inverted.toBGRA32(straightBytes);
				float straightAlpha = float(straightBytes[3]) / 255.0f;
				px.rgbBlue = uint8_t(std::lround(straightBytes[0] * straightAlpha));
				px.rgbGreen = uint8_t(std::lround(straightBytes[1] * straightAlpha));
				px.rgbRed = uint8_t(std::lround(straightBytes[2] * straightAlpha));
				px.rgbReserved = straightBytes[3];
			}
		}
	}

}

namespace newui {

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

		// ThemeData (themedata.h) first - a pre-generated snapshot
		// (tools/themesgen/themesgen.py) of a real Windows install's
		// actual content-rect deflation for this exact (class, part,
		// state) triple, checked ahead of the live HTHEME query below so
		// this answer is available even before this style has ever
		// painted through a live window (theme_ still null at that
		// point - see the "no theme cached yet" fallback right after).
		// Neutral/normal state, same as the live query below - see its
		// own comment for why.
		if (const ThemePartData* data = ThemeData::instance().tryPartData(themeClassName_, partId(), stateId(false))) {
			if (data->contentLeft && data->contentTop && data->contentRight && data->contentBottom) {
				return Rect(*data->contentLeft, *data->contentTop,
					size.width - *data->contentLeft - *data->contentRight,
					size.height - *data->contentTop - *data->contentBottom);
			}
		}

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

	Size ThemedViewStyle::partSize(const Size& fallback) const {
		// ThemeData first - see computeClientBounds()'s own comment just
		// above for why this check comes before the "no theme cached
		// yet" live-query fallback, not after it.
		if (const ThemePartData* data = ThemeData::instance().tryPartData(themeClassName_, partId(), stateId(false))) {
			if (data->size) {
				return *data->size;
			}
		}

		// Same "no theme cached yet" fallback as computeClientBounds()
		// above, same reason.
		if (theme_ == nullptr) {
			return fallback;
		}

		SIZE sz{};
		// hdc/prc both null - fine for TS_TRUE (the natural size the
		// current visual style draws this part/state at), which unlike
		// TS_DRAW doesn't need a real device context or bounding rect to
		// answer against.
		if (FAILED(::GetThemePartSize(theme_, nullptr, partId(), stateId(false), nullptr, TS_TRUE, &sz))) {
			return fallback;
		}

		return Size(static_cast<float>(sz.cx), static_cast<float>(sz.cy));
	}

	namespace {

		// How a themed part is drawn from ThemeData colours. Arrow and Triangle parts draw their glyph in the part's text colour.
		enum class PartLook { None, Box, Frame, Check, Radio, Groove, Arrow, Triangle };

		struct PartInfo {
			PartLook look = PartLook::None;
			float radius = 0.0f;
			bool horizontal = true;  // Groove
		};

		PartInfo partInfo(const std::string& cls, int part) {
			using L = PartLook;
			if (cls == "BUTTON") {
				if (part == BP_PUSHBUTTON) return {L::Box, 4.0f};
				if (part == BP_CHECKBOX) return {L::Check};
				if (part == BP_RADIOBUTTON) return {L::Radio};
				if (part == BP_GROUPBOX) return {L::Frame, 4.0f};
			} else if (cls == "TOOLBAR") {
				if (part == TP_BUTTON || part == TP_SPLITBUTTON) return {L::Box, 4.0f};
				if (part == TP_SPLITBUTTONDROPDOWN) return {L::Arrow, 4.0f};
			} else if (cls == "TRACKBAR") {
				if (part == TKP_TICS || part == TKP_TICSVERT) return {L::Box};
				if (part == TKP_TRACK) return {L::Groove, 0.0f, true};
				if (part == TKP_TRACKVERT) return {L::Groove, 0.0f, false};
				if (part == TKP_THUMB || part == TKP_THUMBVERT) return {L::Box, 3.0f};
			} else if (cls == "SCROLLBAR") {
				if (part == SBP_THUMBBTNHORZ || part == SBP_THUMBBTNVERT) return {L::Box, 3.0f};
				if (part == SBP_LOWERTRACKHORZ || part == SBP_UPPERTRACKHORZ || part == SBP_LOWERTRACKVERT || part == SBP_UPPERTRACKVERT) return {L::Box};
				if (part == SBP_ARROWBTN) return {L::Arrow, 2.0f};
			} else if (cls == "PROGRESS") {
				if (part == PP_BAR || part == PP_BARVERT || part == PP_FILL || part == PP_FILLVERT) return {L::Box, 3.0f};
			} else if (cls == "TAB") {
				if (part == TABP_TABITEM || part == TABP_TABITEMLEFTEDGE || part == TABP_TABITEMRIGHTEDGE || part == TABP_TABITEMBOTHEDGE ||
					part == TABP_TOPTABITEM || part == TABP_TOPTABITEMLEFTEDGE || part == TABP_TOPTABITEMRIGHTEDGE || part == TABP_TOPTABITEMBOTHEDGE) return {L::Box, 3.0f};
				if (part == TABP_PANE) return {L::Box};
			} else if (cls == "EDIT") {
				if (part == EP_EDITTEXT) return {L::Box, 4.0f};
			} else if (cls == "LISTVIEW") {
				if (part == LVP_LISTITEM) return {L::Box, 2.0f};
			} else if (cls == "TREEVIEW") {
				if (part == TVP_TREEITEM) return {L::Box, 2.0f};
				if (part == TVP_GLYPH) return {L::Triangle};
			} else if (cls == "HEADER") {
				if (part == HP_HEADERITEM) return {L::Box};
				if (part == HP_HEADERSORTARROW) return {L::Triangle};
			} else if (cls == "SPIN") {
				if (part == SPNP_UP || part == SPNP_DOWN) return {L::Arrow, 2.0f};
			} else if (cls == "MENU") {
				if (part == MENU_BARITEM || part == MENU_BARBACKGROUND) return {L::Box};
			} else if (cls == "STATUS") {
			if (part == SP_PANE) return {L::Box};
			} else if (cls == "REBAR") {
			if (part == RP_BAND) return {L::Box};
			if (part == RP_CHEVRON || part == RP_CHEVRONVERT) return {L::Arrow, 2.0f};
			} else if (cls == "TOOLTIP") {
				if (part == TTP_STANDARD) return {L::Box, 3.0f};
			}
			return {};
		}

		// Fills a triangle pointing the given way (0 up, 1 down, 2 left, 3 right), centred in a square of `extent`.
		void fillTriangle(BLContext& ctx, float cx, float cy, float extent, int direction) {
			const float h = extent * 0.5f;
			BLPath path;
			switch (direction) {
				case 0: path.move_to(cx - h, cy + h * 0.5f); path.line_to(cx + h, cy + h * 0.5f); path.line_to(cx, cy - h * 0.5f); break;
				case 1: path.move_to(cx - h, cy - h * 0.5f); path.line_to(cx + h, cy - h * 0.5f); path.line_to(cx, cy + h * 0.5f); break;
				case 2: path.move_to(cx + h * 0.5f, cy - h); path.line_to(cx + h * 0.5f, cy + h); path.line_to(cx - h * 0.5f, cy); break;
				default: path.move_to(cx - h * 0.5f, cy - h); path.line_to(cx - h * 0.5f, cy + h); path.line_to(cx + h * 0.5f, cy); break;
			}
			path.close();
			ctx.fill_path(path);
		}

	}

	bool ThemedViewStyle::paintFromThemeData(BLContext& ctx, const Size& size, bool highlighted) const {
		if (!ThemeData::instance().isLoaded()) {
			return false;
		}
		// The explorer-style list and tree skins are recorded in the theme file under their plain class names.
		std::string cls = themeClassName_;
		if (cls == "Explorer::ListView") cls = "LISTVIEW";
		else if (cls == "Explorer::TreeView") cls = "TREEVIEW";

		const int part = partId();
		const int state = stateId(highlighted);
		const PartInfo info = partInfo(cls, part);
		if (info.look == PartLook::None) {
			return false;
		}
		const ThemePartData* data = ThemeData::instance().tryPartData(cls, part, state);
		if (data == nullptr || !(data->fillColor || data->edgeFillColor || data->borderColor)) {
			return false;
		}
		const std::optional<Color> fill = data->fillColor ? data->fillColor : data->edgeFillColor;
		const Color mark = data->textColor ? *data->textColor : UIColorManager::colorFor(UIColorRole::ControlText);

		ctx.save();
		ctx.set_comp_op(toBLCompOp(compositingOp()));
		ctx.set_fill_alpha(opacity());

		const float minDim = size.width < size.height ? size.width : size.height;
		BLPath path;
		switch (info.look) {
			case PartLook::Radio:
				path.add_ellipse(BLEllipse(size.width * 0.5f, size.height * 0.5f, minDim * 0.5f - 0.5f, minDim * 0.5f - 0.5f));
				break;
			case PartLook::Groove: {
				// A thin pill centred in the room the part is given, like a native track's groove.
				const float thickness = 4.0f;
				if (info.horizontal) {
					const float t = thickness < size.height ? thickness : size.height;
					path.add_round_rect(BLRoundRect(0.0f, (size.height - t) * 0.5f, size.width, t, t * 0.5f));
				} else {
					const float t = thickness < size.width ? thickness : size.width;
					path.add_round_rect(BLRoundRect((size.width - t) * 0.5f, 0.0f, t, size.height, t * 0.5f));
				}
				break;
			}
			case PartLook::Triangle:
				break;
			case PartLook::Check:
				path.add_round_rect(BLRoundRect(0.5f, 0.5f, size.width - 1.0f, size.height - 1.0f, minDim * 0.2f));
				break;
			default:
				path.add_round_rect(BLRoundRect(0.5f, 0.5f, size.width - 1.0f, size.height - 1.0f, info.radius));
				break;
		}
		if (info.look != PartLook::Triangle) {
			if (info.look != PartLook::Frame && fill && fill->a > 0.0f) {
				ctx.set_fill_style(fill->toBLRgba32());
				ctx.fill_path(path);
			}
			if (data->borderColor && data->borderColor->a > 0.0f) {
				ctx.set_stroke_style(data->borderColor->toBLRgba32());
				ctx.set_stroke_width(1.0f);
				ctx.stroke_path(path);
			}
		}

		// Check mark and radio dot: both state families run 5..8 for checked.
		const bool checked = (info.look == PartLook::Check || info.look == PartLook::Radio) && state >= CBS_CHECKEDNORMAL && state <= CBS_CHECKEDDISABLED;
		if (checked) {
			ctx.set_fill_style(mark.toBLRgba32());
			ctx.set_stroke_style(mark.toBLRgba32());
			if (info.look == PartLook::Radio) {
				ctx.fill_circle(BLCircle(size.width * 0.5f, size.height * 0.5f, minDim * 0.22f));
			} else {
				BLPath tick;
				tick.move_to(size.width * 0.25f, size.height * 0.52f);
				tick.line_to(size.width * 0.43f, size.height * 0.70f);
				tick.line_to(size.width * 0.76f, size.height * 0.30f);
				ctx.set_stroke_width(minDim * 0.12f);
				ctx.set_stroke_caps(BL_STROKE_CAP_ROUND);
				ctx.set_stroke_join(BL_STROKE_JOIN_ROUND);
				ctx.stroke_path(tick);
			}
		}

		// Glyphs: scrollbar and spin arrows (direction from the part and state ids), tree expanders and header sort arrows.
		if (info.look == PartLook::Arrow || info.look == PartLook::Triangle) {
			int direction = 0;
			if (cls == "SCROLLBAR") direction = state >= 17 ? (state - 17) % 4 : (state - 1) / 4;  // up, down, left, right
			else if (cls == "SPIN") direction = part == SPNP_UP ? 0 : 1;
			else if (cls == "TOOLBAR") direction = 1;
			else if (cls == "REBAR") direction = part == RP_CHEVRON ? 3 : 1;
			else if (cls == "TREEVIEW") direction = state == GLPS_OPENED ? 1 : 3;
			else if (cls == "HEADER") direction = state == HSAS_SORTEDUP ? 0 : 1;
			ctx.set_fill_style(mark.toBLRgba32());
			fillTriangle(ctx, size.width * 0.5f, size.height * 0.5f, minDim * 0.45f, direction);
		}
		ctx.restore();
		return true;
	}

	void ThemedViewStyle::paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const {
		clientBounds = computeClientBounds(size);

		if (size.width <= 0.0f || size.height <= 0.0f) {
			return;
		}

		if (paintFromThemeData(ctx, size, highlighted)) {
			return;
		}

		HWND hwnd = (view() != nullptr && view()->rootView() != nullptr)
			? newui::win32::toHWND(view()->rootView()->windowHandle()) : nullptr;
		if (hwnd == nullptr) {
			return;  // not attached to a live (initialize()'d) window yet
		}

		if (theme_ == nullptr) {
			theme_ = ::OpenThemeData(hwnd, utf8ToWide(themeClassName_).c_str());
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
					// The hover gamma only applies to a part that really draws differently when hot; a track (always its normal state) would
					// otherwise change brightness whenever the pointer wandered on or off it, e.g. during a thumb drag.
					const bool hotLook = highlighted && stateId(true) != stateId(false);
					invertLightnessInPlace(bits, width, height, rowWidthPixels, hotLook,
											   newui::UIColorManager::colorOverride(newui::UIColorRole::ControlBackground));
				}

				// Premultiplied, top-down BGRA - exactly BL_FORMAT_PRGB32,
				// so no channel/premultiplication conversion is needed
				// before wrapping these bits directly as a BLImage.
				BLImage themedImage;
				themedImage.create_from_data(width, height, BL_FORMAT_PRGB32, bits, intptr_t(rowWidthPixels) * 4);

				ctx.save();
				ctx.set_comp_op(toBLCompOp(compositingOp()));
				ctx.blit_image(BLPoint(0, 0), themedImage);
				ctx.restore();
			}

			::EndBufferedPaint(paintBuffer, TRUE);
		}

		::DeleteDC(targetDC);
	}

	void FluentButtonStyle::paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const {
		clientBounds = computeClientBounds(size);

		if (size.width <= 0.0f || size.height <= 0.0f) {
			return;
		}

		// A loaded theme file's colours for this part and state win; the role-based look below is the fallback.
		if (paintFromThemeData(ctx, size, highlighted)) {
			return;
		}

		constexpr float kCornerRadius = 4.0f;  // Fluent's own "in-page control" standard - see class comment (viewstyle.h)

		BLPath path;
		path.add_round_rect(BLRoundRect(0.0f, 0.0f, size.width, size.height, kCornerRadius));

		ctx.save();
		ctx.set_comp_op(toBLCompOp(compositingOp()));
		ctx.set_fill_alpha(opacity());

		ctx.set_fill_style(UIColorManager::colorFor(UIColorRole::ControlBackground).toBLRgba32());
		ctx.fill_path(path);

		if (enabled) {
			// Fluent's own "state layer" convention for hover/pressed
			// feedback - a translucent overlay in the text color, not a
			// hand-tuned per-mode brightness multiplier. WindowText is
			// already dark/light-mode-correct (near-black in light mode,
			// near-white in dark), so a low-alpha overlay of it darkens a
			// light fill and lightens a dark one automatically, in
			// whichever direction each mode actually needs - a fixed
			// brightness multiplier can't do that (it would barely
			// register against an already-near-black dark-mode fill).
			float overlayAlpha = fluentStateOverlayAlpha(pressed, highlighted);
			if (overlayAlpha > 0.0f) {
				Color overlay = UIColorManager::colorFor(UIColorRole::WindowText);
				ctx.set_fill_style(Color(overlay.r, overlay.g, overlay.b, overlayAlpha).toBLRgba32());
				ctx.fill_path(path);
			}
		}

		ctx.set_stroke_style(UIColorManager::colorFor(UIColorRole::ControlBorder).toBLRgba32());
		ctx.set_stroke_width(1.0f);
		ctx.stroke_path(path);

		ctx.restore();
	}

	void FluentCheckBoxStyle::paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const {
		clientBounds = computeClientBounds(size);

		if (size.width <= 0.0f || size.height <= 0.0f) {
			return;
		}

		// A loaded theme file's colours for this part and state win; the role-based look below is the fallback.
		if (paintFromThemeData(ctx, size, highlighted)) {
			return;
		}

		// Toggle has no separate label - this glyph fills the View's whole
		// given size (see class comment, viewstyle.h) - min(width,height)
		// keeps the corner radius/checkmark proportions sane even if a
		// caller ever sizes the box non-square.
		float minDim = size.width < size.height ? size.width : size.height;
		float cornerRadius = minDim * 0.2f;

		BLPath path;
		path.add_round_rect(BLRoundRect(0.0f, 0.0f, size.width, size.height, cornerRadius));

		ctx.save();
		ctx.set_comp_op(toBLCompOp(compositingOp()));
		ctx.set_fill_alpha(opacity());
		if (!enabled) {
			// A flat 50% dim over the whole glyph - same "disabled fades,
			// doesn't recolor" convention ToolbarButton::paint() already
			// uses for its own icon (0.4 there; a filled/checked glyph
			// reads better a bit less faded).
			ctx.set_global_alpha(0.5);
		}

		if (checked) {
			ctx.set_fill_style(UIColorManager::colorFor(UIColorRole::HighlightBackground).toBLRgba32());
			ctx.fill_path(path);
		} else {
			ctx.set_fill_style(UIColorManager::colorFor(UIColorRole::ControlBackground).toBLRgba32());
			ctx.fill_path(path);
			ctx.set_stroke_style(UIColorManager::colorFor(UIColorRole::ControlBorder).toBLRgba32());
			ctx.set_stroke_width(1.0f);
			ctx.stroke_path(path);
		}

		if (enabled) {
			float overlayAlpha = fluentStateOverlayAlpha(pressed, highlighted);
			if (overlayAlpha > 0.0f) {
				Color overlay = UIColorManager::colorFor(UIColorRole::WindowText);
				ctx.set_fill_style(Color(overlay.r, overlay.g, overlay.b, overlayAlpha).toBLRgba32());
				ctx.fill_path(path);
			}
		}

		if (checked) {
			BLPath check;
			check.move_to(size.width * 0.22f, size.height * 0.52f);
			check.line_to(size.width * 0.42f, size.height * 0.72f);
			check.line_to(size.width * 0.78f, size.height * 0.28f);

			ctx.set_stroke_style(UIColorManager::colorFor(UIColorRole::HighlightText).toBLRgba32());
			ctx.set_stroke_width(minDim * 0.12f);
			ctx.set_stroke_caps(BL_STROKE_CAP_ROUND);
			ctx.set_stroke_join(BL_STROKE_JOIN_ROUND);
			ctx.stroke_path(check);
		}

		ctx.restore();
	}

	void FluentRadioButtonStyle::paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const {
		clientBounds = computeClientBounds(size);

		if (size.width <= 0.0f || size.height <= 0.0f) {
			return;
		}

		// A loaded theme file's colours for this part and state win; the role-based look below is the fallback.
		if (paintFromThemeData(ctx, size, highlighted)) {
			return;
		}

		float minDim = size.width < size.height ? size.width : size.height;
		float cx = size.width * 0.5f;
		float cy = size.height * 0.5f;
		float outerR = minDim * 0.5f - 1.0f;  // -1 so a 1px stroke stays fully inside the given bounds
		if (outerR <= 0.0f) {
			return;
		}

		BLPath path;
		path.add_circle(BLCircle(cx, cy, outerR));

		ctx.save();
		ctx.set_comp_op(toBLCompOp(compositingOp()));
		ctx.set_fill_alpha(opacity());
		if (!enabled) {
			ctx.set_global_alpha(0.5);
		}

		if (checked) {
			ctx.set_fill_style(UIColorManager::colorFor(UIColorRole::HighlightBackground).toBLRgba32());
			ctx.fill_path(path);
		} else {
			ctx.set_fill_style(UIColorManager::colorFor(UIColorRole::ControlBackground).toBLRgba32());
			ctx.fill_path(path);
			ctx.set_stroke_style(UIColorManager::colorFor(UIColorRole::ControlBorder).toBLRgba32());
			ctx.set_stroke_width(1.0f);
			ctx.stroke_path(path);
		}

		if (enabled) {
			float overlayAlpha = fluentStateOverlayAlpha(pressed, highlighted);
			if (overlayAlpha > 0.0f) {
				Color overlay = UIColorManager::colorFor(UIColorRole::WindowText);
				ctx.set_fill_style(Color(overlay.r, overlay.g, overlay.b, overlayAlpha).toBLRgba32());
				ctx.fill_path(path);
			}
		}

		if (checked) {
			// The classic radio "bullseye" - a smaller solid dot in the
			// selection-text color, concentric with the outer accent-filled
			// circle above.
			BLPath dot;
			dot.add_circle(BLCircle(cx, cy, outerR * 0.4f));
			ctx.set_fill_style(UIColorManager::colorFor(UIColorRole::HighlightText).toBLRgba32());
			ctx.fill_path(dot);
		}

		ctx.restore();
	}

	void FluentToolbarButtonStyle::paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const {
		clientBounds = computeClientBounds(size);

		if (size.width <= 0.0f || size.height <= 0.0f) {
			return;
		}

		// A loaded theme file's colours for this part and state win; the role-based look below is the fallback.
		if (paintFromThemeData(ctx, size, highlighted)) {
			return;
		}

		// Fluent command-bar convention: a toggled-on ("checked") command
		// gets a persistent light accent tint even at rest; hover/pressed
		// each additionally layer the same WindowText state-overlay every
		// other Fluent* style uses. Nothing painted at all when neither
		// applies - matches the native TS_NORMAL "fully flat at rest" look
		// this style replaces (see class comment).
		float baseAlpha = checked ? (pressed || highlighted ? 0.20f : 0.15f) : 0.0f;
		float overlayAlpha = enabled ? fluentStateOverlayAlpha(pressed, highlighted) : 0.0f;
		if (baseAlpha <= 0.0f && overlayAlpha <= 0.0f) {
			return;
		}

		constexpr float kCornerRadius = 4.0f;
		BLPath path;
		path.add_round_rect(BLRoundRect(0.0f, 0.0f, size.width, size.height, kCornerRadius));

		ctx.save();
		ctx.set_comp_op(toBLCompOp(compositingOp()));
		ctx.set_fill_alpha(opacity());
		if (!enabled) {
			ctx.set_global_alpha(0.5);
		}

		if (baseAlpha > 0.0f) {
			Color accent = UIColorManager::colorFor(UIColorRole::HighlightBackground);
			ctx.set_fill_style(Color(accent.r, accent.g, accent.b, baseAlpha).toBLRgba32());
			ctx.fill_path(path);
		}

		if (overlayAlpha > 0.0f) {
			Color overlay = UIColorManager::colorFor(UIColorRole::WindowText);
			ctx.set_fill_style(Color(overlay.r, overlay.g, overlay.b, overlayAlpha).toBLRgba32());
			ctx.fill_path(path);
		}

		ctx.restore();
	}

	Rect FluentEditStyle::computeClientBounds(const Size& size) const {
		Rect full(0.0f, 0.0f, size.width, size.height);
		// Horizontal/top padding for the rounded border plus a comfortable
		// text inset; the extra 2px at the bottom is reserved for the
		// focused-state underline (paint(), below) so caret/text never
		// visually collides with it even while focused.
		return full.deflate(6.0f, 4.0f, 6.0f, 6.0f);
	}

	void FluentEditStyle::paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const {
		clientBounds = computeClientBounds(size);

		if (size.width <= 0.0f || size.height <= 0.0f) {
			return;
		}

		constexpr float kCornerRadius = 4.0f;  // Fluent's own "in-page control" standard - see FluentButtonStyle's own comment
		BLPath path;
		path.add_round_rect(BLRoundRect(0.0f, 0.0f, size.width, size.height, kCornerRadius));

		ctx.save();
		ctx.set_comp_op(toBLCompOp(compositingOp()));
		ctx.set_fill_alpha(opacity());
		if (!enabled) {
			ctx.set_global_alpha(0.5);
		}

		ctx.set_fill_style(UIColorManager::colorFor(UIColorRole::ControlBackground).toBLRgba32());
		ctx.fill_path(path);

		ctx.set_stroke_style(UIColorManager::colorFor(UIColorRole::ControlBorder).toBLRgba32());
		ctx.set_stroke_width(1.0f);
		ctx.stroke_path(path);

		if (focused && enabled) {
			// WinUI TextBox's own distinctive signature - see class
			// comment. Inset by the corner radius so the underline never
			// runs past the rounded corners it sits just inside of.
			constexpr float kUnderlineWidth = 2.0f;
			ctx.set_stroke_style(UIColorManager::colorFor(UIColorRole::HighlightBackground).toBLRgba32());
			ctx.set_stroke_width(kUnderlineWidth);
			ctx.set_stroke_caps(BL_STROKE_CAP_BUTT);
			ctx.stroke_line(kCornerRadius, size.height - kUnderlineWidth * 0.5f,
				size.width - kCornerRadius, size.height - kUnderlineWidth * 0.5f);
		}

		ctx.restore();
	}

	void FluentGroupBoxStyle::paint(BLContext& ctx, const Size& size, bool /*highlighted*/, Rect& clientBounds) const {
		clientBounds = computeClientBounds(size);

		if (size.width <= 0.0f || size.height <= 0.0f) {
			return;
		}

		constexpr float kCornerRadius = 8.0f;  // matches FluentCardStyle's own "overlay/grouping frame" radius, not an in-page control's 4px
		BLPath path;
		path.add_round_rect(BLRoundRect(0.0f, 0.0f, size.width, size.height, kCornerRadius));

		ctx.save();
		ctx.set_comp_op(toBLCompOp(compositingOp()));
		ctx.set_fill_alpha(opacity());
		if (!enabled) {
			ctx.set_global_alpha(0.5);
		}

		ctx.set_stroke_style(UIColorManager::colorFor(UIColorRole::ControlBorder).toBLRgba32());
		ctx.set_stroke_width(1.0f);
		ctx.stroke_path(path);

		ctx.restore();
	}

	FluentCardStyle::FluentCardStyle() {
		setRectRadius(8.0f);  // Fluent's own overlay/card-level corner radius convention - larger than an in-page control's 4px (see FluentButtonStyle's own comment)
		setElevation(ElevationLevel::Card);
	}

	void FluentCardStyle::paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const {
		clientBounds = computeClientBounds(size);

		if (size.width <= 0.0f || size.height <= 0.0f) {
			return;
		}

		BLPath path;
		path.add_round_rect(BLRoundRect(0.0f, 0.0f, size.width, size.height, rectRadius()));

		ctx.save();
		ctx.set_comp_op(toBLCompOp(compositingOp()));
		ctx.set_fill_alpha(opacity());

		ctx.set_fill_style(UIColorManager::colorFor(UIColorRole::ControlBackground).toBLRgba32());
		ctx.fill_path(path);

		ctx.set_stroke_style(UIColorManager::colorFor(UIColorRole::ControlBorder).toBLRgba32());
		ctx.set_stroke_width(1.0f);
		ctx.stroke_path(path);

		ctx.restore();
	}

	void FluentTrackbarTrackStyle::paint(BLContext& ctx, const Size& size, bool /*highlighted*/, Rect& clientBounds) const {
		clientBounds = computeClientBounds(size);

		if (size.width <= 0.0f || size.height <= 0.0f) {
			return;
		}

		// A thin rounded "pill" groove, centered in whatever cross-axis
		// room the given size actually has - Slider hands this style the
		// *full* trackRect() (controls.cpp), typically much taller/wider
		// than a real groove, same as the native TKP_TRACK part it
		// replaces draws a thin line within.
		constexpr float kThickness = 4.0f;
		BLPath path;
		if (horizontal) {
			float t = kThickness < size.height ? kThickness : size.height;
			float y = (size.height - t) * 0.5f;
			path.add_round_rect(BLRoundRect(0.0f, y, size.width, t, t * 0.5f));
		} else {
			float t = kThickness < size.width ? kThickness : size.width;
			float x = (size.width - t) * 0.5f;
			path.add_round_rect(BLRoundRect(x, 0.0f, t, size.height, t * 0.5f));
		}

		ctx.save();
		ctx.set_comp_op(toBLCompOp(compositingOp()));
		ctx.set_fill_alpha(opacity());
		ctx.set_fill_style(UIColorManager::colorFor(UIColorRole::ControlBorder).toBLRgba32());
		ctx.fill_path(path);
		ctx.restore();
	}

	void FluentTrackbarThumbStyle::paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const {
		clientBounds = computeClientBounds(size);

		if (size.width <= 0.0f || size.height <= 0.0f) {
			return;
		}

		float minDim = size.width < size.height ? size.width : size.height;
		float cx = size.width * 0.5f;
		float cy = size.height * 0.5f;
		float outerR = minDim * 0.5f - 1.0f;
		if (outerR <= 0.0f) {
			return;
		}

		BLPath path;
		path.add_circle(BLCircle(cx, cy, outerR));

		ctx.save();
		ctx.set_comp_op(toBLCompOp(compositingOp()));
		ctx.set_fill_alpha(opacity());
		if (!enabled) {
			ctx.set_global_alpha(0.5);
		}

		ctx.set_fill_style(UIColorManager::colorFor(UIColorRole::HighlightBackground).toBLRgba32());
		ctx.fill_path(path);

		if (enabled) {
			float overlayAlpha = fluentStateOverlayAlpha(pressed, highlighted);
			if (overlayAlpha > 0.0f) {
				Color overlay = UIColorManager::colorFor(UIColorRole::WindowText);
				ctx.set_fill_style(Color(overlay.r, overlay.g, overlay.b, overlayAlpha).toBLRgba32());
				ctx.fill_path(path);
			}
		}

		// A thin light ring for definition against a same-tone
		// background - matches WinUI's own slider thumb (a solid disc
		// with a subtle outline, not a flat edgeless blob). Centered on
		// outerR's own boundary, same as every other stroked shape in
		// this file - the 1px margin outerR already reserves keeps the
		// outer half of this 2px stroke from clipping against size's own
		// edge.
		ctx.set_stroke_style(UIColorManager::colorFor(UIColorRole::WindowBackground).toBLRgba32());
		ctx.set_stroke_width(2.0f);
		ctx.stroke_path(path);

		ctx.restore();
	}

	void FluentProgressBarTrackStyle::paint(BLContext& ctx, const Size& size, bool /*highlighted*/, Rect& clientBounds) const {
		clientBounds = computeClientBounds(size);

		if (size.width <= 0.0f || size.height <= 0.0f) {
			return;
		}

		float shortSide = size.width < size.height ? size.width : size.height;
		float radius = shortSide * 0.5f;

		BLPath path;
		path.add_round_rect(BLRoundRect(0.0f, 0.0f, size.width, size.height, radius));

		ctx.save();
		ctx.set_comp_op(toBLCompOp(compositingOp()));
		ctx.set_fill_alpha(opacity());
		ctx.set_fill_style(UIColorManager::colorFor(UIColorRole::ControlBackground).toBLRgba32());
		ctx.fill_path(path);
		ctx.set_stroke_style(UIColorManager::colorFor(UIColorRole::ControlBorder).toBLRgba32());
		ctx.set_stroke_width(1.0f);
		ctx.stroke_path(path);
		ctx.restore();
	}

	void FluentProgressBarFillStyle::paint(BLContext& ctx, const Size& size, bool /*highlighted*/, Rect& clientBounds) const {
		clientBounds = computeClientBounds(size);

		if (size.width <= 0.0f || size.height <= 0.0f) {
			return;
		}

		float shortSide = size.width < size.height ? size.width : size.height;
		float radius = shortSide * 0.5f;

		BLPath path;
		path.add_round_rect(BLRoundRect(0.0f, 0.0f, size.width, size.height, radius));

		BLRgba32 fillColor;
		switch (state) {
			case FillState::Error:
				// Fluent's own SystemFillColorCritical (light theme) -
				// no UIColorRole maps to a semantic error/warning color
				// (uicolormanager.h), so this is a fixed value rather
				// than a theme-reactive lookup, same trim this class's
				// own base (ThemedProgressBarFillStyle, above) already
				// accepts for marquee/PBFS_PARTIAL.
				fillColor = BLRgba32(0xC4, 0x2B, 0x1C, 0xFF);
				break;
			case FillState::Paused:
				// Fluent's own SystemFillColorCaution.
				fillColor = BLRgba32(0xFF, 0xB9, 0x00, 0xFF);
				break;
			case FillState::Normal:
			default:
				fillColor = UIColorManager::colorFor(UIColorRole::HighlightBackground).toBLRgba32();
				break;
		}

		ctx.save();
		ctx.set_comp_op(toBLCompOp(compositingOp()));
		ctx.set_fill_alpha(opacity());
		ctx.set_fill_style(fillColor);
		ctx.fill_path(path);
		ctx.restore();
	}

	void ThemedTrackbarTicksStyle::paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const {
		ThemedViewStyle::paint(ctx, size, highlighted, clientBounds);

		if (size.width <= 0.0f || size.height <= 0.0f || tickCount <= 0) {
			return;
		}

		Color tickColor = UIColorManager::colorFor(UIColorRole::ControlBorder);
		ctx.save();
		ctx.set_comp_op(toBLCompOp(compositingOp()));
		ctx.set_fill_style(tickColor.toBLRgba32());
		ctx.set_fill_alpha(opacity());

		// tickCount+1 marks (both ends included), evenly spaced across the
		// strip's own full extent - a close approximation of the real
		// thumb-travel-inset positions without this style needing to know
		// the thumb's own size (see the paint() declaration's doc comment,
		// viewstyle.h, for why this draws the lines itself at all).
		for (int i = 0; i <= tickCount; ++i) {
			double t = double(i) / double(tickCount);
			if (horizontal) {
				double x = t * double(size.width);
				ctx.fill_rect(BLRect(x - 0.5, 0.0, 1.0, double(size.height)));
			} else {
				double y = t * double(size.height);
				ctx.fill_rect(BLRect(0.0, y - 0.5, double(size.width), 1.0));
			}
		}

		ctx.restore();
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

		Rect r = Rect(0.0f, 0.0f, size.width, size.height).deflate(highlightInset);
		if (r.size().width <= 0.0f || r.size().height <= 0.0f) {
			return;
		}

		Color accent = UIColorManager::colorFor(UIColorRole::HighlightBackground);

		ctx.save();
		ctx.set_comp_op(toBLCompOp(compositingOp()));
		ctx.set_fill_style(accent.toBLRgba32());
		// Pressed reads as a stronger fill than a plain hover - same
		// "pressed is more emphatic than hot" precedence stateId() already
		// encodes for the native part this replaces.
		ctx.set_fill_alpha(opacity() * (pressed ? 0.55f : 0.30f));
		ctx.fill_round_rect(double(r.left()), double(r.top()),
			double(r.size().width), double(r.size().height), double(highlightCornerRadius));
		ctx.restore();
	}

}
