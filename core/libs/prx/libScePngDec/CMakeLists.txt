# libScePngDec on the shared host PNG decoder (core/Decoder/Png).
add_library(libScePngDec SHARED EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/Export.cpp)
target_include_directories(libScePngDec PRIVATE ${LIBS_INCLUDE_DIR})
set_target_properties(libScePngDec PROPERTIES CXX_EXTENSIONS OFF)
configure_windows_unwind(libScePngDec)
target_link_libraries(libScePngDec PRIVATE decoder_png libc)
