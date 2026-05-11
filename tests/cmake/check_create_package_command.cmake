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
if(DEFINED ENV{APPDATA} AND EXISTS "$ENV{APPDATA}/npm/npm.cmd")
    set(SLOPPY_TEST_NPM "$ENV{APPDATA}/npm/npm.cmd")
else()
    find_program(SLOPPY_TEST_NPM NAMES npm.cmd npm)
endif()
find_program(SLOPPY_TEST_NODE NAMES node.exe node)
if(NOT SLOPPY_TEST_NODE AND EXISTS "C:/Program Files/nodejs/node.exe")
    set(SLOPPY_TEST_NODE "C:/Program Files/nodejs/node.exe")
endif()
if(NOT DEFINED SLOPPY_TEST_NPM_CLI AND DEFINED ENV{APPDATA})
    set(SLOPPY_TEST_NPM_CLI_CANDIDATE "$ENV{APPDATA}/npm/node_modules/npm/bin/npm-cli.js")
    if(EXISTS "${SLOPPY_TEST_NPM_CLI_CANDIDATE}")
        set(SLOPPY_TEST_NPM_CLI "${SLOPPY_TEST_NPM_CLI_CANDIDATE}")
    endif()
endif()
if(NOT DEFINED SLOPPY_TEST_NPM_CLI AND SLOPPY_TEST_NPM)
    execute_process(
        COMMAND "${SLOPPY_TEST_NPM}" root -g
        TIMEOUT 30
        RESULT_VARIABLE SLOPPY_TEST_NPM_ROOT_RESULT
        OUTPUT_VARIABLE SLOPPY_TEST_NPM_ROOT
        ERROR_QUIET)
    if(SLOPPY_TEST_NPM_ROOT_RESULT EQUAL 0)
        string(STRIP "${SLOPPY_TEST_NPM_ROOT}" SLOPPY_TEST_NPM_ROOT)
        set(SLOPPY_TEST_NPM_CLI_CANDIDATE "${SLOPPY_TEST_NPM_ROOT}/npm/bin/npm-cli.js")
        if(EXISTS "${SLOPPY_TEST_NPM_CLI_CANDIDATE}")
            set(SLOPPY_TEST_NPM_CLI "${SLOPPY_TEST_NPM_CLI_CANDIDATE}")
        endif()
    endif()
endif()

function(assert_sloppy_command_success label working_dir expected_stdout)
    execute_process(
        COMMAND "${SLOPPY_CLI}" ${ARGN}
        WORKING_DIRECTORY "${working_dir}"
        TIMEOUT 180
        RESULT_VARIABLE command_result
        OUTPUT_VARIABLE command_stdout
        ERROR_VARIABLE command_stderr)
    if(NOT command_result EQUAL 0)
        message(FATAL_ERROR "${label} failed\nstdout:\n${command_stdout}\nstderr:\n${command_stderr}")
    endif()
    if(NOT "${expected_stdout}" STREQUAL "" AND NOT command_stdout MATCHES "${expected_stdout}")
        message(FATAL_ERROR "${label} output did not match ${expected_stdout}\nstdout:\n${command_stdout}\nstderr:\n${command_stderr}")
    endif()
endfunction()

set(work_dir "${CMAKE_BINARY_DIR}/create-package-command")
set(project_name "created-api")
set(project_dir "${work_dir}/${project_name}")
set(default_project_name "created-api-default")
set(default_project_dir "${work_dir}/${default_project_name}")
set(program_project_name "created-program")
set(program_project_dir "${work_dir}/${program_project_name}")
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
foreach(expected IN ITEMS README.md package.json sloppy.json src/main.ts)
    if(NOT EXISTS "${project_dir}/${expected}")
        message(FATAL_ERROR "sloppy create did not copy ${expected}")
    endif()
endforeach()
if(NOT EXISTS "${project_dir}/.gitignore")
    message(FATAL_ERROR "sloppy create --no-git did not copy .gitignore")
endif()

