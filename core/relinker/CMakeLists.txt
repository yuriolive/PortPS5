add_executable(relinker
        ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/cli/src/CliArgs.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/cli/src/Autorun.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/linux/LinuxElfPatcher.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/linux/LinuxGuestModuleWriter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsGuestModuleWriter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsGuestStartup.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/guest/GuestImageReader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/guest/GuestModuleBuilder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsPePatcher.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsPeFormat.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsLoadImage.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsTlsBuilder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsTrampolineBuilder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsTlsTemplateBuilder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsRelocationBuilder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsImportBuilder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsEntryStubBuilder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsDependencyStubBuilder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsStubEmitter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsPeWriter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/general/SegmentFilter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/general/EntryStubBuilder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/general/ProgramHeaderLayoutBuilder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/general/SectionHeaderTableBuilder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/io/src/ByteWriter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/io/src/ByteReader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/io/src/BufferUtils.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/io/src/FileReader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/io/src/FileWriter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/Amd64OnlyConverter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/InstructionScanner.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/X64InstructionDecoder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/X64InstructionRewriter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/DecodedInstruction.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/Amd64OnlyInstructionMatcher.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/Sse4aOperands.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/Sse4aLowering.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/parsing/ElfReader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/CodeMap.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/SyscallScanner.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/CallSiteResolver.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/CodeInstructionCollector.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/UnusedNidFilter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/UnusedNidFilter/StrictNidFilter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/UnusedNidFilter/StrictReachability.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/UnusedNidFilter/EhFrameReader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/UnusedNidFilter/PltCompactor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/UnusedNidFilter/EntryPointCollector.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/UnusedNidFilter/ControlFlowGraph.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/UnusedNidFilter/GotAccessIndex.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/UnusedNidFilter/RelativeRelocationIndex.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/ValidationPolicy.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/output/SysVDynamicSectionBuilder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/output/CallRegistryWriter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/output/ConversionReport.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/pipeline/RelinkerPipeline.cpp
)
target_include_directories(relinker PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/cli/include
        ${CMAKE_CURRENT_SOURCE_DIR}/domain/include
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/include
        ${CMAKE_CURRENT_SOURCE_DIR}/io/include
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/include
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/include
)

if(WIN32)
    add_executable(windows_dependency_diagnostics_tests EXCLUDE_FROM_ALL
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/tests/WindowsDependencyDiagnosticsTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsDependencyStubBuilder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsStubEmitter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsImportBuilder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsPeWriter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsPeFormat.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/io/src/BufferUtils.cpp
    )
    target_include_directories(windows_dependency_diagnostics_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/include ${CMAKE_CURRENT_SOURCE_DIR}/domain/include ${CMAKE_CURRENT_SOURCE_DIR}/io/include ${CMAKE_CURRENT_SOURCE_DIR}/codegen/include)
    if(BUILD_TESTING)
        set_target_properties(windows_dependency_diagnostics_tests PROPERTIES EXCLUDE_FROM_ALL FALSE)
        add_test(NAME windows_dependency_diagnostics COMMAND windows_dependency_diagnostics_tests)
    endif()

    add_executable(amd64_only_windows_tests EXCLUDE_FROM_ALL
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/tests/Amd64OnlyWindowsTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsTrampolineBuilder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsLoadImage.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsStubEmitter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsPeFormat.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/windows/WindowsPeWriter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/io/src/BufferUtils.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/Amd64OnlyConverter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/InstructionScanner.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/X64InstructionDecoder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/X64InstructionRewriter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/DecodedInstruction.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/Amd64OnlyInstructionMatcher.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/Sse4aOperands.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/Sse4aLowering.cpp
    )
    target_include_directories(amd64_only_windows_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/include ${CMAKE_CURRENT_SOURCE_DIR}/domain/include ${CMAKE_CURRENT_SOURCE_DIR}/io/include ${CMAKE_CURRENT_SOURCE_DIR}/codegen/include ${CMAKE_CURRENT_SOURCE_DIR}/relinker/include)
    if(BUILD_TESTING)
        set_target_properties(amd64_only_windows_tests PROPERTIES EXCLUDE_FROM_ALL FALSE)
        add_test(NAME amd64_only_windows COMMAND amd64_only_windows_tests)
    endif()
endif()


