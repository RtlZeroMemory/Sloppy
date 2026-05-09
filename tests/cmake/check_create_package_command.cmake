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

set(work_dir "${CMAKE_BINARY_DIR}/create-package-command")
set(project_name "created-api")
set(project_dir "${work_dir}/${project_name}")
file(REMOVE_RECURSE "${work_dir}")
file(MAKE_DIRECTORY "${work_dir}")
file(COPY "${PROJECT_SOURCE_DIR}/templates" DESTINATION "${work_dir}")

execute_process(
    COMMAND "${SLOPPY_CLI}" create "${project_name}" --template minimal-api --no-git --format json
    WORKING_DIRECTORY "${work_dir}"
    TIMEOUT 60
    RESULT_VARIABLE create_result
    OUTPUT_VARIABLE create_stdout
    ERROR_VARIABLE create_stderr)

if(NOT create_result EQUAL 0)
    message(FATAL_ERROR "sloppy create failed\nstdout:\n${create_stdout}\nstderr:\n${create_stderr}")
endif()
if(NOT create_stdout MATCHES "\"created\":true")
    message(FATAL_ERROR "sloppy create did not report JSON success\nstdout:\n${create_stdout}")
endif()
foreach(expected IN ITEMS README.md sloppy.json appsettings.json src/main.ts)
    if(NOT EXISTS "${project_dir}/${expected}")
        message(FATAL_ERROR "sloppy create did not copy ${expected}")
    endif()
endforeach()
if(EXISTS "${project_dir}/.gitignore")
    message(FATAL_ERROR "sloppy create --no-git copied .gitignore")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_SLOPPYC=${SLOPPYC_EXECUTABLE}" "${SLOPPY_CLI}" build
    WORKING_DIRECTORY "${project_dir}"
    TIMEOUT 180
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr)

if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "created project build failed\nstdout:\n${build_stdout}\nstderr:\n${build_stderr}")
endif()
foreach(artifact IN ITEMS app.plan.json app.js app.js.map)
    if(NOT EXISTS "${project_dir}/.sloppy/${artifact}")
        message(FATAL_ERROR "created project build did not emit .sloppy/${artifact}")
    endif()
endforeach()

execute_process(
    COMMAND "${SLOPPY_CLI}" openapi --artifacts .sloppy
    WORKING_DIRECTORY "${project_dir}"
    TIMEOUT 60
    RESULT_VARIABLE openapi_result
    OUTPUT_VARIABLE openapi_stdout
    ERROR_VARIABLE openapi_stderr)

if(NOT openapi_result EQUAL 0)
    message(FATAL_ERROR "openapi --artifacts failed for created project\nstdout:\n${openapi_stdout}\nstderr:\n${openapi_stderr}")
endif()
if(NOT openapi_stdout MATCHES "\"/health\"")
    message(FATAL_ERROR "openapi --artifacts did not include created project routes\nstdout:\n${openapi_stdout}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_SLOPPYC=${SLOPPYC_EXECUTABLE}" "${SLOPPY_CLI}" package
            --format json
    WORKING_DIRECTORY "${project_dir}"
    TIMEOUT 180
    RESULT_VARIABLE package_result
    OUTPUT_VARIABLE package_stdout
    ERROR_VARIABLE package_stderr)

if(NOT package_result EQUAL 0)
    message(FATAL_ERROR "created project package failed\nstdout:\n${package_stdout}\nstderr:\n${package_stderr}")
endif()
if(NOT package_stdout MATCHES "\"packaged\":true")
    message(FATAL_ERROR "sloppy package did not report JSON success\nstdout:\n${package_stdout}")
endif()
foreach(packaged IN ITEMS manifest.json artifacts/app.plan.json artifacts/app.js artifacts/app.js.map)
    if(NOT EXISTS "${project_dir}/.sloppy/package/${packaged}")
        message(FATAL_ERROR "sloppy package did not emit ${packaged}")
    endif()
endforeach()
file(READ "${project_dir}/.sloppy/package/manifest.json" manifest_json)
if(NOT manifest_json MATCHES "sloppy.app-package.v1")
    message(FATAL_ERROR "sloppy package manifest is missing schema\n${manifest_json}")
endif()
