# reflectgen build integration
# =============================
# Discovery and generation both happen inside ONE build-time step -
# tools/reflectgen/generate_reflection.py, invoked by the add_custom_command()
# in newui_add_reflectgen_output() below - not split across a CMake-
# configure-time discovery pass and a separate build-time generation pass.
# That split (this file's own earlier design) had a real gap: CMake's build
# graph (a custom command's fixed COMMAND/DEPENDS) is set once at configure
# time, so a discovery step that only *finds* newly-marked files has no way
# to hand its results to a generation step that was already configured
# without them - editing an existing header to add its first "@reflect"/
# NEWUI_REFLECT_PRIVATE() marker silently did nothing until a manual
# reconfigure. generate_reflection.py runs discovery fresh every single
# time it's invoked instead (cheap - discovery is a plain substring scan,
# not a real parse, see its own timing output), so there's nothing to go
# stale.
#
# Real incrementality is kept anyway: each call's custom command DEPENDS
# lists every *candidate* header under that call's own scan directories
# (not just the ones currently reflectable), so Ninja/MSBuild only invokes
# it at all when some candidate's content actually changed (or the scripts
# themselves did) - not on every build. A brand new header (one CMake
# doesn't know to depend on yet) still needs file(GLOB_RECURSE ...
# CONFIGURE_DEPENDS ...) to notice and trigger a reconfigure - reliable
# under Ninja/Makefiles, less so under the Visual Studio generator (this
# project's other build tree - out/build/<config> is Ninja, build/ is VS -
# see HANDOFF.md "Where things stand"), where a manual reconfigure may
# occasionally still be needed for that specific case.
#
# Needs reflectgen's own venv (tools/reflectgen/.venv, see its README's
# "Setup" section), not a bare system Python - reflectgen.py needs the
# `clang` bindings package installed there specifically.
#
# Reusable beyond newui itself: newui_add_reflectgen_output() takes SCAN_DIRS/
# INCLUDE_DIRS arguments (see its own comment) so a project that uses newui
# as a library can call it again for its *own* target/classes, independently
# of newui's own use of the same machinery for the `newui` target below -
# see reflection.md's "Using reflectgen in your own project" section for the
# full walkthrough.

set(NEWUI_REFLECTGEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tools/reflectgen)
set(NEWUI_REFLECTGEN_PYTHON ${NEWUI_REFLECTGEN_DIR}/.venv/Scripts/python.exe)

if(NOT EXISTS ${NEWUI_REFLECTGEN_PYTHON})
    message(FATAL_ERROR
        "reflectgen: '${NEWUI_REFLECTGEN_PYTHON}' not found - run the setup steps in "
        "tools/reflectgen/README.md ('python -m venv .venv' then "
        "'.venv\\Scripts\\pip install -r requirements.txt' from tools/reflectgen) "
        "before configuring.")
endif()

# Directories newui's own newui_add_reflectgen_output(newui) call (below, in
# the top-level CMakeLists.txt) scans by default - include/newui is its
# whole public API surface. A caller with its own classes to reflect passes
# its own SCAN_DIRS explicitly instead (see the function's own comment) -
# this default only applies to a call that omits SCAN_DIRS entirely.
set(NEWUI_REFLECTGEN_SCAN_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include/newui)

# Captured once, here, at the point this file is include()'d from newui's
# own top-level CMakeLists.txt (whether that happens directly, in-tree, or
# via a downstream project's add_subdirectory(newui) - either way
# CMAKE_CURRENT_SOURCE_DIR/CMAKE_CURRENT_BINARY_DIR at *this* point in
# processing are still newui's own root/build dirs, not the caller's) -
# every reflected class needs newui/reflection.h and friends visible
# regardless of whose classes they are, so newui_add_reflectgen_output()
# always adds these, on top of whatever INCLUDE_DIRS a caller passes for
# its own headers.
set(NEWUI_REFLECTGEN_BASE_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/json5/include
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/blend2d
)

