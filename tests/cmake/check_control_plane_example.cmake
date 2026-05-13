if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

set(example_dir "${PROJECT_SOURCE_DIR}/examples/control-plane")
set(example_main "${example_dir}/src/main.js")
set(example_readme "${example_dir}/README.md")

foreach(required_file IN ITEMS
        "${example_dir}/sloppy.json"
        "${example_dir}/appsettings.json"
        "${example_dir}/appsettings.Development.json"
        "${example_main}"
        "${example_dir}/src/routes/health.js"
        "${example_dir}/src/routes/projects.js"
        "${example_dir}/src/routes/apps.js"
        "${example_dir}/src/routes/builds.js"
        "${example_dir}/src/routes/deployments.js"
        "${example_dir}/src/routes/diagnostics.js"
        "${example_dir}/src/services/repositories.js"
        "${example_dir}/src/services/diagnosticsSink.js"
        "${example_dir}/src/db/schema.js"
        "${example_dir}/src/db/seed.js"
        "${example_dir}/src/validation/schemas.js"
        "${example_readme}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Missing control-plane example file: ${required_file}")
    endif()
endforeach()

if(EXISTS "${example_dir}/package.json")
    message(FATAL_ERROR "control-plane must not introduce package-manager scope.")
endif()

function(require_token haystack token description)
    string(FIND "${haystack}" "${token}" found_index)
    if(found_index EQUAL -1)
        message(FATAL_ERROR "${description}: ${token}")
    endif()
endfunction()

file(READ "${example_main}" main_source)
file(READ "${example_dir}/src/routes/health.js" health_source)
file(READ "${example_dir}/src/routes/projects.js" projects_source)
file(READ "${example_dir}/src/routes/apps.js" apps_source)
file(READ "${example_dir}/src/routes/builds.js" builds_source)
file(READ "${example_dir}/src/routes/deployments.js" deployments_source)
file(READ "${example_dir}/src/routes/diagnostics.js" diagnostics_source)
file(READ "${example_readme}" readme_source)

foreach(required_pattern IN ITEMS
        "import { ProblemDetails, Sloppy } from \"sloppy\";"
        "import { sqlite } from \"sloppy/providers/sqlite\";"
        "Sloppy.create()"
        "ProblemDetails.defaults()"
        "sqlite(\"main\", { database: \"control-plane.db\" })"
        "app.useModule(healthModule)"
        "app.useModule(projectsModule)"
        "app.useModule(appsModule)"
        "app.useModule(buildsModule)"
        "app.useModule(deploymentsModule)"
        "app.useModule(diagnosticsModule)"
        "export default app;")
    require_token("${main_source}" "${required_pattern}"
                  "control-plane main source is missing expected app-host shape")
endforeach()

foreach(route_source IN ITEMS
        "${health_source}"
        "${projects_source}"
        "${apps_source}"
        "${builds_source}"
        "${deployments_source}"
        "${diagnostics_source}")
    require_token("${route_source}" "import { Results } from \"sloppy\";"
                  "control-plane route module is missing file-local Results import")
endforeach()

foreach(required_pattern IN ITEMS
        "app.provider(\"sqlite:main\")"
        "ctx.query.owner"
        "ctx.request.json()"
        "Results.created("
        "Results.badRequest"
        "Results.notFound"
        ".withName(\"Projects.List\")"
        ".withName(\"Projects.Create\")"
        ".withName(\"Projects.Get\")")
    require_token("${projects_source}" "${required_pattern}"
                  "control-plane projects route is missing expected dogfood shape")
endforeach()

foreach(required_pattern IN ITEMS
        ".withName(\"Apps.List\")"
        ".withName(\"Apps.Create\")"
        ".withName(\"Apps.Get\")")
    require_token("${apps_source}" "${required_pattern}"
                  "control-plane apps route is missing expected dogfood shape")
endforeach()

foreach(required_pattern IN ITEMS
        ".withName(\"Builds.Create\")"
        ".withName(\"Builds.List\")")
    require_token("${builds_source}" "${required_pattern}"
                  "control-plane builds route is missing expected dogfood shape")
endforeach()

foreach(required_pattern IN ITEMS
        ".withName(\"Deployments.Create\")")
    require_token("${deployments_source}" "${required_pattern}"
                  "control-plane deployments route is missing expected dogfood shape")
endforeach()

foreach(required_pattern IN ITEMS
        ".withName(\"Diagnostics.Recent\")"
        "dogfood app bootstrapped")
    require_token("${diagnostics_source}" "${required_pattern}"
                  "control-plane diagnostics route is missing expected dogfood shape")
endforeach()

foreach(required_pattern IN ITEMS
        "# Control plane"
        "`sloppy build` and `sloppy run` project-mode execution"
        "Current coverage"
        "Current limits"
        "No npm package scope is required")
    require_token("${readme_source}" "${required_pattern}"
                  "control-plane README is missing required scope text")
endforeach()
