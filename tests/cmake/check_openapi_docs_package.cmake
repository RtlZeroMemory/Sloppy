if(NOT DEFINED PROJECT_SOURCE_DIR OR NOT DEFINED CMAKE_BINARY_DIR OR NOT DEFINED SLOPPY_CLI)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR, CMAKE_BINARY_DIR, and SLOPPY_CLI are required")
endif()

set(project_dir "${CMAKE_BINARY_DIR}/openapi-docs-package")
set(build_dir "${project_dir}/build")
set(package_dir "${project_dir}/dist/docs-app/package")
set(program_out_dir "${project_dir}/dist/program-app")
set(outside_dir "${project_dir}/outside")
file(REMOVE_RECURSE "${project_dir}")
file(MAKE_DIRECTORY "${project_dir}")

file(WRITE "${project_dir}/app.ts" [=[
import { Sloppy, Results, Schema } from "sloppy";

const User = Schema.object({ id: Schema.integer(), name: Schema.string() });
const UserParams = Schema.object({ id: Schema.string().uuid() });
const UserQuery = Schema.object({ expand: Schema.string().optional() });
const TraceHeader = Schema.string();
const LiteralStatus = Schema.object({ status: Schema.literal("active") });
const app = Sloppy.create();

app.get("/users/{id:int}", () => Results.ok({ id: 1, name: "Ada" }))
  .name("Users.Get")
  .summary("Get user")
  .query(UserQuery)
  .params(UserParams)
  .header("x-trace", TraceHeader, { required: true })
  .returns(200, User);

app.get("/literal", () => Results.ok({ status: "active" }))
  .name("Literal.Get")
  .returns(200, LiteralStatus);

app.get("/manual", () => Results.ok({ ok: true }))
  .openapi({
    tags: ["Manual"],
    parameters: [{ name: "x-manual", in: "header", schema: { type: "string" } }],
    security: [{ bearerAuth: [] }],
    responses: { "200": { description: "manual response" } }
  });

app.docs({ title: "Docs Test", path: "/docs", openapiPath: "/openapi.json" });

export default app;
]=])

file(WRITE "${project_dir}/strict-incomplete.ts" [=[
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();
app.get("/partial", () => Results.ok({ ok: true }));
app.docs({ strict: true });

export default app;
]=])

file(WRITE "${project_dir}/manual-strict.ts" [=[
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();
app.get("/manual-strict", () => Results.ok({ ok: true }))
  .openapi({
    tags: ["Manual"],
    responses: { "200": { description: "manual response" } }
  });

export default app;
]=])

file(WRITE "${project_dir}/program.ts" [=[
export function main() {
    return 0;
}
]=])