set(invalid_migration_glob_dir "${work_dir}/invalid-migration-glob")
file(MAKE_DIRECTORY "${invalid_migration_glob_dir}")
file(WRITE "${invalid_migration_glob_dir}/sloppy.json" [=[{
  "entry": "src/main.ts",
  "migrations": {
    "main": {
      "provider": "sqlite",
      "path": "migrations/main.sql"
    }
  }
}
]=])
execute_process(
    COMMAND "${SLOPPY_CLI}" package
    WORKING_DIRECTORY "${invalid_migration_glob_dir}"
    TIMEOUT 60
    RESULT_VARIABLE invalid_migration_glob_result
    OUTPUT_VARIABLE invalid_migration_glob_stdout
    ERROR_VARIABLE invalid_migration_glob_stderr)
if(invalid_migration_glob_result EQUAL 0)
    message(FATAL_ERROR "sloppy package unexpectedly accepted a non-glob migration path")
endif()
if(NOT invalid_migration_glob_stderr MATCHES "directory glob ending in \\*\\.sql")
    message(FATAL_ERROR "sloppy package invalid migration path failure did not explain the glob contract\nstdout:\n${invalid_migration_glob_stdout}\nstderr:\n${invalid_migration_glob_stderr}")
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

foreach(removed_template IN ITEMS dogfood full-api)
    execute_process(
        COMMAND "${SLOPPY_CLI}" create "removed-${removed_template}" --template "${removed_template}"
        WORKING_DIRECTORY "${work_dir}"
        TIMEOUT 60
        RESULT_VARIABLE removed_template_create_result
        OUTPUT_VARIABLE removed_template_create_stdout
        ERROR_VARIABLE removed_template_create_stderr)
    if(removed_template_create_result EQUAL 0)
        message(FATAL_ERROR "sloppy create unexpectedly accepted removed template ${removed_template}")
    endif()
    if(NOT removed_template_create_stderr MATCHES "unsupported template")
        message(FATAL_ERROR "sloppy create removed-template failure did not explain unsupported template ${removed_template}\nstdout:\n${removed_template_create_stdout}\nstderr:\n${removed_template_create_stderr}")
    endif()
endforeach()

