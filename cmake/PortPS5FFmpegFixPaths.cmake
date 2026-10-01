# File purpose: post-configure fixup so a native mingw32-make can read the Makefiles FFmpeg's configure wrote.
#
# Why: configure runs under Git for Windows' MSYS sh, which resolves its own location with `pwd` and so
# records the source directory as an MSYS path (/d/a/PortPS5/3rdparty/FFmpeg). The top-level Makefile
# ("include <src>/Makefile") and ffbuild/config.mak (SRC_PATH=<src>) then carry that spelling, which the
# native make.exe cannot open ("No such file or directory"). Rewrite it to the drive-letter spelling.
#
# Usage: cmake -DBUILD_DIR=<ffmpeg build dir> -DSRC_DIR=<ffmpeg source dir, drive-letter form> -P this
# Lifecycle: runs once, right after configure and the licence check, on Windows hosts only.
if(NOT SRC_DIR MATCHES "^([A-Za-z]):/(.*)$")
    message(STATUS "FFmpeg path fixup: ${SRC_DIR} has no drive letter, nothing to rewrite")
    return()
endif()
string(TOLOWER "${CMAKE_MATCH_1}" drive)
set(msys_src "/${drive}/${CMAKE_MATCH_2}")
foreach(file "${BUILD_DIR}/Makefile" "${BUILD_DIR}/ffbuild/config.mak")
    if(EXISTS "${file}")
        file(READ "${file}" text)
        string(REPLACE "${msys_src}" "${SRC_DIR}" text "${text}")
        file(WRITE "${file}" "${text}")
    endif()
endforeach()
message(STATUS "FFmpeg path fixup: ${msys_src} -> ${SRC_DIR}")
