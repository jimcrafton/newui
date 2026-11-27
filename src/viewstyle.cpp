





#include "newui/viewstyle.h"
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

	// Elevation -> shadow blur radius/offset magnitude, for ViewStyle::
	// prePaint()/computePrePaintBounds()'s elevation-driven drop shadow
	// below. Adapted from Fluent's own named elevation scale - Layer=1,
	// Control=2, Card=8, Tooltip=16, Flyout=32, Dialog/Window=128 (see
	// learn.microsoft.com/windows/apps/design/signature-experiences/
	// layering) - and its stated principle ("the higher the elevation,
	// the larger and softer the shadow becomes"), not a literal port:
	// Fluent's own ThemeShadow computes its actual blur/offset via an
	// opaque WinUI3 composition effect with no published formula, so
	// there's nothing to port literally - same "adapt the source
	// material's real intent to this codebase's own architecture, don't
	// assume a literal copy is even possible" approach used everywhere
	// else a docx/design-doc source has driven this codebase's own work.
	//
	// log2(elevation+1), not linear - keeps both bounded even at Window/
	// Dialog's elevation=128, instead of growing unboundedly. A *previous*
	// version of this formula scaled softness linearly (2*elevation_),
	// reaching a 256px blur radius there - once shapes::kBlurPadFactor
	// (3x, shapes.h) pads the mask Shape::paintEffect() (shapes.cpp) has
	// to allocate and box-blur, that's roughly 768px of padding *per
	// side* - a multi-million-pixel offscreen mask, box-blurred fresh on
	// every single paint() call, for one dialog's shadow.
	constexpr float kElevationBlurBase = 2.0f;
	constexpr float kElevationBlurScale = 3.0f;
	constexpr float kElevationOffsetBase = 1.0f;
	constexpr float kElevationOffsetScale = 1.5f;

	// Shadow *darkness* deliberately does not scale with elevation at
	// all - a previous version of this formula used 1.0f/elevation_,
	// making a Dialog's shadow (elevation 128) sixteen times fainter
	// than a Control's (elevation 2): exactly backwards from the real
	// visual hierarchy Fluent describes - a more-elevated surface should
	// read as *more* prominent, not nearly invisible. Real elevation-
	// shadow systems (Material Design's own published key/ambient shadow
	// alpha values - the only place either design language actually
	// publishes a concrete number here) keep shadow alpha roughly
	// constant across elevation too: it's the shadow's *size*, not its
	// darkness, that communicates height.
	constexpr float kElevationShadowAmount = 0.35f;

	float elevationBlurRadius(float elevation) {
		if (elevation <= 0.0f) {
			return 0.0f;
		}
		return kElevationBlurBase + kElevationBlurScale * std::log2(elevation + 1.0f);
	}

	float elevationShadowOffsetMagnitude(float elevation) {
		if (elevation <= 0.0f) {
			return 0.0f;
		}
		return kElevationOffsetBase + kElevationOffsetScale * std::log2(elevation + 1.0f);
	}

	// How far outside the View's own full edge ViewStyle::postPaint()'s
	// default focus ring is drawn - shared between postPaint() itself and
	// computePrePaintBounds() below specifically so the two can never
	// drift out of sync the way they did briefly: computePrePaintBounds()
	// used to only account for the elevation-driven drop shadow (gated on
	// elevation_ > 0.0f), so for any plain, non-elevated View (nearly
	// everything - a Button/TextField/etc. never calls setElevation() at
	// all) it left View::redraw()'s own invalidated region at exactly the
	// view's plain bounds, zero allowance for this ring's own 2px
	// outward bleed. Whether the ring actually showed up after a Tab
	// then depended entirely on whether some *other*, unrelated repaint
	// happened to also cover that extra margin - a real, confirmed live
	// bug (intermittent: tabbing through several controls in a row would
	// show the ring on some and not others, seemingly at random).
	constexpr float kFocusRingOutset = 2.0f;

	// The ring itself is a kFocusRingStrokeWidth-wide *stroke* centered on
	// the kFocusRingOutset boundary (ctx.set_stroke_width() in postPaint(),
	// below) - its actual painted pixels reach kFocusRingStrokeWidth/2
	// *past* that boundary, not right up to it. computePrePaintBoundsRingPad()
	// (below) is what computePrePaintBounds() actually reserves room with -
	// kFocusRingOutset alone (the centerline) is what a first fix here
	// used, and was still too small by exactly that half-stroke-width: a
	// real, confirmed live artifact (a thin sliver of the ring's own top/
	// left edge left behind on the previously-focused control after Tab
	// moved focus elsewhere - only a full, unrelated repaint of the whole
	// window, e.g. from a mouse move, happened to clear it, since nothing
	// about the *targeted* invalidation for that one control's own redraw()
	// covered where those last surviving pixels actually were). Rounded up
	// to a full extra kFocusRingStrokeWidth, not just the exact half, as a
	// deliberate safety margin against any further rounding (pixel-
	// snapping elsewhere in this pipeline - see paintChildren()'s own
	// snappedOutwardToPixels() call, view.cpp - already establishes that
	// "round outward, don't cut it close" is the right default here).
	constexpr float kFocusRingStrokeWidth = 1.0f;

	// Getting the exact half-stroke-width figure above right down to the
	// sub-pixel is brittle - AA coverage, the pixel-snapping in
	// paintChildren() (view.cpp), and DPI scaling can all nudge the
	// ring's true rightmost painted pixel a bit further than the strict
	// math implies. An extra 1px margin costs nothing (this whole pad is
	// tiny next to the elevation shadow pad below) and trades that
	// brittleness for simply always reserving enough room.
	constexpr float kFocusRingPadSafetyMargin = 1.0f;

	float computePrePaintBoundsRingPad() {
		return kFocusRingOutset + kFocusRingStrokeWidth + kFocusRingPadSafetyMargin;
	}

	// Greedy word-wrap for LabelStyle::paint()'s wordWrap() case - breaks
	// text into as many lines as needed so each line's *shaped* width
	// (BLFont::shape()+get_text_metrics(), the same measurement paint()
	// itself uses to draw, not a monospace character-count guess) fits
	// within maxWidth, breaking only at a space - never mid-word, so a
	// single word wider than maxWidth on its own is left to overflow that
	// one line rather than being hyphenated/cut. Always returns at least
	// one line (the whole text, unbroken) if text has no spaces at all or
	// is empty.
	std::vector<std::string> wrapLabelText(const std::string& text, BLFont& font, double maxWidth) {
		std::vector<std::string> lines;
		std::string currentLine;
		std::string word;

		auto shapedWidth = [&font](const std::string& s) -> double {
			BLGlyphBuffer glyphBuffer;
			glyphBuffer.set_utf8_text(s.c_str(), s.size());
			font.shape(glyphBuffer);
			BLTextMetrics metrics;
			font.get_text_metrics(glyphBuffer, metrics);
			return metrics.advance.x;
		};

		for (std::size_t i = 0; i <= text.size(); ++i) {
			bool atBreak = (i == text.size() || text[i] == ' ');
			if (!atBreak) {
				word += text[i];
				continue;
			}
			if (word.empty()) {
				continue;
			}

			std::string candidate = currentLine.empty() ? word : currentLine + " " + word;
			if (currentLine.empty() || shapedWidth(candidate) <= maxWidth) {
				currentLine = candidate;
			} else {
				lines.push_back(currentLine);
				currentLine = word;
			}
			word.clear();
		}

		if (!currentLine.empty()) {
			lines.push_back(currentLine);
		}
		if (lines.empty()) {
			lines.push_back(text);
		}
		return lines;
	}

	// Top-left offset (in fill-rect-local coordinates) an unscaled image
	// of imgSize should be drawn at within a rect of rectSize, per a
	// nine-way alignment anchor - see ImageAlignment's own doc comment
	// (viewstyle.h). Can come out negative (image bigger than rectSize
	// along that axis) - callers translate a pattern by this and then
	// clamp/clip the actual destination rect they fill, rather than this
	// function doing any clamping itself.
	BLPoint alignedImageOffset(newui::ImageAlignment align, const BLSizeI& imgSize, const newui::Size& rectSize) {
		double dx = double(rectSize.width) - double(imgSize.w);
		double dy = double(rectSize.height) - double(imgSize.h);

		double x = 0.0, y = 0.0;
		switch (align) {
			case newui::ImageAlignment::TopLeft:     x = 0.0;      y = 0.0;      break;
			case newui::ImageAlignment::Top:         x = dx * 0.5; y = 0.0;      break;
			case newui::ImageAlignment::TopRight:    x = dx;       y = 0.0;      break;
			case newui::ImageAlignment::Left:        x = 0.0;      y = dy * 0.5; break;
			case newui::ImageAlignment::Center:      x = dx * 0.5; y = dy * 0.5; break;
			case newui::ImageAlignment::Right:       x = dx;       y = dy * 0.5; break;
			case newui::ImageAlignment::BottomLeft:  x = 0.0;      y = dy;       break;
			case newui::ImageAlignment::Bottom:      x = dx * 0.5; y = dy;       break;
			case newui::ImageAlignment::BottomRight: x = dx;       y = dy;       break;
		}
		return BLPoint(x, y);
	}

}

