set(example_dir "${PROJECT_SOURCE_DIR}/examples/data-foundation")
set(example_app "${example_dir}/app.js")
set(example_readme "${example_dir}/README.md")

foreach(required_file IN ITEMS "${example_app}" "${example_readme}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Missing data-foundation example file: ${required_file}")
    endif()
endforeach()

if(EXISTS "${example_dir}/package.json")
    message(FATAL_ERROR "Data-foundation example must not introduce package-manager scope.")
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
        "import {"
        "Sloppy"
        "data"
        "sql"
        "from \"../../stdlib/sloppy/index.js\";"
        "Sloppy.module(\"data\")"
        ".capabilities((caps)"
        "caps.addDatabase(\"data.main\""
        "provider: \"sqlite\""
        "access: \"readwrite\""
        "services.addSingleton(\"data.main\""
        "data.createFakeProvider"
        "query(lowered)"
        "exec(lowered)"
        "db.queryOne`"
        "db.transaction(async (tx)"
        "tx.exec`"
        "const lowered = sql`"
        ".addModule(UsersModule)"
        ".addModule(DataModule)"
        "const app = builder.build();"
        "export default app;")
    require_substring(
        "${example_app_js}" "${required_pattern}"
        "examples/data-foundation/app.js is missing expected data foundation API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "Bootstrap data/capabilities foundation example"
        "What works today"
        "What does not work yet"
        "not a `sloppy run --artifacts` app"
        "`sloppyc` does not compile this example"
        "does not emit `app.plan.json`"
        "real SQLite provider is covered by native C tests"
        "PostgreSQL and SQL Server have separate provider examples"
        "no database connection is opened"
        "no SQL is executed"
        "filesystem and network capabilities are not enforced"
        "examples/sqlserver-basic/"
        "future bare `\"sloppy\"` import is planned only")
    require_substring(
        "${example_readme_md}" "${required_pattern}"
        "examples/data-foundation/README.md is missing required status text")
endforeach()
