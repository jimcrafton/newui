"""
themesgen (v1)

Snapshots the real Windows visual style active on this machine right now
- colors and control part sizes/content-rect insets, across every
interaction state (Normal/Hot/Pressed/Disabled/Checked/...) uxtheme
actually defines for each part, not just the neutral one - into two
newui `.theme` files (`light.theme`/`dark.theme`, JSON5-compatible; this
writes plain JSON via the stdlib `json` module, which is itself a valid
JSON5 subset, so no external dependency is needed at all - unlike
tools/reflectgen, no venv/requirements.txt here).

At runtime, `newui::ThemeData` (include/newui/themedata.h, src/themedata.cpp)
loads one of these and `UIColorManager::colorFor()`/`ThemedViewStyle::
partSize()`/`computeClientBounds()` (uicolormanager.cpp/viewstyle.cpp)
consult it *before* falling back to their own existing live uxtheme
queries - see those files' own comments for the consumer side.

v1 status: standalone script, run manually, output checked in (or
regenerated on demand) - not wired into the CMake build, matching
tools/reflectgen's own "not wired in yet" precedent.

REGISTRY (below) is a hand-maintained mirror of every `ThemedXxxStyle`
subclass's `partId()`/`stateId()` overrides in
`include/newui/viewstyle.h` - the exact same (themeClassName, partId,
stateId) triples that codebase's own live uxtheme calls already use, so
this tool covers exactly what the runtime can ever ask for and nothing
speculative. `src/themedata.cpp`'s own symbol tables are the C++-side
mirror of this same data (built from the real `<vssym32.h>` enum
constants directly, not copied literals, so they can't silently drift -
this file's own numbers below were read directly from
`Windows Kits/10/Include/<ver>/um/vssym32.h`/`vsstyle.h` on the machine
this was written on, not recalled/guessed - state ordinals are **not**
uniformly 1/2/3/4=Normal/Hot/Pressed/Disabled, e.g. SBP_ARROWBTN's
direction shifts the whole state block: ABS_UPNORMAL=1 but
ABS_DOWNNORMAL=5). If a future `ThemedXxxStyle` is added to
viewstyle.h, add its (part, state) rows here *and* to themedata.cpp's
symbol tables - there's no automated way to keep the two in sync (same
category of manual-sync obligation reflectgen's own README documents for
base-class registration ordering).

Usage:
    python themesgen.py -o Resources/Themes
    (writes Resources/Themes/light.theme and Resources/Themes/dark.theme)
"""

import argparse
import colorsys
import ctypes
import json
import os
from ctypes import wintypes

# --- Win32/uxtheme bindings (ctypes, stdlib-only) -----------------------

uxtheme = ctypes.WinDLL("uxtheme")

HTHEME = wintypes.HANDLE

uxtheme.OpenThemeData.restype = HTHEME
uxtheme.OpenThemeData.argtypes = [wintypes.HWND, wintypes.LPCWSTR]

uxtheme.CloseThemeData.restype = ctypes.c_long
uxtheme.CloseThemeData.argtypes = [HTHEME]

uxtheme.GetThemeColor.restype = ctypes.c_long
uxtheme.GetThemeColor.argtypes = [HTHEME, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.POINTER(wintypes.COLORREF)]

uxtheme.GetThemeSysColor.restype = wintypes.COLORREF
uxtheme.GetThemeSysColor.argtypes = [HTHEME, ctypes.c_int]


class SIZE(ctypes.Structure):
    _fields_ = [("cx", ctypes.c_long), ("cy", ctypes.c_long)]


uxtheme.GetThemePartSize.restype = ctypes.c_long
uxtheme.GetThemePartSize.argtypes = [
    HTHEME, wintypes.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(SIZE)
]

TS_TRUE = 0  # natural size the current visual style draws this part/state at


class RECT(ctypes.Structure):
    _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long), ("right", ctypes.c_long), ("bottom", ctypes.c_long)]


