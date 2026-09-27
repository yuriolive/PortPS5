# Helper function for registering PortPS5 test targets into ctest
# Handles EXCLUDE_FROM_ALL, libc PATH prepend on Windows, and labels

function(portps5_add_test test_name target)
    cmake_parse_arguments(ARG "" "" "LABELS;ARGS;PASS_REGULAR_EXPRESSION" ${ARGN})

    if(NOT BUILD_TESTING)
        return()
    endif()

    if(TARGET ${target})
        set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL OFF)
        if(ARG_ARGS)
            add_test(NAME ${test_name} COMMAND ${target} ${ARG_ARGS})
        else()
            add_test(NAME ${test_name} COMMAND ${target})
        endif()
    else()
        if(ARG_ARGS)
            add_test(NAME ${test_name} COMMAND ${target} ${ARG_ARGS})
        else()
            add_test(NAME ${test_name} COMMAND ${target})
        endif()
    endif()

    if(WIN32 AND TARGET libc)
        set_tests_properties(${test_name} PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()

    if(ARG_PASS_REGULAR_EXPRESSION)
        set_tests_properties(${test_name} PROPERTIES
            PASS_REGULAR_EXPRESSION "${ARG_PASS_REGULAR_EXPRESSION}")
    endif()

    if(ARG_LABELS)
        set_tests_properties(${test_name} PROPERTIES LABELS "${ARG_LABELS}")
    else()
        set_tests_properties(${test_name} PROPERTIES LABELS "unit")
    endif()
endfunction()
