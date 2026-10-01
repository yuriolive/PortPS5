# Shared PNG parser/decoder/encoder (docs/spec/image-codecs.md). Static, PIC,
# hidden visibility so stb symbols never leak out of a PRX that links this.
add_library(decoder_png STATIC ${CMAKE_CURRENT_SOURCE_DIR}/src/Png.cpp)
target_include_directories(decoder_png PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(decoder_png SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/3rdparty/stb)
set_target_properties(decoder_png PROPERTIES
        CXX_EXTENSIONS OFF
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        POSITION_INDEPENDENT_CODE ON
)

# portps5_add_gtest only exists when BUILD_TESTING is ON (the release preset turns it OFF,
# and an unguarded call fails the whole configure with 'Unknown CMake command').
if(BUILD_TESTING)
portps5_add_gtest(decoder_png_tests
    SOURCES   ${CMAKE_CURRENT_SOURCE_DIR}/tests/Png.cpp
    LIBRARIES decoder_png
    LABELS    unit
)
endif()
