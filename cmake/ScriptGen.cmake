# scriptgen build integration
# ===========================
# include() this file, then call newui_add_scriptgen_output() once per target that wants pybind11
# bindings generated from classes marked "// @scriptable" (see tools/scriptgen/README.md).
#
# Needs the same venv reflectgen uses (tools/reflectgen/.venv, see its README's "Setup") because
# scriptgen reads the headers with libclang through the shared tools/reflectgen_lib package.
#
#   newui_add_scriptgen_output(<target>
#       SCAN_DIRS <dir-or-header>...   # headers/directories searched for "@scriptable" (recursively)
#       [INCLUDE_DIRS <dir>...]        # -I paths the headers need (and that name their include spelling)
#       [CXX_STANDARD <n>]             # -std=c++<n> for the parse (default 17)
#       [REGISTER_FUNCTION <name>]     # generated "void <name>(pybind11::module_&)" (default registerScripting)
#       [STUBS <file.pyi>]             # also write a type stub file
#       [STRICT])                      # a warning (a member that could not be bound) fails the build
#
# The generated <target>_scripting_generated.cpp is added to <target>'s sources; compiling it needs
# pybind11 and Python headers, so link <target> against pybind11::headers (or pybind11::embed). Only the
# headers that contain "@scriptable" are read; the others are seen through their includes.

set(NEWUI_SCRIPTGEN_DIR ${CMAKE_CURRENT_LIST_DIR}/../tools/scriptgen)
set(NEWUI_SCRIPTGEN_LIB_DIR ${CMAKE_CURRENT_LIST_DIR}/../tools/reflectgen_lib)
set(NEWUI_SCRIPTGEN_PYTHON ${CMAKE_CURRENT_LIST_DIR}/../tools/reflectgen/.venv/Scripts/python.exe)

function(newui_add_scriptgen_output target)
    cmake_parse_arguments(ARG "STRICT" "CXX_STANDARD;REGISTER_FUNCTION;STUBS" "SCAN_DIRS;INCLUDE_DIRS" ${ARGN})

    if(NOT EXISTS ${NEWUI_SCRIPTGEN_PYTHON})
        message(FATAL_ERROR
            "scriptgen: '${NEWUI_SCRIPTGEN_PYTHON}' not found - run the setup steps in "
            "tools/reflectgen/README.md before configuring.")
    endif()
    if(NOT ARG_SCAN_DIRS)
        message(FATAL_ERROR "newui_add_scriptgen_output(${target}): SCAN_DIRS is required")
    endif()
    if(NOT ARG_CXX_STANDARD)
        set(ARG_CXX_STANDARD 17)
    endif()

    set(output ${CMAKE_CURRENT_BINARY_DIR}/generated/${target}_scripting_generated.cpp)
    set(SCRIPTGEN_GENERATED_CPP ${output} PARENT_SCOPE)

    set(candidate_headers)
    foreach(dir ${ARG_SCAN_DIRS})
        if(IS_DIRECTORY ${dir})
            file(GLOB_RECURSE dir_headers CONFIGURE_DEPENDS ${dir}/*.h)
            list(APPEND candidate_headers ${dir_headers})
        else()
            list(APPEND candidate_headers ${dir})
        endif()
    endforeach()

    set(clang_args -std=c++${ARG_CXX_STANDARD})
    foreach(dir ${ARG_INCLUDE_DIRS})
        list(APPEND clang_args -I${dir})
    endforeach()

    set(optional_args)
    set(outputs ${output})
    if(ARG_REGISTER_FUNCTION)
        list(APPEND optional_args --register-function ${ARG_REGISTER_FUNCTION})
    endif()
    if(ARG_STUBS)
        list(APPEND optional_args --stubs ${ARG_STUBS})
        list(APPEND outputs ${ARG_STUBS})
    endif()
    if(ARG_STRICT)
        list(APPEND optional_args --strict)
    endif()

    add_custom_command(
        OUTPUT ${outputs}
        COMMAND ${NEWUI_SCRIPTGEN_PYTHON}
            ${NEWUI_SCRIPTGEN_DIR}/scriptgen.py
            ${ARG_SCAN_DIRS}
            -o ${output}
            --marker-only
            ${optional_args}
            --
            ${clang_args}
        DEPENDS
            ${candidate_headers}
            ${NEWUI_SCRIPTGEN_DIR}/scriptgen.py
            ${NEWUI_SCRIPTGEN_DIR}/scriptcollect.py
            ${NEWUI_SCRIPTGEN_LIB_DIR}/parser.py
            ${NEWUI_SCRIPTGEN_LIB_DIR}/driver.py
        COMMENT "scriptgen: regenerating ${output}"
        VERBATIM
    )

    target_sources(${target} PRIVATE ${output})
endfunction()
