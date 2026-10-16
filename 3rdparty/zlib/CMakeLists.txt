# zlib 1.3.1 - only the inflate (decompression) half, vendored as a static library.
# manyrows reads .xlsx files (ZIP archives of deflate-compressed XML) through it.
# Upstream: https://github.com/madler/zlib/releases/tag/v1.3.1 (zlib-1.3.1.tar.gz,
# sha256 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23).
# Files are unmodified copies; see LICENSE (the zlib license). Z_SOLO drops the gz* file API, which needs
# files (gzguts.h ...) that are not vendored.

cmake_minimum_required(VERSION 3.20)
project(zlib_inflate C)

add_library(zlib_inflate STATIC
    adler32.c crc32.c inffast.c inflate.c inftrees.c zutil.c
    crc32.h inffast.h inffixed.h inflate.h inftrees.h zutil.h zlib.h zconf.h)

target_include_directories(zlib_inflate PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(zlib_inflate PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE Z_SOLO)
