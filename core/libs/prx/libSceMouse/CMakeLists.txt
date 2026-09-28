add_library(libSceMouse SHARED EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/Export.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mouse_impl.cpp
)
target_include_directories(libSceMouse PRIVATE ${LIBS_INCLUDE_DIR})
set_target_properties(libSceMouse PROPERTIES CXX_EXTENSIONS OFF)
configure_windows_unwind(libSceMouse)

# Mouse backend unit tests (M2-gated): GTest cases in tests/Mouse.cpp are
# DISABLED until the M2 mouse-export implementation lands (exports are
# Unsupported() stubs in M1). portps5_add_gtest links GTest::gtest_main so no
# manual main() is needed; the binary compiles in CI to pin the backend API.
if(BUILD_TESTING)
    portps5_add_gtest(mouse_tests
        SOURCES  ${CMAKE_CURRENT_SOURCE_DIR}/tests/Mouse.cpp
                 ${CMAKE_CURRENT_SOURCE_DIR}/../libSceVideoOut/src/MouseInput.cpp
        INCLUDES ${LIBS_INCLUDE_DIR}
                 ${CMAKE_SOURCE_DIR}/3rdparty/SDL2/include
        LIBRARIES libSceMouse libkernel libc
        LABELS   unit
    )
endif()
