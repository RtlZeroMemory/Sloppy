if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()
if(NOT DEFINED CMAKE_BINARY_DIR)
    message(FATAL_ERROR "CMAKE_BINARY_DIR is required")
endif()
if(NOT DEFINED SLOPPY_CLI)
    message(FATAL_ERROR "SLOPPY_CLI is required")
endif()
if(NOT DEFINED SLOPPYC_EXECUTABLE)
    message(FATAL_ERROR "SLOPPYC_EXECUTABLE is required")
endif()
if(NOT DEFINED SLOPPY_EXPECT_RUN_SUCCESS)
    set(SLOPPY_EXPECT_RUN_SUCCESS OFF)
endif()

set(work_dir "${CMAKE_BINARY_DIR}/npm-compat-representatives")
set(fixtures_root "${PROJECT_SOURCE_DIR}/tests/fixtures/npm-compat")
file(REMOVE_RECURSE "${work_dir}")
file(MAKE_DIRECTORY "${work_dir}")

set(representatives
    basic-main-cjs
    basic-main-esm
    exports-nested-conditions
    imports-alias
    self-reference
    interop-cjs-requires-json
    builtins-fs-promises-buffer
    optional-native-unused
    dynamic-require-literal)

function(run_sloppy fixture command)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_SLOPPYC=${SLOPPYC_EXECUTABLE}"
                "${SLOPPY_CLI}" ${command} ${ARGN}
        WORKING_DIRECTORY "${work_dir}/${fixture}"
        TIMEOUT 120
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(NOT result EQUAL 0)
        message(
            FATAL_ERROR
                "${fixture}: sloppy ${command} ${ARGN} failed (${result})\nstdout:\n${stdout}\nstderr:\n${stderr}"
        )
    endif()
    set(${fixture}_${command}_stdout "${stdout}" PARENT_SCOPE)
endfunction()

foreach(fixture IN LISTS representatives)
    set(source_dir "${fixtures_root}/${fixture}")
    set(target_dir "${work_dir}/${fixture}")
    file(COPY "${source_dir}/" DESTINATION "${target_dir}")

    if(NOT EXISTS "${target_dir}/sloppy.json")
        message(FATAL_ERROR "${fixture}: sloppy.json missing from representative fixture")
    endif()
    if(NOT EXISTS "${target_dir}/src/main.ts")
        message(FATAL_ERROR "${fixture}: src/main.ts missing from representative fixture")
    endif()

    run_sloppy("${fixture}" build)
    foreach(expected IN ITEMS app.js app.js.map app.plan.json deps.graph.json)
        if(NOT EXISTS "${target_dir}/.sloppy/${expected}")
            message(FATAL_ERROR "${fixture}: build did not emit ${expected}")
        endif()
    endforeach()

    run_sloppy("${fixture}" package)
    foreach(expected IN ITEMS artifacts/app.js artifacts/deps.graph.json manifest.json)
        if(NOT EXISTS "${target_dir}/.sloppy/package/${expected}")
            message(FATAL_ERROR "${fixture}: package did not emit ${expected}")
        endif()
    endforeach()

    file(GLOB_RECURSE leaked_node_modules
         "${target_dir}/.sloppy/package/**/node_modules")
    if(leaked_node_modules)
        message(
            FATAL_ERROR
                "${fixture}: package contains node_modules paths: ${leaked_node_modules}")
    endif()

    # Validate that the package can run outside the source checkout by copying
    # it to a sibling location. Execution under V8 is gated on
    # SLOPPY_EXPECT_RUN_SUCCESS so the test still exercises the relocatable
    # artifact when V8 is disabled.
    set(outside_dir "${work_dir}/${fixture}-outside")
    file(REMOVE_RECURSE "${outside_dir}")
    file(MAKE_DIRECTORY "${outside_dir}")
    file(COPY "${target_dir}/.sloppy/package/" DESTINATION "${outside_dir}/package")

    if(SLOPPY_EXPECT_RUN_SUCCESS)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_SLOPPYC=${SLOPPYC_EXECUTABLE}"
                    "${SLOPPY_CLI}" run "${outside_dir}/package" --once
            TIMEOUT 60
            RESULT_VARIABLE run_result
            OUTPUT_VARIABLE run_stdout
            ERROR_VARIABLE run_stderr)
        if(NOT run_result EQUAL 0)
            message(
                FATAL_ERROR
                    "${fixture}: outside-checkout run failed (${run_result})\nstdout:\n${run_stdout}\nstderr:\n${run_stderr}"
            )
        endif()
    endif()
endforeach()

message(STATUS "npm-compat representative fixtures: build + package + outside-checkout copy OK for ${representatives}")
