#include "stylepanel.h"

#include <newui/color.h>
#include <newui/mouse_constants.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <stdexcept>

namespace sketch {

    using namespace newui;

    namespace {
        std::string narrow(const std::wstring& text) {
            std::string result;
            for (wchar_t ch : text) {
                result.push_back(ch < 128 ? char(ch) : '?');
            }
            return result;
        }

        std::wstring widen(const std::string& text) {
            return std::wstring(text.begin(), text.end());
        }

        // The leading number in text such as "4 pt" or "55%"; false when there is none.
        bool leadingNumber(const std::wstring& text, double& out) {
            std::string s = narrow(text);
            char* end = nullptr;
            double value = std::strtod(s.c_str(), &end);
            if (end == s.c_str()) {
                return false;
            }
            out = value;
            return true;
        }

        Color toColor(const SketchColor& color) {
            BLRgba32 rgba = color.toRgba32();
            return Color(float(rgba.r()) / 255.0f, float(rgba.g()) / 255.0f, float(rgba.b()) / 255.0f, float(rgba.a()) / 255.0f);
        }

        const char* const kColorSliders[] = {"comp0Slider", "comp1Slider", "comp2Slider", "comp3Slider", "alphaSlider"};

        // How each color space is laid out in the four component sliders.
        struct ModeLayout {
            std::size_t count;
            const char* captions[4];
            float maxima[4];
        };
        const ModeLayout kLayouts[] = {
            {1, {"K", "", "", ""}, {100.0f, 0.0f, 0.0f, 0.0f}},               // Gray: ink coverage like Illustrator (0 = white)
            {3, {"R", "G", "B", ""}, {255.0f, 255.0f, 255.0f, 0.0f}},         // RGB
            {4, {"C", "M", "Y", "K"}, {100.0f, 100.0f, 100.0f, 100.0f}},      // CMYK
            {3, {"H", "S", "B", ""}, {360.0f, 100.0f, 100.0f, 0.0f}},         // HSB
        };

        // Index into kLayouts / the segmented control (Gray, RGB, CMYK, HSB).
        std::size_t modeIndex(ColorSpace space) {
            switch (space) {
                case ColorSpace::Gray: return 0;
                case ColorSpace::CMYK: return 2;
                case ColorSpace::HSB: return 3;
                default: return 1;
            }
        }
        ColorSpace spaceAt(std::size_t index) {
            static const ColorSpace kSpaces[] = {ColorSpace::Gray, ColorSpace::RGB, ColorSpace::CMYK, ColorSpace::HSB};
            return kSpaces[index < 4 ? index : 1];
        }

        // The slider positions for a color in its own space.
        void sliderValues(const SketchColor& color, float out[4]) {
            const ModeLayout& layout = kLayouts[modeIndex(color.space())];
            for (std::size_t i = 0; i < 4; ++i) {
                out[i] = i < layout.count ? color.component(i) * layout.maxima[i] : 0.0f;
            }
            if (color.space() == ColorSpace::Gray) {
                out[0] = (1.0f - color.component(0)) * 100.0f;
            }
        }

        SketchColor colorFromSliders(ColorSpace space, const float v[4], float alpha) {
            const ModeLayout& layout = kLayouts[modeIndex(space)];
            float n[4];
            for (std::size_t i = 0; i < 4; ++i) {
                n[i] = layout.maxima[i] > 0.0f ? v[i] / layout.maxima[i] : 0.0f;
            }
            switch (space) {
                case ColorSpace::CMYK: return SketchColor::cmyk(n[0], n[1], n[2], n[3], alpha);
                case ColorSpace::Gray: return SketchColor::gray(1.0f - n[0], alpha);
                case ColorSpace::HSB: return SketchColor::hsb(n[0], n[1], n[2], alpha);
                default: return SketchColor::rgb(n[0], n[1], n[2], alpha);
            }
        }
    }