execute_process(
    COMMAND "${SLOPPY_CLI}" create "${default_project_name}" --format json
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
if(NOT default_create_stdout MATCHES "\"template\":\"api\"")
    message(FATAL_ERROR "sloppy create default did not use api template\nstdout:\n${default_create_stdout}")
endif()
foreach(expected_default IN ITEMS README.md package.json sloppy.json appsettings.json appsettings.Development.json data/.gitkeep migrations/0001_create_users.sql src/main.ts src/routes/users.ts src/services/usersService.ts src/db/usersRepository.ts)
    if(NOT EXISTS "${default_project_dir}/${expected_default}")
        message(FATAL_ERROR "sloppy create default api template did not copy ${expected_default}")
    endif()
endforeach()

set(public_template_smoke_dir "${work_dir}/public-template-smoke")
file(MAKE_DIRECTORY "${public_template_smoke_dir}")
file(COPY "${PROJECT_SOURCE_DIR}/templates" DESTINATION "${public_template_smoke_dir}")
foreach(public_template IN ITEMS api minimal-api program cli package-api node-compat)
    set(public_project_name "smoke-${public_template}")
    set(public_project_dir "${public_template_smoke_dir}/${public_project_name}")
    execute_process(
        COMMAND "${SLOPPY_CLI}" create "${public_project_name}" --template "${public_template}" --no-git --format json
        WORKING_DIRECTORY "${public_template_smoke_dir}"
        TIMEOUT 60
        RESULT_VARIABLE public_create_result
        OUTPUT_VARIABLE public_create_stdout
        ERROR_VARIABLE public_create_stderr)
    if(NOT public_create_result EQUAL 0)
        message(FATAL_ERROR "sloppy create ${public_template} template failed\nstdout:\n${public_create_stdout}\nstderr:\n${public_create_stderr}")
    endif()
    foreach(expected_public_file IN ITEMS README.md package.json sloppy.json src/main.ts)
        if(NOT EXISTS "${public_project_dir}/${expected_public_file}")
            message(FATAL_ERROR "sloppy create ${public_template} template did not copy ${expected_public_file}")
        endif()
    endforeach()
    if(public_template STREQUAL "package-api")
        if(SLOPPY_TEST_NODE AND EXISTS "${SLOPPY_TEST_NODE}" AND DEFINED SLOPPY_TEST_NPM_CLI AND EXISTS "${SLOPPY_TEST_NPM_CLI}")
            execute_process(
                COMMAND "${SLOPPY_TEST_NODE}" "${SLOPPY_TEST_NPM_CLI}" install --ignore-scripts --no-audit
                WORKING_DIRECTORY "${public_project_dir}"
                TIMEOUT 120
                RESULT_VARIABLE public_npm_result
                OUTPUT_VARIABLE public_npm_stdout
                ERROR_VARIABLE public_npm_stderr)
        elseif(SLOPPY_TEST_NPM)
            execute_process(
                COMMAND "${SLOPPY_TEST_NPM}" install --ignore-scripts --no-audit
                WORKING_DIRECTORY "${public_project_dir}"
                TIMEOUT 120
                RESULT_VARIABLE public_npm_result
                OUTPUT_VARIABLE public_npm_stdout
                ERROR_VARIABLE public_npm_stderr)
        else()
            message(FATAL_ERROR "package-api template test requires node and npm to install the local file dependency")
        endif()
        if(NOT public_npm_result EQUAL 0)
            message(FATAL_ERROR "package-api npm install failed\nstdout:\n${public_npm_stdout}\nstderr:\n${public_npm_stderr}")
        endif()
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_SLOPPYC=${SLOPPYC_EXECUTABLE}" "${SLOPPY_CLI}" build
        WORKING_DIRECTORY "${public_project_dir}"
        TIMEOUT 180
        RESULT_VARIABLE public_build_result
        OUTPUT_VARIABLE public_build_stdout
        ERROR_VARIABLE public_build_stderr)
    if(NOT public_build_result EQUAL 0)
        message(FATAL_ERROR "${public_template} template build failed\nstdout:\n${public_build_stdout}\nstderr:\n${public_build_stderr}")
    endif()
    if(public_template STREQUAL "api" OR public_template STREQUAL "minimal-api" OR public_template STREQUAL "package-api")
        execute_process(
            COMMAND "${SLOPPY_CLI}" routes .sloppy
            WORKING_DIRECTORY "${public_project_dir}"
            TIMEOUT 60
            RESULT_VARIABLE public_routes_result
            OUTPUT_VARIABLE public_routes_stdout
            ERROR_VARIABLE public_routes_stderr)
        if(NOT public_routes_result EQUAL 0)
            message(FATAL_ERROR "${public_template} template routes failed\nstdout:\n${public_routes_stdout}\nstderr:\n${public_routes_stderr}")
        endif()
        if(NOT public_routes_stdout MATCHES "/health")
            message(FATAL_ERROR "${public_template} template routes did not include /health\nstdout:\n${public_routes_stdout}")
        endif()
    endif()
    if(SLOPPY_ENABLE_V8)
        if(public_template STREQUAL "api")
            assert_sloppy_command_success("api template source /health" "${public_project_dir}" "HTTP/1.1 200" run .sloppy --once GET /health)
            assert_sloppy_command_success("api template source /health/ready" "${public_project_dir}" "HTTP/1.1 200" run .sloppy --once GET /health/ready)
            assert_sloppy_command_success("api template source /users" "${public_project_dir}" "HTTP/1.1 200" run .sloppy --once GET /users)
        elseif(public_template STREQUAL "minimal-api")
            assert_sloppy_command_success("minimal-api template source /health" "${public_project_dir}" "HTTP/1.1 200" run .sloppy --once GET /health)
            assert_sloppy_command_success("minimal-api template source /hello" "${public_project_dir}" "Ada" run .sloppy --once GET /hello/Ada)
        elseif(public_template STREQUAL "program")
            assert_sloppy_command_success("program template source run" "${public_project_dir}" "Hello, Ada" run src/main.ts -- --name Ada)
            assert_sloppy_command_success("program template artifact run" "${public_project_dir}" "Hello, Ada" run .sloppy -- --name Ada)
        elseif(public_template STREQUAL "cli")
            assert_sloppy_command_success("cli template source help" "${public_project_dir}" "Commands:" run src/main.ts -- --help)
            assert_sloppy_command_success("cli template source inspect" "${public_project_dir}" "\"path\": \"./package.json\"" run src/main.ts -- inspect package.json)
            assert_sloppy_command_success("cli template artifact help" "${public_project_dir}" "Commands:" run .sloppy -- --help)
        elseif(public_template STREQUAL "package-api")
            assert_sloppy_command_success("package-api template source /health" "${public_project_dir}" "HTTP/1.1 200" run .sloppy --once GET /health)
            assert_sloppy_command_success("package-api template source /users" "${public_project_dir}" "HTTP/1.1 200" run .sloppy --once GET /users/Ada)
        elseif(public_template STREQUAL "node-compat")
            assert_sloppy_command_success("node-compat template artifact run" "${public_project_dir}" "\"event\":\"ok\"|\"event\": \"ok\"" run .sloppy)
            assert_sloppy_command_success("node-compat template artifact UTF-8 buffer" "${public_project_dir}" "\"euroCodePoint\":8364|\"euroCodePoint\": 8364" run .sloppy)
        endif()
    endif()
    if(public_template STREQUAL "api")
        execute_process(
            COMMAND "${SLOPPY_CLI}" capabilities .sloppy
            WORKING_DIRECTORY "${public_project_dir}"
            TIMEOUT 60
            RESULT_VARIABLE public_capabilities_result
            OUTPUT_VARIABLE public_capabilities_stdout
            ERROR_VARIABLE public_capabilities_stderr)
        if(NOT public_capabilities_result EQUAL 0)
            message(FATAL_ERROR "api template capabilities failed\nstdout:\n${public_capabilities_stdout}\nstderr:\n${public_capabilities_stderr}")
        endif()
        if(NOT public_capabilities_stdout MATCHES "data.main")
            message(FATAL_ERROR "api template capabilities did not include SQLite provider metadata\nstdout:\n${public_capabilities_stdout}")
        endif()
        if(SLOPPY_ENABLE_V8)
            set(api_db_status_before_migrate "applied")
            set(api_db_status_json_before_migrate "\"status\":\"current\"")
        else()
            set(api_db_status_before_migrate "pending")
            set(api_db_status_json_before_migrate "\"status\":\"pending\"")
        endif()
        assert_sloppy_command_success("api template db status before explicit migrate" "${public_project_dir}" "${api_db_status_before_migrate}" db status .sloppy --provider main)
        assert_sloppy_command_success("api template db status json before explicit migrate with token" "${public_project_dir}" "${api_db_status_json_before_migrate}" db status .sloppy --provider data.main --format json)
        assert_sloppy_command_success("api template db status absolute target from outside project" "${work_dir}" "${api_db_status_before_migrate}" db status "${public_project_dir}/.sloppy" --provider main)
        assert_sloppy_command_success("api template db migrate" "${public_project_dir}" "applied" db migrate .sloppy --provider main)
        assert_sloppy_command_success("api template db status applied" "${public_project_dir}" "applied" db status .sloppy --provider main)
        assert_sloppy_command_success("api template db status json current with token" "${public_project_dir}" "\"status\":\"current\"" db status .sloppy --provider data.main --format json)
        assert_sloppy_command_success("api template db status absolute target applied from outside project" "${work_dir}" "applied" db status "${public_project_dir}/.sloppy" --provider main)
        if(SLOPPY_ENABLE_V8)
            file(WRITE "${public_project_dir}/valid-user.json" "{\"name\":\"Katherine Johnson\",\"email\":\"katherine@example.test\"}")
            file(WRITE "${public_project_dir}/missing-user-name.json" "{\"email\":\"missing-name@example.test\"}")
            file(WRITE "${public_project_dir}/invalid-user-email.json" "{\"name\":\"Invalid Email\",\"email\":\"not-an-email\"}")
            file(WRITE "${public_project_dir}/malformed-user.json" "{\"name\":")
            assert_sloppy_command_success("api template source POST /users valid JSON" "${public_project_dir}" "HTTP/1.1 201" run .sloppy --header "content-type: application/json" --body-file valid-user.json --once POST /users)
            assert_sloppy_command_success("api template source POST /users missing field validation" "${public_project_dir}" "body.name" run .sloppy --header "content-type: application/json" --body-file missing-user-name.json --once POST /users)
            assert_sloppy_command_success("api template source POST /users invalid email validation" "${public_project_dir}" "Expected an email address" run .sloppy --header "content-type: application/json" --body-file invalid-user-email.json --once POST /users)
            assert_sloppy_command_success("api template source POST /users malformed JSON validation" "${public_project_dir}" "application/problem\\+json" run .sloppy --header "content-type: application/json" --body-file malformed-user.json --once POST /users)
        endif()
    endif()
    if(public_template STREQUAL "package-api" OR public_template STREQUAL "node-compat")
        execute_process(
            COMMAND "${SLOPPY_CLI}" deps .sloppy --format json
            WORKING_DIRECTORY "${public_project_dir}"
            TIMEOUT 60
            RESULT_VARIABLE public_deps_result
            OUTPUT_VARIABLE public_deps_stdout
            ERROR_VARIABLE public_deps_stderr)
        if(NOT public_deps_result EQUAL 0)
            message(FATAL_ERROR "${public_template} template deps failed\nstdout:\n${public_deps_stdout}\nstderr:\n${public_deps_stderr}")
        endif()
        if(public_template STREQUAL "package-api" AND NOT public_deps_stdout MATCHES "validator-lite")
            message(FATAL_ERROR "package-api deps did not include validator-lite\nstdout:\n${public_deps_stdout}")
        endif()
        if(public_template STREQUAL "node-compat" AND NOT public_deps_stdout MATCHES "node:path")
            message(FATAL_ERROR "node-compat deps did not include node:path\nstdout:\n${public_deps_stdout}")
        endif()
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_SLOPPYC=${SLOPPYC_EXECUTABLE}" "${SLOPPY_CLI}" package --format json
        WORKING_DIRECTORY "${public_project_dir}"
        TIMEOUT 180
        RESULT_VARIABLE public_package_result
        OUTPUT_VARIABLE public_package_stdout
        ERROR_VARIABLE public_package_stderr)
    if(NOT public_package_result EQUAL 0)
        message(FATAL_ERROR "${public_template} template package failed\nstdout:\n${public_package_stdout}\nstderr:\n${public_package_stderr}")
    endif()
    if(NOT EXISTS "${public_project_dir}/.sloppy/package/manifest.json")
        message(FATAL_ERROR "${public_template} template package did not create manifest")
    endif()
    if(public_template STREQUAL "api")
        if(NOT EXISTS "${public_project_dir}/.sloppy/package/migrations/0001_create_users.sql")
            message(FATAL_ERROR "api template package did not copy migrations/0001_create_users.sql")
        endif()
        file(READ "${public_project_dir}/.sloppy/package/manifest.json" api_package_manifest)
        if(NOT api_package_manifest MATCHES "\"migrations\"")
            message(FATAL_ERROR "api template package manifest did not include migrations metadata\n${api_package_manifest}")
        endif()
        file(MAKE_DIRECTORY "${public_project_dir}/.sloppy/package/data")
        file(COPY_FILE "${public_project_dir}/data/app.db" "${public_project_dir}/.sloppy/package/data/app.db")
        assert_sloppy_command_success("api template package db status" "${public_project_dir}" "applied" db status .sloppy/package --provider main)
        assert_sloppy_command_success("api template package db status with token" "${public_project_dir}" "applied" db status .sloppy/package --provider data.main)
        assert_sloppy_command_success("api template package db status absolute target from outside project" "${work_dir}" "applied" db status "${public_project_dir}/.sloppy/package" --provider main)
        if(SLOPPY_ENABLE_V8)
            assert_sloppy_command_success("api template packaged POST /users validation" "${public_project_dir}" "body.name" run .sloppy/package --header "content-type: application/json" --body-file missing-user-name.json --once POST /users)
        endif()
    endif()
    if(SLOPPY_ENABLE_V8)
        if(public_template STREQUAL "api")
            assert_sloppy_command_success("api template packaged /health" "${public_project_dir}" "HTTP/1.1 200" run .sloppy/package --once GET /health)
            assert_sloppy_command_success("api template packaged /health/ready" "${public_project_dir}" "HTTP/1.1 200" run .sloppy/package --once GET /health/ready)
            assert_sloppy_command_success("api template packaged /users" "${public_project_dir}" "HTTP/1.1 200" run .sloppy/package --once GET /users)
        elseif(public_template STREQUAL "minimal-api")
            assert_sloppy_command_success("minimal-api template packaged /health" "${public_project_dir}" "HTTP/1.1 200" run .sloppy/package --once GET /health)
        elseif(public_template STREQUAL "program")
            assert_sloppy_command_success("program template packaged run" "${public_project_dir}" "Hello, Ada" run .sloppy/package -- --name Ada)
        elseif(public_template STREQUAL "cli")
            assert_sloppy_command_success("cli template packaged help" "${public_project_dir}" "Commands:" run .sloppy/package -- --help)
        elseif(public_template STREQUAL "package-api")
            assert_sloppy_command_success("package-api template packaged /health" "${public_project_dir}" "HTTP/1.1 200" run .sloppy/package --once GET /health)
            assert_sloppy_command_success("package-api template packaged /users" "${public_project_dir}" "HTTP/1.1 200" run .sloppy/package --once GET /users/Ada)
        elseif(public_template STREQUAL "node-compat")
            assert_sloppy_command_success("node-compat template packaged run" "${public_project_dir}" "\"event\":\"ok\"|\"event\": \"ok\"" run .sloppy/package)
            assert_sloppy_command_success("node-compat template packaged UTF-8 buffer" "${public_project_dir}" "\"euroCodePoint\":8364|\"euroCodePoint\": 8364" run .sloppy/package)
        endif()
    endif()
