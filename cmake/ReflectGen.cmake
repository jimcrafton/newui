# reflectgen build integration
# =============================
# Two stages, both driven by tools/reflectgen/*.py but running at different
# times:
#
#   1. Discovery (CONFIGURE time, execute_process() below) -
#      discover_reflectable.py scans every header under
#      NEWUI_REFLECTGEN_SCAN_DIRS and writes the result to
#      NEWUI_REFLECTGEN_HEADERS_CMAKE as the REFLECTGEN_HEADERS variable,
#      which this file then include()s below. By default this includes
#      every scanned header unconditionally - reflectgen.py itself no
#      longer needs a per-getter/setter annotation to find a property
#      (naming-convention heuristics do that now, see
#      collect_property_accessors() in reflectgen.py), so there's nothing
#      left for a file-level marker to usefully gate; anything that
#      genuinely shouldn't be reflected is excluded per-*class* via
#      "@reflect ignore=true" instead. Set NEWUI_REFLECTGEN_REQUIRE_MARKER
#      ON to go back to the stricter, opt-in mode (only a file containing
#      an "@reflect" annotation or NEWUI_REFLECT_PRIVATE() is included).
#      Re-runs automatically whenever the scanned directory's file *set*
#      changes (file(GLOB_RECURSE CONFIGURE_DEPENDS ...) below) - reliable
#      under Ninja/Makefiles, less so under the Visual Studio generator
#      (this project's other build tree - out/build/<config> is Ninja,
#      build/ is VS - see HANDOFF.md "Where things stand"), where a manual
#      reconfigure may occasionally be needed.
#   2. Generation (BUILD time, newui_add_reflectgen_output()'s
#      add_custom_command() below) - reflectgen.py itself runs against
#      REFLECTGEN_HEADERS, producing one combined generated .cpp per
#      target. Real, per-file incremental: DEPENDS lists REFLECTGEN_HEADERS
#      themselves, so Ninja/MSBuild only reruns this when one of those
#      specific files (or reflectgen.py itself) actually changes - not on
#      every build, and not just when the discovery step's own output
#      changes - same shape any other generated-source build step
#      (protobuf, Qt moc, ...) uses.
#
# Both stages need reflectgen's own venv (tools/reflectgen/.venv, see its
# README's "Setup" section), not a bare system Python - reflectgen.py
# needs the `clang` bindings package installed there specifically.

set(NEWUI_REFLECTGEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tools/reflectgen)
set(NEWUI_REFLECTGEN_PYTHON ${NEWUI_REFLECTGEN_DIR}/.venv/Scripts/python.exe)

if(NOT EXISTS ${NEWUI_REFLECTGEN_PYTHON})
    message(FATAL_ERROR
        "reflectgen: '${NEWUI_REFLECTGEN_PYTHON}' not found - run the setup steps in "
        "tools/reflectgen/README.md ('python -m venv .venv' then "
        "'.venv\\Scripts\\pip install -r requirements.txt' from tools/reflectgen) "
        "before configuring.")
endif()

# Directories discover_reflectable.py scans - include/newui is the whole
# public API surface; add more paths here if reflectable classes ever live
# elsewhere too (src/ headers, examples/, ...).
set(NEWUI_REFLECTGEN_SCAN_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include/newui)

# Default ON, unlike discover_reflectable.py's own default (OFF/scan-all) -
# see this file's own HANDOFF.md entry for why: scanning every real header
# under include/newui unconditionally, as of this writing, produces a
# build-breaking cascade of real compile errors (headers that don't parse
# standalone outside newui.h's aggregation, has_reflect_friend() matching
# an unrelated friend declaration and emitting an inaccessible private-
# member expression, ...) - real gaps worth fixing, not yet fixed. Turn
# this OFF locally (or pass --require-marker=OFF... i.e. set this variable
# OFF) once those are addressed, or to experiment on a class-by-class
# basis by adding markers deliberately.
option(NEWUI_REFLECTGEN_REQUIRE_MARKER
    "Only reflect a header that contains an '@reflect' annotation or NEWUI_REFLECT_PRIVATE() \
somewhere, instead of scanning every header under NEWUI_REFLECTGEN_SCAN_DIRS unconditionally"
    ON)