uxtheme.GetThemeBackgroundContentRect.restype = ctypes.c_long
uxtheme.GetThemeBackgroundContentRect.argtypes = [
    HTHEME, wintypes.HDC, ctypes.c_int, ctypes.c_int, ctypes.POINTER(RECT), ctypes.POINTER(RECT)
]

# Real winuser.h COLOR_* slots (queried via GetThemeSysColor, not
# GetThemeColor - a different, part-independent API - see
# queryThemeSysColor()'s own doc comment in uicolormanager.cpp for why
# WindowBackground/ControlBackground use this instead of a part-based
# fill-color query).
COLOR_WINDOW = 5
COLOR_HOTLIGHT = 26

# Real vssym32.h TMT_* color property IDs (confirmed against the real
# header, not guessed - see this file's own top comment).
TMT_BORDERCOLOR = 3801
TMT_FILLCOLOR = 3802
TMT_TEXTCOLOR = 3803
TMT_EDGEFILLCOLOR = 3808


def _colorref_to_hex(colorref):
    r = colorref & 0xFF
    g = (colorref >> 8) & 0xFF
    b = (colorref >> 16) & 0xFF
    return "#{:02x}{:02x}{:02x}ff".format(r, g, b)


def invert_lightness_hex(hex_color):
    """Mirrors UIColorManager::invertLightness() (uicolormanager.cpp) -
    a plain HSL-lightness invert, hue/alpha untouched. Kept in sync by
    hand with that C++ function - relocating the *same* approximation
    offline (dark.theme is derived from light.theme's own raw queries,
    not separately queried - uxtheme has no live "give me the dark
    variant" API this codebase's approach can reach, confirmed by the
    existing runtime code's own comment: toggling the system setting
    doesn't change what GetThemeColor returns)."""
    r = int(hex_color[1:3], 16) / 255.0
    g = int(hex_color[3:5], 16) / 255.0
    b = int(hex_color[5:7], 16) / 255.0
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    l = 1.0 - l
    r2, g2, b2 = colorsys.hls_to_rgb(h, l, s)
    return "#{:02x}{:02x}{:02x}ff".format(round(r2 * 255), round(g2 * 255), round(b2 * 255))


def query_theme_color(theme_class, part_id, state_id, prop_id):
    htheme = uxtheme.OpenThemeData(None, theme_class)
    if not htheme:
        return None
    try:
        color = wintypes.COLORREF()
        hr = uxtheme.GetThemeColor(htheme, part_id, state_id, prop_id, ctypes.byref(color))
        return _colorref_to_hex(color.value) if hr == 0 else None
    finally:
        uxtheme.CloseThemeData(htheme)


def query_theme_sys_color(theme_class, sys_color_id):
    htheme = uxtheme.OpenThemeData(None, theme_class)
    try:
        return _colorref_to_hex(uxtheme.GetThemeSysColor(htheme, sys_color_id))
    finally:
        if htheme:
            uxtheme.CloseThemeData(htheme)


def query_part_size(theme_class, part_id, state_id):
    htheme = uxtheme.OpenThemeData(None, theme_class)
    if not htheme:
        return None
    try:
        size = SIZE()
        hr = uxtheme.GetThemePartSize(htheme, None, part_id, state_id, None, TS_TRUE, ctypes.byref(size))
        if hr != 0:
            return None
        # GetThemePartSize can report success (hr==0) with a degenerate
        # answer for a part/state a visual style genuinely doesn't
        # populate real TS_TRUE data for - confirmed live on this
        # machine's current style: SCROLLBAR/SBP_ARROWBTN comes back
        # (1,1), not a real ~16-20px arrow button size, regardless of
        # whether OpenThemeData is given a real HWND or NULL (checked
        # both - same degenerate answer either way, so this isn't an
        # hwnd-vs-NULL issue). Caching a value that small verbatim would
        # be *worse* than not caching it at all - the runtime's own
        # existing fallback constants (e.g. ThemedScrollbarArrowStyle's
        # kArrowFallbackSize, controls.cpp) are real, sane defaults;
        # overriding them with "1" would make a previously-usable
        # fallback newly wrong. No real Win32 control part is ever this
        # small, so treat anything under this floor as "no real data"
        # and let the caller's own live-query/fallback chain handle it
        # instead, exactly as it already does when this file doesn't
        # exist at all.
        MIN_SANE_PART_DIMENSION = 3
        if size.cx < MIN_SANE_PART_DIMENSION or size.cy < MIN_SANE_PART_DIMENSION:
            return None
        return {"width": size.cx, "height": size.cy}
    finally:
        uxtheme.CloseThemeData(htheme)


