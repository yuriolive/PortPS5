function(ensure_agc_glslang)
    if(TARGET glslang AND TARGET SPIRV AND TARGET glslang-standalone)
        return()
    endif()

    if(TARGET glslang)
        if(NOT TARGET SPIRV)
            message(FATAL_ERROR "glslang exists but SPIRV target is missing")
        endif()
        if(NOT TARGET glslang-standalone)
            message(FATAL_ERROR "glslang exists but glslang-standalone target is missing")
        endif()
        return()
    endif()

    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_EXTERNAL OFF)
    set(GLSLANG_TESTS OFF)
    set(GLSLANG_ENABLE_INSTALL OFF)
    set(ENABLE_GLSLANG_BINARIES ON)
    set(ENABLE_SPIRV ON)
    set(ENABLE_SPVREMAPPER OFF)
    set(ENABLE_OPT OFF)

    set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
    add_subdirectory(
            "${CMAKE_SOURCE_DIR}/3rdparty/glslang"
            "${CMAKE_BINARY_DIR}/3rdparty/glslang"
            EXCLUDE_FROM_ALL
    )
    foreach(t glslang MachineIndependent GenericCodeGen OSDependent glslang-default-resource-limits glslang-standalone)
        if(TARGET ${t})
            set_target_properties(${t} PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
            target_compile_options(${t} PRIVATE -Wno-error -Wno-maybe-uninitialized)
        endif()
    endforeach()
endfunction()

ensure_agc_glslang()

set(agcTextureDetileShaderSource ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/shaders/TextureDetile.comp)
set(agcTextureDetileGeneratedRoot ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(agcTextureDetileGeneratedDir ${agcTextureDetileGeneratedRoot}/prx/libSceAgcDriver/Graphics/shaders)
set(agcTextureDetileShaderSpv ${agcTextureDetileGeneratedDir}/TextureDetile.spv)
set(agcTextureDetileShaderHeader ${agcTextureDetileGeneratedDir}/TextureDetile_spv.h)
add_custom_command(
        OUTPUT "${agcTextureDetileShaderHeader}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${agcTextureDetileGeneratedDir}"
        COMMAND "$<TARGET_FILE:glslang-standalone>" -V --target-env vulkan1.1 -o "${agcTextureDetileShaderSpv}" "${agcTextureDetileShaderSource}"
        COMMAND ${CMAKE_COMMAND} -DINPUT=${agcTextureDetileShaderSpv} -DOUTPUT=${agcTextureDetileShaderHeader}
        -DSYMBOL=TEXTURE_DETILE_SPV -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_spirv.cmake"
        DEPENDS "${agcTextureDetileShaderSource}" glslang-standalone
        VERBATIM
)

add_library(libSceAgcDriver SHARED EXCLUDE_FROM_ALL
        ${CMAKE_CURRENT_SOURCE_DIR}/Execution/src/DisplayBuffer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Execution/src/Driver.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Execution/src/GuestMemory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Execution/src/ShaderMemory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Execution/src/BdaFeatures.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Execution/src/Pm4.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Execution/src/VulkanDevice.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Execution/src/SwapchainState.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Execution/src/AspectFit.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Execution/src/PresentationScaler.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/Recorder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/HostImport.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/Draw.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/ColorTargetLayout.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/ColorTargetTransfer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/Pipeline.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/Resources.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/ShaderInputState.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/ShaderResources.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/ShaderAddressResources.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/GuestBufferMemory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/BdaResources.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/BufferAddress.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/ShaderValidation.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/State.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/GuestTextureResource.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/GuestSamplerResource.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/TextureFormat.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/TextureTiling.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/TextureDetiler.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/Texture.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/Sampler.cpp
        ${agcTextureDetileShaderHeader}
        ${CMAKE_CURRENT_SOURCE_DIR}/Eq/src/Event.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Eq/src/Query.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Resource/src/Lifecycle.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Resource/src/Registration.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/State/src/Configuration.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/State/src/Status.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Submit/src/Acb.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Submit/src/CommandBuffer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Submit/src/Dcb.cpp
)

target_include_directories(libSceAgcDriver PRIVATE
        ${LIBS_INCLUDE_DIR}
        ${agcTextureDetileGeneratedRoot}
        ${CMAKE_SOURCE_DIR}/3rdparty/Vulkan-Headers/include
        ${CMAKE_SOURCE_DIR}/3rdparty/SPIRV-Headers/include
)

add_shader_recompiler(shader_recompiler_agc_driver libc)

if(ANYPS5_ENABLE_SPIRV_TOOLS)
    # Offline shader request replayer (M1 recompiler-golden gate). Host-only dev tool:
    # built for dev/ci presets, never shipped. Replays serialized `.req` requests through
    # Recompile and diffs disassembled SPIR-V against `.spvasm` goldens (--golden).
    add_executable(agc_shader_replay
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/AgcShaderReplay.cpp
    )
    target_include_directories(agc_shader_replay PRIVATE
        ${CMAKE_SOURCE_DIR}/core/shader/recompiler
        ${CMAKE_SOURCE_DIR}/core/shader/recompiler/RdnaDecoder/include
        ${CMAKE_SOURCE_DIR}/core/shader/recompiler/ControlFlow/include
        ${CMAKE_SOURCE_DIR}/core/shader/recompiler/IntermediateRepresentation/include
        ${CMAKE_SOURCE_DIR}/core/shader/recompiler/Translation/include
        ${CMAKE_SOURCE_DIR}/core/shader/recompiler/Optimization/include
        ${CMAKE_SOURCE_DIR}/core/shader/recompiler/SpirvBackend/include
        ${CMAKE_SOURCE_DIR}/core/shader/recompiler/tests/golden
        ${CMAKE_SOURCE_DIR}/3rdparty/SPIRV-Headers/include
    )
    target_link_libraries(agc_shader_replay PRIVATE
        shader_recompiler_agc_driver
        SPIRV-Tools-static
        SPIRV-Tools-opt
        libc
    )
    set_target_properties(agc_shader_replay PROPERTIES CXX_EXTENSIONS OFF EXCLUDE_FROM_ALL OFF)
    if(WIN32 AND TARGET libc)
        set_target_properties(agc_shader_replay PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:libc>"
        )
    endif()
    if(MINGW)
        target_link_options(agc_shader_replay PRIVATE "-Wl,--allow-multiple-definition")
    endif()
    if(COMMAND configure_windows_unwind)
        configure_windows_unwind(agc_shader_replay)
    endif()

    if(BUILD_TESTING)
        # Process-level regression tests for agc_shader_replay --golden (labelled
        # 'golden' so the recompiler-golden job runs them through ctest --preset golden).
        # Fixtures generate at configure time from one checked-in corpus pair, so they
        # track the corpus without duplicating shaders.
        set(AGC_REPLAY_CORPUS_DIR "${CMAKE_SOURCE_DIR}/core/shader/recompiler/tests/golden/corpus")
        set(AGC_REPLAY_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/golden_fixtures")
        file(MAKE_DIRECTORY "${AGC_REPLAY_FIXTURE_DIR}/crlf" "${AGC_REPLAY_FIXTURE_DIR}/empty")
        # CRLF fixture: golden checked out with Windows line endings must still compare
        # equal to the LF-only disassembler output (Macroscope review on PR #31).
        configure_file("${AGC_REPLAY_CORPUS_DIR}/smem_load.wave64.req" "${AGC_REPLAY_FIXTURE_DIR}/crlf/smem_load.wave64.req" COPYONLY)
        file(READ "${AGC_REPLAY_CORPUS_DIR}/smem_load.wave64.spvasm" AGC_REPLAY_SPVASM)
        string(REPLACE "\n" "\r\n" AGC_REPLAY_SPVASM_CRLF "${AGC_REPLAY_SPVASM}")
        file(WRITE "${AGC_REPLAY_FIXTURE_DIR}/crlf/smem_load.wave64.spvasm" "${AGC_REPLAY_SPVASM_CRLF}")
        add_test(NAME agc_replay_golden_crlf COMMAND agc_shader_replay --golden "${AGC_REPLAY_FIXTURE_DIR}/crlf")
        set_tests_properties(agc_replay_golden_crlf PROPERTIES LABELS "golden")
        # Empty-corpus fixture: replaying zero requests must fail, so CI can never pass
        # vacuously on a missing or wiped corpus (Macroscope review on PR #31).
        add_test(NAME agc_replay_golden_empty COMMAND agc_shader_replay --golden "${AGC_REPLAY_FIXTURE_DIR}/empty")
        set_tests_properties(agc_replay_golden_empty PROPERTIES LABELS "golden" WILL_FAIL TRUE)
        # Mismatch fixture: a valid request with a corrupted golden must fail, locking the
        # diff-failure exit path (not just the pass path the CRLF fixture covers).
        file(MAKE_DIRECTORY "${AGC_REPLAY_FIXTURE_DIR}/mismatch")
        configure_file("${AGC_REPLAY_CORPUS_DIR}/smem_load.wave64.req" "${AGC_REPLAY_FIXTURE_DIR}/mismatch/smem_load.wave64.req" COPYONLY)
        file(WRITE "${AGC_REPLAY_FIXTURE_DIR}/mismatch/smem_load.wave64.spvasm" "deliberately corrupted golden\n")
        add_test(NAME agc_replay_golden_mismatch COMMAND agc_shader_replay --golden "${AGC_REPLAY_FIXTURE_DIR}/mismatch")
        set_tests_properties(agc_replay_golden_mismatch PROPERTIES LABELS "golden" WILL_FAIL TRUE)
        # Bad-request fixture: an unparseable .req must fail instead of passing silently.
        file(MAKE_DIRECTORY "${AGC_REPLAY_FIXTURE_DIR}/badreq")
        file(WRITE "${AGC_REPLAY_FIXTURE_DIR}/badreq/broken.wave64.req" "!!!not-base64!!!\n")
        add_test(NAME agc_replay_golden_badreq COMMAND agc_shader_replay --golden "${AGC_REPLAY_FIXTURE_DIR}/badreq")
        set_tests_properties(agc_replay_golden_badreq PROPERTIES LABELS "golden" WILL_FAIL TRUE)
        if(WIN32)
            foreach(replayTest agc_replay_golden_crlf agc_replay_golden_empty agc_replay_golden_mismatch agc_replay_golden_badreq)
                set_tests_properties(${replayTest} PROPERTIES ENVIRONMENT_MODIFICATION
                    "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
            endforeach()
        endif()
    endif()
endif()

target_link_libraries(libSceAgcDriver PRIVATE
        shader_recompiler_agc_driver
        SDL2-static
        libc
)

set_target_properties(libSceAgcDriver PROPERTIES CXX_EXTENSIONS OFF)
configure_windows_unwind(libSceAgcDriver)

foreach(runtimeLinkFlag IN ITEMS -static-libgcc -static-libstdc++ -static)
    string(REPLACE "${runtimeLinkFlag}" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
endforeach()

add_executable(agc_driver_tests EXCLUDE_FROM_ALL
        tests/Submit.cpp
)

target_include_directories(agc_driver_tests PRIVATE
        ${LIBS_INCLUDE_DIR}
)

target_link_libraries(agc_driver_tests PRIVATE
        libSceAgcDriver
        libc
)

set_target_properties(agc_driver_tests PROPERTIES CXX_EXTENSIONS OFF)
configure_windows_unwind(agc_driver_tests)

add_executable(agc_shader_memory_tests EXCLUDE_FROM_ALL
        tests/ShaderMemory.cpp
        Execution/src/ShaderMemory.cpp
        Execution/src/GuestMemory.cpp
)
target_include_directories(agc_shader_memory_tests PRIVATE ${LIBS_INCLUDE_DIR})
target_link_libraries(agc_shader_memory_tests PRIVATE shader_recompiler_agc_driver libc)
set_target_properties(agc_shader_memory_tests PROPERTIES CXX_EXTENSIONS OFF)
configure_windows_unwind(agc_shader_memory_tests)

add_executable(agc_driver_flip_tests EXCLUDE_FROM_ALL
        tests/Flip.cpp
)

target_include_directories(agc_driver_flip_tests PRIVATE
        ${LIBS_INCLUDE_DIR}
)

target_link_libraries(agc_driver_flip_tests PRIVATE
        libSceAgcDriver
        libc
)

set_target_properties(agc_driver_flip_tests PROPERTIES CXX_EXTENSIONS OFF)
configure_windows_unwind(agc_driver_flip_tests)

add_executable(agc_driver_pm4_tests EXCLUDE_FROM_ALL
        tests/Pm4.cpp
)

target_include_directories(agc_driver_pm4_tests PRIVATE
        ${LIBS_INCLUDE_DIR}
)

target_link_libraries(agc_driver_pm4_tests PRIVATE
        libSceAgcDriver
        libc
)

set_target_properties(agc_driver_pm4_tests PROPERTIES CXX_EXTENSIONS OFF)
configure_windows_unwind(agc_driver_pm4_tests)

add_executable(agc_driver_graphics_tests EXCLUDE_FROM_ALL
        tests/Graphics.cpp
        tests/ColorTargetLayout.cpp
        tests/BdaResources.cpp
        tests/GuestAllocations.cpp
        tests/TextureFormat.cpp
        tests/TextureTiling.cpp
        tests/GuestTextureResource.cpp
        tests/GuestSamplerResource.cpp
        tests/TextureDetiler.cpp
        Graphics/src/Draw.cpp
        Graphics/src/Pipeline.cpp
        Graphics/src/GraphicsPipelineCache.cpp
        Graphics/src/State.cpp
        Graphics/src/ColorTargetLayout.cpp
        Graphics/src/ColorTargetTransfer.cpp
        Graphics/src/Resources.cpp
        Graphics/src/ShaderResources.cpp
        Graphics/src/ShaderAddressResources.cpp
        Graphics/src/GuestBufferMemory.cpp
        Graphics/src/BdaResources.cpp
        Graphics/src/BufferAddress.cpp
        Graphics/src/ShaderValidation.cpp
        Graphics/src/GuestTextureResource.cpp
        Graphics/src/GuestSamplerResource.cpp
        Graphics/src/TextureFormat.cpp
        Graphics/src/TextureTiling.cpp
        Graphics/src/TextureDetiler.cpp
        Graphics/src/Texture.cpp
        Graphics/src/Sampler.cpp
        ${agcTextureDetileShaderHeader}
        Execution/src/GuestMemory.cpp
)

target_include_directories(agc_driver_graphics_tests PRIVATE
        ${LIBS_INCLUDE_DIR}
        ${agcTextureDetileGeneratedRoot}
        ${CMAKE_SOURCE_DIR}/core/shader/recompiler
        ${CMAKE_SOURCE_DIR}/core/shader/recompiler/IntermediateRepresentation/include
        ${CMAKE_SOURCE_DIR}/core/shader/recompiler/ControlFlow/include
        ${CMAKE_SOURCE_DIR}/core/shader/recompiler/RdnaDecoder/include
        ${CMAKE_SOURCE_DIR}/3rdparty/Vulkan-Headers/include
        ${CMAKE_SOURCE_DIR}/3rdparty/SPIRV-Headers/include
)

target_link_libraries(agc_driver_graphics_tests PRIVATE
        shader_recompiler_agc_driver
        libc
)

set_target_properties(agc_driver_graphics_tests PROPERTIES CXX_EXTENSIONS OFF)
configure_windows_unwind(agc_driver_graphics_tests)

add_executable(agc_driver_bda_device_tests EXCLUDE_FROM_ALL
        tests/BdaDevice.cpp
        tests/BdaShader.cpp
        tests/BdaExecution.cpp
        tests/BdaContracts.cpp
        Execution/src/BdaFeatures.cpp
        Graphics/src/Resources.cpp
        Graphics/src/BufferAddress.cpp
)

target_include_directories(agc_driver_bda_device_tests PRIVATE
        ${LIBS_INCLUDE_DIR}
        ${CMAKE_SOURCE_DIR}/core/shader/recompiler
        ${CMAKE_SOURCE_DIR}/3rdparty/Vulkan-Headers/include
)
target_link_libraries(agc_driver_bda_device_tests PRIVATE SDL2-static libc shader_recompiler_agc_driver)
set_target_properties(agc_driver_bda_device_tests PROPERTIES CXX_EXTENSIONS OFF)
configure_windows_unwind(agc_driver_bda_device_tests)

if(BUILD_TESTING)
    # libSceAgcDriver tests exercise GPU translation and submission pipelines.
    # Per docs/spec/verification.md §1, the hosted CI 'unit' job covers host-only
    # libc, libkernel, and relinker tests. All libSceAgcDriver tests are labeled
    # 'lavapipe' to run in the driver test suite (Milestone 1 driver-lavapipe job).
    set(AGC_DRIVER_TESTS
        agc_driver_tests
        agc_driver_flip_tests
        agc_driver_graphics_tests
        agc_driver_bda_device_tests
        agc_driver_pm4_tests
        agc_shader_memory_tests
    )

    foreach(agcTest IN LISTS AGC_DRIVER_TESTS)
        string(REGEX REPLACE "_tests$" "" agcTestName "${agcTest}")
        set_target_properties(${agcTest} PROPERTIES EXCLUDE_FROM_ALL OFF)
        add_test(NAME ${agcTestName} COMMAND ${agcTest})
        if(WIN32)
            set_tests_properties(${agcTestName} PROPERTIES ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
        endif()
        set_tests_properties(${agcTestName} PROPERTIES LABELS "lavapipe")
    endforeach()

    # Recorder and HostImport (portps5-6): GoogleTest on a real Vulkan device (lavapipe on hosted CI).
    # Labelled 'lavapipe' like the other driver suites; tests skip themselves when no device exists.
    # Compute dispatch through the real recompiler + Vulkan device. Links the driver DLL; gtest
    # discovery gives each TEST its own process (the driver latches errors process-wide).
    portps5_add_gtest(agc_driver_recompiler_tests
        SOURCES  tests/Recompiler.cpp
        INCLUDES ${LIBS_INCLUDE_DIR}
        LIBRARIES libSceAgcDriver libc
        LABELS   lavapipe
    )
    set_target_properties(agc_driver_recompiler_tests PROPERTIES CXX_EXTENSIONS OFF)

    portps5_add_gtest(agc_recorder_tests
        SOURCES  ${CMAKE_CURRENT_SOURCE_DIR}/tests/RecorderTests.cpp
                 ${CMAKE_CURRENT_SOURCE_DIR}/tests/HostImportTests.cpp
                 ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/Recorder.cpp
                 ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/HostImport.cpp
                 ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/Resources.cpp
                 ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/BufferAddress.cpp
                 ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/src/BufferPool.cpp
                 ${CMAKE_CURRENT_SOURCE_DIR}/Execution/src/GuestMemory.cpp
        INCLUDES ${LIBS_INCLUDE_DIR}
                 ${CMAKE_CURRENT_SOURCE_DIR}/tests
                 ${CMAKE_SOURCE_DIR}/core/shader/recompiler
                 ${CMAKE_SOURCE_DIR}/3rdparty/Vulkan-Headers/include
        LIBRARIES SDL2-static libc
        LABELS   lavapipe
    )

    # Hosted BytesEqual tests: pure helper used by the TextureCache revalidation (no GPU, no guest memory).
    portps5_add_gtest(agc_bytes_equal_tests
        SOURCES  tests/BytesEqualTests.cpp
                 tests/SnapshotValidityTests.cpp
        INCLUDES ${LIBS_INCLUDE_DIR}
        LIBRARIES libc
        LABELS   unit
    )

    # Hosted depth/stencil decode tests: synthetic register maps through DecodeState,
    # no GPU and no guest memory (draws use no colour target).
    portps5_add_gtest(agc_driver_depth_stencil_tests
        SOURCES  tests/DepthStencilState.cpp
                 Graphics/src/State.cpp
                 Graphics/src/ColorTargetLayout.cpp
                 Execution/src/GuestMemory.cpp
                 Execution/src/GuestGpuRange.cpp
        INCLUDES ${LIBS_INCLUDE_DIR}
                 ${CMAKE_SOURCE_DIR}/core/shader/recompiler
                 ${CMAKE_SOURCE_DIR}/core/shader/recompiler/IntermediateRepresentation/include
                 ${CMAKE_SOURCE_DIR}/core/shader/recompiler/ControlFlow/include
                 ${CMAKE_SOURCE_DIR}/core/shader/recompiler/RdnaDecoder/include
                 ${CMAKE_SOURCE_DIR}/3rdparty/Vulkan-Headers/include
                 ${CMAKE_SOURCE_DIR}/3rdparty/SPIRV-Headers/include
        LIBRARIES shader_recompiler_agc_driver libc
        LABELS   unit
    )

    # Hosted tests for the render-target sampling image pool (pure logic, fake handles, no device).
    portps5_add_gtest(agc_driver_resident_image_pool_tests
        SOURCES  tests/ResidentImagePool.cpp
                 Graphics/src/ResidentImagePool.cpp
        INCLUDES ${LIBS_INCLUDE_DIR}
                 ${CMAKE_SOURCE_DIR}/3rdparty/Vulkan-Headers/include
        # libc supplies __gxx_personality_v0, which the project's Windows unwind configuration
        # references from every test executable (see core/libs/CMakeLists.txt, config_tests).
        LIBRARIES libc
        LABELS   unit
    )

    # Hosted depth-surface tests: format policy, loadOp mapping and ResidentDepth lifetime
    # against a mock Vulkan device table (no GPU, no guest memory).
    portps5_add_gtest(agc_driver_depth_surface_tests
        SOURCES  tests/DepthSurface.cpp
                 Graphics/src/DepthFormat.cpp
                 Graphics/src/DepthSurface.cpp
                 Graphics/src/State.cpp
                 Graphics/src/ColorTargetLayout.cpp
                 Execution/src/GuestMemory.cpp
                 Execution/src/GuestGpuRange.cpp
        INCLUDES ${LIBS_INCLUDE_DIR}
                 ${CMAKE_SOURCE_DIR}/core/shader/recompiler
                 ${CMAKE_SOURCE_DIR}/core/shader/recompiler/IntermediateRepresentation/include
                 ${CMAKE_SOURCE_DIR}/core/shader/recompiler/ControlFlow/include
                 ${CMAKE_SOURCE_DIR}/core/shader/recompiler/RdnaDecoder/include
                 ${CMAKE_SOURCE_DIR}/3rdparty/Vulkan-Headers/include
                 ${CMAKE_SOURCE_DIR}/3rdparty/SPIRV-Headers/include
        LIBRARIES shader_recompiler_agc_driver libc
        LABELS   unit
    )

    portps5_add_gtest(recompiler_fixes_tests
        SOURCES  ${CMAKE_SOURCE_DIR}/core/shader/recompiler/tests/RecompilerFixesTests.cpp
        INCLUDES ${CMAKE_SOURCE_DIR}/core/shader/recompiler
        LIBRARIES shader_recompiler_agc_driver libc
        LABELS   unit
    )

    # Instruction-semantics tests for the RDNA2 fixes ported from upstream (lane E): decode, translate and
    # interpret the emitted IR for hand-assembled synthetic instructions. No GPU, no game data.
    portps5_add_gtest(recompiler_ported_instruction_tests
        SOURCES  ${CMAKE_SOURCE_DIR}/core/shader/recompiler/tests/RdnaPortedInstructionTests.cpp
        INCLUDES ${CMAKE_SOURCE_DIR}/core/shader/recompiler ${CMAKE_SOURCE_DIR}/core/shader/recompiler/tests/golden
        LIBRARIES shader_recompiler_agc_driver libc
        LABELS   unit
    )

    # Hosted recompiler-golden gate (M1 exit): synthetic corpus replay over every
    # decoded instruction class. Labelled 'golden' so ctest --preset golden runs only
    # this suite; it is excluded from the 'unit' preset and its expected-count gate.
    portps5_add_gtest(recompiler_golden_tests
        SOURCES  ${CMAKE_SOURCE_DIR}/core/shader/recompiler/tests/golden/RecompilerGoldenTests.cpp
        INCLUDES ${CMAKE_SOURCE_DIR}/core/shader/recompiler ${CMAKE_SOURCE_DIR}/core/shader/recompiler/tests/golden
        LIBRARIES shader_recompiler_agc_driver libc
        LABELS   golden
    )
endif()

option(AGC_BUILD_VISUAL_TEST "Enable the standalone AGC SPIR-V visual test" OFF)

if(AGC_BUILD_VISUAL_TEST)
    ensure_agc_glslang()

    set(visualShaders)

    foreach(stage IN ITEMS vert frag)
        set(shaderOutput "${CMAKE_CURRENT_BINARY_DIR}/Triangle.${stage}.spv")

        add_custom_command(
                OUTPUT "${shaderOutput}"
                COMMAND "$<TARGET_FILE:glslang-standalone>"
                -V
                --target-env vulkan1.1
                -o "${shaderOutput}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/shaders/Triangle.${stage}"
                DEPENDS
                glslang-standalone
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/shaders/Triangle.${stage}"
                VERBATIM
        )

        list(APPEND visualShaders "${shaderOutput}")
    endforeach()

    add_executable(agc_driver_visual_test EXCLUDE_FROM_ALL
            tests/Visual.cpp
            Execution/src/VulkanDevice.cpp
            Execution/src/SwapchainState.cpp
            Execution/src/AspectFit.cpp
            Execution/src/PresentationScaler.cpp
            Execution/src/GuestMemory.cpp
            Execution/src/BdaFeatures.cpp
            Graphics/src/Draw.cpp
            Graphics/src/ColorTargetLayout.cpp
            Graphics/src/ColorTargetTransfer.cpp
            Graphics/src/Pipeline.cpp
            Graphics/src/Resources.cpp
            Graphics/src/ShaderResources.cpp
            Graphics/src/ShaderAddressResources.cpp
            Graphics/src/GuestBufferMemory.cpp
            Graphics/src/BdaResources.cpp
            Graphics/src/BufferAddress.cpp
            Graphics/src/ShaderValidation.cpp
            Graphics/src/GuestTextureResource.cpp
            Graphics/src/GuestSamplerResource.cpp
            Graphics/src/TextureFormat.cpp
            Graphics/src/TextureTiling.cpp
            Graphics/src/TextureDetiler.cpp
            Graphics/src/Texture.cpp
            Graphics/src/Sampler.cpp
            ${agcTextureDetileShaderHeader}
            ${visualShaders}
    )

    target_include_directories(agc_driver_visual_test PRIVATE
            ${LIBS_INCLUDE_DIR}
            ${agcTextureDetileGeneratedRoot}
            ${CMAKE_SOURCE_DIR}/core/shader/recompiler
            ${CMAKE_SOURCE_DIR}/core/shader/recompiler/IntermediateRepresentation/include
            ${CMAKE_SOURCE_DIR}/core/shader/recompiler/ControlFlow/include
            ${CMAKE_SOURCE_DIR}/core/shader/recompiler/RdnaDecoder/include
            ${CMAKE_SOURCE_DIR}/3rdparty/Vulkan-Headers/include
            ${CMAKE_SOURCE_DIR}/3rdparty/SPIRV-Headers/include
    )

    target_link_libraries(agc_driver_visual_test PRIVATE
            SDL2-static
            libc
    )

    set_target_properties(agc_driver_visual_test PROPERTIES CXX_EXTENSIONS OFF)
    configure_windows_unwind(agc_driver_visual_test)

    if(WIN32)
        set(visualRuntimeFiles "$<TARGET_FILE:libc>")

        if(MINGW)
            get_filename_component(visualCompilerDirectory "${CMAKE_CXX_COMPILER}" DIRECTORY)

            foreach(runtimeName IN ITEMS
                    libgcc_s_seh-1.dll
                    libstdc++-6.dll
                    libwinpthread-1.dll
            )
                set(runtimePath "${visualCompilerDirectory}/${runtimeName}")

                if(NOT EXISTS "${runtimePath}")
                    message(FATAL_ERROR "Cannot locate MinGW runtime: ${runtimeName}")
                endif()

                list(APPEND visualRuntimeFiles "${runtimePath}")
            endforeach()
        endif()

        add_custom_target(agc_driver_visual_runtime
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                ${visualRuntimeFiles}
                "$<TARGET_FILE_DIR:agc_driver_visual_test>"
                DEPENDS libc
                COMMAND_EXPAND_LISTS
                VERBATIM
        )

        add_dependencies(agc_driver_visual_test agc_driver_visual_runtime)
    endif()
endif()

include(PostCompile.cmake)