    StylePanel::StylePanel(RootView& root, SketchDocument& document)
        : root_(root), document_(document) {

        // Fill / Stroke wells in the tool palette choose what the Color panel edits.
        if (auto* well = find<SubView>("fillWell")) {
            well->onMouseDown += [this](View&, const Point&, std::uint32_t, std::uint32_t) {
                setTarget(Target::Fill);
                return SyncReturn::Handled;
            };
        }
        if (auto* well = find<SubView>("strokeWell")) {
            well->onMouseDown += [this](View&, const Point&, std::uint32_t, std::uint32_t) {
                setTarget(Target::Stroke);
                return SyncReturn::Handled;
            };
        }

        // The "Fill / Stroke" text in the Color panel header toggles the target too (the wells sit at the very
        // bottom of the tool palette, which is easy to miss or to have clipped by a short window).
        if (auto* info = find<View>("colorPanelHeaderInfo")) {
            info->onMouseDown += [this](View&, const Point&, std::uint32_t, std::uint32_t) {
                setTarget(target_ == Target::Fill ? Target::Stroke : Target::Fill);
                return SyncReturn::Handled;
            };
        }

        // Sliders: a drag is live while the button is down and one undo step on release. The four component
        // sliders change meaning and range with the color mode (see kLayouts) - refresh() sets them; alpha is 0..100.
        for (const char* name : kColorSliders) {
            if (auto* slider = find<Slider>(name)) {
                slider->onMouseDown += [this](View&, const Point&, std::uint32_t, std::uint32_t) {
                    beginDrag();
                    return SyncReturn::Ignored;  // the slider still needs the press
                };
                slider->onMouseUp += [this](View&, const Point&, std::uint32_t, std::uint32_t) {
                    endDrag();
                    return SyncReturn::Ignored;
                };
                slider->onValueChanged += [this, name](Slider& changed) {
                    if (updating_) {
                        return SyncReturn::Ignored;
                    }
                    if (auto* value = find<TextField>((std::string(name) + "Value").c_str())) {
                        updating_ = true;
                        value->setText(widen(std::to_string(int(changed.value() + 0.5f))));
                        updating_ = false;
                    }
                    applySlidersAsColor();
                    return SyncReturn::Handled;
                };
            }
            // Typing a number in the value box moves the slider (and so applies the color).
            if (auto* field = find<TextField>((std::string(name) + "Value").c_str())) {
                field->onReturnPressed += [this, name](TextField& typed) {
                    double number = 0.0;
                    if (auto* slider = find<Slider>(name)) {
                        if (leadingNumber(typed.text(), number)) {
                            slider->setValue(float(number));
                        } else {
                            refresh();
                        }
                    }
                    return SyncReturn::Handled;
                };
            }
        }

        if (auto* opacity = find<Slider>("opacitySlider")) {
            opacity->onMouseDown += [this](View&, const Point&, std::uint32_t, std::uint32_t) {
                beginDrag();
                return SyncReturn::Ignored;
            };
            opacity->onMouseUp += [this](View&, const Point&, std::uint32_t, std::uint32_t) {
                endDrag();
                return SyncReturn::Ignored;
            };
            opacity->onValueChanged += [this](Slider& changed) {
                if (updating_) {
                    return SyncReturn::Ignored;
                }
                if (auto* value = find<TextField>("opacitySliderValue")) {
                    updating_ = true;
                    value->setText(widen(std::to_string(int(changed.value() + 0.5f))));
                    updating_ = false;
                }
                applyOpacity();
                return SyncReturn::Handled;
            };
        }
        if (auto* field = find<TextField>("opacitySliderValue")) {
            field->onReturnPressed += [this](TextField& typed) {
                double number = 0.0;
                if (auto* slider = find<Slider>("opacitySlider")) {
                    if (leadingNumber(typed.text(), number)) {
                        slider->setValue(float(number));
                    } else {
                        refresh();
                    }
                }
                return SyncReturn::Handled;
            };
        }

        if (auto* hex = find<TextField>("hexField")) {
            hex->onReturnPressed += [this](TextField&) {
                applyHex();
                return SyncReturn::Handled;
            };
            hex->onLostFocus += [this](View&) {
                applyHex();
                return SyncReturn::Handled;
            };
        }
        if (auto* weight = find<TextField>("strokeWeightField")) {
            weight->onReturnPressed += [this](TextField&) {
                applyStrokeWeight();
                return SyncReturn::Handled;
            };
            weight->onLostFocus += [this](View&) {
                applyStrokeWeight();
                return SyncReturn::Handled;
            };
        }

        // Cap and join are radio groups of toggle buttons: checking one applies it, unchecking the active one snaps back.
        auto wireOption = [this](const char* name, std::function<void(StrokeOptions&)> set, const char* what) {
            if (auto* button = find<ToolbarButton>(name)) {
                button->onCheckedChanged += [this, set, what](ToolbarButton& changed) {
                    if (updating_) {
                        return SyncReturn::Ignored;
                    }
                    if (changed.isChecked()) {
                        applyStrokeOptions(set, what);
                    }
                    refresh();
                    return SyncReturn::Handled;
                };
            }
        };
        wireOption("capButtButton", [](StrokeOptions& o) { o.cap = StrokeCap::Butt; }, "Butt Cap");
        wireOption("capRoundButton", [](StrokeOptions& o) { o.cap = StrokeCap::Round; }, "Round Cap");
        wireOption("capSquareButton", [](StrokeOptions& o) { o.cap = StrokeCap::Square; }, "Square Cap");
        wireOption("joinMiterButton", [](StrokeOptions& o) { o.join = StrokeJoin::Miter; }, "Miter Join");
        wireOption("joinRoundButton", [](StrokeOptions& o) { o.join = StrokeJoin::Round; }, "Round Join");
        wireOption("joinBevelButton", [](StrokeOptions& o) { o.join = StrokeJoin::Bevel; }, "Bevel Join");
        if (auto* miter = find<TextField>("miterLimitField")) {
            miter->onReturnPressed += [this](TextField&) {
                applyMiterLimit();
                return SyncReturn::Handled;
            };
            miter->onLostFocus += [this](View&) {
                applyMiterLimit();
                return SyncReturn::Handled;
            };
        }

        if (auto* swatches = find<SwatchGrid>("swatchGrid")) {
            swatches->onSwatchPicked += [this](SwatchGrid& grid) {
                std::size_t index = grid.selectedIndex();
                if (index < grid.swatches().size()) {
                    try {
                        // Swatches are hex (RGB); the document's color mode decides the space they are applied in.
                        SketchColor color = SketchColor::fromHex(grid.swatches()[index]).convertedTo(document_.colorMode());
                        applyColor(color, "Apply Swatch");
                    } catch (const std::runtime_error&) {
                    }
                }
                return SyncReturn::Handled;
            };
        }

        // Fill type: none / solid / linear / radial. Switching between kinds keeps what it can (a gradient's
        // selected stop becomes the solid color, a solid becomes the gradient's first stop).
        auto wireType = [this](const char* name, int kind) {
            if (auto* button = find<ToolbarButton>(name)) {
                button->onCheckedChanged += [this, kind](ToolbarButton& changed) {
                    if (updating_) {
                        return SyncReturn::Ignored;
                    }
                    if (changed.isChecked()) {
                        SketchPaint current = targetPaint();
                        if (kind == 0) {
                            applyPaint(SketchPaint::none(), "No Paint");
                        } else if (kind == 1) {
                            applyPaint(SketchPaint::solid(current.isNone() ? lastSolid_ : displayedColor()), "Solid Paint");
                        } else {
                            SketchGradient gradient = current.isGradient() ? current.gradient()
                                : SketchGradient::fromColor(current.isNone() ? lastSolid_ : current.color());
                            applyGradient(gradient, kind == 2 ? SketchPaint::Kind::Linear : SketchPaint::Kind::Radial,
                                kind == 2 ? "Linear Gradient" : "Radial Gradient");
                        }
                    }
                    refresh();
                    return SyncReturn::Handled;
                };
            }
        };
        wireType("fillNoneButton", 0);
        wireType("fillSolidButton", 1);
        wireType("fillLinearButton", 2);
        wireType("fillRadialButton", 3);

        // The gradient bar: click/drag stops. Dragging is live and one undo step on release, like a slider.
        if (auto* bar = find<GradientBar>("gradientBar")) {
            bar->onMouseDown += [this](View&, const Point&, std::uint32_t, std::uint32_t) {
                beginDrag();
                return SyncReturn::Ignored;
            };
            bar->onMouseUp += [this](View&, const Point&, std::uint32_t, std::uint32_t) {
                endDrag();
                return SyncReturn::Ignored;
            };
            bar->onStopSelected += [this](GradientBar& changed) {
                selectedStop_ = changed.selectedStop();
                refresh();
                onStopSelected(*this);
                return SyncReturn::Handled;
            };
            bar->onStopsEdited += [this](GradientBar& changed) {
                SketchPaint paint = targetPaint();
                if (!paint.isGradient()) {
                    refresh();  // the bar only edits a gradient paint
                    return SyncReturn::Handled;
                }
                SketchGradient gradient = paint.gradient();
                gradient.stops.clear();
                for (const GradientBar::Stop& stop : changed.parsedStops()) {
                    gradient.stops.push_back({stop.offset, SketchColor::rgb(stop.color.r, stop.color.g, stop.color.b, stop.color.a)});
                }
                selectedStop_ = changed.selectedStop();
                applyGradient(gradient, paint.kind(), "Edit Gradient");
                return SyncReturn::Handled;
            };
        }
        for (const char* name : {"stopOffsetField", "gradientAngleField"}) {
            if (auto* field = find<TextField>(name)) {
                field->onReturnPressed += [this](TextField&) {
                    applyGradientFields();
                    return SyncReturn::Handled;
                };
                field->onLostFocus += [this](View&) {
                    applyGradientFields();
                    return SyncReturn::Handled;
                };
            }
        }
        auto wireButton = [this](const char* name, std::function<void()> action) {
            if (auto* button = find<ToolbarButton>(name)) {
                button->onClick += [action](Control&) {
                    action();
                    return SyncReturn::Handled;
                };
            }
        };
        wireButton("addStopButton", [this]() {
            SketchPaint paint = targetPaint();
            if (!paint.isGradient() || paint.gradient().stops.size() < 1) {
                return;
            }
            SketchGradient gradient = paint.gradient();
            std::size_t index = std::min(selectedStop_, gradient.stops.size() - 1);
            // Halfway between the selected stop and the next one (or the previous one at the end).
            double here = gradient.stops[index].offset;
            double other = index + 1 < gradient.stops.size() ? gradient.stops[index + 1].offset
                : (index > 0 ? gradient.stops[index - 1].offset : 1.0);
            double offset = (here + other) * 0.5;
            gradient.stops.push_back({offset, gradient.colorAt(offset)});
            gradient.sortStops();
            for (std::size_t i = 0; i < gradient.stops.size(); ++i) {
                if (gradient.stops[i].offset == offset) {
                    selectedStop_ = i;
                }
            }
            applyGradient(gradient, paint.kind(), "Add Gradient Stop");
            refresh();
            onStopSelected(*this);
        });
        wireButton("deleteStopButton", [this]() {
            SketchPaint paint = targetPaint();
            if (!paint.isGradient() || paint.gradient().stops.size() <= 2) {
                return;  // a gradient keeps at least two stops
            }
            SketchGradient gradient = paint.gradient();
            std::size_t index = std::min(selectedStop_, gradient.stops.size() - 1);
            gradient.stops.erase(gradient.stops.begin() + std::ptrdiff_t(index));
            selectedStop_ = std::min(index, gradient.stops.size() - 1);
            applyGradient(gradient, paint.kind(), "Delete Gradient Stop");
            refresh();
            onStopSelected(*this);
        });
        wireButton("reverseGradientButton", [this]() {
            SketchPaint paint = targetPaint();
            if (!paint.isGradient()) {
                return;
            }
            SketchGradient gradient = paint.gradient();
            gradient.reverse();
            selectedStop_ = gradient.stops.empty() ? 0 : gradient.stops.size() - 1 - std::min(selectedStop_, gradient.stops.size() - 1);
            applyGradient(gradient, paint.kind(), "Reverse Gradient");
            refresh();
        });
        if (auto* spread = find<DropDownList>("spreadCombo")) {
            spread->onSelectionChanged += [this](DropDownList& combo) {
                SketchPaint paint = targetPaint();
                std::optional<std::size_t> index = combo.selectedIndex();
                if (updating_ || !paint.isGradient() || !index.has_value()) {
                    return SyncReturn::Ignored;
                }
                SketchGradient gradient = paint.gradient();
                gradient.spread = *index == 1 ? GradientSpread::Repeat : (*index == 2 ? GradientSpread::Reflect : GradientSpread::Pad);
                if (gradient != paint.gradient()) {
                    applyGradient(gradient, paint.kind(), "Gradient Spread");
                }
                return SyncReturn::Handled;
            };
        }

        // Apply: across the object (default) or, for a stroke, along its length.
        if (auto* apply = find<DropDownList>("gradientApplyCombo")) {
            apply->onSelectionChanged += [this](DropDownList& combo) {
                SketchPaint paint = targetPaint();
                std::optional<std::size_t> index = combo.selectedIndex();
                if (updating_ || !paint.isGradient() || !index.has_value()) {
                    return SyncReturn::Ignored;
                }
                SketchGradient gradient = paint.gradient();
                gradient.alongStroke = *index == 1 && target_ == Target::Stroke;
                if (gradient != paint.gradient()) {
                    applyGradient(gradient, paint.kind(), "Gradient Along Stroke");
                }
                return SyncReturn::Handled;
            };
        }

        // The color-mode control shows the space of the color being edited; clicking another converts that
        // color into it (one undo step). With no paint there is nothing to convert, so it just snaps back.
        if (auto* mode = find<SegmentedControl>("colorModeControl")) {
            mode->onSelectionChanged += [this](SegmentedControl& changed) {
                if (updating_) {
                    return SyncReturn::Ignored;
                }
                std::optional<std::size_t> index = changed.selectedIndex();
                if (index.has_value() && !targetPaint().isNone()) {
                    SketchColor color = displayedColor();
                    ColorSpace space = spaceAt(*index);
                    if (color.space() != space) {
                        applyColor(color.convertedTo(space), "Convert Color Mode");
                    }
                }
                refresh();
                return SyncReturn::Handled;
            };
        }

        document_.onSelectionChanged += [this](SketchDocument&) {
            refresh();
            return SyncReturn::Handled;
        };
        document_.onChanged += [this](SketchDocument&) {
            if (!dragging_) {
                refresh();
            }
            return SyncReturn::Handled;
        };
        refresh();
    }