def query_content_rect(theme_class, part_id, state_id, bounds_w=200, bounds_h=200):
    htheme = uxtheme.OpenThemeData(None, theme_class)
    if not htheme:
        return None
    try:
        bounds = RECT(0, 0, bounds_w, bounds_h)
        content = RECT()
        hr = uxtheme.GetThemeBackgroundContentRect(
            htheme, None, part_id, state_id, ctypes.byref(bounds), ctypes.byref(content)
        )
        if hr != 0:
            return None
        # Insets from each edge (matches Rect::deflate(left,top,right,bottom)'s
        # own shape, geometry.h) - right/bottom are how far *in* from the
        # far edge, not absolute coordinates.
        return {
            "left": content.left,
            "top": content.top,
            "right": bounds_w - content.right,
            "bottom": bounds_h - content.bottom,
        }
    finally:
        uxtheme.CloseThemeData(htheme)


# --- Registry: mirrors include/newui/viewstyle.h - see this file's own
# top comment for the "keep both sides in sync by hand" contract.
#
# ROLES: (roleName, query) pairs for UIColorManager's own UIColorRole
# values - matches colorFor()'s exact queries (uicolormanager.cpp),
# except HighlightBackground/HighlightText (see this file's own top
# comment for why those are excluded).
ROLES = [
    ("WindowBackground", lambda: query_theme_sys_color("WINDOW", COLOR_WINDOW)),
    ("ControlBackground", lambda: query_theme_sys_color("WINDOW", COLOR_WINDOW)),
    ("WindowText", lambda: query_theme_color("BUTTON", 1, 1, TMT_TEXTCOLOR)),  # BP_PUSHBUTTON/PBS_NORMAL
    ("ControlText", lambda: query_theme_color("BUTTON", 1, 1, TMT_TEXTCOLOR)),
    ("ControlBorder", lambda: query_theme_color("BUTTON", 1, 1, TMT_EDGEFILLCOLOR)),
    ("DisabledText", lambda: query_theme_color("BUTTON", 1, 4, TMT_TEXTCOLOR)),  # PBS_DISABLED
    ("LinkText", lambda: query_theme_sys_color("WINDOW", COLOR_HOTLIGHT)),
    # LinkHoverText is COLOR_HOTLIGHT lightened at runtime (colorFor()'s
    # own logic, uicolormanager.cpp) - not a separate query, so not
    # captured as its own role here; UIColorManager derives it from
    # LinkText the same way regardless of whether ThemeData is loaded.
]

