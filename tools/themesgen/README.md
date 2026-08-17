# themesgen (v1)

Snapshots the real Windows visual style active on this machine right now
- colors and control part sizes/content-rect insets, across every
interaction state a part actually defines (Normal/Hot/Pressed/Disabled/
Checked/...) - into `light.theme`/`dark.theme` (JSON5-compatible; plain
JSON is a valid JSON5 subset, so this writes plain JSON via the stdlib
`json` module - no external dependency, unlike `tools/reflectgen`).

`newui::ThemeData` (`include/newui/themedata.h`, `src/themedata.cpp`)
loads one of these at runtime; `UIColorManager::colorFor()`/
`ThemedViewStyle::partSize()`/`computeClientBounds()` consult it before
falling back to their own existing live uxtheme queries. See
`themesgen.py`'s own top-of-file comment for the full design and the
"keep the Python registry and `themedata.cpp`'s symbol tables in sync by
hand" contract - there's no automated link between the two.

v1 status: standalone script, run manually, output checked in (or
regenerated on demand) - not wired into the CMake build, matching
`tools/reflectgen`'s own "not wired in yet" precedent. No setup needed -
stdlib-only, run directly:

```
python tools/themesgen/themesgen.py -o Resources/Themes
```

Writes `Resources/Themes/light.theme` and `Resources/Themes/dark.theme`.
Windows only (uses `uxtheme.dll` via `ctypes`) - run it on whatever
machine's visual style you actually want to capture; the output files
themselves are plain data and load fine on any machine.
