#pragma once

#include <newui/subview.h>

#include <cstddef>
#include <string>
#include <vector>

namespace sketch {

    // The stop bar of the gradient editor (the "GradientEditor" region of the mockup's Color
    // panel): a horizontal gradient preview with one draggable marker per stop beneath it.
    //
    // S0 scaffold: stops are plain "offset color" strings (offset 0..1, then any
    // newui::Color::fromString() text) so a .newui file can set them; painting is real, the stop
    // editing interaction (click to add, drag to move, Delete to remove) arrives with S3.
    // @reflect category=basic
    class GradientBar : public newui::SubView {
    public:
        // Declared (not implicit) so reflectgen registers it - a .newui load builds one through it.
        GradientBar();
        virtual ~GradientBar() {}

        // Each entry is "<offset> <color>", e.g. "0.55 #e8452cff".
        // @reflect collection add=addStop remove=removeStop
        const std::vector<std::string>& stops() const { return stops_; }
        void addStop(const std::string& stop);
        void removeStop(const std::string& stop);

        // Index into stops() of the stop being edited; stops().size() (the default) means none.
        std::size_t selectedStop() const { return selectedStop_; }
        void setSelectedStop(std::size_t index);

        void paint(BLContext& ctx) override;

    private:
        static constexpr float kMarkerSize = 10.0f;

        std::vector<std::string> stops_;
        std::size_t selectedStop_ = 0;
    };

}