# PARTS: one entry per (themeClassName, partName, partId) - "states" is a
# list of (stateName, stateId) pairs. Grouped/commented by the
# ThemedXxxStyle subclass(es) each row comes from.
PARTS = [
    # ThemedButtonStyle / ThemedCheckBoxStyle / ThemedRadioButtonStyle / ThemedGroupBoxStyle
    ("BUTTON", "BP_PUSHBUTTON", 1, [
        ("PBS_NORMAL", 1), ("PBS_HOT", 2), ("PBS_PRESSED", 3), ("PBS_DISABLED", 4),
    ]),
    ("BUTTON", "BP_CHECKBOX", 3, [
        ("CBS_UNCHECKEDNORMAL", 1), ("CBS_UNCHECKEDHOT", 2), ("CBS_UNCHECKEDPRESSED", 3), ("CBS_UNCHECKEDDISABLED", 4),
        ("CBS_CHECKEDNORMAL", 5), ("CBS_CHECKEDHOT", 6), ("CBS_CHECKEDPRESSED", 7), ("CBS_CHECKEDDISABLED", 8),
    ]),
    ("BUTTON", "BP_RADIOBUTTON", 2, [
        ("RBS_UNCHECKEDNORMAL", 1), ("RBS_UNCHECKEDHOT", 2), ("RBS_UNCHECKEDPRESSED", 3), ("RBS_UNCHECKEDDISABLED", 4),
        ("RBS_CHECKEDNORMAL", 5), ("RBS_CHECKEDHOT", 6), ("RBS_CHECKEDPRESSED", 7), ("RBS_CHECKEDDISABLED", 8),
    ]),
    ("BUTTON", "BP_GROUPBOX", 4, [("GBS_NORMAL", 1), ("GBS_DISABLED", 2)]),

    # ThemedToolbarButtonStyle / DropDownButtonStyle / DropDownButtonGlyphStyle / SplitButtonStyle / SplitButtonDropDownStyle / SeparatorStyle
    ("TOOLBAR", "TP_BUTTON", 1, [
        ("TS_NORMAL", 1), ("TS_HOT", 2), ("TS_PRESSED", 3), ("TS_DISABLED", 4), ("TS_CHECKED", 5), ("TS_HOTCHECKED", 6),
    ]),
    ("TOOLBAR", "TP_DROPDOWNBUTTON", 2, [
        ("TS_NORMAL", 1), ("TS_HOT", 2), ("TS_PRESSED", 3), ("TS_DISABLED", 4), ("TS_CHECKED", 5), ("TS_HOTCHECKED", 6),
    ]),
    ("TOOLBAR", "TP_DROPDOWNBUTTONGLYPH", 7, [
        ("TS_NORMAL", 1), ("TS_HOT", 2), ("TS_PRESSED", 3), ("TS_DISABLED", 4),
    ]),
    ("TOOLBAR", "TP_SPLITBUTTON", 3, [
        ("TS_NORMAL", 1), ("TS_HOT", 2), ("TS_PRESSED", 3), ("TS_DISABLED", 4), ("TS_CHECKED", 5), ("TS_HOTCHECKED", 6),
    ]),
    ("TOOLBAR", "TP_SPLITBUTTONDROPDOWN", 4, [
        ("TS_NORMAL", 1), ("TS_HOT", 2), ("TS_PRESSED", 3), ("TS_DISABLED", 4), ("TS_CHECKED", 5), ("TS_HOTCHECKED", 6),
    ]),
    ("TOOLBAR", "TP_SEPARATOR", 5, [("TS_NORMAL", 1)]),
    ("TOOLBAR", "TP_SEPARATORVERT", 6, [("TS_NORMAL", 1)]),

    # ThemedStatusPaneStyle - no real state enum for this part
    ("STATUS", "SP_PANE", 1, [("DEFAULT", 0)]),

    # ThemedRebarBandStyle / ThemedRebarChevronStyle
    ("REBAR", "RP_BAND", 3, [("DEFAULT", 0)]),
    ("REBAR", "RP_CHEVRON", 4, [("CHEVS_NORMAL", 1), ("CHEVS_HOT", 2), ("CHEVS_PRESSED", 3)]),
    ("REBAR", "RP_CHEVRONVERT", 5, [("CHEVSV_NORMAL", 1), ("CHEVSV_HOT", 2), ("CHEVSV_PRESSED", 3)]),

    # ThemedTooltipStyle
    ("TOOLTIP", "TTP_STANDARD", 1, [("TTSS_NORMAL", 1), ("TTSS_LINK", 2)]),

    # ThemedSpinButtonStyle
    ("SPIN", "SPNP_UP", 1, [("UPS_NORMAL", 1), ("UPS_HOT", 2), ("UPS_PRESSED", 3), ("UPS_DISABLED", 4)]),
    ("SPIN", "SPNP_DOWN", 2, [("DNS_NORMAL", 1), ("DNS_HOT", 2), ("DNS_PRESSED", 3), ("DNS_DISABLED", 4)]),

    # ThemedEditStyle
    ("EDIT", "EP_EDITTEXT", 1, [
        ("ETS_NORMAL", 1), ("ETS_HOT", 2), ("ETS_DISABLED", 4), ("ETS_FOCUSED", 5), ("ETS_READONLY", 6),
    ]),

    # ThemedListItemStyle
    ("LISTVIEW", "LVP_LISTITEM", 1, [
        ("LISS_NORMAL", 1), ("LISS_HOT", 2), ("LISS_SELECTED", 3), ("LISS_DISABLED", 4), ("LISS_HOTSELECTED", 6),
    ]),

    # ThemedHeaderItemStyle / ThemedHeaderSortArrowStyle
    ("HEADER", "HP_HEADERITEM", 1, [
        ("HIS_NORMAL", 1), ("HIS_HOT", 2), ("HIS_PRESSED", 3),
        ("HIS_SORTEDNORMAL", 4), ("HIS_SORTEDHOT", 5), ("HIS_SORTEDPRESSED", 6),
    ]),
    ("HEADER", "HP_HEADERSORTARROW", 4, [("HSAS_SORTEDUP", 1), ("HSAS_SORTEDDOWN", 2)]),

    # ThemedTreeItemStyle / ThemedTreeGlyphStyle
    ("TREEVIEW", "TVP_TREEITEM", 1, [
        ("TREIS_NORMAL", 1), ("TREIS_HOT", 2), ("TREIS_SELECTED", 3), ("TREIS_DISABLED", 4), ("TREIS_HOTSELECTED", 6),
    ]),
    ("TREEVIEW", "TVP_GLYPH", 2, [("GLPS_CLOSED", 1), ("GLPS_OPENED", 2)]),

    # ThemedTabItemStyle - 8 edge/position variants, all sharing the same
    # TIS_* state shape (each technically has its own identically-
    # numbered *STATES enum family in vsstyle.h - only the numbers matter
    # at the Win32 call boundary, see viewstyle.h's own comment)
    ("TAB", "TABP_TOPTABITEM", 5, [("TIS_NORMAL", 1), ("TIS_HOT", 2), ("TIS_SELECTED", 3), ("TIS_DISABLED", 4)]),
    ("TAB", "TABP_TOPTABITEMLEFTEDGE", 6, [("TIS_NORMAL", 1), ("TIS_HOT", 2), ("TIS_SELECTED", 3), ("TIS_DISABLED", 4)]),
    ("TAB", "TABP_TOPTABITEMRIGHTEDGE", 7, [("TIS_NORMAL", 1), ("TIS_HOT", 2), ("TIS_SELECTED", 3), ("TIS_DISABLED", 4)]),
    ("TAB", "TABP_TOPTABITEMBOTHEDGE", 8, [("TIS_NORMAL", 1), ("TIS_HOT", 2), ("TIS_SELECTED", 3), ("TIS_DISABLED", 4)]),
    ("TAB", "TABP_TABITEM", 1, [("TIS_NORMAL", 1), ("TIS_HOT", 2), ("TIS_SELECTED", 3), ("TIS_DISABLED", 4)]),
    ("TAB", "TABP_TABITEMLEFTEDGE", 2, [("TIS_NORMAL", 1), ("TIS_HOT", 2), ("TIS_SELECTED", 3), ("TIS_DISABLED", 4)]),
    ("TAB", "TABP_TABITEMRIGHTEDGE", 3, [("TIS_NORMAL", 1), ("TIS_HOT", 2), ("TIS_SELECTED", 3), ("TIS_DISABLED", 4)]),
    ("TAB", "TABP_TABITEMBOTHEDGE", 4, [("TIS_NORMAL", 1), ("TIS_HOT", 2), ("TIS_SELECTED", 3), ("TIS_DISABLED", 4)]),
    ("TAB", "TABP_PANE", 9, [("DEFAULT", 0)]),

    # ThemedTrackbarTrackStyle / ThumbStyle / TicksStyle
    ("TRACKBAR", "TKP_TRACK", 1, [("TRS_NORMAL", 1)]),
    ("TRACKBAR", "TKP_TRACKVERT", 2, [("TRVS_NORMAL", 1)]),
    # TUS_DISABLED=5, not 4 - TUS_FOCUSED=4 sits between Pressed and
    # Disabled and is unused by the runtime code, so it's skipped here too.
    ("TRACKBAR", "TKP_THUMB", 3, [("TUS_NORMAL", 1), ("TUS_HOT", 2), ("TUS_PRESSED", 3), ("TUS_DISABLED", 5)]),
    ("TRACKBAR", "TKP_THUMBVERT", 6, [("TUVS_NORMAL", 1), ("TUVS_HOT", 2), ("TUVS_PRESSED", 3), ("TUVS_DISABLED", 5)]),
    ("TRACKBAR", "TKP_TICS", 9, [("TSS_NORMAL", 1)]),
    ("TRACKBAR", "TKP_TICSVERT", 10, [("TSVS_NORMAL", 1)]),

    # ThemedProgressBarTrackStyle / FillStyle
    ("PROGRESS", "PP_BAR", 1, [("DEFAULT", 0)]),
    ("PROGRESS", "PP_BARVERT", 2, [("DEFAULT", 0)]),
    ("PROGRESS", "PP_FILL", 5, [("PBFS_NORMAL", 1), ("PBFS_ERROR", 2), ("PBFS_PAUSED", 3)]),
    ("PROGRESS", "PP_FILLVERT", 6, [("PBFVS_NORMAL", 1), ("PBFVS_ERROR", 2), ("PBFVS_PAUSED", 3)]),

    # ThemedScrollbarThumbStyle / ArrowStyle / TrackStyle
    ("SCROLLBAR", "SBP_THUMBBTNHORZ", 2, [
        ("SCRBS_NORMAL", 1), ("SCRBS_HOT", 2), ("SCRBS_PRESSED", 3), ("SCRBS_DISABLED", 4),
    ]),
    ("SCROLLBAR", "SBP_THUMBBTNVERT", 3, [
        ("SCRBS_NORMAL", 1), ("SCRBS_HOT", 2), ("SCRBS_PRESSED", 3), ("SCRBS_DISABLED", 4),
    ]),
    # Direction shifts the whole state block - NOT 1/2/3/4 per direction,
    # see this file's own top comment.
    ("SCROLLBAR", "SBP_ARROWBTN", 1, [
        ("ABS_UPNORMAL", 1), ("ABS_UPHOT", 2), ("ABS_UPPRESSED", 3), ("ABS_UPDISABLED", 4),
        ("ABS_DOWNNORMAL", 5), ("ABS_DOWNHOT", 6), ("ABS_DOWNPRESSED", 7), ("ABS_DOWNDISABLED", 8),
        ("ABS_LEFTNORMAL", 9), ("ABS_LEFTHOT", 10), ("ABS_LEFTPRESSED", 11), ("ABS_LEFTDISABLED", 12),
        ("ABS_RIGHTNORMAL", 13), ("ABS_RIGHTHOT", 14), ("ABS_RIGHTPRESSED", 15), ("ABS_RIGHTDISABLED", 16),
    ]),
    ("SCROLLBAR", "SBP_LOWERTRACKHORZ", 4, [
        ("SCRBS_NORMAL", 1), ("SCRBS_HOT", 2), ("SCRBS_PRESSED", 3), ("SCRBS_DISABLED", 4),
    ]),
    ("SCROLLBAR", "SBP_UPPERTRACKHORZ", 5, [
        ("SCRBS_NORMAL", 1), ("SCRBS_HOT", 2), ("SCRBS_PRESSED", 3), ("SCRBS_DISABLED", 4),
    ]),
    ("SCROLLBAR", "SBP_LOWERTRACKVERT", 6, [
        ("SCRBS_NORMAL", 1), ("SCRBS_HOT", 2), ("SCRBS_PRESSED", 3), ("SCRBS_DISABLED", 4),
    ]),
    ("SCROLLBAR", "SBP_UPPERTRACKVERT", 7, [
        ("SCRBS_NORMAL", 1), ("SCRBS_HOT", 2), ("SCRBS_PRESSED", 3), ("SCRBS_DISABLED", 4),
    ]),

    # ThemedMenuBarItemStyle / ThemedMenuBarBackgroundStyle
    ("MENU", "MENU_BARITEM", 8, [
        ("MBI_NORMAL", 1), ("MBI_HOT", 2), ("MBI_PUSHED", 3), ("MBI_DISABLED", 4),
    ]),
    ("MENU", "MENU_BARBACKGROUND", 7, [("MB_ACTIVE", 1)]),
]

