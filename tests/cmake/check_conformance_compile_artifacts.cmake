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

set(source_path "${PROJECT_SOURCE_DIR}/${SLOPPY_SOURCE}")
set(output_root "${CMAKE_BINARY_DIR}/conformance/compile-artifacts/${SLOPPY_CONFORMANCE_CASE}")
set(output_a "${output_root}/out-a")
set(output_b "${output_root}/out-b")

file(REMOVE_RECURSE "${output_root}")
file(MAKE_DIRECTORY "${output_root}")

foreach(output_dir IN ITEMS "${output_a}" "${output_b}")
    execute_process(
        COMMAND "${CARGO_EXECUTABLE}" run --manifest-path
                "${PROJECT_SOURCE_DIR}/compiler/Cargo.toml" -- build "${source_path}" --out
                "${output_dir}"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_stdout
        ERROR_VARIABLE build_stderr)
    if(NOT build_result EQUAL 0)
        message(
            FATAL_ERROR
                "Conformance build failed for ${SLOPPY_CONFORMANCE_CASE}\nstdout:\n${build_stdout}\nstderr:\n${build_stderr}")
    endif()
endforeach()

foreach(artifact IN ITEMS app.plan.json app.js app.js.map)
    set(artifact_a "${output_a}/${artifact}")
    set(artifact_b "${output_b}/${artifact}")
    if(NOT EXISTS "${artifact_a}")
        message(FATAL_ERROR "Missing conformance artifact: ${artifact_a}")
    endif()
    if(NOT EXISTS "${artifact_b}")
        message(FATAL_ERROR "Missing repeated conformance artifact: ${artifact_b}")
    endif()

    file(SHA256 "${artifact_a}" hash_a)
    file(SHA256 "${artifact_b}" hash_b)
    if(NOT hash_a STREQUAL hash_b)
        message(FATAL_ERROR "Conformance artifact is not deterministic: ${artifact}")
    endif()

    file(READ "${artifact_a}" artifact_text)
    string(FIND "${artifact_text}" "${PROJECT_SOURCE_DIR}" checkout_path_index)
    if(NOT checkout_path_index EQUAL -1)
        message(FATAL_ERROR "Conformance artifact contains checkout-local path: ${artifact}")
    endif()
endforeach()
