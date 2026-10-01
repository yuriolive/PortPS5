cmake_minimum_required(VERSION 3.20)
project(PortPS5 CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(BUILD_TESTING "Build and register tests" OFF)
if(BUILD_TESTING)
    enable_testing()
    include(PortPS5Testing)
    include(PortPS5GTest)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
else()
    # Test helpers are only defined when BUILD_TESTING is ON. Subsystem
    # CMakeLists (e.g. core/Decoder/*) call portps5_add_gtest unconditionally,
    # and the CodeQL deep build configures with BUILD_TESTING=OFF, so provide
    # no-op stubs instead of failing with "Unknown CMake command".
    function(portps5_add_gtest)
    endfunction()
    function(portps5_add_test)
    endfunction()
endif()

option(ANYPS5_ENABLE_SPIRV_TOOLS "Enable SPIRV-Tools validation and optimization" OFF)
option(PORTPS5_COMPILE_WARNING_AS_ERROR "Treat warnings as errors for PortPS5 targets" OFF)

if(CMAKE_COMPILE_WARNING_AS_ERROR)
    set(PORTPS5_COMPILE_WARNING_AS_ERROR ON CACHE BOOL "Treat warnings as errors for PortPS5 targets" FORCE)
    unset(CMAKE_COMPILE_WARNING_AS_ERROR CACHE)
endif()

if(NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        message(STATUS "Found ccache: ${CCACHE_PROGRAM}")
    endif()
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_VIDEO ON CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_X11 ON CACHE BOOL "" FORCE)
set(SDL_WAYLAND OFF CACHE BOOL "" FORCE)
set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN ON CACHE BOOL "" FORCE)
set(SDL_JOYSTICK ON CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI ON CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_STATIC_PIC ON CACHE BOOL "" FORCE)
set(ANYPS5_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "")
add_subdirectory(3rdparty/SDL2)
set(CMAKE_EXE_LINKER_FLAGS "${ANYPS5_EXE_LINKER_FLAGS}")

if(PORTPS5_COMPILE_WARNING_AS_ERROR)
    set(CMAKE_COMPILE_WARNING_AS_ERROR ON)
endif()

add_subdirectory(core/shader/recompiler)
add_subdirectory(core/Decoder/Jpeg)
add_subdirectory(core/Decoder/Png)
add_subdirectory(core/libs)
add_subdirectory(core/relinker)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

