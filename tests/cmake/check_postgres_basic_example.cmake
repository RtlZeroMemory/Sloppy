set(example_app "${PROJECT_SOURCE_DIR}/examples/postgres-basic/app.js")
set(example_readme "${PROJECT_SOURCE_DIR}/examples/postgres-basic/README.md")

foreach(required_file IN ITEMS "${example_app}" "${example_readme}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Missing PostgreSQL example file: ${required_file}")
    endif()
endforeach()

file(READ "${example_app}" app_text)
foreach(required_pattern IN ITEMS
    "Sloppy.module"
    "data.postgres"
    "provider: \"postgres\""
    "configKey:"
    "SLOPPY_POSTGRES_TEST_URL"
    "connectionString:"
    "data.postgres.open"
    "maxConnections: 2"
    "placeholderStyle: data.postgres.placeholderStyle"
    "db.transaction("
    "returning id, name"
    "tx.exec"
    "tx.queryOne")
    string(FIND "${app_text}" "${required_pattern}" match_index)
    if(match_index EQUAL -1)
        message(FATAL_ERROR "PostgreSQL example missing pattern: ${required_pattern}")
    endif()
endforeach()

file(READ "${example_readme}" readme_text)
foreach(required_pattern IN ITEMS
    "requires PostgreSQL"
    "SLOPPY_POSTGRES_TEST_URL"
    "not part of default CI live database execution"
    "no migrations"
    "no ORM"
    "pool behavior is a small bounded skeleton"
    "Connection strings must be redacted")
    string(FIND "${readme_text}" "${required_pattern}" match_index)
    if(match_index EQUAL -1)
        message(FATAL_ERROR "PostgreSQL example README missing pattern: ${required_pattern}")
    endif()
endforeach()
