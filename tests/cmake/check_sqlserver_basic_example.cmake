set(example_app "${PROJECT_SOURCE_DIR}/examples/sqlserver-basic/app.js")
set(example_readme "${PROJECT_SOURCE_DIR}/examples/sqlserver-basic/README.md")

foreach(required_file IN ITEMS "${example_app}" "${example_readme}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Missing SQL Server example file: ${required_file}")
    endif()
endforeach()

file(READ "${example_app}" app_text)
foreach(required_pattern IN ITEMS
    "Sloppy.module"
    "data.sqlserver"
    "provider: \"sqlserver\""
    "configKey:"
    "SLOPPY_SQLSERVER_TEST_CONNECTION_STRING"
    "connectionString:"
    "data.sqlserver.open"
    "maxConnections: 2"
    "placeholderStyle: data.sqlserver.placeholderStyle"
    "data.sqlserver.doctor"
    "db.transaction("
    "tx.exec"
    "tx.queryOne")
    string(FIND "${app_text}" "${required_pattern}" match_index)
    if(match_index EQUAL -1)
        message(FATAL_ERROR "SQL Server example missing pattern: ${required_pattern}")
    endif()
endforeach()

file(READ "${example_readme}" readme_text)
foreach(required_pattern IN ITEMS
    "requires SQL Server and Microsoft ODBC Driver 18 for SQL Server"
    "SLOPPY_SQLSERVER_TEST_CONNECTION_STRING"
    "not part of default CI live database execution"
    "no migrations"
    "no ORM"
    "pooling is bounded and provider-owned"
    "true-async ODBC connection/statement mode"
    "unsupported drivers report SQL Server async-driver unavailability"
    "Connection strings must be redacted"
    "TrustServerCertificate=yes")
    string(FIND "${readme_text}" "${required_pattern}" match_index)
    if(match_index EQUAL -1)
        message(FATAL_ERROR "SQL Server example README missing pattern: ${required_pattern}")
    endif()
endforeach()
