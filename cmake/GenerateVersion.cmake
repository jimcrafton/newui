# Regenerates newui/version.h with an incremented, rolling-over build
# number. Run via `cmake -P` from a custom build step (see root
# CMakeLists.txt's newui_version target) rather than at `cmake` configure
# time, so BUILD advances on every build invocation, not just the first
# configure.
#
# Versioning scheme: Major.Minor.Release.Build, each build increments
# Build by 1. Build rolls over at 9999 (resets to 0, Release += 1);
# Release rolls over at 999 (resets to 0, Minor += 1); Minor rolls over at
# 99 (resets to 0, Major += 1, no ceiling on Major). So the *current*
# Major/Minor/Release aren't fixed - they're state that can advance purely
# from accumulated builds, which is why they're persisted in STATE_FILE
# (tracked in git, at the repo root - not a build-tree file, precisely so
# a `build/` wipe-and-reconfigure doesn't reset version progression back
# to the INITIAL_* seed).
#
# Expected -D arguments: INITIAL_MAJOR, INITIAL_MINOR, INITIAL_RELEASE
# (used only the very first time, when STATE_FILE doesn't exist yet),
# STATE_FILE, IN_FILE, OUT_FILE.

set(NEWUI_BUILD_ROLLOVER 9999)
set(NEWUI_RELEASE_ROLLOVER 999)
set(NEWUI_MINOR_ROLLOVER 99)

if(EXISTS "${STATE_FILE}")
    file(READ "${STATE_FILE}" STATE)
    string(STRIP "${STATE}" STATE)
    string(REPLACE "." ";" STATE_LIST "${STATE}")
    list(GET STATE_LIST 0 newui_VERSION_MAJOR)
    list(GET STATE_LIST 1 newui_VERSION_MINOR)
    list(GET STATE_LIST 2 newui_VERSION_RELEASE)
    list(GET STATE_LIST 3 newui_VERSION_BUILD)
else()
    set(newui_VERSION_MAJOR ${INITIAL_MAJOR})
    set(newui_VERSION_MINOR ${INITIAL_MINOR})
    set(newui_VERSION_RELEASE ${INITIAL_RELEASE})
    set(newui_VERSION_BUILD 0)
endif()

math(EXPR newui_VERSION_BUILD "${newui_VERSION_BUILD} + 1")
if(newui_VERSION_BUILD GREATER ${NEWUI_BUILD_ROLLOVER})
    set(newui_VERSION_BUILD 0)
    math(EXPR newui_VERSION_RELEASE "${newui_VERSION_RELEASE} + 1")
    if(newui_VERSION_RELEASE GREATER ${NEWUI_RELEASE_ROLLOVER})
        set(newui_VERSION_RELEASE 0)
        math(EXPR newui_VERSION_MINOR "${newui_VERSION_MINOR} + 1")
        if(newui_VERSION_MINOR GREATER ${NEWUI_MINOR_ROLLOVER})
            set(newui_VERSION_MINOR 0)
            math(EXPR newui_VERSION_MAJOR "${newui_VERSION_MAJOR} + 1")
        endif()
    endif()
endif()

set(newui_VERSION "${newui_VERSION_MAJOR}.${newui_VERSION_MINOR}.${newui_VERSION_RELEASE}.${newui_VERSION_BUILD}")
file(WRITE "${STATE_FILE}" "${newui_VERSION}")

configure_file("${IN_FILE}" "${OUT_FILE}" @ONLY)
