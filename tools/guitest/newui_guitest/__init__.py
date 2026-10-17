"""newui_guitest - drive a newui app with real input and ask it what it shows (see automation.md).

ONLY for apps built on the newui framework: it talks to newui's automation server (newui_automation) over a named pipe, which
other applications do not have. For anything else it fails with "no automation pipe".

    from newui_guitest import App, AppTestCase, ScreenShot
"""
from .app import App, AutomationError, GuiTestError, MenuEntry, Rect, TreeRow, UserBusyError
from .screenshot import ScreenShot, dark, differs
from .runner import main
from .testing import AppTestCase

__all__ = ["App", "AppTestCase", "main", "AutomationError", "GuiTestError", "MenuEntry", "Rect", "ScreenShot", "TreeRow",
           "UserBusyError", "dark", "differs"]
