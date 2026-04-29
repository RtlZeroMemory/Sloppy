if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()
if(NOT DEFINED CMAKE_BINARY_DIR)
    message(FATAL_ERROR "CMAKE_BINARY_DIR is required")
endif()
if(NOT DEFINED CARGO_EXECUTABLE)
    message(FATAL_ERROR "CARGO_EXECUTABLE is required")
endif()
if(NOT DEFINED SLOPPY_CONFORMANCE_CASE)
    message(FATAL_ERROR "SLOPPY_CONFORMANCE_CASE is required")
endif()
if(NOT DEFINED SLOPPY_SOURCE)
    message(FATAL_ERROR "SLOPPY_SOURCE is required")
endif()
if(NOT DEFINED SLOPPY_EXPECTED_ERROR)
    message(FATAL_ERROR "SLOPPY_EXPECTED_ERROR is required")
endif()

set(source_path "${PROJECT_SOURCE_DIR}/${SLOPPY_SOURCE}")
set(output_dir "${CMAKE_BINARY_DIR}/conformance/rejected/${SLOPPY_CONFORMANCE_CASE}")

file(REMOVE_RECURSE "${output_dir}")
file(MAKE_DIRECTORY "${output_dir}")

execute_process(
    COMMAND "${CARGO_EXECUTABLE}" run --manifest-path "${PROJECT_SOURCE_DIR}/compiler/Cargo.toml"
            -- build "${source_path}" --out "${output_dir}"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr)

if(build_result EQUAL 0)
    message(FATAL_ERROR "Unsupported conformance source unexpectedly compiled: ${SLOPPY_SOURCE}")
endif()

set(combined_output "${build_stdout}\n${build_stderr}")
string(FIND "${combined_output}" "${SLOPPY_EXPECTED_ERROR}" error_index)
if(error_index EQUAL -1)
    message(
        FATAL_ERROR
            "Rejected conformance source did not report expected error '${SLOPPY_EXPECTED_ERROR}'\nstdout:\n${build_stdout}\nstderr:\n${build_stderr}")
endif()

foreach(artifact IN ITEMS app.plan.json app.js app.js.map)
    if(EXISTS "${output_dir}/${artifact}")
        message(FATAL_ERROR "Rejected conformance source left success artifact: ${artifact}")
    endif()
endforeach()