    void StylePanel::setTarget(Target target) {
        target_ = target;
        refresh();
        onTargetChanged(*this);
    }

    void StylePanel::setSelectedStop(std::size_t index) {
        selectedStop_ = index;
        refresh();
    }

    SketchPaint StylePanel::targetPaint() const {
        if (!document_.selection().empty()) {
            if (const SketchObject* object = document_.findById(document_.selection().front())) {
                return target_ == Target::Fill ? object->fill() : object->stroke();
            }
        }
        return target_ == Target::Fill ? document_.defaultFill() : document_.defaultStroke();
    }

    void StylePanel::beginDrag() {
        if (!dragging_) {
            dragging_ = true;
            dragBefore_ = document_.snapshotStyle();
            dragDescription_.clear();
        }
    }

    void StylePanel::endDrag() {
        if (!dragging_) {
            return;
        }
        dragging_ = false;
        if (!dragBefore_.empty() && !dragDescription_.empty()) {
            document_.commitStyle(dragBefore_, dragDescription_);
        }
        dragBefore_.clear();
        refresh();
    }

    void StylePanel::edit(const char* description, const std::function<void()>& onSelection, const std::function<void()>& onDefaults) {
        if (document_.selection().empty()) {
            onDefaults();
            return;
        }
        std::vector<StyleState> before;
        if (!dragging_) {
            before = document_.snapshotStyle();
        }
        onSelection();
        if (dragging_) {
            dragDescription_ = description;
        } else {
            document_.commitStyle(before, description);
        }
    }

