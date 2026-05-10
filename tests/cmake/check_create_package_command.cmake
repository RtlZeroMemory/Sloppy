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
set(default_project_name "created-api-default")
set(default_project_dir "${work_dir}/${default_project_name}")
set(placeholder_work_dir "${work_dir}/placeholder-template")
set(placeholder_project_name "created-api-placeholder")
set(placeholder_project_dir "${placeholder_work_dir}/${placeholder_project_name}")
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
if(NOT EXISTS "${project_dir}/.gitignore")
    message(FATAL_ERROR "sloppy create --no-git did not copy .gitignore")
endif()

execute_process(
    COMMAND "${SLOPPY_CLI}" create "bad name" --template minimal-api
    WORKING_DIRECTORY "${work_dir}"
    TIMEOUT 60
    RESULT_VARIABLE invalid_name_create_result
    OUTPUT_VARIABLE invalid_name_create_stdout
    ERROR_VARIABLE invalid_name_create_stderr)

if(invalid_name_create_result EQUAL 0)
    message(FATAL_ERROR "sloppy create unexpectedly accepted an invalid project name")
endif()
if(NOT invalid_name_create_stderr MATCHES "project name")
    message(FATAL_ERROR "sloppy create invalid-name failure did not explain project naming\nstdout:\n${invalid_name_create_stdout}\nstderr:\n${invalid_name_create_stderr}")
endif()

execute_process(
    COMMAND "${SLOPPY_CLI}" create "missing-template" --template definitely-missing
    WORKING_DIRECTORY "${work_dir}"
    TIMEOUT 60
    RESULT_VARIABLE missing_template_create_result
    OUTPUT_VARIABLE missing_template_create_stdout
    ERROR_VARIABLE missing_template_create_stderr)

if(missing_template_create_result EQUAL 0)
    message(FATAL_ERROR "sloppy create unexpectedly accepted a missing template")
endif()
if(NOT missing_template_create_stderr MATCHES "unsupported template|template not found")
    message(FATAL_ERROR "sloppy create missing-template failure did not explain template lookup\nstdout:\n${missing_template_create_stdout}\nstderr:\n${missing_template_create_stderr}")
endif()

execute_process(
    COMMAND "${SLOPPY_CLI}" create "${default_project_name}" --template minimal-api --format json
    WORKING_DIRECTORY "${work_dir}"
    TIMEOUT 60
    RESULT_VARIABLE default_create_result
    OUTPUT_VARIABLE default_create_stdout
    ERROR_VARIABLE default_create_stderr)

if(NOT default_create_result EQUAL 0)
    message(FATAL_ERROR "sloppy create without --no-git failed\nstdout:\n${default_create_stdout}\nstderr:\n${default_create_stderr}")
endif()
if(NOT EXISTS "${default_project_dir}/.gitignore")
    message(FATAL_ERROR "sloppy create without --no-git did not copy .gitignore")
endif()

file(MAKE_DIRECTORY "${placeholder_work_dir}")
file(COPY "${PROJECT_SOURCE_DIR}/templates" DESTINATION "${placeholder_work_dir}")
file(RENAME
    "${placeholder_work_dir}/templates/minimal-api/.gitignore"
    "${placeholder_work_dir}/templates/minimal-api/gitignore")
execute_process(
    COMMAND "${SLOPPY_CLI}" create "${placeholder_project_name}" --template minimal-api --format json
    WORKING_DIRECTORY "${placeholder_work_dir}"
    TIMEOUT 60
    RESULT_VARIABLE placeholder_create_result
    OUTPUT_VARIABLE placeholder_create_stdout
    ERROR_VARIABLE placeholder_create_stderr)

if(NOT placeholder_create_result EQUAL 0)
    message(FATAL_ERROR "sloppy create with npm gitignore placeholder failed\nstdout:\n${placeholder_create_stdout}\nstderr:\n${placeholder_create_stderr}")
