#pragma once

// Precompiled header for the newui static library target.
//
// Only stable, externally-owned headers belong here: standard library,
// vendored 3rdparty deps, and newui/newui.h (unchanged since the initial
// commit, and the header that sets WIN32_LEAN_AND_MEAN/NOMINMAX/UNICODE
// before <windows.h> is pulled in). Actively-developed local headers
// (view.h, subview.h, rootview.h, application.h, viewstyle.h, controls.h,
// reflection.h, frame.h, ...) are deliberately excluded - putting them
// here would force a full library rebuild on every edit to any of them.

// Windows / platform (macros must be defined before windows.h; newui.h
// already does this)
#include "newui/newui.h"

// Standard library
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Vendored 3rdparty (never edited by this team)
#include <blend2d/blend2d.h>