endforeach()

execute_process(
    COMMAND "${SLOPPY_CLI}" create "dogfood-public" --template dogfood
    WORKING_DIRECTORY "${work_dir}"
    TIMEOUT 60
    RESULT_VARIABLE dogfood_create_result
    OUTPUT_VARIABLE dogfood_create_stdout
    ERROR_VARIABLE dogfood_create_stderr)
if(dogfood_create_result EQUAL 0)
    message(FATAL_ERROR "sloppy create unexpectedly accepted public dogfood template")
endif()
if(NOT dogfood_create_stderr MATCHES "unsupported template: dogfood")
    message(FATAL_ERROR "sloppy create dogfood failure did not explain unsupported template\nstdout:\n${dogfood_create_stdout}\nstderr:\n${dogfood_create_stderr}")
endif()

execute_process(
    COMMAND "${SLOPPY_CLI}" create "${program_project_name}" --template program --format json
    WORKING_DIRECTORY "${work_dir}"
    TIMEOUT 60
    RESULT_VARIABLE program_create_result
    OUTPUT_VARIABLE program_create_stdout
    ERROR_VARIABLE program_create_stderr)

if(NOT program_create_result EQUAL 0)
    message(FATAL_ERROR "sloppy create program template failed\nstdout:\n${program_create_stdout}\nstderr:\n${program_create_stderr}")
