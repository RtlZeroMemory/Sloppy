set(results_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/results.js")
set(app_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/app.js")
set(index_source "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/index.js")

foreach(required_file IN ITEMS "${results_source}" "${app_source}" "${index_source}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Missing bootstrap API source file: ${required_file}")
    endif()
endforeach()

file(READ "${results_source}" results_js)
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
        "__sloppyResult: true"
        "kind,"
        "status:"
        "body,"
        "Object.freeze")
    require_substring("${results_js}" "${required_pattern}" "results.js is missing expected API shape pattern")
endforeach()

foreach(required_pattern IN ITEMS
        "create()"
        "mapGet(pattern, handler)"
        "method: \"GET\""
        "name: null"
        "metadata: {}"
        "withName(name)"
        "__getRoutes()"
        "starting with '/'"
        "handler must be a function")
    require_substring("${app_js}" "${required_pattern}" "app.js is missing expected API shape pattern")
endforeach()

foreach(required_pattern IN ITEMS "export { Sloppy }" "export { Results }")
    require_substring("${index_js}" "${required_pattern}" "index.js is missing expected export pattern")
endforeach()

foreach(deferred_pattern IN ITEMS "app.run" "app.listen" "app.build" "app.freeze")
    reject_substring(
        "${app_js}" "${deferred_pattern}"
        "app.js includes future app-host API outside TASK 11.B/11.C scope")
endforeach()
