"""python -m newui_guitest <command>

ONLY works with apps built on the newui framework (they must run newui's automation server). Not for any other app.

    check <exe>    READ-ONLY smoke check, the right first run for a new app: starts it, says hello, reports the root view's
                   rectangle and the protocol version, optionally dumps views, closes it. Sends no mouse or keyboard input.
    test <tests>   run GUI tests (they DO drive the real mouse and keyboard): see `python -m newui_guitest test --help`

    python -m newui_guitest check D:\\build\\myapp.exe --view canvas
    python -m newui_guitest test D:\\code\\myapp\\tests\\gui\\test_myapp.py --exe D:\\build\\myapp.exe
"""
import argparse
import sys

from . import App
from .runner import main as run_tests


def check(argv) -> int:
    parser = argparse.ArgumentParser(prog="newui_guitest check", description="start the app, say hello, report; sends no input")
    parser.add_argument("exe")
    parser.add_argument("args", nargs="*", help="extra arguments for the app")
    parser.add_argument("--root", default="rootview", help="name of the root view (default: rootview)")
    parser.add_argument("--view", action="append", default=[], help="also report this view (repeatable)")
    parser.add_argument("--connect-timeout", type=float, default=App.CONNECT_TIMEOUT, metavar="SECONDS")
    parser.add_argument("--reply-timeout", type=float, default=App.REPLY_TIMEOUT, metavar="SECONDS")
    options = parser.parse_args(argv)
    App.CONNECT_TIMEOUT = options.connect_timeout
    App.REPLY_TIMEOUT = options.reply_timeout

    with App(options.exe, options.args) as app:
        print("protocol:", app.ask("hello")["protocol"])
        for name in [options.root] + options.view:
            rect = app.find(name)
            print("%-20s x=%.0f y=%.0f w=%.0f h=%.0f visible=%s" % (name, rect.x, rect.y, rect.w, rect.h, rect.visible))
    print("ok")
    return 0


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv and argv[0] == "check":
        return check(argv[1:])
    if argv and argv[0] == "test":
        return run_tests(argv[1:])
    print(__doc__)
    return 0 if not argv or argv[0] in ("-h", "--help") else 2


if __name__ == "__main__":
    sys.exit(main())
