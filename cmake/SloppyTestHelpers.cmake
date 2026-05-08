# Test helper setup and shared test-registration functions. Included by cmake/SloppyTests.cmake.

    include(CTest)
    find_program(SLOPPY_POWERSHELL_EXECUTABLE NAMES pwsh powershell powershell.exe)
    if(SLOPPY_POWERSHELL_EXECUTABLE)
        set(SLOPPY_POWERSHELL_ARGS -NoProfile)
        if(WIN32)
            list(APPEND SLOPPY_POWERSHELL_ARGS -ExecutionPolicy Bypass)
        endif()
        list(APPEND SLOPPY_POWERSHELL_ARGS -File)
    endif()

    function(sloppy_add_c_unit_test target_name test_name source_file)
        add_executable(${target_name} ${source_file})
        target_link_libraries(${target_name} PRIVATE sloppy_core)
        target_include_directories(${target_name} PRIVATE "${PROJECT_SOURCE_DIR}/include")
        target_compile_features(${target_name} PRIVATE c_std_17)
        sloppy_apply_warnings(${target_name})
        sloppy_apply_sanitizers(${target_name})
        add_test(NAME ${test_name} COMMAND ${target_name})
    endfunction()

    function(sloppy_add_cxx_unit_test target_name test_name source_file)
        add_executable(${target_name} ${source_file})
        target_link_libraries(${target_name} PRIVATE sloppy_core)
        target_include_directories(${target_name} PRIVATE "${PROJECT_SOURCE_DIR}/include")
        target_compile_features(${target_name} PRIVATE cxx_std_17)
        sloppy_apply_warnings(${target_name})
        sloppy_apply_sanitizers(${target_name})
        add_test(NAME ${test_name} COMMAND ${target_name})
    endfunction()

    function(sloppy_add_fuzz_seed_replay target_name test_name source_file corpus_name)
        file(
            GLOB
            seed_files
            CONFIGURE_DEPENDS
            "${PROJECT_SOURCE_DIR}/tests/fuzz/corpus/${corpus_name}/*")
        if(NOT seed_files)
            message(FATAL_ERROR "fuzz seed corpus '${corpus_name}' has no committed seeds")
        endif()

        add_executable(${target_name} ${source_file})
        target_link_libraries(${target_name} PRIVATE sloppy_core)
        target_include_directories(
            ${target_name} PRIVATE "${PROJECT_SOURCE_DIR}/include"
                                   "${PROJECT_SOURCE_DIR}/tests/fuzz")
        target_compile_features(${target_name} PRIVATE c_std_17)
        target_compile_definitions(${target_name} PRIVATE SLOPPY_FUZZ_STANDALONE=1)
        sloppy_apply_warnings(${target_name})
        sloppy_apply_sanitizers(${target_name})
        add_test(NAME ${test_name} COMMAND ${target_name} ${seed_files})
        set_tests_properties(
            ${test_name}
            PROPERTIES LABELS "fuzz;property;seed-replay"
                       WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    endfunction()

    function(sloppy_add_libfuzzer_target target_name source_file)
        if(NOT SLOPPY_ENABLE_LIBFUZZER)
            return()
        endif()

        add_executable(${target_name} ${source_file})
        target_link_libraries(${target_name} PRIVATE sloppy_core)
        target_include_directories(
            ${target_name} PRIVATE "${PROJECT_SOURCE_DIR}/include"
                                   "${PROJECT_SOURCE_DIR}/tests/fuzz")
        target_compile_features(${target_name} PRIVATE c_std_17)
        target_compile_options(${target_name} PRIVATE -fsanitize=fuzzer)
        if(MSVC)
            target_link_libraries(${target_name} PRIVATE "${SLOPPY_CLANG_FUZZER_LIB}")
        else()
            target_link_options(${target_name} PRIVATE -fsanitize=fuzzer)
        endif()
        sloppy_apply_warnings(${target_name})
        sloppy_apply_sanitizers(${target_name})
    endfunction()