    void StylePanel::applyPaint(const SketchPaint& paint, const char* description) {
        bool fill = target_ == Target::Fill;
        edit(description,
            [this, fill, paint]() { if (fill) { document_.setSelectionFill(paint); } else { document_.setSelectionStroke(paint); } },
            [this, fill, paint]() { if (fill) { document_.setDefaultFill(paint); } else { document_.setDefaultStroke(paint); } });
        if (paint.kind() == SketchPaint::Kind::Solid) {
            lastSolid_ = paint.color();
        }
    }

    void StylePanel::applySlidersAsColor() {
        auto* a = find<Slider>("alphaSlider");
        if (a == nullptr) {
            return;
        }
        float v[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        for (int i = 0; i < 4; ++i) {
            if (auto* slider = find<Slider>(kColorSliders[i])) {
                v[i] = slider->value();
            }
        }
        // Edit in the space the color already has (what the mode control shows).
        SketchColor color = colorFromSliders(displayedColor().space(), v, a->value() / 100.0f);
        applyColor(color, target_ == Target::Fill ? "Change Fill" : "Change Stroke");
    }

    SketchColor StylePanel::displayedColor() const {
        SketchPaint paint = targetPaint();
        if (paint.isGradient() && !paint.gradient().stops.empty()) {
            return paint.gradient().stops[std::min(selectedStop_, paint.gradient().stops.size() - 1)].color;
        }
        // No paint: show black in the document's color mode (what choosing a color would start from).
        return paint.isNone() ? SketchColor::rgb(0.0f, 0.0f, 0.0f).convertedTo(document_.colorMode()) : paint.color();
    }

    void StylePanel::applyColor(const SketchColor& color, const char* description) {
        SketchPaint paint = targetPaint();
        if (paint.isGradient() && !paint.gradient().stops.empty()) {
            SketchGradient gradient = paint.gradient();
            gradient.stops[std::min(selectedStop_, gradient.stops.size() - 1)].color = color;
            applyGradient(gradient, paint.kind(), description);
        } else {
            applyPaint(SketchPaint::solid(color), description);
        }
    }

    void StylePanel::applyGradient(const SketchGradient& gradient, SketchPaint::Kind kind, const char* description) {
        applyPaint(kind == SketchPaint::Kind::Radial ? SketchPaint::radial(gradient) : SketchPaint::linear(gradient), description);
    }

    // Stop offset (%) and angle (degrees) typed into their fields.
    void StylePanel::applyGradientFields() {
        if (updating_) {
            return;
        }
        SketchPaint paint = targetPaint();
        if (paint.isGradient() && !paint.gradient().stops.empty()) {
            SketchGradient gradient = paint.gradient();
            std::size_t index = std::min(selectedStop_, gradient.stops.size() - 1);
            double number = 0.0;
            if (auto* field = find<TextField>("stopOffsetField")) {
                if (leadingNumber(field->text(), number)) {
                    gradient.stops[index].offset = std::min(1.0, std::max(0.0, number / 100.0));
                    // Keep the edited stop selected after the sort.
                    GradientStop edited = gradient.stops[index];
                    gradient.sortStops();
                    for (std::size_t i = 0; i < gradient.stops.size(); ++i) {
                        if (gradient.stops[i] == edited) {
                            selectedStop_ = i;
                        }
                    }
                }
            }
            if (auto* field = find<TextField>("gradientAngleField")) {
                if (leadingNumber(field->text(), number)) {
                    // One object: turn its explicit axis about its midpoint; several (different boxes): back to
                    // the angle-derived axis.
                    if (gradient.hasAxis && document_.selection().size() == 1) {
                        const SketchObject* object = document_.findById(document_.selection().front());
                        gradient.setAngle(object != nullptr ? object->geometryBounds() : BLBox(0, 0, 1, 1), number);
                    } else {
                        gradient.hasAxis = false;
                        gradient.angleDegrees = number;
                    }
                }
            }
            if (gradient != paint.gradient()) {
                applyGradient(gradient, paint.kind(), "Edit Gradient");
            }
        }
        refresh();
    }

    void StylePanel::applyHex() {
        if (updating_) {
            return;
        }
        auto* hex = find<TextField>("hexField");
        auto* a = find<Slider>("alphaSlider");
        if (hex == nullptr) {
            return;
        }
        try {
            SketchColor parsed = SketchColor::fromHex(narrow(hex->text()));
            // A 6-digit hex has no alpha of its own - keep the current one.
            if (narrow(hex->text()).size() <= 7 && a != nullptr) {
                parsed.setAlpha(a->value() / 100.0f);
            }
            // Hex is RGB; the color keeps the space it is being edited in (hex of a CMYK color converts back).
            SketchColor current = displayedColor();
            bool same = parsed.toHex() == current.toHex() && parsed.alpha() == current.alpha();
            if (!same || targetPaint().isNone()) {
                applyColor(parsed.convertedTo(current.space()), target_ == Target::Fill ? "Change Fill" : "Change Stroke");
            }
        } catch (const std::runtime_error&) {
        }
        refresh();
    }

    void StylePanel::applyStrokeWeight() {
        if (updating_) {
            return;
        }
        auto* field = find<TextField>("strokeWeightField");
        double width = 0.0;
        if (field != nullptr && leadingNumber(field->text(), width) && width >= 0.0) {
            edit("Stroke Weight",
                [this, width]() { document_.setSelectionStrokeWidth(width); },
                [this, width]() { document_.setDefaultStrokeWidth(width); });
        }
        refresh();
    }

    void StylePanel::applyStrokeOptions(const std::function<void(StrokeOptions&)>& change, const char* description) {
        if (!document_.selection().empty()) {
            std::vector<StyleState> before = document_.snapshotStyle();
            document_.changeSelectionStrokeOptions(change);
            document_.commitStyle(before, description);
        } else {
            StrokeOptions options = document_.defaultStrokeOptions();
            change(options);
            document_.setDefaultStrokeOptions(options);
        }
    }

    void StylePanel::applyMiterLimit() {
        if (updating_) {
            return;
        }
        double limit = 0.0;
        auto* field = find<TextField>("miterLimitField");
        if (field != nullptr && leadingNumber(field->text(), limit) && limit >= 1.0) {
            applyStrokeOptions([limit](StrokeOptions& o) { o.miterLimit = limit; }, "Miter Limit");
        }
        refresh();
    }

    void StylePanel::applyOpacity() {
        auto* slider = find<Slider>("opacitySlider");
        if (slider == nullptr) {
            return;
        }
        double opacity = std::min(1.0, std::max(0.0, double(slider->value()) / 100.0));
        edit("Opacity",
            [this, opacity]() { document_.setSelectionOpacity(opacity); },
            []() {});
    }

    void StylePanel::syncSlider(const char* sliderName, float value) {
        if (auto* slider = find<Slider>(sliderName)) {
            slider->setValue(value);
        }
        if (auto* field = find<TextField>((std::string(sliderName) + "Value").c_str())) {
            field->setText(widen(std::to_string(int(value + 0.5f))));
        }
    }

    void StylePanel::syncWells() {
        auto paintWell = [this](const char* name, const SketchPaint& paint, bool active) {
            if (auto* well = find<SubView>(name)) {
                gfx::Fill& fill = well->style().backgroundFill();
                fill.setKind(gfx::PaintKind::Color);
                fill.setColor(paint.isNone() ? Color(1.0f, 1.0f, 1.0f, 1.0f) : toColor(paint.color()));
                well->style().setBorderWidth(active ? 3.0f : 1.0f);
                well->style().markDirty();
            }
        };
        const SketchObject* first = document_.selection().empty() ? nullptr : document_.findById(document_.selection().front());
        SketchPaint fill = first != nullptr ? first->fill() : document_.defaultFill();
        SketchPaint stroke = first != nullptr ? first->stroke() : document_.defaultStroke();
        paintWell("fillWell", fill, target_ == Target::Fill);
        paintWell("strokeWell", stroke, target_ == Target::Stroke);
        if (auto* info = find<Label>("colorPanelHeaderInfo")) {
            info->setText(target_ == Target::Fill ? "Fill \xE2\x97\x8F   Stroke \xE2\x97\x8B" : "Fill \xE2\x97\x8B   Stroke \xE2\x97\x8F");
        }
    }

    void StylePanel::refresh() {
        if (updating_) {
            return;
        }
        updating_ = true;

        SketchPaint paint = targetPaint();
        if (paint.isGradient() && !paint.gradient().stops.empty()) {
            selectedStop_ = std::min(selectedStop_, paint.gradient().stops.size() - 1);
        } else {
            selectedStop_ = 0;
        }
        // Sliders and hex show the solid color, or the selected stop of a gradient.
        SketchColor shown = displayedColor();
        const ModeLayout& layout = kLayouts[modeIndex(shown.space())];
        float values[4];
        sliderValues(shown, values);
        for (std::size_t i = 0; i < 4; ++i) {
            std::string sliderName = std::string("comp") + char('0' + int(i)) + "Slider";
            bool used = i < layout.count;
            if (auto* row = find<View>((sliderName + "Row").c_str())) {
                row->setVisible(used);
            }
            if (!used) {
                continue;
            }
            if (auto* caption = find<Label>((sliderName + "Caption").c_str())) {
                caption->setText(layout.captions[i]);
            }
            if (auto* slider = find<Slider>(sliderName.c_str())) {
                slider->setRange(0.0f, layout.maxima[i]);
            }
            syncSlider(sliderName.c_str(), values[i]);
        }
        syncSlider("alphaSlider", paint.isNone() ? 100.0f : shown.alpha() * 100.0f);
        if (auto* hex = find<TextField>("hexField")) {
            hex->setText(widen(paint.isNone() ? std::string("none") : shown.toHex()));
        }

        // The gradient editor row: the bar always previews (the gradient, or the color fading to white).
        if (auto* bar = find<GradientBar>("gradientBar")) {
            SketchGradient preview = paint.isGradient() ? paint.gradient() : SketchGradient::fromColor(shown);
            std::vector<GradientBar::Stop> stops;
            for (const GradientStop& stop : preview.stops) {
                stops.push_back({stop.offset, toColor(stop.color)});
            }
            bar->setStops(stops);
            bar->setSelectedStop(paint.isGradient() ? selectedStop_ : stops.size());
        }
        if (auto* field = find<TextField>("stopOffsetField")) {
            char buffer[32];
            double offset = paint.isGradient() && !paint.gradient().stops.empty() ? paint.gradient().stops[selectedStop_].offset * 100.0 : 0.0;
            std::snprintf(buffer, sizeof(buffer), "%g%%", std::round(offset * 10.0) / 10.0);
            field->setText(widen(paint.isGradient() ? buffer : ""));
        }
        if (auto* field = find<TextField>("gradientAngleField")) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%g", std::round((paint.isGradient() ? paint.gradient().angleDegrees : 0.0) * 10.0) / 10.0);
            field->setText(paint.isGradient() ? widen(buffer) + L"\u00b0" : std::wstring());
        }
        if (auto* label = find<Label>("gradientTypeLabel")) {
            label->setText(paint.kind() == SketchPaint::Kind::Radial ? "Radial" : (paint.kind() == SketchPaint::Kind::Linear ? "Linear" : "-"));
        }
        if (auto* spread = find<DropDownList>("spreadCombo")) {
            std::size_t index = !paint.isGradient() ? 0
                : (paint.gradient().spread == GradientSpread::Repeat ? 1 : (paint.gradient().spread == GradientSpread::Reflect ? 2 : 0));
            if (spread->selectedIndex() != std::optional<std::size_t>(index)) {
                spread->setSelectedIndex(index);
            }
        }

        if (auto* apply = find<DropDownList>("gradientApplyCombo")) {
            // Only a stroke can run its colors along its length.
            apply->setEnabled(paint.isGradient() && target_ == Target::Stroke);
            std::size_t index = paint.isGradient() && paint.gradient().alongStroke && target_ == Target::Stroke ? 1 : 0;
            if (apply->selectedIndex() != std::optional<std::size_t>(index)) {
                apply->setSelectedIndex(index);
            }
        }

        auto setChecked = [this](const char* name, bool checked) {
            if (auto* button = find<ToolbarButton>(name)) {
                if (button->isChecked() != checked) {
                    button->setChecked(checked);
                }
            }
        };
        setChecked("fillNoneButton", paint.isNone());
        setChecked("fillSolidButton", paint.kind() == SketchPaint::Kind::Solid);
        setChecked("fillLinearButton", paint.kind() == SketchPaint::Kind::Linear);
        setChecked("fillRadialButton", paint.kind() == SketchPaint::Kind::Radial);

        if (auto* mode = find<SegmentedControl>("colorModeControl")) {
            std::size_t index = modeIndex(shown.space());
            if (mode->selectedIndex() != std::optional<std::size_t>(index)) {
                mode->setSelectedIndex(index);
            }
        }

        const SketchObject* first = document_.selection().empty() ? nullptr : document_.findById(document_.selection().front());
        double weight = first != nullptr ? first->strokeWidth() : document_.defaultStrokeWidth();
        if (auto* field = find<TextField>("strokeWeightField")) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%g pt", weight);
            field->setText(widen(buffer));
        }
        const StrokeOptions& options = first != nullptr ? first->strokeOptions() : document_.defaultStrokeOptions();
        for (auto [name, on] : std::initializer_list<std::pair<const char*, bool>>{
                {"capButtButton", options.cap == StrokeCap::Butt}, {"capRoundButton", options.cap == StrokeCap::Round},
                {"capSquareButton", options.cap == StrokeCap::Square}, {"joinMiterButton", options.join == StrokeJoin::Miter},
                {"joinRoundButton", options.join == StrokeJoin::Round}, {"joinBevelButton", options.join == StrokeJoin::Bevel}}) {
            if (auto* button = find<ToolbarButton>(name)) {
                if (button->isChecked() != on) {
                    button->setChecked(on);
                }
            }
        }
        if (auto* field = find<TextField>("miterLimitField")) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%g", options.miterLimit);
            field->setText(widen(buffer));
        }
        syncSlider("opacitySlider", float((first != nullptr ? first->opacity() : 1.0) * 100.0));
        syncWells();

        updating_ = false;
    }

}
