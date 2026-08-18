# reflectgen_sizeof_probe - a tiny standalone executable that prints
# sizeof() for every newui::reflection registry class - built fresh from
# whatever include/newui/reflection.h *currently* says, so reflectgen.py's
# own memory-footprint estimate (--sizeof-probe, see reflectgen.py and
# sizeof_probe.cpp's own header comments) tracks real struct-layout
# changes instead of numbers hardcoded once and left to drift.
#
# Deliberately not linked against the `newui` target - reflection.h needs
# nothing beyond its own includes for sizeof() to work (see its own header
# comment), so this stays a minimal, fast-to-build, independent target.
add_executable(reflectgen_sizeof_probe sizeof_probe.cpp)

target_include_directories(reflectgen_sizeof_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include
)

target_compile_features(reflectgen_sizeof_probe PRIVATE cxx_std_17)
