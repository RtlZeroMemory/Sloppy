if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

set(example_root "${PROJECT_SOURCE_DIR}/examples")
set(framework_examples
    framework-v2-hello
    framework-v2-validation-errors
    framework-v2-explicit-binding
    framework-v2-di-services
    framework-v2-controller
    framework-v2-sqlite-crud
    framework-v2-postgres-crud
    framework-v2-sqlserver-crud)

function(require_file path)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Missing Framework v2 example file: ${path}")
    endif()
endfunction()

function(require_substring haystack needle description)
    string(FIND "${haystack}" "${needle}" found_index)
    if(found_index EQUAL -1)
        message(FATAL_ERROR "${description}: ${needle}")
    endif()
endfunction()

foreach(example_name IN LISTS framework_examples)
    require_file("${example_root}/${example_name}/README.md")
    if(EXISTS "${example_root}/${example_name}/package.json")
        message(FATAL_ERROR "${example_name} must not introduce package-manager scope")
    endif()
endforeach()

foreach(example_name IN ITEMS
        framework-v2-hello
        framework-v2-validation-errors
        framework-v2-explicit-binding
        framework-v2-di-services)
    require_file("${example_root}/${example_name}/app.ts")
endforeach()

foreach(example_name IN ITEMS framework-v2-controller)
    require_file("${example_root}/${example_name}/app.js")
endforeach()

foreach(example_name IN ITEMS
        framework-v2-sqlite-crud
        framework-v2-postgres-crud
        framework-v2-sqlserver-crud)
    require_file("${example_root}/${example_name}/app.ts")
endforeach()

file(READ "${example_root}/framework-v2-hello/app.ts" hello_app)
require_substring("${hello_app}" "Route<string>" "Framework v2 hello typed route binding")
require_substring("${hello_app}" "RequestContext" "Framework v2 hello request context")
require_substring("${hello_app}" "Results.ok" "Framework v2 hello result mapping")

file(READ "${example_root}/framework-v2-validation-errors/app.ts" validation_app)
require_substring("${validation_app}" "type UserCreate" "Framework v2 validation schema type")
require_substring("${validation_app}" "Body<UserCreate>" "Framework v2 validation body binding")
require_substring("${validation_app}" "Results.created" "Framework v2 validation result mapping")

file(READ "${example_root}/framework-v2-explicit-binding/app.ts" binding_app)
foreach(required_pattern IN ITEMS
        "Route<string>"
        "Route<number>"
        "Header<\"x-trace-id\">"
        "Query<boolean>"
        "Body<UserPatch>"
        "RequestContext")
    require_substring(
        "${binding_app}" "${required_pattern}"
        "Framework v2 explicit binding example is missing expected binding")
endforeach()

file(READ "${example_root}/framework-v2-di-services/app.ts" di_app)
foreach(required_pattern IN ITEMS
        "app.services.addSingleton"
        "app.services.addScoped"
        "app.services.addTransient"
        "Service<GreetingService>"
        "Service<RequestCounter>"
        "Service<TransientStamp>")
    require_substring(
        "${di_app}" "${required_pattern}"
        "Framework v2 DI example is missing expected service shape")
endforeach()

file(READ "${example_root}/framework-v2-controller/app.js" controller_app)
foreach(required_pattern IN ITEMS
        "from \"sloppy\""
        "static inject = [\"GreetingService\"]"
        "app.services.addScoped"
        "app.mapController"
        "users.get(\"/{id:int}\", \"get\")")
    require_substring(
        "${controller_app}" "${required_pattern}"
        "Framework v2 controller example is missing expected controller shape")
endforeach()

function(reject_substring haystack needle description)
    string(FIND "${haystack}" "${needle}" found_index)
    if(NOT found_index EQUAL -1)
        message(FATAL_ERROR "${description}: ${needle}")
    endif()
endfunction()

