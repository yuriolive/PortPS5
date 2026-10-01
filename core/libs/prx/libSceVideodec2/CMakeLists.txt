# core/libs/prx/libSceVideodec2/CMakeLists.txt
# libSceVideodec2 PRX plus the static H.264 wrapper shared with its unit tests. FFmpeg comes from
# the pinned 3rdparty/FFmpeg submodule via cmake/PortPS5FFmpeg.cmake (LGPL-2.1+ only); when it
# cannot be built the wrapper compiles its "unavailable" stubs and CreateDecoder reports API_FAIL.

include(PortPS5FFmpeg)
portps5_setup_ffmpeg()

add_library(portps5_h264_decoder STATIC EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/H264Decoder.cpp)
target_include_directories(portps5_h264_decoder PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(portps5_h264_decoder PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(TARGET portps5::ffmpeg)
    target_link_libraries(portps5_h264_decoder PUBLIC portps5::ffmpeg)
endif()

add_library(libSceVideodec2 SHARED EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/Export.cpp)
target_include_directories(libSceVideodec2 PRIVATE ${LIBS_INCLUDE_DIR})
target_link_libraries(libSceVideodec2 PRIVATE portps5_h264_decoder libc libkernel)
# FFmpeg's symbols stay private: the NID patcher must not see them as exports.
target_link_options(libSceVideodec2 PRIVATE -Wl,--exclude-libs,ALL)
configure_windows_unwind(libSceVideodec2)