endif()
if(NOT program_create_stdout MATCHES "\"template\":\"program\"")
    message(FATAL_ERROR "sloppy create program template did not report program template\nstdout:\n${program_create_stdout}")
endif()
foreach(expected IN ITEMS README.md package.json sloppy.json src/main.ts)
    if(NOT EXISTS "${program_project_dir}/${expected}")
        message(FATAL_ERROR "sloppy create program template did not copy ${expected}")
    endif()
endforeach()
file(READ "${program_project_dir}/sloppy.json" program_sloppy_json)
if(NOT program_sloppy_json MATCHES "\"kind\": \"program\"")
    message(FATAL_ERROR "program template sloppy.json did not pin Program Mode\n${program_sloppy_json}")
endif()
file(WRITE "${program_project_dir}/src/main.ts" "import { unsafeFfi as ffi, t } from \"sloppy/ffi\";\n\nconst native = ffi.library(\"sloppy_ffi_test\", {\n  addI32: ffi.fn(t.i32, [t.i32, t.i32], { symbol: \"sloppy_ffi_add_i32\" })\n});\n\nexport function main() {\n  void native;\n  return \"ok\";\n}\n")
file(WRITE "${program_project_dir}/sloppy.json" "{\n  \"entry\": \"src/main.ts\",\n  \"kind\": \"program\",\n  \"ffiLibraries\": {\n    \"sloppy_ffi_test\": \"native/sloppy_ffi_test.dll\"\n  }\n}\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_SLOPPYC=${SLOPPYC_EXECUTABLE}" "${SLOPPY_CLI}" package
            --format json
    WORKING_DIRECTORY "${program_project_dir}"
    TIMEOUT 180
    RESULT_VARIABLE ffi_missing_package_result
    OUTPUT_VARIABLE ffi_missing_package_stdout
    ERROR_VARIABLE ffi_missing_package_stderr)
