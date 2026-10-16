#pragma once

#include <blend2d/blend2d.h>

#include <string>

namespace sketch {

    // The glyph outlines of text, as a path: what a Text object draws, strokes and hit-tests like any other shape.

    // The font a new text uses when none is named (the system's message font).
    std::string defaultTextFontName();

    // Outlines of `text` (UTF-8; '\n' starts a new line) in the named font ("" = the default) at `fontSize` points. The first
    // line's baseline sits on y = 0 and the text starts at x = 0; further lines are placed one line height lower (y grows downward).
    // Spaces make no outline, so text of only blanks (or no text) returns an empty path.
    BLPath textOutline(const std::string& text, const std::string& fontName, double fontSize);

    // The distance between two lines of the named font at `fontSize`.
    double textLineHeight(const std::string& fontName, double fontSize);

    // The width of one line of text (no '\n' in it) - where the editing caret sits at the end of the line.
    double textAdvance(const std::string& line, const std::string& fontName, double fontSize);

}
