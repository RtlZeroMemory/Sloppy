set(hello_dir "${PROJECT_SOURCE_DIR}/examples/hello")
set(hello_app "${hello_dir}/app.js")
set(hello_readme "${hello_dir}/README.md")

foreach(required_file IN ITEMS "${hello_app}" "${hello_readme}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Missing hello example file: ${required_file}")
    endif()
endforeach()

if(EXISTS "${hello_dir}/package.json")
    message(FATAL_ERROR "Hello example must not introduce package-manager scope.")
endif()

file(READ "${hello_app}" hello_app_js)
file(READ "${hello_readme}" hello_readme_md)

function(require_substring haystack needle description)
    string(FIND "${haystack}" "${needle}" found_index)
    if(found_index EQUAL -1)
        message(FATAL_ERROR "${description}: ${needle}")
    endif()
endfunction()

foreach(required_pattern IN ITEMS
        "import { Sloppy, Results } from \"sloppy\";"
        "const builder = Sloppy.createBuilder();"
        "builder.config.addObject"
        "builder.logging.addMemorySink();"
        "builder.services.addSingleton(\"message\", () => \"Hello from Sloppy\");"
        "const app = builder.build();"
        "app.mapGet(\"/\", ({ services }) => Results.text(services.get(\"message\")))"
        ".withName(\"Hello.Index\")"
        "export default app;")
    require_substring(
        "${hello_app_js}" "${required_pattern}"
        "examples/hello/app.js is missing expected API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "Bootstrap app-host example"
        "What to inspect"
        "Current limitations"
        "Runtime Command"
        "not executed with `sloppy run --artifacts`"
        "`sloppyc` does not compile this example"
        "`app.plan.json` is not emitted"
        "Sloppy facade import"
        "not the current execution path")
    require_substring(
        "${hello_readme_md}" "${required_pattern}"
        "examples/hello/README.md is missing required status text")
endforeach()
