if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

set(example_dir "${PROJECT_SOURCE_DIR}/examples/request-context")
set(app_js "${example_dir}/app.js")
set(readme "${example_dir}/README.md")

foreach(path IN ITEMS "${app_js}" "${readme}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Missing request-context example file: ${path}")
    endif()
endforeach()

file(READ "${app_js}" app_source)
foreach(required IN ITEMS
        "import { Sloppy, Results } from \"sloppy\""
        "app.mapGet(\"/users/{id:int}\""
        "route.id"
        "query.q"
        "request.path"
        "Results.json"
        "export default app")
    string(FIND "${app_source}" "${required}" found_index)
    if(found_index EQUAL -1)
        message(FATAL_ERROR "request-context app.js missing expected source: ${required}")
    endif()
endforeach()

file(READ "${readme}" readme_source)
foreach(required IN ITEMS
        "dev-only"
        "route.id"
        "query.q"
        "receives `route.id` as the string"
        "last-wins"
        "body parsing"
        "not implemented")
    string(FIND "${readme_source}" "${required}" found_index)
    if(found_index EQUAL -1)
        message(FATAL_ERROR "request-context README missing honest scope text: ${required}")
    endif()
endforeach()
