set(example_dir "${PROJECT_SOURCE_DIR}/examples/sqlite-basic")
set(example_app "${example_dir}/app.js")
set(example_readme "${example_dir}/README.md")

foreach(required_file IN ITEMS "${example_app}" "${example_readme}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Missing sqlite-basic example file: ${required_file}")
    endif()
endforeach()

if(EXISTS "${example_dir}/package.json")
    message(FATAL_ERROR "sqlite-basic example must not introduce package-manager scope.")
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
        "from \"../../stdlib/sloppy/index.js\";"
        "Sloppy.module(\"data.sqlite\")"
        "caps.addDatabase(\"data.main\""
        "provider: \"sqlite\""
        "path: \":memory:\""
        "capability: \"data.main\""
        "access: \"readwrite\""
        "services.addSingleton(\"data.main\""
        "data.sqlite.open"
        "db.exec`"
        "db.queryOne`"
        "Results.ok"
        "Results.notFound"
        "const lowered = sql`"
        ".addModule(SqliteModule)"
        ".addModule(UsersModule)"
        "export default app;")
    require_substring(
        "${example_app_js}" "${required_pattern}"
        "examples/sqlite-basic/app.js is missing expected SQLite API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "SQLite provider API-shape example"
        "native C SQLite provider opens `:memory:`"
        "V8-enabled runtime tests cover the real SQLite JS-to-native bridge"
        "`data.sqlite.open({ path: \":memory:\", capability: \"data.main\" })` opens a native"
        "V8 runtime installs the SQLite bridge intrinsics"
        "not a `sloppy run --artifacts` app"
        "`sloppyc` does not compile this example"
        "does not emit `app.plan.json`"
        "executable SQLite proof is the internal V8-gated fixture"
        "PostgreSQL and SQL Server providers are covered"
        "PostgreSQL and SQL Server do not have JS-to-native provider bridges yet"
        "no ORM, migrations, connection pooling")
    require_substring(
        "${example_readme_md}" "${required_pattern}"
        "examples/sqlite-basic/README.md is missing required status text")
endforeach()
