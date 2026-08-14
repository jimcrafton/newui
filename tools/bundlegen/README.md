# bundlegen

Scaffolds the on-disk resource-bundle layout `newui::Bundle`
(`include/newui/bundle.h`) expects to find next to an app's `.exe`:

```
<target_dir>/
    Info.json          ("name"/"version")
    Resources/
        Fonts/
        Images/
        UIs/            (saveViewTree()/saveViewTreeToFile() output)
```

No setup required - stdlib only, unlike `tools/reflectgen` (no `venv`, no
`requirements.txt`).

## How this fits into the framework

`Bundle` (a Meyer's singleton, `Bundle::instance()`) is a pure runtime
*reader*: `executableDir()`/`resourcesDir()` are computed once from the
running `.exe`'s own path, `resourcePath()`/`loadImage()`/`loadTextFile()`
resolve relative to `resourcesDir()`, and `appName()`/`appVersion()` are
lazily parsed from `Info.json` (falling back to
`Application::instance().getName()` / `""` if it's missing a field, or
missing entirely). None of that creates anything on disk - it all assumes
the layout above already exists. This script is the other half: it's what
actually creates that layout, so a new app has somewhere for `Bundle` to
find things from the start.

Getting `Resources/` and `Info.json` to actually end up *next to the
built `.exe`* (not just wherever you ran this script) is a separate,
CMake-side step: `newui_add_resources(target resourceSourceDir)` and
`newui_add_bundle_info(target infoJsonPath)`, both in the top-level
`CMakeLists.txt`, copy them into place via a post-build step each. So the
normal flow for a new app is:

```
python tools/bundlegen/bundlegen.py path/to/MyApp --name "My App" --version 1.0.0
```

then, in that app's own `CMakeLists.txt`:

```cmake
newui_add_resources(my_app "${CMAKE_CURRENT_SOURCE_DIR}/path/to/MyApp/Resources")
newui_add_bundle_info(my_app "${CMAKE_CURRENT_SOURCE_DIR}/path/to/MyApp/Info.json")
```

The two CMake calls are independent - a target can use one, the other, or
both, matching the two independent files this script scaffolds. Neither
this script nor those CMake functions are wired into the build
automatically for any target yet - each is a deliberate opt-in, same
"standalone, run/called manually" status `tools/reflectgen` documents
itself as having.

## Usage

```
python bundlegen.py <target_dir> --name "My App" [--version 1.0.0] [--force]
```

- `target_dir` - where to create the layout. Point it at a source-tree
  folder you'll wire into CMake via `newui_add_resources()` (the normal
  case), or straight at a build output directory for quick manual
  testing.
- `--name` - required; written to `Info.json`'s `"name"` field.
- `--version` - optional; written to `Info.json`'s `"version"` field if
  given. Omitted entirely (not written as `""`) if you don't pass it -
  `Bundle::appVersion()` already falls back to `""` for a missing field.
- `--force` - overwrite an existing `Info.json`. Without it, an existing
  `Info.json` is left alone so a re-run never clobbers hand edits.

Safe to re-run: directory creation is idempotent, and each empty
`Fonts`/`Images`/`UIs` directory gets a `.gitkeep` placeholder (only
written if not already present) so the empty scaffolded structure is
actually something `git add` will pick up - directories with nothing in
them aren't tracked by git otherwise.
