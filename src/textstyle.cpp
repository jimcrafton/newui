#include "newui/textstyle.h"

#include <algorithm>
#include <unordered_map>

namespace newui {

    namespace {
        float blendChannel(float backdrop, float source, CompositeMode mode) {
            switch (mode) {
                case CompositeMode::Multiply: return backdrop * source;
                case CompositeMode::Screen: return backdrop + source - backdrop * source;
                case CompositeMode::Darken: return std::min(backdrop, source);
                case CompositeMode::Lighten: return std::max(backdrop, source);
                default: return source;
            }
        }
    }

    Color composite(const Color& source, const Color& backdrop, CompositeMode mode) {
        const float as = source.a;
        const float ab = backdrop.a;
        if (mode == CompositeMode::Src) {
            return source;
        }
        if (mode == CompositeMode::Plus) {
            const float alpha = std::min(1.0f, as + ab);
            if (alpha <= 0.0f) {
                return Color(0.0f, 0.0f, 0.0f, 0.0f);
            }
            auto plus = [&](float s, float b) { return std::min(1.0f, as * s + ab * b) / alpha; };
            return Color(plus(source.r, backdrop.r), plus(source.g, backdrop.g), plus(source.b, backdrop.b), alpha);
        }
        // the W3C compositing formula: the source over the backdrop, the two colors mixed by the blend function where they overlap
        const float alpha = as + ab * (1.0f - as);
        if (alpha <= 0.0f) {
            return Color(0.0f, 0.0f, 0.0f, 0.0f);
        }
        auto channel = [&](float s, float b) {
            const float mixed = blendChannel(b, s, mode);
            return ((1.0f - as) * ab * b + (1.0f - ab) * as * s + as * ab * mixed) / alpha;
        };
        return Color(channel(source.r, backdrop.r), channel(source.g, backdrop.g), channel(source.b, backdrop.b), alpha);
    }

    TextStyleSheet::~TextStyleSheet() {
        for (TextStyle* style : styles_) {
            delete style;
        }
    }

    void TextStyleSheet::addStyle(TextStyle* style) {
        if (style != nullptr && std::find(styles_.begin(), styles_.end(), style) == styles_.end()) {
            styles_.push_back(style);
        }
    }

    void TextStyleSheet::removeStyle(TextStyle* style) {
        auto it = std::find(styles_.begin(), styles_.end(), style);
        if (it != styles_.end()) {
            styles_.erase(it);
        }
    }

    TextStyle* TextStyleSheet::style(const std::string& name) const {
        for (TextStyle* style : styles_) {
            if (style->name() == name) {
                return style;
            }
        }
        return nullptr;
    }

    namespace text {

        namespace {
            struct OverlayRange {
                std::size_t start;
                std::size_t length;
                const TextStyle* style;
            };

            // Colors for the pieces of [overlay.start, +length): each piece has the color the last-drawn run covering it gives (the
            // base color where none does), with the overlay composited over it. `order` is run indexes sorted by start and
            // `maxEnd[k]` the furthest end among the first k + 1 of them, so only the runs near the overlay are looked at.
            void appendOverlay(std::vector<TextColorRun>& runs, std::size_t plainRuns, const std::vector<std::size_t>& order,
                               const std::vector<std::size_t>& maxEnd, const OverlayRange& overlay, const Color& baseColor) {
                const std::size_t start = overlay.start;
                const std::size_t end = overlay.start + overlay.length;
                std::vector<std::size_t> covering;   // plain runs overlapping the overlay
                const std::size_t high = static_cast<std::size_t>(std::partition_point(order.begin(), order.end(), [&](std::size_t i) {
                    return runs[i].start < end;
                }) - order.begin());
                for (std::size_t k = high; k-- > 0 && maxEnd[k] > start;) {
                    if (order[k] < plainRuns && runs[order[k]].start + runs[order[k]].length > start) {
                        covering.push_back(order[k]);
                    }
                }
                std::vector<std::size_t> edges = { start, end };
                for (std::size_t index : covering) {
                    edges.push_back(std::clamp(runs[index].start, start, end));
                    edges.push_back(std::clamp(runs[index].start + runs[index].length, start, end));
                }
                std::sort(edges.begin(), edges.end());
                edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
                for (std::size_t e = 0; e + 1 < edges.size(); ++e) {
                    const std::size_t from = edges[e];
                    const std::size_t to = edges[e + 1];
                    Color backdrop = baseColor;
                    bool found = false;
                    std::size_t last = 0;
                    for (std::size_t index : covering) {
                        const TextColorRun& run = runs[index];
                        if (run.start <= from && run.start + run.length >= to && (!found || index > last)) {
                            last = index;
                            found = true;
                        }
                    }
                    if (found) {
                        backdrop = runs[last].color;
                    }
                    if (backdrop.isNull()) {
                        continue;
                    }
                    runs.push_back(TextColorRun{ from, to - from,
                                                 composite(overlay.style->overlayColor(), backdrop, overlay.style->overlayComposite()) });
                }
            }
        }

