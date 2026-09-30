# Shared JPEG encoder/decoder (docs/spec/image-codecs.md). Static, PIC, hidden
# visibility so stb symbols never leak out of a PRX that links this.
add_library(decoder_jpeg STATIC ${CMAKE_CURRENT_SOURCE_DIR}/src/Jpeg.cpp)
target_include_directories(decoder_jpeg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(decoder_jpeg SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/3rdparty/stb)
set_target_properties(decoder_jpeg PROPERTIES
        CXX_EXTENSIONS OFF
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        POSITION_INDEPENDENT_CODE ON
)

portps5_add_gtest(decoder_jpeg_tests
    SOURCES   ${CMAKE_CURRENT_SOURCE_DIR}/tests/Jpeg.cpp
    LIBRARIES decoder_jpeg
    LABELS    unit
)