if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME optional_plt COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/tests/test_optional_plt.py $<TARGET_FILE:relinker>)
    add_test(NAME empty_tls COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/tests/test_empty_tls.py $<TARGET_FILE:relinker>)
    add_test(NAME tls_function_coverage COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/tests/test_tls_function_coverage.py $<TARGET_FILE:relinker>)
    add_test(NAME linux_load_alignment COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/tests/test_linux_load_alignment.py $<TARGET_FILE:relinker>)
    add_test(NAME windows_gui COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/tests/test_windows_gui.py $<TARGET_FILE:relinker>)
    add_test(NAME guest_intel_trampolines COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/tests/test_guest_intel_trampolines.py $<TARGET_FILE:relinker>)

    add_executable(amd64_only_converter_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/tests/Amd64OnlyConverterTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/Amd64OnlyConverter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/InstructionScanner.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/X64InstructionDecoder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/X64InstructionRewriter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/DecodedInstruction.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/Amd64OnlyInstructionMatcher.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/Sse4aOperands.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/Sse4aLowering.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/linux/LinuxElfPatcher.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/general/SegmentFilter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/general/EntryStubBuilder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/general/ProgramHeaderLayoutBuilder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/src/general/SectionHeaderTableBuilder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/io/src/ByteWriter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/io/src/BufferUtils.cpp
    )
    target_include_directories(amd64_only_converter_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/domain/include
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/include
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/include
        ${CMAKE_CURRENT_SOURCE_DIR}/io/include
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/include
    )
    add_test(NAME amd64_only_converter COMMAND amd64_only_converter_tests)

    add_executable(codemap_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/tests/CodeMapTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/CodeMap.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/CodeInstructionCollector.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/SyscallScanner.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/UnusedNidFilter/EhFrameReader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/UnusedNidFilter/ControlFlowGraph.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/UnusedNidFilter/EntryPointCollector.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/UnusedNidFilter/GotAccessIndex.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/UnusedNidFilter/RelativeRelocationIndex.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/parsing/ElfReader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/Amd64OnlyConverter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/InstructionScanner.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/X64InstructionDecoder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/X64InstructionRewriter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/DecodedInstruction.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/Amd64OnlyInstructionMatcher.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/Sse4aOperands.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/Sse4aLowering.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/io/src/BufferUtils.cpp
    )
    target_include_directories(codemap_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/domain/include
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/include
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/include
        ${CMAKE_CURRENT_SOURCE_DIR}/io/include
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/include
    )
    add_test(NAME codemap COMMAND codemap_tests)

    add_executable(strict_nid_filter_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/tests/StrictNidFilterTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/UnusedNidFilter/StrictNidFilter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/UnusedNidFilter/StrictReachability.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/UnusedNidFilter/EhFrameReader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/UnusedNidFilter/PltCompactor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/parsing/ElfReader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/output/SysVDynamicSectionBuilder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/src/x86/X64InstructionDecoder.cpp
    )
    target_sources(strict_nid_filter_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/UnusedNidFilter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/UnusedNidFilter/ControlFlowGraph.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/UnusedNidFilter/EntryPointCollector.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/UnusedNidFilter/GotAccessIndex.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/UnusedNidFilter/RelativeRelocationIndex.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/CodeMap.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/src/analysis/CodeInstructionCollector.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/io/src/BufferUtils.cpp
    )
    target_include_directories(strict_nid_filter_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/domain/include
        ${CMAKE_CURRENT_SOURCE_DIR}/codegen/include
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/include
        ${CMAKE_CURRENT_SOURCE_DIR}/elfpatcher/include
        ${CMAKE_CURRENT_SOURCE_DIR}/io/include
    )
    add_test(NAME strict_nid_filter COMMAND strict_nid_filter_tests)

    # Relinker hardening regression tests (GoogleTest). Each target compiles only
    # the translation units it exercises, like the legacy runners above.
    set(RELINKER_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    portps5_add_gtest(relinker_windows_config_startup_tests
        SOURCES
            ${RELINKER_DIR}/elfpatcher/tests/WindowsConfigStartupTests.cpp
            ${RELINKER_DIR}/elfpatcher/src/windows/WindowsEntryStubBuilder.cpp
            ${RELINKER_DIR}/elfpatcher/src/windows/WindowsGuestStartup.cpp
            ${RELINKER_DIR}/elfpatcher/src/windows/WindowsDependencyStubBuilder.cpp
            ${RELINKER_DIR}/elfpatcher/src/windows/WindowsStubEmitter.cpp
            ${RELINKER_DIR}/elfpatcher/src/windows/WindowsImportBuilder.cpp
            ${RELINKER_DIR}/elfpatcher/src/windows/WindowsPeFormat.cpp
            ${RELINKER_DIR}/io/src/BufferUtils.cpp
        INCLUDES
            ${RELINKER_DIR}/elfpatcher/include
            ${RELINKER_DIR}/domain/include
            ${RELINKER_DIR}/io/include
            ${RELINKER_DIR}/codegen/include
    )
    portps5_add_gtest(relinker_elf_reader_bounds_tests
        SOURCES
            ${RELINKER_DIR}/relinker/tests/ElfReaderBoundsTests.cpp
            ${RELINKER_DIR}/relinker/src/parsing/ElfReader.cpp
        INCLUDES
            ${RELINKER_DIR}/domain/include
            ${RELINKER_DIR}/relinker/include
            ${RELINKER_DIR}/io/include
    )
    portps5_add_gtest(relinker_buffer_bounds_tests
        SOURCES
            ${RELINKER_DIR}/io/tests/BufferBoundsTests.cpp
            ${RELINKER_DIR}/io/src/ByteReader.cpp
            ${RELINKER_DIR}/io/src/BufferUtils.cpp
        INCLUDES ${RELINKER_DIR}/io/include
    )
    portps5_add_gtest(relinker_file_writer_tests
        SOURCES
            ${RELINKER_DIR}/io/tests/FileWriterTests.cpp
            ${RELINKER_DIR}/io/src/FileWriter.cpp
        INCLUDES ${RELINKER_DIR}/domain/include ${RELINKER_DIR}/io/include
    )
    portps5_add_gtest(relinker_x64_decoder_emms_tests
        SOURCES
            ${RELINKER_DIR}/codegen/tests/X64DecoderEmmsTests.cpp
            ${RELINKER_DIR}/codegen/src/x86/X64InstructionDecoder.cpp
        INCLUDES ${RELINKER_DIR}/codegen/include ${RELINKER_DIR}/domain/include
    )
    portps5_add_gtest(relinker_linux_guest_module_writer_tests
        SOURCES
            ${RELINKER_DIR}/elfpatcher/tests/LinuxGuestModuleWriterTests.cpp
            ${RELINKER_DIR}/elfpatcher/src/linux/LinuxGuestModuleWriter.cpp
            ${RELINKER_DIR}/io/src/BufferUtils.cpp
        INCLUDES
            ${RELINKER_DIR}/elfpatcher/include
            ${RELINKER_DIR}/relinker/include
            ${RELINKER_DIR}/domain/include
            ${RELINKER_DIR}/codegen/include
            ${RELINKER_DIR}/io/include
    )
    portps5_add_gtest(relinker_sse4a_lowering_tests
        SOURCES
            ${RELINKER_DIR}/codegen/tests/Sse4aRegisterFormTests.cpp
            ${RELINKER_DIR}/codegen/tests/Sse4aImmediateFormTests.cpp
            ${RELINKER_DIR}/codegen/tests/Sse4aExecutionHarness.hpp
            ${RELINKER_DIR}/codegen/src/x86/Amd64OnlyInstructionMatcher.cpp
            ${RELINKER_DIR}/codegen/src/x86/DecodedInstruction.cpp
            ${RELINKER_DIR}/codegen/src/x86/Sse4aOperands.cpp
            ${RELINKER_DIR}/codegen/src/x86/Sse4aLowering.cpp
        INCLUDES ${RELINKER_DIR}/codegen/include ${RELINKER_DIR}/domain/include
    )
    portps5_add_gtest(relinker_amd64_only_short_site_tests
        SOURCES
            ${RELINKER_DIR}/codegen/tests/Amd64OnlyShortSiteTests.cpp
            ${RELINKER_DIR}/codegen/src/Amd64OnlyConverter.cpp
            ${RELINKER_DIR}/codegen/src/x86/X64InstructionDecoder.cpp
            ${RELINKER_DIR}/codegen/src/x86/DecodedInstruction.cpp
            ${RELINKER_DIR}/codegen/src/x86/Amd64OnlyInstructionMatcher.cpp
            ${RELINKER_DIR}/codegen/src/x86/Sse4aOperands.cpp
            ${RELINKER_DIR}/codegen/src/x86/Sse4aLowering.cpp
        INCLUDES ${RELINKER_DIR}/codegen/include ${RELINKER_DIR}/domain/include
    )
    if(WIN32)
        portps5_add_gtest(relinker_windows_short_site_tests
            SOURCES
                ${RELINKER_DIR}/elfpatcher/tests/WindowsShortSiteTests.cpp
                ${RELINKER_DIR}/elfpatcher/src/windows/WindowsTrampolineBuilder.cpp
                ${RELINKER_DIR}/elfpatcher/src/windows/WindowsLoadImage.cpp
                ${RELINKER_DIR}/elfpatcher/src/windows/WindowsStubEmitter.cpp
                ${RELINKER_DIR}/elfpatcher/src/windows/WindowsPeFormat.cpp
                ${RELINKER_DIR}/io/src/BufferUtils.cpp
                ${RELINKER_DIR}/codegen/src/Amd64OnlyConverter.cpp
                ${RELINKER_DIR}/codegen/src/x86/X64InstructionDecoder.cpp
                ${RELINKER_DIR}/codegen/src/x86/DecodedInstruction.cpp
                ${RELINKER_DIR}/codegen/src/x86/Amd64OnlyInstructionMatcher.cpp
                ${RELINKER_DIR}/codegen/src/x86/Sse4aOperands.cpp
                ${RELINKER_DIR}/codegen/src/x86/Sse4aLowering.cpp
            INCLUDES
                ${RELINKER_DIR}/elfpatcher/include
                ${RELINKER_DIR}/domain/include
                ${RELINKER_DIR}/io/include
                ${RELINKER_DIR}/codegen/include
                ${RELINKER_DIR}/relinker/include
        )
    endif()
    add_test(NAME windows_lazy_got COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/relinker/tests/test_windows_lazy_got.py $<TARGET_FILE:relinker>)

    get_property(ALL_RELINKER_TESTS DIRECTORY PROPERTY TESTS)
    set_property(TEST ${ALL_RELINKER_TESTS} PROPERTY LABELS "unit")
endif()
