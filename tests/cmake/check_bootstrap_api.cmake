set(results_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/results.js")
set(schema_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/schema.js")
set(data_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/data.js")
set(codec_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/codec.js")
set(ffi_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/ffi.js")
set(fs_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/fs.js")
set(time_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/time.js")
set(workers_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/workers.js")
set(problem_details_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/problem-details.js")
set(request_id_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/request-id.js")
set(request_logging_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/request-logging.js")
set(auth_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/auth.js")
set(public_config_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/config.js")
set(testservices_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/testservices.js")
set(testing_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/testing.js")
set(app_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/app.js")
set(index_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/index.js")
set(capabilities_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/internal/capabilities.js")
set(config_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/internal/config.js")
set(logging_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/internal/logging.js")
set(modules_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/internal/modules.js")
set(routes_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/internal/routes.js")
set(services_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/internal/services.js")
set(shared_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/internal/shared.js")
set(runtime_classic_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/internal/runtime-classic.js")

foreach(required_file IN ITEMS "${results_source}" "${schema_source}" "${data_source}" "${codec_source}" "${ffi_source}" "${fs_source}" "${time_source}" "${workers_source}" "${problem_details_source}" "${request_id_source}" "${request_logging_source}" "${auth_source}" "${public_config_source}" "${testservices_source}" "${testing_source}" "${app_source}" "${index_source}" "${capabilities_source}" "${config_source}" "${logging_source}" "${modules_source}" "${routes_source}" "${services_source}" "${shared_source}" "${runtime_classic_source}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Missing bootstrap API source file: ${required_file}")
    endif()
endforeach()

file(READ "${results_source}" results_js)
file(READ "${schema_source}" schema_js)
file(READ "${data_source}" data_js)
file(READ "${codec_source}" codec_js)
file(READ "${ffi_source}" ffi_js)
file(READ "${fs_source}" fs_js)
file(READ "${time_source}" time_js)
file(READ "${workers_source}" workers_js)
file(READ "${problem_details_source}" problem_details_js)
file(READ "${request_id_source}" request_id_js)
file(READ "${request_logging_source}" request_logging_js)
file(READ "${auth_source}" auth_js)
file(READ "${public_config_source}" public_config_js)
file(READ "${testservices_source}" testservices_js)
file(READ "${testing_source}" testing_js)
file(READ "${app_source}" app_js)
file(READ "${index_source}" index_js)
file(READ "${capabilities_source}" capabilities_js)
file(READ "${config_source}" config_js)
file(READ "${logging_source}" logging_js)
file(READ "${modules_source}" modules_js)
file(READ "${routes_source}" routes_js)
file(READ "${services_source}" services_js)
file(READ "${shared_source}" shared_js)
file(READ "${runtime_classic_source}" runtime_classic_js)

function(require_substring haystack needle description)
    string(FIND "${haystack}" "${needle}" found_index)
    if(found_index EQUAL -1)
        message(FATAL_ERROR "${description}: ${needle}")
    endif()
endfunction()

function(reject_substring haystack needle description)
    string(FIND "${haystack}" "${needle}" found_index)
    if(NOT found_index EQUAL -1)
        message(FATAL_ERROR "${description}: ${needle}")
    endif()
endfunction()

foreach(required_pattern IN ITEMS
        "text/plain; charset=utf-8"
        "application/json; charset=utf-8"
        "text/html; charset=utf-8"
        "application/problem+json; charset=utf-8"
        "__sloppyResult: true"
        "kind,"
        "status:"
        "body,"
        "headers:"
        "ok,"
        "created,"
        "accepted,"
        "noContent,"
        "notFound,"
        "badRequest,"
        "status,"
        "problem,"
        "html,"
        "Object.freeze")
    require_substring("${results_js}" "${required_pattern}" "results.js is missing expected API shape pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "[\\x00-\\x1F\\x7F]"
        "contentType must not contain control characters")
    require_substring("${results_js}" "${required_pattern}" "results.js is missing contentType control-character validation")
    require_substring("${runtime_classic_js}" "${required_pattern}" "runtime-classic.js is missing contentType control-character validation")
endforeach()

foreach(required_pattern IN ITEMS
        "kind: \"string\""
        "\"number\""
        "\"boolean\""
        "kind: \"object\""
        "min(length)"
        "email()"
        "validate(value)"
        "issues:"
        "metadata:"
        "__validateAtPath")
    require_substring("${schema_js}" "${required_pattern}" "schema.js is missing expected API shape pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "function sql(strings, ...values)"
        "createFakeProvider"
        "lowerQueryTemplate"
        "__sloppyQuery"
        "placeholderStyle"
        "question"
        "postgres"
        "named"
        "transaction(callback)"
        "nested transactions are not supported yet"
        "transaction scope is closed"
        "fake data provider method missing"
        "sqlite"
        "openSqlite"
        "nativeStdlibBridge"
        "tagged template")
    require_substring("${data_js}" "${required_pattern}" "data.js is missing expected API shape pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "const Base64 = Object.freeze"
        "const Base64Url = Object.freeze"
        "const Hex = Object.freeze"
        "const Text = Object.freeze"
        "const Binary = Object.freeze"
        "class Utf8StreamingDecoder"
        "class BinaryReader"
        "class BinaryWriter"
        "SLOPPY_E_CODEC_INVALID_BASE64"
        "SLOPPY_E_CODEC_INVALID_BASE64URL"
        "SLOPPY_E_CODEC_INVALID_HEX"
        "SLOPPY_E_CODEC_MALFORMED_UTF8"
        "SLOPPY_E_CODEC_BINARY_READ_OUT_OF_BOUNDS"
        "SLOPPY_E_CODEC_BINARY_INVALID_ENDIAN_OR_FIELD_SIZE"
        "SLOPPY_E_CODEC_COMPRESSION_BACKEND_UNAVAILABLE"
        "u32le"
        "u16be"
        "u64le"
        "padding ?? \"optional\""
        "Base64Url input must use the URL-safe alphabet"
        "utf8Malformed(fatal, message)")
    require_substring("${codec_js}" "${required_pattern}" "codec.js is missing expected API contract pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "const File = Object.freeze"
        "readText(path, options)"
        "readBytes(path, options)"
        "readJson(path, options)"
        "writeText(path, text, options)"
        "writeBytes(path, bytes, options)"
        "writeJson(path, value, options)"
        "appendText(path, text, options)"
        "appendBytes(path, bytes, options)"
        "exists(path, options)"
        "stat(path, options)"
        "copy(fromPath, toPath, options)"
        "move(fromPath, toPath, options)"
        "delete(path, options)"
        "async open(path, options)"
        "watch(path, options)"
        "createSymlink(targetPath, linkPath, options)"
        "readLink(path, options)"
        "createTemp(directory, options)"
        "const Directory = Object.freeze"
        "create(path, options)"
        "list(path, options)"
        "async *walk(path, options)"
        "delete(path, options)"
        "exists(path, options)"
        "createTemp(directory, options)"
        "watch(path, options)"
        "applyTimeOptions"
        "class FileHandle"
        "readBytes(maxBytes = 64 * 1024, options)"
        "readText(maxBytes, options)"
        "writeBytes(bytes, options)"
        "writeText(text, options)"
        "seek(offset, origin = \"start\", options)"
        "truncate(size, options)"
        "flush(options)"
        "sync(options)"
        "readChunks(options)"
        "readLines(options)"
        "class FileWatcher"
        "nextEvent(options)"
        "const Path = Object.freeze"
        "FileWatcher")
    require_substring("${fs_js}" "${required_pattern}" "fs.js is missing expected API shape pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "class TimeoutError"
        "class CancelledError"
        "class InvalidDeadlineError"
        "class TimerDisposedError"
        "const Deadline = Object.freeze"
        "class CancellationController"
        "const Time = Object.freeze"
        "delay(ms, options"
        "timeout(operationOrPromise, options"
        "interval(ms, options"
        "every(interval, handler"
        "yield(options"
        "systemClock()"
        "fakeClock(options"
        "nativeTime(\"Time.delay\")"
        "stdlib.time")
    require_substring("${time_js}" "${required_pattern}" "time.js is missing expected API contract pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "const BackgroundService = Object.freeze"
        "const WorkQueue = Object.freeze"
        "const WorkerPool = Object.freeze"
        "const Worker = Object.freeze"
        "maxQueued"
        "concurrency"
        "overflow"
        "retry"
        "SLOPPY_E_WORK_QUEUE_FULL"
        "SLOPPY_E_WORK_JOB_TIMEOUT"
        "SLOPPY_E_WORKER_UNSUPPORTED_PAYLOAD"
        "SloppyWorkerError"
        "WorkerCancellationController"
        "WorkerCancellationSignal"
        "__sloppyWorkerResource"
        "serializePayload")
    require_substring("${workers_js}" "${required_pattern}" "workers.js is missing expected API contract pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "export const t"
        "export const unsafeFfi"
        "unsafeFfi.fn"
        "unsafeFfi.library"
        "unsafeFfi.struct"
        "SLOPPY_E_FFI_RUNTIME_UNAVAILABLE"
        "Object.defineProperty")
    require_substring("${ffi_js}" "${required_pattern}" "ffi.js is missing expected API contract pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "const DETAIL_POLICIES = new Set"
        "function validateDetailPolicy(value)"
        "defaults(options)"
        "__sloppyErrorPolicy: true"
        "__sloppyProblemDetails: true"
        "detail:"
        "Object.freeze")
    require_substring("${problem_details_js}" "${required_pattern}" "problem-details.js is missing expected API contract pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "const DEFAULT_HEADER = \"x-request-id\""
        "function headerValueSafe(value)"
        "trustIncoming"
        "responseHeader"
        "generator"
        "requestIdMiddleware"
        "Object.freeze")
    require_substring("${request_id_js}" "${required_pattern}" "request-id.js is missing expected API contract pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "includeRoute"
        "includeDuration"
        "includeRequestId"
        "request completed"
        "durationMs"
        "requestLoggingMiddleware"
        "Object.freeze")
    require_substring("${request_logging_js}" "${required_pattern}" "request-logging.js is missing expected API contract pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "function jwtBearer(options)"
        "function apiKey(options)"
        "function cookieSession(options)"
        "function signIn(ctx, claims"
        "function signOut(ctx"
        "function verifyJwt(token, scheme)"
        "function verifySessionCookie(value, scheme)"
        "SLOPPY_E_AUTH_UNAUTHORIZED"
        "SLOPPY_E_AUTH_FORBIDDEN"
        "SLOPPY_E_AUTH_INVALID_TOKEN"
        "constantTimeEquals"
        "authorizeRoute"
        "createAuthState"
        "snapshotAuthState"
        "Object.freeze")
    require_substring("${auth_js}" "${required_pattern}" "auth.js is missing expected API contract pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "function required(key)"
        "function boolean(key"
        "__sloppyConfigReference: true"
        "[Config reference redacted]"
        "Object.freeze")
    require_substring("${public_config_js}" "${required_pattern}" "config.js is missing expected public Config pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "class DockerCliBackend"
        "SLOPPY_E_TESTSERVICES_DOCKER_UNAVAILABLE"
        "SLOPPY_E_TESTSERVICES_PROVIDER_UNAVAILABLE"
        "function providerBridgeAvailable(kind)"
        "function startupFailureMessage(kind, options, state, reason)"
        "const TestServices = Object.freeze"
        "export { DockerCliBackend, TestServices }")
    require_substring("${testservices_js}" "${required_pattern}" "testservices.js is missing expected TestServices API pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "function createTestHost(app, options = {})"
        "function responseFromResult(result)"
        "function matchRoutePattern(pattern, path)"
        "function createHeadersLike(entries)"
        "function normalizeRequestBody(options, headerEntries)"
        "function createContext(app, hostState, method, targetParts, headers, route, matchedRoute, bodyKind, bodyBytes"
        "function createDiagnosticsStore(secrets = [])"
        "function createMetricsStore()"
        "function createOpenApiHelpers(loadDocument)"
        "class RequestBuilder"
        "const TestHost = Object.freeze"
        "TestServices,"
        "const FakeClock = Object.freeze"
        "const TestData = Object.freeze"
        "request(method, target, options = undefined)"
        "get(target, options)"
        "post(target, options)"
        "put(target, options)"
        "patch(target, options)"
        "delete(target, options)"
        "options(target, options)"
        "close()"
        "const Testing = Object.freeze"
        "export { createTestHost, FakeClock, TestData, TestHost, TestServices, Testing }")
    require_substring("${testing_js}" "${required_pattern}" "testing.js is missing expected app test host pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "const __sloppyTestServices = (() =>"
        "class DockerCliBackend"
        "SLOPPY_E_TESTSERVICES_DOCKER_UNAVAILABLE"
        "SLOPPY_E_TESTSERVICES_PROVIDER_UNAVAILABLE"
        "DockerCliBackend,"
        "TestServices,")
    require_substring("${runtime_classic_js}" "${required_pattern}" "runtime-classic.js is missing expected TestServices runtime pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "addDatabase(token, options)"
        "capability token already declared"
        "capability token is not declared"
        "function createCapabilityRegistry(guard)"
        "function createCapabilityProvider(capabilitySnapshot)"
        "Object.freeze")
    require_substring("${capabilities_js}" "${required_pattern}" "internal/capabilities.js is missing expected app-host capability pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "addObject(object)"
        "getSecret(key)"
        "bind(prefix, schema)"
        "providerConfigPrefix"
        "Sloppy config key"
        "literal default"
        "Object.freeze")
    require_substring("${config_js}" "${required_pattern}" "internal/config.js is missing expected app-host config pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "setMinimumLevel(level)"
        "setQueueCapacity(capacity)"
        "addRedactionKey(key)"
        "addMemorySink(options = undefined)"
        "writeTo"
        "forCategory(nextCategory)"
        "isEnabled(level)"
        "trace(message, fields)"
        "debug(message, fields)"
        "info(message, fields)"
        "warn(message, fields)"
        "error(message, fields)"
        "MEMORY_SINK_STATE"
        "Object.freeze")
    require_substring("${logging_js}" "${required_pattern}" "internal/logging.js is missing expected app-host logging pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "function createModule(name)"
        "dependsOn(...names)"
        "capabilities(callback)"
        "function resolveModuleOrder(moduleStates)"
        "module dependency missing"
        "module dependency cycle detected"
        "module phase failed"
        "function createModuleDebugEntries(orderedModules, capabilitySnapshot, serviceSnapshot, routes)"
        "Object.freeze")
    require_substring("${modules_js}" "${required_pattern}" "internal/modules.js is missing expected app-host module pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "function registerRoute("
        "function createRouteGroup("
        "getInheritedMiddleware = () => []"
        "getCorsPolicy = () => null"
        "function createRouterGroup(prefix, configure)"
        "function normalizeCorsPolicy(policy)"
        "function createCorsPreflightHandler(state)"
        "mapGet: createMapMethod(\"GET\")"
        "function createControllerMapper("
        "Access-Control-Allow-Origin"
        "Access-Control-Allow-Methods"
        "withTags(...tags)"
        "withName(name)"
        "use(middleware)"
        "middleware:"
        "groupPrefix"
        "groupName"
        "handler must be a function"
        "route: {}"
        "Object.freeze")
    require_substring("${routes_js}" "${required_pattern}" "internal/routes.js is missing expected app-host routing pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "addSingleton(token, factoryOrValue)"
        "addTransient(token, factory)"
        "addScoped(token, factory)"
        "createScope()"
        "function createServiceProvider(registrations, capabilities)"
        "function finishWithCleanup(result, cleanup)"
        "Sloppy service provider is disposed"
        "Object.freeze")
    require_substring("${services_js}" "${required_pattern}" "internal/services.js is missing expected app-host services pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "function isPlainObject(value)"
        "function createMutationGuard(subject)"
        "function isPromiseLike(value)"
        "Object.freeze")
    require_substring("${shared_js}" "${required_pattern}" "internal/shared.js is missing expected app-host shared helper pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "const Time = Object.freeze"
        "const Deadline = Object.freeze"
        "class CancellationController"
        "Time,"
        "Deadline,"
        "CancellationController,"
        "TimeoutError,"
        "CancelledError,"
        "InvalidDeadlineError,"
        "TimerDisposedError,"
        "BackgroundService,"
        "WorkQueue,"
        "WorkerPool,"
        "Worker,"
        "WorkerCancellationController,"
        "WorkerCancellationSignal,"
        "SloppyWorkerError,"
        "__createFrameworkServiceProvider,"
        "SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE: runtime feature stdlib.time")
    require_substring("${runtime_classic_js}" "${required_pattern}" "runtime-classic.js is missing expected time runtime export pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "const Base64 = Object.freeze"
        "const Base64Url = Object.freeze"
        "const Hex = Object.freeze"
        "const Text = Object.freeze"
        "Base64,"
        "Base64Url,"
        "Hex,"
        "Text,"
        "Binary,"
        "Compression,"
        "Checksums,")
    require_substring("${runtime_classic_js}" "${required_pattern}" "runtime-classic.js is missing expected codec runtime export pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "const HTTP_CLIENT_PROTOCOLS = new Set([\"auto\", \"http/1.1\", \"h2\", \"h2c\"]);"
        "function normalizeHttpProtocol(baseOptions, requestObject, operation)"
        "async function sendHttp2RequestOnce(request, pool, lifecycle)"
        "return await sendHttp2RequestOnce(request, pool, lifecycle);")
    require_substring("${runtime_classic_js}" "${required_pattern}" "runtime-classic.js is missing expected HTTP/2 client runtime pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "createBuilder()"
        "module: createModule"
        "addModule(module)"
        "capabilities,"
        "config,"
        "logging,"
        "services,"
        "auth:"
        "build()"
        "create()"
        "function normalizeErrorPolicyOptions(options, config)"
        "function errorPolicyResult(error, context, policyState, config)"
        "mapGet(pattern, optionsOrHandler, maybeHandler)"
        "mapPost(pattern, optionsOrHandler, maybeHandler)"
        "mapPut(pattern, optionsOrHandler, maybeHandler)"
        "mapPatch(pattern, optionsOrHandler, maybeHandler)"
        "mapDelete(pattern, optionsOrHandler, maybeHandler)"
        "useCors(policy)"
        "useErrors(options = undefined)"
        "mapError(type, mapper)"
        "health()"
        "management(options = undefined)"
        "mapHealthChecks(options)"
        "DEFAULT_LIVENESS_PATH"
        "DEFAULT_READINESS_PATH"
        "function runHealthChecks(checks, mode, context)"
        "mapGroup(prefix)"
        "freeze()"
        "isFrozen()"
        "use(provider)"
        "docs(options = undefined)"
        "__getRoutes()"
        "__handleErrorStatus(status, context = undefined)"
        "__debug()"
        "__getModuleGraph()"
        "__getPlanContributions()"
        "useModule(moduleOrFactory)"
        "mapController(prefix, Controller, configure)"
        "controller(prefix, Controller, configure)"
        "group: createRouterGroup")
    require_substring("${app_js}" "${required_pattern}" "app.js is missing expected API shape pattern")
endforeach()

foreach(required_pattern IN ITEMS "export { Router, Sloppy }" "export { Auth }" "export { Config }" "Base64" "Base64Url" "Hex" "Text" "Binary" "Compression" "Checksums" "export {" "data" "sql" "File" "Directory" "Path" "Health" "Metrics" "Time" "Deadline" "CancellationController" "BackgroundService" "WorkQueue" "WorkerPool" "Worker" "export { ProblemDetails }" "export { RequestId }" "export { RequestLogging }" "export { Results }" "export { schema }" "FakeClock" "TestData" "TestHost" "Testing")
    require_substring("${index_js}" "${required_pattern}" "index.js is missing expected export pattern")
endforeach()

foreach(deferred_pattern IN ITEMS "app.run" "app.listen" "app.build" "addJsonFile" "addEnv" "addConsole")
    reject_substring(
        "${app_js}" "${deferred_pattern}"
        "app.js includes app-host API outside the documented bootstrap scope")
endforeach()
