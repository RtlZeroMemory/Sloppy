set(example_dir "${PROJECT_SOURCE_DIR}/examples/modules-basic")
set(example_app "${example_dir}/app.js")
set(example_readme "${example_dir}/README.md")

foreach(required_file IN ITEMS "${example_app}" "${example_readme}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Missing modules-basic example file: ${required_file}")
    endif()
endforeach()

if(EXISTS "${example_dir}/package.json")
    message(FATAL_ERROR "Modules-basic example must not introduce package-manager scope.")
endif()

file(READ "${example_app}" example_app_js)
file(READ "${example_readme}" example_readme_md)

function(require_substring haystack needle description)
    string(FIND "${haystack}" "${needle}" found_index)
    if(found_index EQUAL -1)
        message(FATAL_ERROR "${description}: ${needle}")
    endif()
endfunction()

foreach(required_pattern IN ITEMS
        "import { Sloppy, Results } from \"sloppy\";"
        "Sloppy.module(\"data\")"
        "Sloppy.module(\"users\")"
        ".dependsOn(\"data\")"
        ".metadata(\"area\", \"users\")"
        "services.addSingleton(\"data.users\""
        "services.addSingleton(\"users.message\""
        "app.mapGroup(\"/users\")"
        ".withTags(\"Users\")"
        ".mapGet(\"/{id:int}\""
        "Results.ok"
        ".withName(\"Users.Get\")"
        ".addModule(UsersModule)"
        ".addModule(DataModule)"
        "const app = builder.build();"
        "export default app;")
    require_substring(
        "${example_app_js}" "${required_pattern}"
        "examples/modules-basic/app.js is missing expected module API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "Bootstrap module example"
        "What to inspect"
        "Current product state"
        "`sloppy run --artifacts` currently runs emitted artifacts"
        "`sloppyc` compilation and `app.plan.json` emission"
        "in-memory service example"
        "Module package loading and native plugins belong to later module/package work"
        "Bare `\"sloppy\"` imports are the current source shape")
    require_substring(
        "${example_readme_md}" "${required_pattern}"
        "examples/modules-basic/README.md is missing required status text")
endforeach()
