"""The command line of the harness: choose the app, choose the tests, tune the driver, run them.

    python -m newui_guitest test <tests...> --exe D:\\build\\myapp.exe [options]

<tests> are any mix of: a test file (test_myapp.py), a folder (discovers test*.py), or a dotted name
(test_myapp, test_myapp.Typing, test_myapp.Typing.test_x). With none, the whole current folder is discovered.

A test file can also be its own entry point:

    if __name__ == "__main__":
        newui_guitest.main()          # python test_myapp.py --exe D:\\build\\myapp.exe -k Typing
"""
import argparse
import os
import sys
import unittest
from typing import List, Optional

from .app import App
from .testing import Settings


def build_parser(prog: str = "newui_guitest test") -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog=prog, description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("tests", nargs="*", help="test files, folders or dotted names (default: discover in the current folder)")

    what = p.add_argument_group("what to run")
    what.add_argument("--exe", help="the app to test (overrides the test classes' own `exe`)")
    what.add_argument("--arg", action="append", default=[], dest="app_args", metavar="ARG",
                      help="an extra command-line argument for the app; repeat for several, e.g. --arg=--demo --arg=--small")
    what.add_argument("--cwd", help="working directory for the app (default: the folder you ran from)")
    what.add_argument("-k", dest="patterns", action="append", default=[], metavar="TEXT",
                      help="only tests whose name contains TEXT (repeatable)")
    what.add_argument("--list", action="store_true", help="list the tests and exit; starts nothing")
    what.add_argument("--failfast", "-f", action="store_true", help="stop at the first failure")
    what.add_argument("--quiet", "-q", action="store_true", help="one dot per test instead of one line")

    tune = p.add_argument_group("driver behaviour (the defaults suit a desk with a person at it)")
    tune.add_argument("--no-guard", action="store_true",
                      help="do not wait for the person to leave the mouse and keyboard alone, and do not abort on their input "
                           "(an unattended VM or build machine only)")
    tune.add_argument("--quiet-period", type=float, metavar="SECONDS", default=App.QUIET_PERIOD,
                      help="how long the mouse and keyboard must be untouched before input is sent (default %(default)s)")
    tune.add_argument("--quiet-timeout", type=float, metavar="SECONDS", default=App.QUIET_TIMEOUT,
                      help="how long to wait for that quiet before giving up (default %(default)s)")
    tune.add_argument("--connect-timeout", type=float, metavar="SECONDS", default=App.CONNECT_TIMEOUT,
                      help="how long to wait for the app's automation pipe after starting it (default %(default)s)")
    tune.add_argument("--reply-timeout", type=float, metavar="SECONDS", default=App.REPLY_TIMEOUT,
                      help="how long one request may go unanswered (default %(default)s)")
    return p


def _load(targets: List[str], default_module=None) -> unittest.TestSuite:
    loader = unittest.TestLoader()
    if not targets:
        return loader.loadTestsFromModule(default_module) if default_module else loader.discover(os.getcwd())
    suite = unittest.TestSuite()
    for target in targets:
        path = os.path.abspath(target)
        if os.path.isdir(path):
            suite.addTests(loader.discover(path, top_level_dir=path))
        elif os.path.isfile(path) and path.endswith(".py"):
            folder = os.path.dirname(path)
            if folder not in sys.path:
                sys.path.insert(0, folder)
            suite.addTests(loader.loadTestsFromName(os.path.splitext(os.path.basename(path))[0]))
        else:
            if os.getcwd() not in sys.path:
                sys.path.insert(0, os.getcwd())
            suite.addTests(loader.loadTestsFromName(target))
    return suite


def _flatten(suite):
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            yield from _flatten(item)
        else:
            yield item


def main(argv: Optional[List[str]] = None, default_module=None, prog: str = "newui_guitest test") -> int:
    """Parses the command line, runs the tests, returns the exit code (0 = all passed).
    A test file calls main() with no arguments: its own tests run unless it is given others."""
    options = build_parser(prog).parse_args(argv)
    if default_module is None and not options.tests:
        main_module = sys.modules.get("__main__")
        if main_module is not None and getattr(main_module, "__file__", "").endswith(".py") and "newui_guitest" not in main_module.__name__:
            default_module = main_module
    if options.exe:
        Settings.exe = os.path.abspath(options.exe)
    Settings.args = list(options.app_args)
    Settings.cwd = os.path.abspath(options.cwd) if options.cwd else None
    App.user_input_guard = not options.no_guard
    App.QUIET_PERIOD = options.quiet_period
    App.QUIET_TIMEOUT = options.quiet_timeout
    App.CONNECT_TIMEOUT = options.connect_timeout
    App.REPLY_TIMEOUT = options.reply_timeout

    suite = unittest.TestSuite(t for t in _flatten(_load(options.tests, default_module))
                               if all(p in t.id() for p in options.patterns))
    if options.list:
        for test in _flatten(suite):
            print(test.id())
        return 0
    if Settings.exe is None and not all(getattr(type(t), "exe", True) for t in _flatten(suite)):
        print("no app to test: pass --exe <path to the app>", file=sys.stderr)
        return 2
    result = unittest.TextTestRunner(verbosity=1 if options.quiet else 2, failfast=options.failfast).run(suite)
    return 0 if result.wasSuccessful() else 1
