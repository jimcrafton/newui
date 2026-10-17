# Reference copy of the Claude Code skill

`newui-gui-driver\` is the reference copy of the skill that teaches Claude Code to write and run GUI tests with this driver
(`D:\code\newui-demos\tools\guitest`). It is kept here, under version control, because `.claude\` is not committed.
Claude Code only loads a skill from a `.claude\skills\` folder, so **install** it by copying the folder:

```
# this project only
Copy-Item -Recurse -Force D:\code\newui-demos\tools\guitest\skill\newui-gui-driver D:\code\newui-demos\.claude\skills\

# every project of this user
Copy-Item -Recurse -Force D:\code\newui-demos\tools\guitest\skill\newui-gui-driver C:\Users\<you>\.claude\skills\
```

Start a new Claude Code session afterwards; it then triggers on a matching request (GUI test, drive the app, newui automation) or as
`/newui-gui-driver`.

How to use it once installed (example requests, what Claude does on its own, safety): `D:\code\newui-demos\tools\guitest\README.md`, "Using the Claude Code skill".

What the skill needs besides its own files, all in this repository:

| File | Why |
|---|---|
| `D:\code\newui-demos\tools\guitest\` (`newui_guitest` package) | the driver; the skill's templates put it on `sys.path` |
| `D:\code\newui-demos\automation.md` | the protocol, how to launch and customize the driver, pitfalls |
| `D:\code\newui-demos\tools\guitest\README.md` | the driver API and command-line options |
| `D:\code\newui-demos\demos\sketch\tests\gui\test_sketch_gui.py` | a worked example |

The paths inside the skill are absolute paths of this checkout. If the repository lives elsewhere, edit them after copying.

**Keep it in sync:** edit the copy here, then re-run the install command above. Do not edit the installed copy.
