# libSceJpegEnc on the shared host JPEG encoder (core/Decoder/Jpeg).
add_library(libSceJpegEnc SHARED EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/Export.cpp)
target_include_directories(libSceJpegEnc PRIVATE ${LIBS_INCLUDE_DIR})
set_target_properties(libSceJpegEnc PROPERTIES CXX_EXTENSIONS OFF)
configure_windows_unwind(libSceJpegEnc)
target_link_libraries(libSceJpegEnc PRIVATE decoder_jpeg)
