set(results_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/results.js")
set(schema_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/schema.js")
set(data_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/data.js")
set(app_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/app.js")
set(index_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/index.js")

foreach(required_file IN ITEMS "${results_source}" "${schema_source}" "${data_source}" "${app_source}" "${index_source}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Missing bootstrap API source file: ${required_file}")
    endif()
endforeach()

file(READ "${results_source}" results_js)
file(READ "${schema_source}" schema_js)
file(READ "${data_source}" data_js)
file(READ "${app_source}" app_js)
file(READ "${index_source}" index_js)

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

foreach(required_pattern IN ITEMS "export { Sloppy }" "export {" "data" "sql" "export { Results }" "export { schema }")
    require_substring("${index_js}" "${required_pattern}" "index.js is missing expected export pattern")
endforeach()

foreach(deferred_pattern IN ITEMS "app.run" "app.listen" "app.build" "addJsonFile" "addEnv" "addConsole")
    reject_substring(
        "${app_js}" "${deferred_pattern}"
        "app.js includes future app-host API outside EPIC-12 skeleton scope")
endforeach()
