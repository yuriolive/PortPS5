# cmake/PortPS5FFmpeg.cmake
# Builds the pinned FFmpeg (3rdparty/FFmpeg, tag n9.0.2) as static libavcodec + libavutil with only
# the H.264 decoder, and exposes it as the imported target portps5::ffmpeg.
#
# LICENCE GATE (docs/spec/build-toolchain.md "FFmpeg"): PortPS5 is GPL-2.0-only, FFmpeg is LGPL-2.1+.
# The configure line below passes NO --enable-gpl, --enable-version3 or --enable-nonfree, and enables
# no external library. After configure, the generated ffbuild/config.mak and config.h are checked and
# the build fails if any of CONFIG_GPL / CONFIG_VERSION3 / CONFIG_NONFREE is on or FFMPEG_LICENSE is
# not "LGPL version 2.1 or later". The linked library also reports its licence at run time and a
# unit test (H264DecoderTests) asserts it.
#
# The build needs a POSIX sh and make (Git for Windows' sh and the toolchain's mingw32-make on the
# Windows runner). When they are missing the target is not created, unless PORTPS5_REQUIRE_FFMPEG is
# ON (the ci preset), which turns that into a configure error so tests cannot be skipped silently.

include_guard(GLOBAL)

option(PORTPS5_ENABLE_FFMPEG "Build the pinned LGPL FFmpeg for H.264 decoding (libSceVideodec2)" ON)
option(PORTPS5_REQUIRE_FFMPEG "Fail configure if FFmpeg cannot be built (used by the ci preset)" OFF)
# Extra FFmpeg ./configure arguments, for cross builds only (for example --enable-cross-compile --host-cc=gcc).
set(PORTPS5_FFMPEG_CONFIGURE_EXTRA "" CACHE STRING "Extra arguments for FFmpeg's configure (cross builds)")

function(portps5_setup_ffmpeg)
    if(TARGET portps5::ffmpeg)
        return()
    endif()
    set(src "${CMAKE_SOURCE_DIR}/3rdparty/FFmpeg")
    set(reason "")
    if(NOT PORTPS5_ENABLE_FFMPEG)
        set(reason "PORTPS5_ENABLE_FFMPEG is OFF")
    elseif(NOT EXISTS "${src}/configure")
        set(reason "3rdparty/FFmpeg is not checked out (git submodule update --init 3rdparty/FFmpeg)")
    else()
        # Git for Windows keeps sh and the coreutils FFmpeg's configure needs in usr/bin.
        find_program(PORTPS5_SH sh HINTS "$ENV{ProgramFiles}/Git/usr/bin" "C:/Program Files/Git/usr/bin")
        get_filename_component(compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        find_program(PORTPS5_MAKE NAMES make mingw32-make HINTS "${compiler_dir}")
        if(NOT PORTPS5_SH)
            set(reason "no sh found")
        elseif(NOT PORTPS5_MAKE)
            set(reason "no make found")
        endif()
    endif()
    if(reason)
        if(PORTPS5_REQUIRE_FFMPEG AND PORTPS5_ENABLE_FFMPEG)
            message(FATAL_ERROR "FFmpeg is required but cannot be built: ${reason}")
        endif()
        message(STATUS "FFmpeg: not built (${reason}); libSceVideodec2 reports codec errors")
        return()
    endif()

    enable_language(C)
    get_filename_component(sh_dir "${PORTPS5_SH}" DIRECTORY)
    get_filename_component(make_dir "${PORTPS5_MAKE}" DIRECTORY)
    # PATH for configure and make: sh and make first. The separator must reach the shell as one argument,
    # so on a Windows host ';' is written as $<SEMICOLON> (a bare ';' would split the CMake list).
    if(CMAKE_HOST_WIN32)
        set(sep "$<SEMICOLON>")
        string(REPLACE ";" "$<SEMICOLON>" base_path "$ENV{PATH}")
    else()
        set(sep ":")
        set(base_path "$ENV{PATH}")
    endif()
    set(env_path "${sh_dir}${sep}${make_dir}${sep}${base_path}")
    find_program(PORTPS5_NASM nasm)
    set(asm_args --disable-x86asm)
    if(PORTPS5_NASM)
        set(asm_args "--x86asmexe=${PORTPS5_NASM}")
    endif()

    # Git for Windows' sh reports an MSYS uname, which makes FFmpeg's configure refuse ("Native MSYS builds
    # are discouraged"). The compiler is MinGW-w64, so name the target explicitly instead of probing uname.
    set(target_args "")
    if(CMAKE_HOST_WIN32)
        set(target_args --target-os=mingw32 --arch=x86_64)
    endif()

    set(bin "${CMAKE_BINARY_DIR}/ffmpeg")
    set(options
        --prefix=${bin}/install
        --cc=${CMAKE_C_COMPILER}
        ${target_args}
        --enable-static --disable-shared
        --disable-autodetect --disable-everything
        --disable-programs --disable-doc --disable-debug --disable-network
        --disable-avdevice --disable-avformat --disable-avfilter --disable-swresample --disable-swscale
        --disable-pthreads --disable-w32threads
        --enable-avcodec --enable-avutil
        --enable-decoder=h264
        ${asm_args}
        ${PORTPS5_FFMPEG_CONFIGURE_EXTRA})
    cmake_host_system_information(RESULT jobs QUERY NUMBER_OF_LOGICAL_CORES)

    include(ExternalProject)
    set(libs "${bin}/libavcodec/libavcodec.a" "${bin}/libavutil/libavutil.a")
    ExternalProject_Add(portps5_ffmpeg_build
        SOURCE_DIR "${src}"
        BINARY_DIR "${bin}"
        CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env "PATH=${env_path}" "${PORTPS5_SH}" "${src}/configure" ${options}
        # Licence gate: refuse to build if the configure picked up any GPL/v3/non-free component.
        COMMAND ${CMAKE_COMMAND} -DCONFIG_MAK=${bin}/ffbuild/config.mak -DCONFIG_H=${bin}/config.h
                -P "${CMAKE_SOURCE_DIR}/cmake/PortPS5FFmpegLicenseCheck.cmake"
        BUILD_COMMAND ${CMAKE_COMMAND} -E env "PATH=${env_path}" "${PORTPS5_MAKE}" -j${jobs} libavcodec/libavcodec.a libavutil/libavutil.a
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS ${libs}
        EXCLUDE_FROM_ALL ON
        USES_TERMINAL_BUILD OFF)

    add_library(portps5::ffmpeg INTERFACE IMPORTED GLOBAL)
    # Headers live in the source tree; avconfig.h and config.h are generated into the build tree.
    set_target_properties(portps5::ffmpeg PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${src};${bin}"
        # bcrypt: libavutil seeds its RNG with BCryptGenRandom on Windows.
        INTERFACE_LINK_LIBRARIES "${bin}/libavcodec/libavcodec.a;${bin}/libavutil/libavutil.a;$<$<PLATFORM_ID:Windows>:bcrypt>"
        INTERFACE_COMPILE_DEFINITIONS "APS5_HAVE_FFMPEG=1")
    add_dependencies(portps5::ffmpeg portps5_ffmpeg_build)
    message(STATUS "FFmpeg: building ${src} (LGPL-2.1+, H.264 decoder only)")
endfunction()