if(ffi_missing_package_result EQUAL 0)
    message(FATAL_ERROR "FFI package unexpectedly succeeded with a missing local native library")
endif()
if(NOT ffi_missing_package_stderr MATCHES "failed to copy native library")
    message(FATAL_ERROR "FFI package missing-library failure did not explain the missing local artifact\nstdout:\n${ffi_missing_package_stdout}\nstderr:\n${ffi_missing_package_stderr}")
endif()
file(MAKE_DIRECTORY "${program_project_dir}/native")
file(WRITE "${program_project_dir}/native/sloppy_ffi_test.dll" "package-manifest-only native fixture\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_SLOPPYC=${SLOPPYC_EXECUTABLE}" "${SLOPPY_CLI}" package
            --format json
    WORKING_DIRECTORY "${program_project_dir}"
    TIMEOUT 180
    RESULT_VARIABLE ffi_package_result
    OUTPUT_VARIABLE ffi_package_stdout
    ERROR_VARIABLE ffi_package_stderr)
if(NOT ffi_package_result EQUAL 0)
    message(FATAL_ERROR "FFI package failed\nstdout:\n${ffi_package_stdout}\nstderr:\n${ffi_package_stderr}")
endif()
if(NOT EXISTS "${program_project_dir}/.sloppy/package/artifacts/native/sloppy_ffi_test.dll")
    message(FATAL_ERROR "FFI package did not copy local native library")