        ExpandedTextStyles expandTextStyles(const TextStyleSheet& sheet, const std::vector<TextStyleRange>& ranges, const Color& baseColor) {
            ExpandedTextStyles out;
            out.colorRuns.reserve(ranges.size());
            std::vector<OverlayRange> overlays;
            // A document has thousands of ranges but a handful of style names - look each up once.
            std::unordered_map<std::string, const TextStyle*> resolved;
            for (const TextStyleRange& range : ranges) {
                auto found = resolved.find(range.style);
                if (found == resolved.end()) {
                    found = resolved.emplace(range.style, sheet.style(range.style)).first;
                }
                const TextStyle* style = found->second;
                if (style == nullptr || range.length == 0) {
                    continue;
                }
                if (!style->color().isNull()) {
                    out.colorRuns.push_back(TextColorRun{ range.start, range.length, style->color() });
                }
                if (!style->overlayColor().isNull()) {
                    overlays.push_back(OverlayRange{ range.start, range.length, style });
                }
                if (style->isBold() || style->isItalic() || style->isUnderline() || style->isStrikethrough()
                    || !style->fontName().empty() || style->fontSize() > 0.0f) {
                    TextFontRun run;
                    run.start = range.start;
                    run.length = range.length;
                    run.bold = style->isBold();
                    run.italic = style->isItalic();
                    run.underline = style->isUnderline();
                    run.strikethrough = style->isStrikethrough();
                    run.fontName = style->fontName();
                    run.fontSize = style->fontSize();
                    out.fontRuns.push_back(run);
                }
                if (!style->backgroundColor().isNull()) {
                    TextDecoration background;
                    background.start = range.start;
                    background.length = range.length;
                    background.kind = TextDecorationKind::Background;
                    background.color = style->backgroundColor();
                    out.decorations.push_back(background);
                }
                if (style->decoration() != TextDecorationKind::None) {
                    TextDecoration decoration;
                    decoration.start = range.start;
                    decoration.length = range.length;
                    decoration.kind = style->decoration();
                    decoration.color = style->decorationColor();
                    out.decorations.push_back(decoration);
                }
                if (!range.annotation.empty() || !style->lineBackgroundColor().isNull()) {
                    LineAnnotation annotation;
                    annotation.offset = range.start;
                    annotation.text = range.annotation;
                    annotation.color = !style->decorationColor().isNull() ? style->decorationColor() : style->color();
                    annotation.lineBackground = style->lineBackgroundColor();
                    out.annotations.push_back(std::move(annotation));
                }
            }
            // Overlays last, so they are drawn over everything else.
            if (!overlays.empty()) {
                const std::size_t plainRuns = out.colorRuns.size();
                std::vector<std::size_t> order(plainRuns);
                for (std::size_t i = 0; i < plainRuns; ++i) {
                    order[i] = i;
                }
                std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                    return out.colorRuns[a].start < out.colorRuns[b].start;
                });
                std::vector<std::size_t> maxEnd(plainRuns);
                std::size_t reach = 0;
                for (std::size_t k = 0; k < plainRuns; ++k) {
                    reach = std::max(reach, out.colorRuns[order[k]].start + out.colorRuns[order[k]].length);
                    maxEnd[k] = reach;
                }
                for (const OverlayRange& overlay : overlays) {
                    appendOverlay(out.colorRuns, plainRuns, order, maxEnd, overlay, baseColor);
                }
            }
            return out;
        }
    }
}
