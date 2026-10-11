#include "gradientbar.h"

#include <newui/color.h>
#include <newui/uicolormanager.h>

#include <algorithm>
#include <sstream>

namespace sketch {

    using namespace newui;

    namespace {
        struct ParsedStop {
            double offset = 0.0;
            Color color;
        };

        // "<offset> <color>" - an entry that doesn't parse is skipped rather than failing the
        // whole bar (a hand-edited .newui file may well contain a typo).
        bool parseStop(const std::string& text, ParsedStop& out) {
            std::istringstream in(text);
            double offset = 0.0;
            std::string colorText;
            if (!(in >> offset >> colorText)) {
                return false;
            }
            Color color;
            if (!Color::fromString(colorText, color)) {
                return false;
            }
            out.offset = offset < 0.0 ? 0.0 : (offset > 1.0 ? 1.0 : offset);
            out.color = color;
            return true;
        }
    }

    GradientBar::GradientBar() {
        setName("GradientBar");
        setVisible(true);
        setDesiredSize(Size(0.0f, 28.0f));
        selectedStop_ = 0;
    }

    void GradientBar::addStop(const std::string& stop) {
        stops_.push_back(stop);
        style().markDirty();
    }

    void GradientBar::removeStop(const std::string& stop) {
        auto it = std::find(stops_.begin(), stops_.end(), stop);
        if (it != stops_.end()) {
            stops_.erase(it);
            style().markDirty();
        }
    }

    void GradientBar::setSelectedStop(std::size_t index) {
        selectedStop_ = index;
        style().markDirty();
    }

    void GradientBar::paint(BLContext& ctx) {
        Rect client = getClientBounds();
        if (client.width() <= 0.0f || client.height() <= 0.0f) {
            return;
        }

        std::vector<ParsedStop> stops;
        for (const std::string& text : stops_) {
            ParsedStop stop;
            if (parseStop(text, stop)) {
                stops.push_back(stop);
            }
        }

        // The bar leaves room below for the markers; the margin keeps the first/last marker
        // inside the view.
        double margin = kMarkerSize * 0.5;
        double barLeft = client.left() + margin;
        double barWidth = client.width() - 2.0 * margin;
        double barTop = client.top() + 2.0;
        double barHeight = client.height() - kMarkerSize - 4.0;
        if (barWidth <= 0.0 || barHeight <= 0.0) {
            return;
        }

        ctx.save();
        if (stops.size() >= 2) {
            BLGradient gradient(BLLinearGradientValues(barLeft, 0.0, barLeft + barWidth, 0.0));
            for (const ParsedStop& stop : stops) {
                gradient.add_stop(stop.offset, stop.color.toBLRgba32());
            }
            ctx.set_fill_style(gradient);
        } else if (stops.size() == 1) {
            ctx.set_fill_style(stops[0].color.toBLRgba32());
        } else {
            ctx.set_fill_style(UIColorManager::colorFor(UIColorRole::ControlBackground).toBLRgba32());
        }
        ctx.fill_rect(BLRect(barLeft, barTop, barWidth, barHeight));
        ctx.set_stroke_style(UIColorManager::colorFor(UIColorRole::ControlBorder).toBLRgba32());
        ctx.set_stroke_width(1.0);
        ctx.stroke_rect(BLRect(barLeft - 0.5, barTop - 0.5, barWidth + 1.0, barHeight + 1.0));

        // One marker per stop, in the stop's own color; the selected one gets an accent outline.
        double markerTop = barTop + barHeight + 2.0;
        for (std::size_t i = 0; i < stops.size(); ++i) {
            double x = barLeft + stops[i].offset * barWidth - margin;
            ctx.set_fill_style(stops[i].color.toBLRgba32());
            ctx.fill_rect(BLRect(x, markerTop, kMarkerSize, kMarkerSize));
            bool selected = (i == selectedStop_);
            ctx.set_stroke_style(selected
                ? UIColorManager::colorFor(UIColorRole::HighlightBackground).toBLRgba32()
                : BLRgba32(0xFF333333u));
            ctx.set_stroke_width(selected ? 2.0 : 1.0);
            ctx.stroke_rect(BLRect(x, markerTop, kMarkerSize, kMarkerSize));
        }
        ctx.restore();
    }

}
