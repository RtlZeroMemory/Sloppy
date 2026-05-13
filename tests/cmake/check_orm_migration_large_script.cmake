if(NOT DEFINED SLOPPY_CLI)
    message(FATAL_ERROR "SLOPPY_CLI is required")
endif()

set(work_dir "${CMAKE_CURRENT_BINARY_DIR}/orm-migration-large-script")
file(REMOVE_RECURSE "${work_dir}")
file(MAKE_DIRECTORY "${work_dir}/compiled")
file(WRITE "${work_dir}/sloppy.json" "{\n  \"entry\": \"app.ts\",\n  \"migrations\": {\n    \"main\": {\n      \"provider\": \"sqlite\",\n      \"path\": \"migrations/*.sql\"\n    }\n  }\n}\n")

file(WRITE "${work_dir}/compiled/app.plan.json" "{\n")
file(APPEND "${work_dir}/compiled/app.plan.json" "  \"schemaVersion\": 1,\n")
file(APPEND "${work_dir}/compiled/app.plan.json" "  \"kind\": \"program\",\n")
file(APPEND "${work_dir}/compiled/app.plan.json" "  \"compilerVersion\": \"sloppyc-placeholder\",\n")
file(APPEND "${work_dir}/compiled/app.plan.json" "  \"runtimeMinimumVersion\": \"0.1.0\",\n")
file(APPEND "${work_dir}/compiled/app.plan.json" "  \"stdlibVersion\": \"0.1.0\",\n")
file(APPEND "${work_dir}/compiled/app.plan.json" "  \"target\": { \"platform\": \"windows-x64\", \"engine\": \"v8\" },\n")
file(APPEND "${work_dir}/compiled/app.plan.json" "  \"bundle\": { \"path\": \".sloppy/app.js\", \"id\": \"cli-orm-large\", \"hash\": \"test-only\" },\n")
file(APPEND "${work_dir}/compiled/app.plan.json" "  \"sourceMap\": { \"path\": \".sloppy/app.js.map\", \"id\": \"cli-orm-large-map\", \"hash\": \"test-only\" },\n")
file(APPEND "${work_dir}/compiled/app.plan.json" "  \"handlers\": [],\n")
file(APPEND "${work_dir}/compiled/app.plan.json" "  \"routes\": [],\n")
file(APPEND "${work_dir}/compiled/app.plan.json" "  \"dataProviders\": [ { \"token\": \"data.main\", \"provider\": \"sqlite\", \"providerKind\": \"sqlite\", \"service\": \"data.main\", \"database\": \"app.db\" } ],\n")
file(APPEND "${work_dir}/compiled/app.plan.json" "  \"features\": { \"orm\": true },\n")
file(APPEND "${work_dir}/compiled/app.plan.json" "  \"orm\": {\n")
file(APPEND "${work_dir}/compiled/app.plan.json" "    \"tables\": [\n")
foreach(index RANGE 0 179)
    if(index EQUAL 179)
        set(comma "")
    else()
        set(comma ",")
    endif()
    file(APPEND "${work_dir}/compiled/app.plan.json" "      { \"model\": \"Table${index}\", \"name\": \"table_${index}\", \"columns\": [ { \"name\": \"id\", \"type\": \"int\", \"primaryKey\": true } ] }${comma}\n")
endforeach()
file(APPEND "${work_dir}/compiled/app.plan.json" "    ],\n")
file(APPEND "${work_dir}/compiled/app.plan.json" "    \"relations\": [],\n")
file(APPEND "${work_dir}/compiled/app.plan.json" "    \"extraction\": { \"status\": \"static\" }\n")
file(APPEND "${work_dir}/compiled/app.plan.json" "  }\n")
file(APPEND "${work_dir}/compiled/app.plan.json" "}\n")

execute_process(
    COMMAND "${SLOPPY_CLI}" orm migration script "${work_dir}/compiled" --provider main
    WORKING_DIRECTORY "${work_dir}"
    RESULT_VARIABLE script_result
    OUTPUT_VARIABLE script_stdout
    ERROR_VARIABLE script_stderr)
if(NOT script_result EQUAL 0 OR
   NOT script_stdout MATCHES "create table \"table_0\"" OR
   NOT script_stdout MATCHES "create table \"table_179\"")
    message(FATAL_ERROR "sloppy orm migration script did not handle a large table set\nstdout:\n${script_stdout}\nstderr:\n${script_stderr}")
endif()