endif()
if(NOT EXISTS "${placeholder_project_dir}/.gitignore")
    message(FATAL_ERROR "sloppy create did not restore npm gitignore placeholder as .gitignore")
endif()
if(EXISTS "${placeholder_project_dir}/gitignore")
    message(FATAL_ERROR "sloppy create leaked npm gitignore placeholder as gitignore")
endif()

set(occupied_file_name "occupied-file")
file(WRITE "${work_dir}/${occupied_file_name}" "already here\n")
execute_process(
    COMMAND "${SLOPPY_CLI}" create "${occupied_file_name}" --template minimal-api
    WORKING_DIRECTORY "${work_dir}"
    TIMEOUT 60
    RESULT_VARIABLE occupied_create_result
    OUTPUT_VARIABLE occupied_create_stdout
    ERROR_VARIABLE occupied_create_stderr)

if(occupied_create_result EQUAL 0)
    message(FATAL_ERROR "sloppy create unexpectedly succeeded for an existing file destination")
endif()
if(NOT occupied_create_stderr MATCHES "destination exists")
    message(FATAL_ERROR "sloppy create existing-file failure was not a destination diagnostic\nstdout:\n${occupied_create_stdout}\nstderr:\n${occupied_create_stderr}")
endif()

set(empty_dir_project_name "created-empty-dir")
set(empty_dir_project_dir "${work_dir}/${empty_dir_project_name}")
file(MAKE_DIRECTORY "${empty_dir_project_dir}")
execute_process(
    COMMAND "${SLOPPY_CLI}" create "${empty_dir_project_name}" --template minimal-api --no-git --format json
    WORKING_DIRECTORY "${work_dir}"
    TIMEOUT 60
    RESULT_VARIABLE empty_dir_create_result
    OUTPUT_VARIABLE empty_dir_create_stdout
    ERROR_VARIABLE empty_dir_create_stderr)

if(NOT empty_dir_create_result EQUAL 0)
    message(FATAL_ERROR "sloppy create failed for an existing empty directory\nstdout:\n${empty_dir_create_stdout}\nstderr:\n${empty_dir_create_stderr}")
endif()
if(NOT EXISTS "${empty_dir_project_dir}/README.md")
    message(FATAL_ERROR "sloppy create did not populate existing empty directory")
endif()

set(non_empty_project_name "created-non-empty")
set(non_empty_project_dir "${work_dir}/${non_empty_project_name}")
file(MAKE_DIRECTORY "${non_empty_project_dir}")
file(WRITE "${non_empty_project_dir}/stale.txt" "stale file\n")
execute_process(
    COMMAND "${SLOPPY_CLI}" create "${non_empty_project_name}" --template minimal-api
    WORKING_DIRECTORY "${work_dir}"
    TIMEOUT 60
    RESULT_VARIABLE non_empty_create_result
    OUTPUT_VARIABLE non_empty_create_stdout
    ERROR_VARIABLE non_empty_create_stderr)

if(non_empty_create_result EQUAL 0)
    message(FATAL_ERROR "sloppy create unexpectedly overwrote a non-empty directory without --force")
endif()
if(NOT non_empty_create_stderr MATCHES "destination exists")
    message(FATAL_ERROR "sloppy create non-empty-directory failure did not report destination state\nstdout:\n${non_empty_create_stdout}\nstderr:\n${non_empty_create_stderr}")
endif()

execute_process(
    COMMAND "${SLOPPY_CLI}" create "${non_empty_project_name}" --template minimal-api --force --no-git --format json
    WORKING_DIRECTORY "${work_dir}"
    TIMEOUT 60
    RESULT_VARIABLE force_create_result
    OUTPUT_VARIABLE force_create_stdout
    ERROR_VARIABLE force_create_stderr)

if(NOT force_create_result EQUAL 0)
    message(FATAL_ERROR "sloppy create --force failed for a non-empty directory\nstdout:\n${force_create_stdout}\nstderr:\n${force_create_stderr}")