execute_process(
    COMMAND "${SLOPPY_CLI}" build app.ts --out "${build_dir}"
    WORKING_DIRECTORY "${project_dir}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "sloppy build failed\nstdout:\n${build_stdout}\nstderr:\n${build_stderr}")
endif()

set(openapi_path "${build_dir}/openapi.json")
if(NOT EXISTS "${openapi_path}")
    message(FATAL_ERROR "app.docs() build did not emit openapi.json")
endif()
file(READ "${openapi_path}" openapi_json)
if(NOT openapi_json MATCHES "\"/users/\\{id\\}\"")
    message(FATAL_ERROR "build OpenAPI artifact did not include /users/{id}\n${openapi_json}")
endif()
if(openapi_json MATCHES "\"/openapi.json\"" OR openapi_json MATCHES "\"/docs\"")
    message(FATAL_ERROR "OpenAPI artifact leaked internal docs routes\n${openapi_json}")
endif()
if(openapi_json MATCHES "\"const\"")
    message(FATAL_ERROR "OpenAPI 3.0.3 artifact emitted unsupported const schemas\n${openapi_json}")
endif()
if(NOT openapi_json MATCHES "\"enum\"[ \t\r\n]*:[ \t\r\n]*\\[[ \t\r\n]*\"active\"[ \t\r\n]*\\]")
    message(FATAL_ERROR "literal schema was not emitted as enum\n${openapi_json}")
endif()
string(REGEX MATCHALL "\"name\"[ \t\r\n]*:[ \t\r\n]*\"id\"" id_parameter_matches "${openapi_json}")
list(LENGTH id_parameter_matches id_parameter_count)
if(NOT id_parameter_count EQUAL 1)
    message(FATAL_ERROR "path parameter id was not de-duplicated; count=${id_parameter_count}\n${openapi_json}")
endif()
string(REGEX MATCHALL "\"name\"[ \t\r\n]*:[ \t\r\n]*\"x-trace\"" trace_parameter_matches "${openapi_json}")
list(LENGTH trace_parameter_matches trace_parameter_count)
if(NOT trace_parameter_count EQUAL 1)
    message(FATAL_ERROR "header parameter x-trace was not de-duplicated; count=${trace_parameter_count}\n${openapi_json}")
endif()

find_program(SLOPPY_TEST_NODE NAMES node.exe node)
if(SLOPPY_TEST_NODE)
    file(WRITE "${project_dir}/assert-openapi.mjs" [=[
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";

const document = JSON.parse(await readFile(process.argv[2], "utf8"));
assert.ok(document.paths["/users/{id}"], "expected generated users path");
assert.equal(document.paths["/users/{id}"].get.operationId, "Users.Get");
assert.equal(document.paths["/users/{id}"].get.summary, "Get user");
assert.equal(document.paths["/users/{id}"].get.parameters.filter((parameter) => parameter.name === "id" && parameter.in === "path").length, 1);
assert.equal(document.paths["/users/{id}"].get.parameters.filter((parameter) => parameter.name === "x-trace" && parameter.in === "header").length, 1);
assert.deepEqual(document.components.schemas.LiteralStatus.properties.status, { type: "string", enum: ["active"] });
const manual = document.paths["/manual"].get;
assert.deepEqual(Object.keys(manual).sort(), ["parameters", "responses", "security", "tags"]);
assert.deepEqual(manual.tags, ["Manual"]);
assert.deepEqual(manual.parameters, [{ name: "x-manual", in: "header", schema: { type: "string" } }]);
assert.deepEqual(manual.security, [{ bearerAuth: [] }]);
assert.deepEqual(manual.responses, { 200: { description: "manual response" } });
]=])
    execute_process(
        COMMAND "${SLOPPY_TEST_NODE}" "${project_dir}/assert-openapi.mjs" "${openapi_path}"
        WORKING_DIRECTORY "${project_dir}"
        RESULT_VARIABLE assert_openapi_result
        OUTPUT_VARIABLE assert_openapi_stdout
        ERROR_VARIABLE assert_openapi_stderr)
    if(NOT assert_openapi_result EQUAL 0)
        message(FATAL_ERROR "OpenAPI artifact JSON assertions failed\nstdout:\n${assert_openapi_stdout}\nstderr:\n${assert_openapi_stderr}")
    endif()
endif()

execute_process(
    COMMAND "${SLOPPY_CLI}" build strict-incomplete.ts --out "${project_dir}/strict-build"
    WORKING_DIRECTORY "${project_dir}"
    RESULT_VARIABLE strict_build_result
    OUTPUT_VARIABLE strict_build_stdout
    ERROR_VARIABLE strict_build_stderr)
if(strict_build_result EQUAL 0)
    message(FATAL_ERROR "app.docs({ strict: true }) build succeeded with incomplete contracts\nstdout:\n${strict_build_stdout}\nstderr:\n${strict_build_stderr}")
endif()
set(strict_build_output "${strict_build_stdout}\n${strict_build_stderr}")
if(NOT strict_build_output MATCHES "strict requires complete route contracts")
    message(FATAL_ERROR "strict docs build did not report incomplete contracts\nstdout:\n${strict_build_stdout}\nstderr:\n${strict_build_stderr}")
endif()

set(manual_strict_dir "${project_dir}/manual-strict-build")
execute_process(
    COMMAND "${SLOPPY_CLI}" build manual-strict.ts --out "${manual_strict_dir}"
    WORKING_DIRECTORY "${project_dir}"
    RESULT_VARIABLE manual_strict_build_result
    OUTPUT_VARIABLE manual_strict_build_stdout
    ERROR_VARIABLE manual_strict_build_stderr)
if(NOT manual_strict_build_result EQUAL 0)
    message(FATAL_ERROR "manual strict fixture build failed\nstdout:\n${manual_strict_build_stdout}\nstderr:\n${manual_strict_build_stderr}")
endif()
execute_process(
    COMMAND "${SLOPPY_CLI}" openapi "${manual_strict_dir}" --strict
    WORKING_DIRECTORY "${project_dir}"
    RESULT_VARIABLE manual_strict_openapi_result
    OUTPUT_VARIABLE manual_strict_openapi_stdout
    ERROR_VARIABLE manual_strict_openapi_stderr)
if(NOT manual_strict_openapi_result EQUAL 0)
    message(FATAL_ERROR "manual full OpenAPI replacement did not pass strict mode\nstdout:\n${manual_strict_openapi_stdout}\nstderr:\n${manual_strict_openapi_stderr}")
endif()
if(NOT manual_strict_openapi_stdout MATCHES "\"/manual-strict\"" OR
   NOT manual_strict_openapi_stdout MATCHES "\"responses\"")
    message(FATAL_ERROR "manual strict OpenAPI output did not include replacement operation\n${manual_strict_openapi_stdout}")
endif()

execute_process(
    COMMAND "${SLOPPY_CLI}" package app.ts --out "${project_dir}/dist/docs-app"
    WORKING_DIRECTORY "${project_dir}"
    RESULT_VARIABLE package_result
    OUTPUT_VARIABLE package_stdout
    ERROR_VARIABLE package_stderr)
if(NOT package_result EQUAL 0)
    message(FATAL_ERROR "sloppy package failed\nstdout:\n${package_stdout}\nstderr:\n${package_stderr}")
endif()

if(NOT EXISTS "${package_dir}/artifacts/openapi.json")
    message(FATAL_ERROR "package did not include artifacts/openapi.json")
endif()
file(READ "${package_dir}/artifacts/openapi.json" packaged_openapi_json)
if(NOT packaged_openapi_json MATCHES "\"/users/\\{id\\}\"")
    message(FATAL_ERROR "packaged OpenAPI artifact did not include /users/{id}\n${packaged_openapi_json}")
endif()

file(MAKE_DIRECTORY "${program_out_dir}")
file(WRITE "${program_out_dir}/openapi.json" "{\"paths\":{\"/stale\":{}}}")
execute_process(
    COMMAND "${SLOPPY_CLI}" package program.ts --kind program --out "${program_out_dir}"
    WORKING_DIRECTORY "${project_dir}"
    RESULT_VARIABLE program_package_result
    OUTPUT_VARIABLE program_package_stdout
    ERROR_VARIABLE program_package_stderr)
if(NOT program_package_result EQUAL 0)
    message(FATAL_ERROR "program package failed\nstdout:\n${program_package_stdout}\nstderr:\n${program_package_stderr}")
endif()
if(EXISTS "${program_out_dir}/package/artifacts/openapi.json")
    message(FATAL_ERROR "program package copied stale openapi.json")
endif()
file(READ "${program_out_dir}/package/manifest.json" program_manifest_json)
if(program_manifest_json MATCHES "\"openapi\"")
    message(FATAL_ERROR "program package manifest recorded stale openapi artifact\n${program_manifest_json}")
endif()

file(REMOVE_RECURSE "${outside_dir}")
file(MAKE_DIRECTORY "${outside_dir}")
file(COPY "${package_dir}" DESTINATION "${outside_dir}")

if(SLOPPY_ENABLE_V8)
    execute_process(
        COMMAND "${SLOPPY_CLI}" run package --once GET /openapi.json
        WORKING_DIRECTORY "${outside_dir}"
        RESULT_VARIABLE openapi_run_result
        OUTPUT_VARIABLE openapi_run_stdout
        ERROR_VARIABLE openapi_run_stderr)
    if(NOT openapi_run_result EQUAL 0)
        message(FATAL_ERROR "packaged /openapi.json failed\nstdout:\n${openapi_run_stdout}\nstderr:\n${openapi_run_stderr}")
    endif()
    if(NOT openapi_run_stdout MATCHES "\"/users/\\{id\\}\"")
        message(FATAL_ERROR "packaged /openapi.json did not serve the generated route\n${openapi_run_stdout}")
    endif()

    execute_process(
        COMMAND "${SLOPPY_CLI}" run package --once GET /docs
        WORKING_DIRECTORY "${outside_dir}"
        RESULT_VARIABLE docs_run_result
        OUTPUT_VARIABLE docs_run_stdout
        ERROR_VARIABLE docs_run_stderr)
    if(NOT docs_run_result EQUAL 0)
        message(FATAL_ERROR "packaged /docs failed\nstdout:\n${docs_run_stdout}\nstderr:\n${docs_run_stderr}")
    endif()
    if(NOT docs_run_stdout MATCHES "/openapi.json")
        message(FATAL_ERROR "/docs did not reference the configured OpenAPI path\n${docs_run_stdout}")
    endif()
endif()