# Color properties queried per (part, state) - a curated set of broadly-
# populated properties, not every TMT_* constant uxtheme defines (most
# are unpopulated for most parts) - each is simply omitted from the
# output if GetThemeColor fails for it, same "try it, fall back if not"
# contract queryThemeColor() (uicolormanager.cpp) already has.
PART_COLOR_PROPS = {
    "fillColor": TMT_FILLCOLOR,
    "edgeFillColor": TMT_EDGEFILLCOLOR,
    "borderColor": TMT_BORDERCOLOR,
    "textColor": TMT_TEXTCOLOR,
}


def build_light_theme():
    roles = {}
    for role_name, query in ROLES:
        value = query()
        if value is not None:
            roles[role_name] = value

    parts = {}
    for theme_class, part_name, part_id, states in PARTS:
        class_obj = parts.setdefault(theme_class, {})
        part_obj = class_obj.setdefault(part_name, {})
        for state_name, state_id in states:
            entry = {}

            size = query_part_size(theme_class, part_id, state_id)
            if size is not None:
                entry["size"] = size

            content_rect = query_content_rect(theme_class, part_id, state_id)
            if content_rect is not None:
                entry["contentRect"] = content_rect

            colors = {}
            for color_key, prop_id in PART_COLOR_PROPS.items():
                value = query_theme_color(theme_class, part_id, state_id, prop_id)
                if value is not None:
                    colors[color_key] = value
            if colors:
                entry["colors"] = colors

            if entry:
                part_obj[state_name] = entry

    return {"roles": roles, "parts": parts}


def _invert_theme_colors(node):
    """Recursively HSL-inverts every '#RRGGBBAA' string value in a
    light-theme dict, producing dark.theme's own content - see
    invert_lightness_hex()'s own doc comment for why this is a derived
    transform, not a second live query."""
    if isinstance(node, dict):
        return {key: _invert_theme_colors(value) for key, value in node.items()}
    if isinstance(node, str) and node.startswith("#") and len(node) == 9:
        return invert_lightness_hex(node)
    return node


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "-o", "--output-dir", default=".",
        help="Directory to write light.theme/dark.theme into (default: current directory)"
    )
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)

    light = build_light_theme()
    dark = _invert_theme_colors(light)

    light_path = os.path.join(args.output_dir, "light.theme")
    dark_path = os.path.join(args.output_dir, "dark.theme")

    with open(light_path, "w", encoding="utf-8") as f:
        json.dump(light, f, indent=4)
    with open(dark_path, "w", encoding="utf-8") as f:
        json.dump(dark, f, indent=4)

    print("Wrote {} ({} roles, {} theme classes)".format(light_path, len(light["roles"]), len(light["parts"])))
    print("Wrote {}".format(dark_path))


if __name__ == "__main__":
    main()
