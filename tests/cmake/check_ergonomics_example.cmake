set(ergonomics_dir "${PROJECT_SOURCE_DIR}/examples/ergonomics")
set(ergonomics_app "${ergonomics_dir}/app.js")
set(ergonomics_readme "${ergonomics_dir}/README.md")

foreach(required_file IN ITEMS "${ergonomics_app}" "${ergonomics_readme}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Missing ergonomics example file: ${required_file}")
    endif()
endforeach()

if(EXISTS "${ergonomics_dir}/package.json")
    message(FATAL_ERROR "Ergonomics example must not introduce package-manager scope.")
endif()

file(READ "${ergonomics_app}" ergonomics_app_js)
file(READ "${ergonomics_readme}" ergonomics_readme_md)

function(require_substring haystack needle description)
    string(FIND "${haystack}" "${needle}" found_index)
    if(found_index EQUAL -1)
        message(FATAL_ERROR "${description}: ${needle}")
    endif()
endfunction()

foreach(required_pattern IN ITEMS
        "import { Sloppy, Results, schema } from \"sloppy\";"
        "const builder = Sloppy.createBuilder();"
        "builder.config.addObject"
        "builder.logging.addMemorySink();"
        "builder.services.addSingleton(\"users.message\""
        "const app = builder.build();"
        "schema.object"
        "schema.string().min(1)"
        "app.mapGroup(\"/users\")"
        ".withTags(\"Users\")"
        ".withName(\"Users\")"
        "users.mapGet(\"{id:int}\""
        "Results.ok"
        "users.mapGet(\"/search\", { query: SearchQuery }"
        "Results.accepted"
        "Results.noContent"
        "export default app;")
    require_substring(
        "${ergonomics_app_js}" "${required_pattern}"
        "examples/ergonomics/app.js is missing expected API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "Bootstrap developer ergonomics API-shape example"
        "route groups"
        "result helpers"
        "schema metadata"
        "not executed with `sloppy run --artifacts`"
        "`sloppyc` compilation and route-group/schema extraction are still pending"
        "`app.plan.json` is not emitted"
        "Validation metadata is not wired to automatic `400` responses yet"
        "OpenAPI generation is planned separately"
        "runnable"
        "application host")
    require_substring(
        "${ergonomics_readme_md}" "${required_pattern}"
        "examples/ergonomics/README.md is missing required status text")
endforeach()