# Default OFF - scan-all is the real default (see reflection.md and this
# file's own HANDOFF.md entry): every real header under include/newui is
# reflectable unless explicitly opted out via "@reflect ignore=true" or
# NEWUI_REFLECT_PRIVATE()'s own per-member opt-in, without requiring a
# developer to go mark up every class by hand first. This used to default
# ON because scan-all produced a build-breaking cascade of real compile
# errors (headers that don't parse standalone outside newui.h's
# aggregation, has_reflect_friend() matching an unrelated friend
# declaration, unqualified nested-type spellings, inherited/mismatched
# Delegate senders, non-copy-constructible getters/setters/collection
# elements, abstract-class constructors, non-const-reference method
# params, and - the last and most surprising one - MSVC's own
# std::is_copy_constructible_v<T> being simply wrong for a T that holds a
# std::vector<std::unique_ptr<T>> of itself, undermining reflection.h's
# own if-constexpr safety guards) - every one of those is now either
# fixed in reflectgen.py itself or worked around by skipping the specific
# unsupported shape (still real, directly-callable C++, just not
# reflected) - verified via a full scan + standalone compile of every
# real header in this project. Set this back ON locally to return to the
# old opt-in-only behavior, or to experiment on a class-by-class basis.
# Applies to every newui_add_reflectgen_output() call, not just newui's
# own - a downstream project's classes may hit a gap of their own reflectgen
# doesn't cover yet, same as this project did (the option is process-wide,
# simplest and consistent - see reflection.md if a call ever needs to
# differ).
option(NEWUI_REFLECTGEN_REQUIRE_MARKER
    "Only reflect a header that contains an '@reflect' annotation or NEWUI_REFLECT_PRIVATE() \
somewhere, instead of scanning every header under its scan directories unconditionally"
    OFF)

# Registers a build-time custom command that discovers and generates in one
# step (see this file's own header comment for why) and adds the resulting
# single combined .cpp to `target`'s sources.
#
#   newui_add_reflectgen_output(<target>
#       [SCAN_DIRS <dir>...]        # default: NEWUI_REFLECTGEN_SCAN_DIRS
#                                   # (newui's own include/newui)
#       [INCLUDE_DIRS <dir>...])   # extra -I paths beyond
#                                   # NEWUI_REFLECTGEN_BASE_INCLUDE_DIRS
#                                   # (newui's own headers + 3rdparty deps,
#                                   # always included)
#
# Call once per target that wants generated registration functions
# compiled in - newui's own top-level CMakeLists.txt calls this once for
# the `newui` target itself with no extra arguments (scans include/newui);
# a project using newui as a library calls it again for its *own* target,
# passing its own SCAN_DIRS (see reflection.md's "Using reflectgen in your
# own project" for the full walkthrough) - the two calls are completely
# independent, each gets its own generated .cpp.
function(newui_add_reflectgen_output target)
    cmake_parse_arguments(ARG "" "" "SCAN_DIRS;INCLUDE_DIRS" ${ARGN})

    if(NOT ARG_SCAN_DIRS)
        set(ARG_SCAN_DIRS ${NEWUI_REFLECTGEN_SCAN_DIRS})
    endif()

    set(output ${CMAKE_CURRENT_BINARY_DIR}/generated/${target}_reflection_generated.cpp)

    set(require_marker_arg)
    if(NEWUI_REFLECTGEN_REQUIRE_MARKER)
        set(require_marker_arg --require-marker)
    endif()

    set(scan_dir_args)
    set(candidate_headers)
    foreach(dir ${ARG_SCAN_DIRS})
        list(APPEND scan_dir_args --scan-dir ${dir})
        # CONFIGURE_DEPENDS: re-runs configure the next time CMake
        # configures if this dir's file *set* changes - see this file's
        # own header comment for the Ninja-vs-VS-generator caveat.
        file(GLOB_RECURSE dir_headers CONFIGURE_DEPENDS ${dir}/*.h)
        list(APPEND candidate_headers ${dir_headers})
    endforeach()

    set(include_args)
    foreach(dir ${NEWUI_REFLECTGEN_BASE_INCLUDE_DIRS} ${ARG_INCLUDE_DIRS})
        list(APPEND include_args --clang-arg=-I${dir})
    endforeach()

    add_custom_command(
        OUTPUT ${output}
        COMMAND ${NEWUI_REFLECTGEN_PYTHON}
            ${NEWUI_REFLECTGEN_DIR}/generate_reflection.py
            --python ${NEWUI_REFLECTGEN_PYTHON}
            ${scan_dir_args}
            -o ${output}
            ${require_marker_arg}
            ${include_args}
        DEPENDS
            ${candidate_headers}
            ${NEWUI_REFLECTGEN_DIR}/generate_reflection.py
            ${NEWUI_REFLECTGEN_DIR}/discover_reflectable.py
            ${NEWUI_REFLECTGEN_DIR}/reflectgen.py
        COMMENT "reflectgen: discovering + regenerating ${output}"
        VERBATIM
    )

    target_sources(${target} PRIVATE ${output})

    if(MSVC)
        # Scan-all's own generated .cpp - one registration function per
        # reflected class/enum, all in a single translation unit - is
        # large enough (114 classes across newui's own headers alone) to
        # exceed MSVC's default per-object-file section count (C1128)
        # well before it exceeds any real code-size concern. Scoped to
        # just this one generated file rather than the whole target - no
        # other source approaches that limit.
        set_source_files_properties(${output} PROPERTIES COMPILE_OPTIONS "/bigobj")
    endif()
endfunction()