file(READ "${example_root}/framework-v2-sqlite-crud/app.ts" sqlite_app)
require_file("${example_root}/framework-v2-sqlite-crud/appsettings.json")
file(READ "${example_root}/framework-v2-sqlite-crud/appsettings.json" sqlite_config)
require_substring(
    "${sqlite_config}" "\"Providers\""
    "Framework v2 SQLite example is missing normal provider config")
require_substring(
    "${sqlite_config}" "\":memory:\""
    "Framework v2 SQLite example is missing local SQLite database config")
foreach(required_pattern IN ITEMS
        "Sqlite<\"main\">"
        "Body<UserCreate>"
        "Route<PositiveInt>"
        "input.name"
        "input.email"
        "db.query<"
        "db.queryOne<"
        "db.exec("
        "{ signal: ctx.signal, deadline: ctx.deadline }"
        "Results.ok"
        "Results.notFound"
        "Results.created")
    require_substring(
        "${sqlite_app}" "${required_pattern}"
        "Framework v2 SQLite example is missing expected CRUD shape")
endforeach()
foreach(rejected_pattern IN ITEMS
        "capabilities.addDatabase"
        "app.use("
        "app.provider("
        "ctx.request.json"
        "ctx.route.")
    reject_substring(
        "${sqlite_app}" "${rejected_pattern}"
        "Framework v2 SQLite example must not use manual ctx/provider style")
endforeach()

file(READ "${example_root}/framework-v2-postgres-crud/app.ts" postgres_app)
foreach(required_pattern IN ITEMS
        "Postgres<\"main\">"
        "Body<UserCreate>"
        "Route<PositiveInt>"
        "input.name"
        "input.email"
        "{ signal: ctx.signal, deadline: ctx.deadline }"
        "Results.created"
        "Results.ok"
        "Results.notFound")
    require_substring(
        "${postgres_app}" "${required_pattern}"
        "Framework v2 PostgreSQL example is missing expected live provider shape")
endforeach()
foreach(rejected_pattern IN ITEMS
        "capabilities.addDatabase"
        "app.use("
        "app.provider("
        "ctx.request.json"
        "ctx.route.")
    reject_substring(
        "${postgres_app}" "${rejected_pattern}"
        "Framework v2 PostgreSQL example must not use manual ctx/provider style")
endforeach()

file(READ "${example_root}/framework-v2-sqlserver-crud/app.ts" sqlserver_app)
foreach(required_pattern IN ITEMS
        "SqlServer<\"main\">"
        "Body<UserCreate>"
        "Route<PositiveInt>"
        "input.name"
        "input.email"
        "{ signal: ctx.signal, deadline: ctx.deadline }"
        "Results.created"
        "Results.ok"
        "Results.notFound")
    require_substring(
        "${sqlserver_app}" "${required_pattern}"
        "Framework v2 SQL Server example is missing expected live provider shape")
endforeach()
foreach(rejected_pattern IN ITEMS
        "capabilities.addDatabase"
        "app.use("
        "app.provider("
        "ctx.request.json"
        "ctx.route.")
    reject_substring(
        "${sqlserver_app}" "${rejected_pattern}"
        "Framework v2 SQL Server example must not use manual ctx/provider style")
endforeach()

foreach(example_name IN LISTS framework_examples)
    file(READ "${example_root}/${example_name}/README.md" readme_text)
    require_substring(
        "${readme_text}" "This example"
        "${example_name} README is missing useful example text")
endforeach()

file(READ "${example_root}/framework-v2-postgres-crud/README.md" postgres_readme)
foreach(required_pattern IN ITEMS
        "PostgreSQL service"
        "PostgreSQL integration checks"
        "connection string")
    require_substring(
        "${postgres_readme}" "${required_pattern}"
        "Framework v2 PostgreSQL README is missing live provider setup text")
endforeach()

file(READ "${example_root}/framework-v2-sqlserver-crud/README.md" sqlserver_readme)
foreach(required_pattern IN ITEMS
        "SQL Server integration checks"
        "driver cases"
        "Microsoft ODBC Driver 18")
    require_substring(
        "${sqlserver_readme}" "${required_pattern}"
        "Framework v2 SQL Server README is missing live provider setup text")
endforeach()
