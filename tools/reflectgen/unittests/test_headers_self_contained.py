"""Every public newui header must compile standing alone (each one includes what it uses).

Code generators add just the header that defines a class to a file that names it (see cpp_codetools'
IncludeManager / Class::header()), so a header that only works after some other one has been included
first would break that. Run with the reflectgen venv, like the other tests here.
"""

import glob
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import reflectgen as rg
import clang.cindex as cindex

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
INCLUDE_DIRS = [os.path.join(REPO, "include"), os.path.join(REPO, "build", "include"),
                os.path.join(REPO, "3rdparty", "json5", "include"), os.path.join(REPO, "3rdparty", "wil", "include"),
                os.path.join(REPO, "3rdparty", "blend2d")]


class PublicHeadersAreSelfContainedTest(unittest.TestCase):
    def test_each_header_compiles_on_its_own(self):
        # (newui/version.h is generated into the build tree - without a build there is nothing to parse against)
        if not os.path.isfile(os.path.join(REPO, "build", "include", "newui", "version.h")):
            self.skipTest("no generated newui/version.h - build newui first")
        rg.configure_libclang()
        args = ["-x", "c++", "-std=c++17", "-DWIN32_LEAN_AND_MEAN", "-DNOMINMAX",
                "-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH", "-D_MSVC_STL_USE_ABORT_AS_DOOM_FUNCTION",
                "-Wno-pragma-once-outside-header"] + ["-I" + d for d in INCLUDE_DIRS]
        index = cindex.Index.create()
        broken = []
        for path in sorted(glob.glob(os.path.join(REPO, "include", "newui", "*.h"))):
            tu = index.parse(path, args=args, options=0x200)   # KeepGoing
            errors = [d for d in tu.diagnostics
                      if d.severity >= cindex.Diagnostic.Error and d.location.file is not None
                      and "newui" in d.location.file.name.replace(os.sep, "/")]
            if errors:
                broken.append(os.path.basename(path) + ": " + errors[0].spelling)
        self.assertEqual(broken, [], "headers that don't compile on their own")


if __name__ == "__main__":
    unittest.main()