namespace newui {
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
		//assert(nullptr != view_);
		//assert(nullptr != view_->rootView());

		if (nullptr != view_ && nullptr != view_->rootView()) {
			view_->redraw();
		}
	}

	namespace {
		// One-time conversion from a raw blend2d BLGradient (what setBackgroundGradient()'s own
		// public API still takes, for existing callers) into backgroundFill_'s own reflectable
		// gfx::Gradient representation (graphics.h) - the reverse of Gradient::toBLGradient()
		// (graphics.cpp), which this mirrors field-for-field. Radial's own r1 (second radius) is
		// never preserved - gfx::Gradient's own model has no field for it, matching
		// toBLGradient()'s reverse direction, which always passes 0.0 for it too - not a new
		// limitation introduced here.
		//
		// KNOWN GAP: gfx::Gradient's own geometry fields (linearStart_/linearEnd_/radialCenter_/
		// etc.) are proportional [0,1] fractions of whatever box they're eventually resolved
		// against (toBLGradient()'s own comment) - src's absolute pixel values pass through here
		// completely unconverted, since setBackgroundGradient() (this function's only caller) has
		// no target box to normalize against at this call site. Left as-is rather than guessed at,
		// since setBackgroundGradient(const BLGradient&) has zero real callers anywhere in this
		// codebase as of this writing (grep confirmed) - revisit (likely by adding a Rect bounds
		// parameter to setBackgroundGradient() itself) if a real caller ever needs this path to
		// round-trip correctly.
		gfx::Gradient toGfxGradient(const BLGradient& src) {
			gfx::Gradient g;
			switch (src.type()) {
				case BL_GRADIENT_TYPE_RADIAL: {
					g.setKind(gfx::GradientKind::Radial);
					const BLRadialGradientValues& v = src.radial();
					g.setRadialCenter(Point(float(v.x0), float(v.y0)));
					g.setRadialFocalOffset(Point(float(v.x1 - v.x0), float(v.y1 - v.y0)));
					g.setRadialRadius(float(v.r0));
					break;
				}
				case BL_GRADIENT_TYPE_CONIC: {
					g.setKind(gfx::GradientKind::Conic);
					const BLConicGradientValues& v = src.conic();
					g.setConicCenter(Point(float(v.x0), float(v.y0)));
					g.setConicAngle(float(v.angle));
					g.setConicRepeat(float(v.repeat));
					break;
				}
				case BL_GRADIENT_TYPE_LINEAR:
				default: {
					g.setKind(gfx::GradientKind::Linear);
					const BLLinearGradientValues& v = src.linear();
					g.setLinearStart(Point(float(v.x0), float(v.y0)));
					g.setLinearEnd(Point(float(v.x1), float(v.y1)));
					break;
				}
			}
			g.setExtendMode(gfx::toExtendMode(src.extend_mode()));
			for (const BLGradientStop& stop : src.stops_view()) {
				g.stops().push_back(gfx::GradientStop(float(stop.offset), Color(stop.rgba.value)));
			}
			return g;
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
		backgroundFill_.setImage(image);
		backgroundFill_.setKind(gfx::PaintKind::Image);
	}

	void ViewStyle::setBackgroundGradient(const BLGradient& gradient)
	{
		backgroundFill_.setGradient(toGfxGradient(gradient));
		backgroundFill_.setKind(gfx::PaintKind::Gradient);
	}

	void ViewStyle::setBackgroundColor(const Color& color)
	{
		backgroundFill_.setColor(color);
		backgroundFill_.setKind(gfx::PaintKind::Color);
	}

	void ViewStyle::setBackgroundColor(const BLRgba32& color)
	{
		setBackgroundColor(Color(color));
	}

	void ViewStyle::setHilightColor(const Color& color)
	{
		highlightFill_ = color;
	}

	Rect ViewStyle::computeClientBounds(const Size& size) const
	{
		Rect result(0.0f, 0.0f, size.width, size.height);

		return result.deflate(borderWidth_);
	}
	void ViewStyle::paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const
	{
		clientBounds = computeClientBounds(size);

		if (size.width <= 0.0f || size.height <= 0.0f) {
			return;
		}

		// highlightFill is solid-color-only (Color, not backgroundFill_'s gfx::Fill) - it never had
		// a real gradient/image setter or its own FillStyle branching to begin with, just a raw
		// value read directly, so a plain, unconditional solid fill here is exactly what every
		// existing caller (only ever setHilightColor()) already produced.
		bool useHighlight = highlighted && !highlightFill_.isNull();

		ctx.save();
		ctx.set_comp_op( toBLCompOp(compositingOp_));

		if (useHighlight) {
			ctx.set_fill_style(highlightFill_.toBLRgba32());
			ctx.set_fill_alpha(opacity_);

			if (rectRadius_ != 0.0) {
				ctx.fill_round_rect(BLRoundRect(0, 0, size.width, size.height, rectRadius_));
			}
			else {
				ctx.fill_rect(BLRect(0, 0, size.width, size.height));
			}
		}
		else if (BLVar background = backgroundFill_.toBLVar(Rect(0.0f, 0.0f, size.width, size.height)); !background.is_null()) {
			// backgroundFill_'s own opacity() (gfx::Fill, graphics.h - already used correctly by
			// the Shapes system, shapes.cpp) combines multiplicatively with this style's own
			// opacity - a real, live-caught bug otherwise: backgroundFill_.opacity() was a real,
			// editable Properties-panel field that had no effect on anything at all, since every
			// set_fill_alpha() call here only ever read the outer, pre-existing ViewStyle::opacity.
			float backgroundAlpha = opacity_ * backgroundFill_.opacity();

			switch (backgroundFill_.kind()) {
				case gfx::PaintKind::Color: {
					ctx.set_fill_style(background);
					ctx.set_fill_alpha(backgroundAlpha);

					if (rectRadius_ != 0.0) {
						ctx.fill_round_rect(BLRoundRect(0, 0, size.width, size.height, rectRadius_));
					}
					else {
						ctx.fill_rect(BLRect(0, 0, size.width, size.height));
					}

				}
				break;

				case gfx::PaintKind::Image: {
					// background may not actually hold a BLPattern (e.g. resolvedImage() failed to
					// load) - fall back to drawing whatever's really there rather than
					// reinterpreting it as one.
					if (!background.is_pattern()) {
						ctx.set_fill_style(background);
						ctx.set_fill_alpha(backgroundAlpha);

						if (rectRadius_ != 0.0) {
							ctx.fill_round_rect(BLRoundRect(0, 0, size.width, size.height, rectRadius_));
						}
						else {
							ctx.fill_rect(BLRect(0, 0, size.width, size.height));
						}
						break;
					}

					BLPattern pattern = background.as<BLPattern>();
					BLImage img = pattern.get_image();
					BLSizeI imgSize = img.size();

					ctx.set_fill_alpha(backgroundAlpha);

					if (imgSize.w <= 0 || imgSize.h <= 0) {
						break;  // nothing decoded - no natural size to tile/stretch/align against
					}

					switch (imageFillMode_) {
						case ImageFillMode::Stretch: {
							// Scaled to exactly cover the fill rect (ignores
							// the image's own aspect ratio) - PAD rather
							// than REPEAT since the transform already maps
							// the image's own edges onto the rect's edges
							// exactly, so PAD vs. REPEAT never actually
							// differs in the pixels drawn; PAD is cheaper.
							pattern.set_extend_mode(BL_EXTEND_MODE_PAD);
							pattern.set_transform(BLMatrix2D::make_scaling(
								double(size.width) / double(imgSize.w),
								double(size.height) / double(imgSize.h)));
							ctx.set_fill_style(pattern);

							if (rectRadius_ != 0.0) {
								ctx.fill_round_rect(BLRoundRect(0, 0, size.width, size.height, rectRadius_));
							}
							else {
								ctx.fill_rect(BLRect(0, 0, size.width, size.height));
							}
						}
						break;

						case ImageFillMode::Align: {
							// Unscaled, anchored per imageAlignment - only
							// the image's own (possibly rect-clipped)
							// footprint is actually filled, not the whole
							// view, so a smaller-than-the-view image
							// doesn't get its edge pixels padded/smeared
							// across the rest of the fill rect the way
							// BL_EXTEND_MODE_PAD would if the fill shape
							// were the full rect instead.
							BLPoint offset = alignedImageOffset(imageAlignment_, imgSize, size);
							pattern.set_extend_mode(BL_EXTEND_MODE_PAD);
							pattern.set_transform(BLMatrix2D::make_translation(offset));
							ctx.set_fill_style(pattern);

							double rx = offset.x < 0.0 ? 0.0 : offset.x;
							double ry = offset.y < 0.0 ? 0.0 : offset.y;
							double w = double(imgSize.w) < size.width ? double(imgSize.w) : double(size.width);
							double h = double(imgSize.h) < size.height ? double(imgSize.h) : double(size.height);
							if (rx + w > size.width) w = size.width - rx;
							if (ry + h > size.height) h = size.height - ry;

							if (w > 0.0 && h > 0.0) {
								ctx.fill_rect(BLRect(rx, ry, w, h));
							}
						}
						break;

						case ImageFillMode::Tile:
						default: {
							pattern.set_extend_mode(BL_EXTEND_MODE_REPEAT);
							pattern.reset_transform();
							ctx.set_fill_style(pattern);

							if (rectRadius_ != 0.0) {
								ctx.fill_round_rect(BLRoundRect(0, 0, size.width, size.height, rectRadius_));
							}
							else {
								ctx.fill_rect(BLRect(0, 0, size.width, size.height));
							}
						}
						break;
					}
				}
				break;

				case gfx::PaintKind::Gradient: {
					ctx.set_fill_style(background);
					ctx.set_fill_alpha(backgroundAlpha);
					ctx.fill_rect(BLRect(0, 0, size.width, size.height));
				}
				break;

				case gfx::PaintKind::None:
				default:
					break;
			}
		}

		if (borderWidth_ > 0.0f && !borderFill_.isNull()) {
			double inset = borderWidth_ * 0.5;
			ctx.set_stroke_style(borderFill_.toBLRgba32());
			ctx.set_stroke_alpha(opacity_);
			ctx.set_stroke_width(borderWidth_);

			if (rectRadius_ != 0.0) {
				ctx.stroke_round_rect(BLRoundRect(inset, inset, size.width - inset, size.height - inset, rectRadius_));
			}
			else {
				ctx.stroke_box(inset, inset, size.width - inset, size.height - inset);
			}

			
		}

		ctx.restore();
	}

	void ViewStyle::computePrePaintBounds(Rect& outDirtyBounds) const
	{
		// postPaint()'s own default focus ring always *potentially* needs
		// this much room, regardless of elevation - it's gated on
		// isFocused() only at actual paint time (postPaint() has to run
		// unconditionally every pass so an override stays free to draw
		// something else regardless of focus state - see its own doc
		// comment), so there's no way to know here, ahead of time,
		// whether a given redraw() is happening because focus is about to
		// land on (or leave) this particular View. Reserving the room
		// unconditionally is the only correct option - see this
		// constant's own comment (above) for the real bug leaving it out
		// caused. Cheap either way: 2px is negligible next to the
		// elevation pad below, when there is one.
		float pad = computePrePaintBoundsRingPad();

		if (elevation_ > 0.0f) {
			// Mirrors Shape::boundsWithEffects()'s own drop-shadow pad
			// formula exactly (shapes.cpp: softness * kBlurPadFactor +
			// offsetMag), using the same shapes::kBlurPadFactor constant
			// (shapes.h) - so this can never drift out of sync with what
			// prePaint()'s own shapes::Rectangle actually ends up
			// painting. Takes the max with the focus-ring pad above, not
			// a sum - both are measured outward from the same edges, so
			// what actually matters is the outer-most extent either
			// effect could reach in a given direction, not their total.
			float shadowPad = elevationBlurRadius(elevation_) * newui::shapes::kBlurPadFactor
				+ elevationShadowOffsetMagnitude(elevation_);
			pad = pad > shadowPad ? pad : shadowPad;
		}

		outDirtyBounds = outDirtyBounds.inflate(pad);
	}

	void ViewStyle::prePaint(BLContext& ctx, const Size& size, bool /*highlighted*/) const
	{
		if (size.width <= 0.0f || size.height <= 0.0f) {
			return;
		}

		if (elevation_ <= 0.0f) {
			return;
		}

		// The "elevation" effect is a soft drop shadow behind the view's
		// own fill - see elevationBlurRadius()/elevationShadowOffsetMagnitude()'s
		// own comments (above) for where the actual numbers come from.
		newui::shapes::Rectangle dropShadow(0.0f, 0.0f, size.width, size.height);
		dropShadow.style().fill().setColor(newui::Color(0.0f, 0.0f, 0.0f));
		dropShadow.style().fill().setKind(newui::gfx::PaintKind::Color);
		dropShadow.style().stroke().setKind(newui::gfx::PaintKind::None);
		// Near-zero, not the shadow's own opacity - deliberate, not a
		// bug. Shape::render() (shapes.cpp) wraps both the plain fill/
		// stroke above (only present to trace the mask *shape* from) and
		// the dropShadow effect below in the same outer
		// ctx.set_global_alpha(style_.opacity()) call, but
		// Shape::paintEffect() then makes its own separate
		// ctx.set_global_alpha(amount) call while blitting the shadow
		// mask - a flat state overwrite, not a multiply-on-top (confirmed
		// against Blend2D's own set_global_alpha(), a plain setter -
		// core/context.h) - restoring back to style_.opacity() only
		// afterward. So dropShadow().amount() below is genuinely the
		// shadow's real effective opacity, independent of this - which
		// exists purely to keep the plain black fill/stroke this Shape
		// also carries from ever actually appearing on screen as a solid
		// black box of its own.
		dropShadow.style().setOpacity(0.001f);

		float offsetMag = elevationShadowOffsetMagnitude(elevation_);
		dropShadow.style().dropShadow().setEnabled(true);
		dropShadow.style().dropShadow().setOffset(newui::Point(offsetMag, offsetMag));
		dropShadow.style().dropShadow().setSoftness(elevationBlurRadius(elevation_));
		dropShadow.style().dropShadow().setAmount(kElevationShadowAmount);

		dropShadow.render(ctx);
	}

	void ViewStyle::postPaint(BLContext& ctx, const Size& size, bool /*highlighted*/, const Rect& /*clientBounds*/) const
	{
		// This is the default *effect*, not a mandatory part of what
		// postPaint() itself means - only draw it while the owning View
		// is actually focused. Checked here, not by the caller (View::
		// postPaintStyle(), view.cpp) - postPaint() is called
		// unconditionally every paint pass now (see this method's own
		// doc comment, viewstyle.h), so a future override drawing some
		// other unclipped effect stays free to run regardless of focus.
		const bool hasError = view() != nullptr && !view()->errorText().empty();
		if (view() == nullptr || (!hasError && !view()->isFocused())) {
			return;
		}

		// kFocusRingOutset (above) pushes the ring outside the View's own
		// full edge rather than inset within it - matches real Windows 11/
		// Fluent keyboard focus visuals (the "high-visibility" focus
		// rectangle's FocusVisualMargin - see learn.microsoft.com/
		// windows/apps/design/accessibility/keyboard-accessibility),
		// which draw with a small *outward* gap from the control's own
		// edge, closer to a CSS outline than an inset box-shadow. This
		// only actually renders visibly because postPaint() itself now
		// runs in its own unclipped scope (View::paintChildren()'s phase
		// 3, view.cpp) *and* View::redraw()'s own invalidated region is
		// grown to match (ViewStyle::computePrePaintBounds(), above,
		// reads this same constant) - an earlier attempt at this same
		// outset, before either of those existed, was invisible or only
		// intermittently visible for exactly that reason. kCornerRadius
		// matches Fluent's own 4px standard for in-page controls
		// (learn.microsoft.com/windows/apps/design/signature-
		// experiences/geometry).
		//
		// Deliberately built from `size` (the View's own full given
		// bounds), not the `clientBounds` out-parameter - the two look
		// identical for most styles here (Button/Toggle/ToolbarButton/the
		// plain ViewStyle default all return the full size unchanged from
		// computeClientBounds(), nothing native to deflate for), so this
		// went unnoticed until FluentEditStyle's own real text-padding
		// deflation (computeClientBounds(), this file) made the gap
		// large enough to see: with clientBounds here instead, the ring
		// landed *inside* FluentEditStyle's own visible border rather
		// than outset past it, a real, confirmed live bug - the deflation
		// this method never accounted for outweighed kFocusRingOutset's
		// own 2px compensation in the wrong direction. computePrePaintBounds()
		// (below) already only ever worked in terms of the View's full
		// bounds, never clientBounds, so `size` is what actually keeps
		// the two in agreement.
		constexpr float kCornerRadius = 4.0f;
		Rect fullBounds(0.0f, 0.0f, size.width, size.height);
		Rect ring = fullBounds.inflate(kFocusRingOutset);
		if (ring.size().width <= 0.0f || ring.size().height <= 0.0f) {
			return;
		}

		ctx.save();
		ctx.set_comp_op(BL_COMP_OP_SRC_OVER);
		// A control a Binding rejected the value of gets the same ring in error red (Fluent's
		// system-critical red), whether or not it is focused.
		if (hasError) {
			ctx.set_stroke_style(BLRgba32(0xFFD13438u));
		}
		else {
			ctx.set_stroke_style(UIColorManager::colorFor(UIColorRole::HighlightBackground).toBLRgba32());
		}
		ctx.set_stroke_width(1.0f);

		// A solid rounded-rect outline in the accent color - not the
		// classic dashed DrawFocusRect() look, a deliberate choice, not a
		// missing feature: BLContext::set_stroke_dash_array()/
		// set_stroke_dash_offset() exist and correctly store the dash
		// pattern into the context's stroke_options (confirmed by reading
		// raster/rastercontext.cpp's own setter implementations), but
		// this vendored Blend2D's actual path stroker
		// (core/pathstroke.cpp) never reads stroke_options.dash_array at
		// all - dashing is simply unimplemented in this build, tried and
		// confirmed live (a 6px dash/gap pattern still rendered as one
		// unbroken line). A solid accent-colored ring is a legitimate,
		// modern focus style in its own right (the same idea CSS's
		// :focus-visible/Fluent/Material conventions already use), not a
		// downgrade - implementing dashing by hand (segmenting the path
		// into alternating drawn/skipped pieces around the rounded
		// corners) was considered and deliberately not done.
		BLPath path;
		path.add_round_rect(BLRoundRect(ring.left(), ring.top(), ring.size().width, ring.size().height, kCornerRadius));
		ctx.stroke_path(path);
		ctx.restore();
	}

	void LabelStyle::paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const {
		ViewStyle::paint(ctx, size, highlighted, clientBounds);

		if (text().empty() || textColor().isNull()) {
			return;
		}

		BLFont* blFont = font().blFont();
		if (blFont == nullptr || !blFont->is_valid()) {
			throw std::runtime_error("LabelStyle::paint: font not resolved to a valid BLFont");
		}

		if (clientBounds.size().width <= 0.0f || clientBounds.size().height <= 0.0f) {
			return;
		}

		const BLFontMetrics& fontMetrics = blFont->metrics();
		double lineHeight = fontMetrics.ascent + fontMetrics.descent;

		std::vector<std::string> lines = wordWrap()
			? wrapLabelText(text(), *blFont, clientBounds.size().width)
			: std::vector<std::string>{ text() };

		double totalTextHeight = lineHeight * static_cast<double>(lines.size());
		double slack = clientBounds.size().height - totalTextHeight;
		if (verticalTextAlignment_ == VerticalTextAlignment::Top) {
			slack = 0.0;
		} else if (verticalTextAlignment_ == VerticalTextAlignment::Middle) {
			slack *= 0.5;
		}
		double y = clientBounds.top() + slack + fontMetrics.ascent;

		ctx.save();
		ctx.set_comp_op(toBLCompOp(compositingOp()));
		ctx.set_fill_style(textColor().toBLRgba32());
		ctx.set_fill_alpha(opacity());

		// Tracks the last drawn line's own position/width for the
		// underline below - with wordWrap() off, lines has exactly one
		// element, so this ends up identical to the original single-line
		// behavior; with wrapping on, only the last line gets underlined
		// (a multi-line underline under every wrapped line would need its
		// own visual design decision this doesn't attempt to guess).
		double lastLineX = clientBounds.left();
		double lastLineWidth = 0.0;
		double lastLineY = y;

		for (const std::string& line : lines) {
			BLGlyphBuffer glyphBuffer;
			glyphBuffer.set_utf8_text(line.c_str(), line.size());
			blFont->shape(glyphBuffer);

			BLTextMetrics textMetrics;
			blFont->get_text_metrics(glyphBuffer, textMetrics);
			double textWidth = textMetrics.advance.x;
			double x = clientBounds.left();
			if (textAlignment_ == TextAlignment::Center) {
				x += (clientBounds.size().width - textWidth) * 0.5;
			} else if (textAlignment_ == TextAlignment::Right) {
				x += clientBounds.size().width - textWidth;
			}

			ctx.fill_utf8_text(BLPoint(x, y), *blFont, line.c_str(), line.size());

			lastLineX = x;
			lastLineWidth = textWidth;
			lastLineY = y;
			y += lineHeight;
		}

		if (font().underlined()) {
			// Same fill_style/alpha already set above for the text
			// itself - the underline is meant to look like part of the
			// same stroke of "color", not a separate element. Positioned/
			// sized from the font's own underline_position/
			// underline_thickness (BLFontMetrics) so it tracks whatever
			// font is actually in use rather than a guessed fixed offset.
			double thickness = fontMetrics.underline_thickness > 0.0f
				? double(fontMetrics.underline_thickness) : 1.0;
			double underlineY = lastLineY + fontMetrics.underline_position;
			ctx.fill_rect(BLRect(lastLineX, underlineY, lastLineWidth, thickness));
		}

		ctx.restore();
	}

	void ImageFillStyle::paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const {
		// highlightFill is solid-color-only now (Color, not backgroundFill()'s gfx::Fill) - it can
		// never be FillImage, so a checkerboard backdrop is only ever possibly needed for the
		// *background* fill, regardless of highlighted. Same guards ViewStyle::paint() itself
		// applies before touching background - mirrored here since this runs *before* delegating
		// to it, to decide whether there's even an image fill worth backing with a checkerboard in
		// the first place.
		bool useHighlight = highlighted && !highlightFill().isNull();
		BLVar background = backgroundFill().toBLVar(Rect(0.0f, 0.0f, size.width, size.height));

		if (!useHighlight && size.width > 0.0f && size.height > 0.0f && backgroundFill().kind() == gfx::PaintKind::Image &&
				!background.is_null() && background.is_pattern()) {
			BLImage img = background.as<BLPattern>().get_image();

			// See this class's own comment (viewstyle.h) for why format()
			// alone is the right "does this actually have an alpha
			// channel" signal - blend2d's PNG decoder already resolved
			// that question once, at load time.
			if (img.format() == BL_FORMAT_PRGB32) {
				double cs = double(checkerSize() > 0.0f ? checkerSize() : 8.0f);
				BLRgba32 colorA = checkerColorA().toBLRgba32();
				BLRgba32 colorB = checkerColorB().toBLRgba32();

				ctx.save();
				// Always plain SRC_OVER, fully opaque, regardless of this
				// style's own compositingOp/opacity - the checkerboard is
				// a neutral "you're looking at transparency" indicator,
				// not part of the image's own painted content, so neither
				// an exotic blend mode nor a faded opacity meant for the
				// image itself should touch it.
				ctx.set_comp_op(BL_COMP_OP_SRC_OVER);
				for (double y = 0.0; y < double(size.height); y += cs) {
					double h = (y + cs > double(size.height)) ? double(size.height) - y : cs;
					int row = int(y / cs);
					for (double x = 0.0; x < double(size.width); x += cs) {
						double w = (x + cs > double(size.width)) ? double(size.width) - x : cs;
						int col = int(x / cs);
						ctx.set_fill_style(((row + col) % 2 == 0) ? colorA : colorB);
						ctx.fill_rect(BLRect(x, y, w, h));
					}
				}
				ctx.restore();
			}
		}

		ViewStyle::paint(ctx, size, highlighted, clientBounds);
	}

}
