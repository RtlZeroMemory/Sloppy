set(results_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/results.js")
set(schema_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/schema.js")
set(data_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/data.js")
set(codec_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/codec.js")
set(fs_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/fs.js")
set(time_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/time.js")
set(workers_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/workers.js")
set(app_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/app.js")
set(index_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/index.js")
set(runtime_classic_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/internal/runtime-classic.js")

foreach(required_file IN ITEMS "${results_source}" "${schema_source}" "${data_source}" "${codec_source}" "${fs_source}" "${time_source}" "${workers_source}" "${app_source}" "${index_source}" "${runtime_classic_source}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Missing bootstrap API source file: ${required_file}")
    endif()
endforeach()

file(READ "${results_source}" results_js)
file(READ "${schema_source}" schema_js)
file(READ "${data_source}" data_js)
file(READ "${codec_source}" codec_js)
file(READ "${fs_source}" fs_js)
file(READ "${time_source}" time_js)
file(READ "${workers_source}" workers_js)
file(READ "${app_source}" app_js)
file(READ "${index_source}" index_js)
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
        "SLOPPY_E_CODEC_CHECKSUM_UNSUPPORTED_ALGORITHM"
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
        "createBuilder()"
        "module: createModule"
        "dependsOn(...names)"
        "capabilities(callback)"
        "addModule(module)"
        "addDatabase(token, options)"
        "capability token already declared"
        "capability token is not declared"
        "capabilities,"
        "resolveModuleOrder"
        "module dependency missing"
        "module dependency cycle detected"
        "module phase failed"
        "config,"
        "logging,"
        "services,"
        "build()"
        "create()"
        "mapGet(pattern, optionsOrHandler, maybeHandler)"
        "mapPost(pattern, optionsOrHandler, maybeHandler)"
        "mapPut(pattern, optionsOrHandler, maybeHandler)"
        "mapPatch(pattern, optionsOrHandler, maybeHandler)"
        "mapDelete(pattern, optionsOrHandler, maybeHandler)"
        "mapGroup(prefix)"
        "withTags(...tags)"
        "freeze()"
        "isFrozen()"
        "method,"
        "name: null"
        "metadata:"
        "groupPrefix"
        "groupName"
        "withName(name)"
        "__getRoutes()"
        "__debug()"
        "__getModuleGraph()"
        "__getPlanContributions()"
        "addObject(object)"
        "setMinimumLevel(level)"
        "addMemorySink()"
        "addSingleton(token, factoryOrValue)"
        "addTransient(token, factory)"
        "createScope()"
        "starting with '/'"
        "handler must be a function"
        "route: Object.freeze({})")
    require_substring("${app_js}" "${required_pattern}" "app.js is missing expected API shape pattern")
endforeach()

foreach(required_pattern IN ITEMS "export { Sloppy }" "Base64" "Base64Url" "Hex" "Text" "Binary" "Compression" "Checksums" "export {" "data" "sql" "File" "Directory" "Path" "Time" "Deadline" "CancellationController" "BackgroundService" "WorkQueue" "WorkerPool" "Worker" "export { Results }" "export { schema }")
    require_substring("${index_js}" "${required_pattern}" "index.js is missing expected export pattern")
endforeach()

foreach(deferred_pattern IN ITEMS "app.run" "app.listen" "app.build" "addJsonFile" "addEnv" "addConsole")
    reject_substring(
        "${app_js}" "${deferred_pattern}"
        "app.js includes future app-host API outside EPIC-12 skeleton scope")
endforeach()