# CONFIGURE_DEPENDS: re-runs *this whole file* (discovery included) the
# next time CMake configures if any header under NEWUI_REFLECTGEN_SCAN_DIRS
# was added or removed since the last configure - see this file's own
# header comment for the Ninja-vs-VS-generator caveat.
file(GLOB_RECURSE NEWUI_REFLECTGEN_CANDIDATE_HEADERS CONFIGURE_DEPENDS
    ${NEWUI_REFLECTGEN_SCAN_DIRS}/*.h
)

set(NEWUI_REFLECTGEN_HEADERS_CMAKE ${CMAKE_CURRENT_BINARY_DIR}/generated/reflectgen_headers.cmake)

set(NEWUI_REFLECTGEN_DISCOVER_ARGS)
if(NEWUI_REFLECTGEN_REQUIRE_MARKER)
    list(APPEND NEWUI_REFLECTGEN_DISCOVER_ARGS --require-marker)
endif()

execute_process(
    COMMAND ${NEWUI_REFLECTGEN_PYTHON}
        ${NEWUI_REFLECTGEN_DIR}/discover_reflectable.py
        ${NEWUI_REFLECTGEN_CANDIDATE_HEADERS}
        -o ${NEWUI_REFLECTGEN_HEADERS_CMAKE}
        ${NEWUI_REFLECTGEN_DISCOVER_ARGS}
    RESULT_VARIABLE NEWUI_REFLECTGEN_DISCOVER_RESULT
    OUTPUT_VARIABLE NEWUI_REFLECTGEN_DISCOVER_OUTPUT
    ERROR_VARIABLE NEWUI_REFLECTGEN_DISCOVER_ERROR
)
if(NOT NEWUI_REFLECTGEN_DISCOVER_RESULT EQUAL 0)
    message(FATAL_ERROR "reflectgen: discover_reflectable.py failed:\n${NEWUI_REFLECTGEN_DISCOVER_ERROR}")
endif()
message(STATUS "${NEWUI_REFLECTGEN_DISCOVER_OUTPUT}")

# Sets REFLECTGEN_HEADERS (possibly empty - see
# newui_add_reflectgen_output()'s own empty-case handling below).
include(${NEWUI_REFLECTGEN_HEADERS_CMAKE})

# Registers a build-time custom command that runs reflectgen.py against
# REFLECTGEN_HEADERS (discovered above) and adds the resulting single
# combined .cpp to `target`'s sources - call once per target that wants
# generated registration functions compiled in.
function(newui_add_reflectgen_output target)
    set(output ${CMAKE_CURRENT_BINARY_DIR}/generated/${target}_reflection_generated.cpp)

    if(REFLECTGEN_HEADERS)
        # reflectgen.py's own generate() only ever emits #include
        # "newui/reflection.h" plus whatever --include args it's given
        # (reflectgen.py's own --help) - it does NOT automatically #include
        # each class's *source* header, so without this every
        # &Class::member/&Class::method expression in the generated file
        # would reference an undeclared type. One --include per discovered
        # header (an absolute path in a quoted #include works fine - MSVC/
        # Clang/GCC all treat a quoted #include containing a full path as a
        # direct file reference, bypassing normal search-path rules -
        # appropriate here since this generated file only ever lives in
        # this build tree, never committed/shared across machines).
        set(include_args)
        foreach(header ${REFLECTGEN_HEADERS})
            list(APPEND include_args --include ${header})
        endforeach()

        add_custom_command(
            OUTPUT ${output}
            COMMAND ${NEWUI_REFLECTGEN_PYTHON}
                ${NEWUI_REFLECTGEN_DIR}/reflectgen.py
                ${REFLECTGEN_HEADERS}
                -o ${output}
                ${include_args}
                -- -I${CMAKE_CURRENT_SOURCE_DIR}/include
                   -I${CMAKE_CURRENT_BINARY_DIR}/include
                   -I${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/json5/include
                   -I${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/blend2d
            DEPENDS ${REFLECTGEN_HEADERS} ${NEWUI_REFLECTGEN_DIR}/reflectgen.py
            COMMENT "reflectgen: regenerating ${output}"
            VERBATIM
        )
    else()
        # REFLECTGEN_HEADERS empty - either NEWUI_REFLECTGEN_SCAN_DIRS has
        # no headers at all, or NEWUI_REFLECTGEN_REQUIRE_MARKER is ON and
        # none of them carry a marker (the default scan-all mode normally
        # means this branch is never actually reached in practice, since
        # any real directory under include/newui has *something* in it).
        # reflectgen.py itself requires at least one input file (argparse
        # nargs="+"), so it can't just be invoked with an empty
        # REFLECTGEN_HEADERS - write the same "nothing to register" output
        # reflectgen.py's own generate() would produce for zero classes/
        # enums instead, so `output` always exists as a real, compilable
        # translation unit for target_sources() below, and the very next
        # configure that finds something switches back to the
        # add_custom_command() branch automatically (this whole file
        # re-runs on every reconfigure).
        file(WRITE ${output}
"// Generated by reflectgen (tools/reflectgen) - see tools/reflectgen/README.md.
//
// No reflectable headers found under ${NEWUI_REFLECTGEN_SCAN_DIRS} yet -
// see cmake/ReflectGen.cmake's own comment on this fallback.
")
    endif()

    target_sources(${target} PRIVATE ${output})
endfunction()