endif()
file(READ "${program_project_dir}/.sloppy/package/manifest.json" ffi_package_manifest)
foreach(required_ffi_manifest_entry IN ITEMS "\"native\"" "\"id\": \"sloppy_ffi_test\"" "\"path\": \"artifacts/native/sloppy_ffi_test.dll\"" "\"sha256\": \"sha256:")
    if(NOT ffi_package_manifest MATCHES "${required_ffi_manifest_entry}")
        message(FATAL_ERROR "FFI package manifest is missing ${required_ffi_manifest_entry}\n${ffi_package_manifest}")
    endif()
endforeach()
string(REPLACE "\\" "/" program_project_dir_slash "${program_project_dir}")
string(REPLACE "\\" "/" ffi_package_manifest_slash "${ffi_package_manifest}")
string(FIND "${ffi_package_manifest_slash}" "${program_project_dir_slash}" ffi_manifest_absolute_path_index)
if(NOT ffi_manifest_absolute_path_index EQUAL -1)
    message(FATAL_ERROR "FFI package manifest leaked an absolute project path\n${ffi_package_manifest}")
endif()
file(APPEND "${program_project_dir}/.sloppy/package/artifacts/native/sloppy_ffi_test.dll" "corrupt packaged native library\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_SLOPPYC=${SLOPPYC_EXECUTABLE}" "${SLOPPY_CLI}" run
            .sloppy/package
    WORKING_DIRECTORY "${program_project_dir}"
    TIMEOUT 60
    RESULT_VARIABLE ffi_corrupt_package_run_result
    OUTPUT_VARIABLE ffi_corrupt_package_run_stdout
    ERROR_VARIABLE ffi_corrupt_package_run_stderr)
if(ffi_corrupt_package_run_result EQUAL 0)
    message(FATAL_ERROR "sloppy run unexpectedly accepted a package with a corrupt native library")
endif()
if(NOT ffi_corrupt_package_run_stderr MATCHES "package native library hash mismatch")
    message(FATAL_ERROR "sloppy run corrupt package failure did not report native hash mismatch\nstdout:\n${ffi_corrupt_package_run_stdout}\nstderr:\n${ffi_corrupt_package_run_stderr}")
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
if(EXISTS "${non_empty_project_dir}/stale.txt")
    message(FATAL_ERROR "sloppy create --force did not remove stale files")
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
if(NOT manifest_json MATCHES "\"kind\": \"web\"")
    message(FATAL_ERROR "sloppy package manifest did not record web kind\n${manifest_json}")
endif()
foreach(required_manifest_entry IN ITEMS "\"plan\": \"artifacts/app.plan.json\"" "\"bundle\": \"artifacts/app.js\"" "\"sourceMap\": \"artifacts/app.js.map\"")
    if(NOT manifest_json MATCHES "${required_manifest_entry}")
        message(FATAL_ERROR "sloppy package manifest is missing ${required_manifest_entry}\n${manifest_json}")
    endif()
endforeach()
