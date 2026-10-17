"""unittest glue: one app per test, closed (killed) however the test ends.

    class MyAppTests(AppTestCase):
        def test_filter(self):
            self.app.click("filterInput")
            self.app.type("Box")
            self.assertEqual("Box", self.app.text_of("filterInput"))

The executable comes from the command line (see runner.py):
    python -m newui_guitest test test_myapp.py --exe D:\\build\\myapp.exe
A test class may also name a default in `exe`, used when the command line gives none.
"""
import unittest
from typing import List, Optional

from .app import App


class Settings:
    """What the command line chose for this run (set by runner.main); every AppTestCase reads it."""
    exe: Optional[str] = None            # --exe: overrides a test class's own `exe`
    args: List[str] = []                 # --arg (repeatable): appended to the class's `args`
    cwd: Optional[str] = None            # --cwd: working directory of the app


class AppTestCase(unittest.TestCase):
    exe = None          # default path of the app's executable, used when --exe is not given
    args = ()           # extra command-line arguments for the app; --automation <pipe> is added by App

    def launch(self) -> App:
        """Starts the app for this test. Override for an app that needs more than exe + args."""
        exe = Settings.exe or self.exe
        if not exe:
            self.fail("no executable: pass --exe <path to the app>")
        return App(exe, tuple(self.args) + tuple(Settings.args), cwd=Settings.cwd)

    def setUp(self):
        self.app = self.launch()
        self.addCleanup(self.app.close)
