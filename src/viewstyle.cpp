





#include "newui/viewstyle.h"
#include "newui/view.h"
#include "newui/rootview.h"
#include "newui/color.h"
#include "newui/themedata.h"
#include "newui/uicolormanager.h"

#include <array>

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

	void invertLightnessInPlace(RGBQUAD* bits, int width, int height, int strideInPixels, bool highlighted) {
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
				newui::Color::fromHSL(hsl).toBGRA32(straightBytes);
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
	BLCompOp toBLCompOp(CompositingFlag f)
	{
		BLCompOp result = (BLCompOp)0;

		switch (f) {
		case CompSrcOver: { result = BL_COMP_OP_SRC_OVER; } break;
			case CompSrcopy: { result = BL_COMP_OP_SRC_COPY; } break;
			case CompSrcIn: { result = BL_COMP_OP_SRC_IN; } break;
			case CompSrcOut: { result = BL_COMP_OP_SRC_OUT; } break;
			case CompSrcAtop: { result = BL_COMP_OP_SRC_ATOP; } break;
			case CompDstOver: { result = BL_COMP_OP_DST_OVER; } break;
			case CompDstCopy: { result = BL_COMP_OP_DST_COPY; } break;
			case CompDstIn: { result = BL_COMP_OP_DST_IN; } break;
			case CompDstOut: { result = BL_COMP_OP_DST_OUT; } break;
			case CompDstAtop: { result = BL_COMP_OP_DST_ATOP; } break;
			case CompXOR: { result = BL_COMP_OP_XOR; } break;
			case CompClear: { result = BL_COMP_OP_CLEAR; } break;
			case CompPlus: { result = BL_COMP_OP_PLUS; } break;
			case CompMinus: { result = BL_COMP_OP_MINUS; } break;
			case CompModulate: { result = BL_COMP_OP_MODULATE; } break;
			case CompMultiply: { result = BL_COMP_OP_MULTIPLY; } break;
			case CompScreen: { result = BL_COMP_OP_SCREEN; } break;
			case CompOverlay: { result = BL_COMP_OP_OVERLAY; } break;
			case CompDarken: { result = BL_COMP_OP_DARKEN; } break;
			case CompLighten: { result = BL_COMP_OP_LIGHTEN; } break;
			case CompColorDodge: { result = BL_COMP_OP_COLOR_DODGE; } break;
			case CompColorBurn: { result = BL_COMP_OP_COLOR_BURN; } break;
			case CompLinearBurn: { result = BL_COMP_OP_LINEAR_BURN; } break;
			case CompLinearLight: { result = BL_COMP_OP_LINEAR_LIGHT; } break;
			case CompPinLight: { result = BL_COMP_OP_PIN_LIGHT; } break;
			case CompHardLight: { result = BL_COMP_OP_HARD_LIGHT; } break;
			case CompSoftLight: { result = BL_COMP_OP_SOFT_LIGHT; } break;
			case CompDifference: { result = BL_COMP_OP_DIFFERENCE; } break;
			case CompExclusion: { result = BL_COMP_OP_EXCLUSION; } break;
		}

		return result;
	}

	CompositingFlag toCompositingFlag(BLCompOp f)
	{
		CompositingFlag result = (CompositingFlag)0;

		switch (f) {
			case BL_COMP_OP_SRC_OVER: { result = CompSrcOver; } break;
			case BL_COMP_OP_SRC_COPY: { result = CompSrcopy; } break;
			case BL_COMP_OP_SRC_IN: { result = CompSrcIn; } break;
			case BL_COMP_OP_SRC_OUT: { result = CompSrcOut; } break;
			case BL_COMP_OP_SRC_ATOP: { result = CompSrcAtop; } break;
			case BL_COMP_OP_DST_OVER: { result = CompDstOver; } break;
			case BL_COMP_OP_DST_COPY: { result = CompDstCopy; } break;
			case BL_COMP_OP_DST_IN: { result = CompDstIn; } break;
			case BL_COMP_OP_DST_OUT: { result = CompDstOut; } break;
			case BL_COMP_OP_DST_ATOP: { result = CompDstAtop; } break;
			case BL_COMP_OP_XOR: { result = CompXOR; } break;
			case BL_COMP_OP_CLEAR: { result = CompClear; } break;
			case BL_COMP_OP_PLUS: { result = CompPlus; } break;
			case BL_COMP_OP_MINUS: { result = CompMinus; } break;
			case BL_COMP_OP_MODULATE: { result = CompModulate; } break;
			case BL_COMP_OP_MULTIPLY: { result = CompMultiply; } break;
			case BL_COMP_OP_SCREEN: { result = CompScreen; } break;
			case BL_COMP_OP_OVERLAY: { result = CompOverlay; } break;
			case BL_COMP_OP_DARKEN: { result = CompDarken; } break;
			case BL_COMP_OP_LIGHTEN: { result = CompLighten; } break;
			case BL_COMP_OP_COLOR_DODGE: { result = CompColorDodge; } break;
			case BL_COMP_OP_COLOR_BURN: { result = CompColorBurn; } break;
			case BL_COMP_OP_LINEAR_BURN: { result = CompLinearBurn; } break;
			case BL_COMP_OP_LINEAR_LIGHT: { result = CompLinearLight; } break;
			case BL_COMP_OP_PIN_LIGHT: { result = CompPinLight; } break;
			case BL_COMP_OP_HARD_LIGHT: { result = CompHardLight; } break;
			case BL_COMP_OP_SOFT_LIGHT: { result = CompSoftLight; } break;
			case BL_COMP_OP_DIFFERENCE: { result = CompDifference; } break;
			case BL_COMP_OP_EXCLUSION: { result = CompExclusion; } break;
		}

		return result;
	}

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
			view_->redraw();
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
		backgroundFill_ = BLPattern(image);
		bkgFillStyle = FillStyle::FillImage;
	}

	void ViewStyle::setBackgroundGradient(const BLGradient& gradient)
	{
		backgroundFill_ = gradient;
		bkgFillStyle = FillStyle::FillGradient;
	}

	void ViewStyle::setBackgroundColor(const Color& color)
	{
		backgroundFill_ = color.toBLRgba32();
		bkgFillStyle = FillStyle::FillColor;
	}

	void ViewStyle::setBackgroundColor(const BLRgba32& color)
	{
		backgroundFill_ = color;
		bkgFillStyle = FillStyle::FillColor;
	}

	void ViewStyle::setHilightColor(const Color& color)
	{
		highlightFill = color.toBLRgba32();;
		hilightFillStyle = FillStyle::FillColor;		
	}

	Rect ViewStyle::computeClientBounds(const Size& size) const
	{
		Rect result(0.0f, 0.0f, size.width, size.height);

		return result.deflate(borderWidth);
	}
	void ViewStyle::paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const
	{
		clientBounds = computeClientBounds(size);

		if (size.width <= 0.0f || size.height <= 0.0f) {
			return;
		}

		bool useHighlight = highlighted && !highlightFill.is_null();
		const BLVar& background = useHighlight ? highlightFill : backgroundFill_;
		FillStyle fillStyle = useHighlight ? hilightFillStyle : bkgFillStyle;

		ctx.save();
		ctx.set_comp_op( toBLCompOp(compositingOp));



		if (!background.is_null()) {

			switch (fillStyle) {
				case FillStyle::FillColor:
				case FillStyle::FillImage: {
					ctx.set_fill_style(background);
					ctx.set_fill_alpha(opacity);

					if (rectRadius != 0.0) {
						ctx.fill_round_rect(BLRoundRect(0, 0, size.width, size.height, rectRadius));
					}
					else {
						ctx.fill_rect(BLRect(0, 0, size.width, size.height));
					}

				}
				break;

				case FillStyle::FillGradient: {
					ctx.set_fill_style(background);
					ctx.set_fill_alpha(opacity);
					ctx.fill_rect(BLRect(0, 0, size.width, size.height));
				}
				break;
			}
		}

		if (borderWidth > 0.0f && !borderFill.is_null()) {
			double inset = borderWidth * 0.5;
			ctx.set_stroke_style(borderFill);
			ctx.set_stroke_alpha(opacity);
			ctx.set_stroke_width(borderWidth);

			if (rectRadius != 0.0) {
				ctx.stroke_round_rect(BLRoundRect(inset, inset, size.width - inset, size.height - inset, rectRadius));
			}
			else {
				ctx.stroke_box(inset, inset, size.width - inset, size.height - inset);
			}

			
		}

		ctx.restore();
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
				ctx.set_comp_op(toBLCompOp(compositingOp));
				ctx.blit_image(BLPoint(0, 0), themedImage);
				ctx.restore();
			}

			::EndBufferedPaint(paintBuffer, TRUE);
		}

		::DeleteDC(targetDC);
	}

	void ThemedTrackbarTicksStyle::paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const {
		ThemedViewStyle::paint(ctx, size, highlighted, clientBounds);

		if (size.width <= 0.0f || size.height <= 0.0f || tickCount <= 0) {
			return;
		}

		Color tickColor = UIColorManager::colorFor(UIColorRole::ControlBorder);
		ctx.save();
		ctx.set_comp_op(toBLCompOp(compositingOp));
		ctx.set_fill_style(tickColor.toBLRgba32());
		ctx.set_fill_alpha(opacity);

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
		ctx.set_comp_op(toBLCompOp(compositingOp));
		ctx.set_fill_style(accent.toBLRgba32());
		// Pressed reads as a stronger fill than a plain hover - same
		// "pressed is more emphatic than hot" precedence stateId() already
		// encodes for the native part this replaces.
		ctx.set_fill_alpha(opacity * (pressed ? 0.55f : 0.30f));
		ctx.fill_round_rect(double(r.left()), double(r.top()),
			double(r.size().width), double(r.size().height), double(highlightCornerRadius));
		ctx.restore();
	}

}