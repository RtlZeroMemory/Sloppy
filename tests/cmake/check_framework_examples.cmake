if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

set(example_root "${PROJECT_SOURCE_DIR}/examples")
set(framework_examples
    framework-hello
    framework-validation-errors
    framework-explicit-binding
    framework-di-services
    framework-controller
    framework-sqlite-crud
    framework-postgres-crud
    framework-sqlserver-crud)

function(require_file path)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Missing framework example file: ${path}")
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
        framework-hello
        framework-validation-errors
        framework-explicit-binding
        framework-di-services)
    require_file("${example_root}/${example_name}/app.ts")
endforeach()

foreach(example_name IN ITEMS framework-controller)
    require_file("${example_root}/${example_name}/app.js")
endforeach()

foreach(example_name IN ITEMS
        framework-sqlite-crud
        framework-postgres-crud
        framework-sqlserver-crud)
    require_file("${example_root}/${example_name}/app.ts")
endforeach()

file(READ "${example_root}/framework-hello/app.ts" hello_app)
require_substring("${hello_app}" "Route<string>" "framework hello typed route binding")
require_substring("${hello_app}" "RequestContext" "framework hello request context")
require_substring("${hello_app}" "Results.ok" "framework hello result mapping")

file(READ "${example_root}/framework-validation-errors/app.ts" validation_app)
require_substring("${validation_app}" "type UserCreate" "framework validation schema type")
require_substring("${validation_app}" "Body<UserCreate>" "framework validation body binding")
require_substring("${validation_app}" "Results.created" "framework validation result mapping")

file(READ "${example_root}/framework-explicit-binding/app.ts" binding_app)
foreach(required_pattern IN ITEMS
        "Route<string>"
        "Route<number>"
        "Header<\"x-trace-id\">"
        "Query<boolean>"
        "Body<UserPatch>"
        "RequestContext")
    require_substring(
        "${binding_app}" "${required_pattern}"
        "framework explicit binding example is missing expected binding")
endforeach()

file(READ "${example_root}/framework-di-services/app.ts" di_app)
foreach(required_pattern IN ITEMS
        "app.services.addSingleton"
        "app.services.addScoped"
        "app.services.addTransient"
        "Service<GreetingService>"
        "Service<RequestCounter>"
        "Service<TransientStamp>")
    require_substring(
        "${di_app}" "${required_pattern}"
        "framework DI example is missing expected service shape")
endforeach()

file(READ "${example_root}/framework-controller/app.js" controller_app)
foreach(required_pattern IN ITEMS
        "from \"sloppy\""
        "static inject = [\"GreetingService\"]"
        "app.services.addScoped"
        "app.mapController"
        "users.get(\"/{id:int}\", \"get\")")
    require_substring(
        "${controller_app}" "${required_pattern}"
        "framework controller example is missing expected controller shape")
endforeach()

function(reject_substring haystack needle description)
    string(FIND "${haystack}" "${needle}" found_index)
    if(NOT found_index EQUAL -1)
        message(FATAL_ERROR "${description}: ${needle}")
    endif()
endfunction()

file(READ "${example_root}/framework-sqlite-crud/app.ts" sqlite_app)
require_file("${example_root}/framework-sqlite-crud/appsettings.json")
file(READ "${example_root}/framework-sqlite-crud/appsettings.json" sqlite_config)
require_substring(
    "${sqlite_config}" "\"Providers\""
    "framework SQLite example is missing normal provider config")
require_substring(
    "${sqlite_config}" "\":memory:\""
    "framework SQLite example is missing local SQLite database config")
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
        "framework SQLite example is missing expected CRUD shape")
endforeach()
foreach(rejected_pattern IN ITEMS
        "capabilities.addDatabase"
        "app.use("
        "app.provider("
        "ctx.request.json"
        "ctx.route.")
    reject_substring(
        "${sqlite_app}" "${rejected_pattern}"
        "framework SQLite example must not use manual ctx/provider style")
endforeach()

file(READ "${example_root}/framework-postgres-crud/app.ts" postgres_app)
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
        "framework PostgreSQL example is missing expected live provider shape")
endforeach()
foreach(rejected_pattern IN ITEMS
        "capabilities.addDatabase"
        "app.use("
        "app.provider("
        "ctx.request.json"
        "ctx.route.")
    reject_substring(
        "${postgres_app}" "${rejected_pattern}"
        "framework PostgreSQL example must not use manual ctx/provider style")
endforeach()

file(READ "${example_root}/framework-sqlserver-crud/app.ts" sqlserver_app)
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
        "framework SQL Server example is missing expected live provider shape")
endforeach()
foreach(rejected_pattern IN ITEMS
        "capabilities.addDatabase"
        "app.use("
        "app.provider("
        "ctx.request.json"
        "ctx.route.")
    reject_substring(
        "${sqlserver_app}" "${rejected_pattern}"
        "framework SQL Server example must not use manual ctx/provider style")
endforeach()

foreach(example_name IN LISTS framework_examples)
    file(READ "${example_root}/${example_name}/README.md" readme_text)
    require_substring(
        "${readme_text}" "## "
        "${example_name} README is missing section content")
endforeach()

file(READ "${example_root}/framework-postgres-crud/README.md" postgres_readme)
foreach(required_pattern IN ITEMS
        "PostgreSQL service"
        "PostgreSQL integration checks"
        "connection string")
    require_substring(
        "${postgres_readme}" "${required_pattern}"
        "framework PostgreSQL README is missing live provider setup text")
endforeach()

file(READ "${example_root}/framework-sqlserver-crud/README.md" sqlserver_readme)
foreach(required_pattern IN ITEMS
        "SQL Server integration checks"
        "driver cases"
        "Microsoft ODBC Driver 18")
    require_substring(
        "${sqlserver_readme}" "${required_pattern}"
        "framework SQL Server README is missing live provider setup text")
endforeach()