endif()
if(NOT EXISTS "${non_empty_project_dir}/README.md")
    message(FATAL_ERROR "sloppy create --force did not copy known template files")
endif()
if(NOT EXISTS "${non_empty_project_dir}/stale.txt")
    message(FATAL_ERROR "sloppy create --force deleted stale files despite the documented contract")
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

file(MAKE_DIRECTORY "${project_dir}/.sloppy/package")
file(WRITE "${project_dir}/.sloppy/package/stale.txt" "stale package output\n")

function(run_artifacts_metadata description expected_pattern)
    execute_process(
        COMMAND "${SLOPPY_CLI}" ${ARGN} --artifacts .sloppy
        WORKING_DIRECTORY "${project_dir}"
        TIMEOUT 60
        RESULT_VARIABLE metadata_result
        OUTPUT_VARIABLE metadata_stdout
        ERROR_VARIABLE metadata_stderr)

    if(NOT metadata_result EQUAL 0)
        message(FATAL_ERROR "${description} failed for created project\nstdout:\n${metadata_stdout}\nstderr:\n${metadata_stderr}")
    endif()
    if(NOT metadata_stdout MATCHES "${expected_pattern}")
        message(FATAL_ERROR "${description} did not include expected output\nstdout:\n${metadata_stdout}")
    endif()
endfunction()

run_artifacts_metadata("routes --artifacts" "\"/health\"" routes --format json)
run_artifacts_metadata("openapi --artifacts" "\"/health\"" openapi)
run_artifacts_metadata("capabilities --artifacts" "\"capabilities\"" capabilities --format json)
run_artifacts_metadata("audit --artifacts" "\"findings\"" audit --format json)
run_artifacts_metadata("doctor --artifacts" "route metadata present" doctor --format text)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_SLOPPYC=${SLOPPYC_EXECUTABLE}" "${SLOPPY_CLI}" package
            --out custom-out --format json
    WORKING_DIRECTORY "${project_dir}"
    TIMEOUT 60
    RESULT_VARIABLE project_out_package_result
    OUTPUT_VARIABLE project_out_package_stdout
    ERROR_VARIABLE project_out_package_stderr)

if(project_out_package_result EQUAL 0)
    message(FATAL_ERROR "sloppy package unexpectedly accepted --out in project mode")
endif()
if(NOT project_out_package_stderr MATCHES "--out applies to positional source input")
    message(FATAL_ERROR "sloppy package project-mode --out failure did not explain the contract\nstdout:\n${project_out_package_stdout}\nstderr:\n${project_out_package_stderr}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_SLOPPYC=${SLOPPYC_EXECUTABLE}" "${SLOPPY_CLI}" package
            README.md --format json
    WORKING_DIRECTORY "${project_dir}"
    TIMEOUT 60
    RESULT_VARIABLE unsupported_source_package_result
    OUTPUT_VARIABLE unsupported_source_package_stdout
    ERROR_VARIABLE unsupported_source_package_stderr)

if(unsupported_source_package_result EQUAL 0)
    message(FATAL_ERROR "sloppy package unexpectedly accepted an unsupported source path")
endif()
if(NOT unsupported_source_package_stderr MATCHES "unsupported source input")
    message(FATAL_ERROR "sloppy package unsupported-source failure did not explain the input contract\nstdout:\n${unsupported_source_package_stdout}\nstderr:\n${unsupported_source_package_stderr}")
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
if(EXISTS "${project_dir}/.sloppy/package/stale.txt")
    message(FATAL_ERROR "sloppy package did not remove stale package output")
endif()
file(READ "${project_dir}/.sloppy/package/manifest.json" manifest_json)
if(NOT manifest_json MATCHES "sloppy.app-package.v1")
    message(FATAL_ERROR "sloppy package manifest is missing schema\n${manifest_json}")
endif()
