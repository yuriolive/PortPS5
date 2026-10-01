# cmake/PortPS5FFmpegLicenseCheck.cmake
# Script run right after FFmpeg's configure (cmake -P): fails the build when the configured FFmpeg is
# anything other than plain LGPL-2.1+. Inputs: CONFIG_MAK, CONFIG_H.
file(READ "${CONFIG_MAK}" mak)
# GPLV3/LGPLV3 matter as much as GPL: version 3 terms are incompatible with GPL-2.0-only. A flag that is
# off appears in config.mak as "!CONFIG_X=yes", so only a line starting with CONFIG_X=yes counts.
file(READ "${CONFIG_H}" header)
foreach(flag GPL VERSION3 NONFREE GPLV3 LGPLV3)
    if(mak MATCHES "(^|\n)CONFIG_${flag}=yes" OR header MATCHES "#define CONFIG_${flag} 1")
        message(FATAL_ERROR "FFmpeg configure enabled CONFIG_${flag}; PortPS5 (GPL-2.0-only) may only link a plain LGPL-2.1+ FFmpeg")
    endif()
endforeach()
if(NOT header MATCHES "#define FFMPEG_LICENSE \"LGPL version 2.1 or later\"")
    message(FATAL_ERROR "FFmpeg reports an unexpected licence; expected \"LGPL version 2.1 or later\" (see ${CONFIG_H})")
endif()
message(STATUS "FFmpeg licence check passed: LGPL version 2.1 or later")
