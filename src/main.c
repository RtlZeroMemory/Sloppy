/*
 * Sloppy CLI.
 *
 * Provides plan introspection commands and the development `sloppy run` path.
 * Runtime execution requires V8-enabled artifacts and intentionally avoids production
 * HTTP, package-manager behavior, Node behavior, middleware, and streaming behavior.
 */
#include "sloppy/arena.h"
#include "sloppy/alloc.h"
#include "sloppy/builder.h"
#include "sloppy/breadcrumbs.h"
#include "sloppy/capability.h"
#include "sloppy/checked_math.h"
#include "sloppy/compiler.h"
#include "sloppy/crash_report.h"
#include "sloppy/data_sqlite.h"
#include "sloppy/data_postgres.h"
#include "sloppy/data_sqlserver.h"
#include "sloppy/diagnostics.h"
#include "sloppy/engine.h"
#include "sloppy/fs.h"
#include "sloppy/app_host.h"
#include "sloppy/http.h"
#include "sloppy/http_dispatch.h"
#include "sloppy/http_response.h"
#include "sloppy/http_transport.h"
#include "sloppy/plan.h"
#include "sloppy/platform.h"
#include "sloppy/platform_dynlib.h"
#include "sloppy/platform_process.h"
#include "sloppy/platform_thread.h"
#include "sloppy/route.h"
#include "sloppy/route_artifact.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include "cli/dev_watch_plan.h"
#include "cli/sloppyrc.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <yyjson.h>

#define SL_CLI_MAX_ROUTES 1024U
#define SL_CLI_MAX_HANDLERS 128U
#define SL_CLI_MAX_MODULES 64U
#define SL_CLI_MAX_DEPS 16U
#define SL_CLI_MAX_PROVIDERS 32U
#define SL_CLI_MAX_CAPABILITIES 64U
#define SL_CLI_MAX_ROUTE_BINDINGS 16U
#define SL_CLI_MAX_ROUTE_EFFECTS 16U
#define SL_CLI_MAX_ROUTE_TAGS 16U
#define SL_CLI_MAX_ROUTE_RESPONSES 16U
#define SL_CLI_MAX_ROUTE_HEALTH_CHECKS 32U
#define SL_CLI_MAX_ROUTE_AUTH_ROLES 8U
#define SL_CLI_MAX_ROUTE_AUTH_CLAIMS 8U
#define SL_CLI_MAX_AUTH_SCHEMES 8U
#define SL_CLI_MAX_AUTH_POLICIES 16U
#define SL_CLI_MAX_SCHEMAS 32U
#define SL_CLI_MAX_SCHEMA_PROPERTIES 32U
#define SL_CLI_MAX_DOCTOR_CHECKS 32U
#define SL_CLI_MAX_REQUIRED_FEATURES 32U
#define SL_CLI_MAX_HTTP_CLIENTS 32U
#define SL_CLI_MAX_CONFIG_REQUIREMENTS 64U
#define SL_CLI_MAX_FFI_LIBRARIES 32U
#define SL_CLI_MAX_FFI_FUNCTIONS 128U
#define SL_CLI_MAX_FFI_PARAMETERS 16U
#define SL_CLI_MAX_FFI_STRUCTS 32U
#define SL_CLI_MAX_FFI_STRUCT_FIELDS 32U
#define SL_CLI_MAX_PROGRAM_ARGS 64U
#define SL_CLI_FILE_MAX_BYTES 1048576U
#define SL_CLI_FILE_READ_SCRATCH_BYTES 65536U
#define SL_CLI_ARENA_BYTES 2097152U
#define SL_CLI_DIAG_ARENA_BYTES 65536U
#define SL_RUN_ARTIFACT_FILE_MAX_BYTES SL_CLI_FILE_MAX_BYTES
#define SL_RUN_STDLIB_FILE_MAX_BYTES 524288U
#define SL_CLI_FILE_READ_MAX_BYTES SL_RUN_ARTIFACT_FILE_MAX_BYTES
#define SL_CLI_FILE_READ_ARENA_BYTES (SL_CLI_FILE_READ_MAX_BYTES + SL_CLI_FILE_READ_SCRATCH_BYTES)
#define SL_RUN_FILE_READ_ARENA_BYTES                                                               \
    (SL_RUN_ARTIFACT_FILE_MAX_BYTES + SL_CLI_FILE_READ_SCRATCH_BYTES)
#define SL_RUN_ARENA_BYTES 262144U
#define SL_RUN_PROGRAM_CONTEXT_BYTES 32768U
#define SL_RUN_PLAN_ARENA_BYTES SL_CLI_ARENA_BYTES
#define SL_RUN_ROUTE_ARENA_BYTES 524288U
#define SL_RUN_MAX_ROUTES SL_CLI_MAX_ROUTES
#define SL_RUN_APP_SCOPE_MAX_CLEANUPS 16U
#define SL_RUN_REQUEST_SCOPE_MAX_CLEANUPS 16U
#define SL_RUN_REQUEST_MAX_BYTES 8192U
#define SL_RUN_REQUEST_WIRE_BODY_OVERHEAD_BYTES 4096U
#define SL_RUN_RESPONSE_MAX_BYTES SL_HTTP_TRANSPORT_DEFAULT_RESPONSE_BYTES
#define SL_RUN_LOGGING_ARENA_BYTES 262144U
#define SL_RUN_PLAN_INTERN_BASE_FIELDS 7U
#define SL_RUN_PATH_MAX_BYTES 1024U
#define SL_CREATE_ARENA_BYTES 65536U
#define SL_PACKAGE_MANIFEST_BYTES 16384U
#define SL_RUN_CONFIG_HOST_MAX_BYTES 128U
#define SL_RUN_DEFAULT_HOST "127.0.0.1"
#define SL_RUN_DEFAULT_PORT 5173U
#define SL_RUN_DEFAULT_ENVIRONMENT "Development"
#define SL_RUN_DEFAULT_SOURCE_OUT_DIR ".sloppy"
#ifndef SLOPPY_BOOTSTRAP_BUILD_DIR
#define SLOPPY_BOOTSTRAP_BUILD_DIR ""
#endif
#ifndef SLOPPY_COMPILER_BUILD_PATH
#define SLOPPY_COMPILER_BUILD_PATH ""
#endif

/* Internal CLI fragments keep command parsing, metadata, run, and reporting code bounded while
 * preserving one translation unit for private helper visibility. */
#include "cli/cli_types.inc"
#include "cli/cli_common.inc"
#include "cli/cli_metadata.inc"
#include "cli/cli_run.inc"
#include "cli/cli_create.inc"
#include "cli/cli_package.inc"
#include "cli/cli_lookup.inc"
#include "cli/cli_db.inc"
#include "cli/cli_routes.inc"
#include "cli/cli_ops.inc"
#include "cli/cli_deps.inc"
#include "cli/cli_doctor.inc"
#include "cli/cli_audit.inc"
#include "cli/cli_openapi.inc"
#include "cli/cli_dev.inc"

int main(int argc, char** argv)
{
    SlCliOptions options = {0};

    if (sl_cli_parse_options(argc, argv, &options) != 0) {
        return 1;
    }

    if (options.command != NULL && strcmp(options.command, "--version") == 0) {
        sl_cli_print_version();
        return 0;
    }

    if (options.help || options.command == NULL) {
        if (options.command != NULL) {
            sl_cli_print_command_help(options.command);
        }
        else {
            sl_cli_print_help();
        }
        return 0;
    }

    if (strcmp(options.command, "routes") == 0) {
        return sl_cli_command_routes(&options);
    }
    if (strcmp(options.command, "health") == 0) {
        return sl_cli_command_health(&options);
    }
    if (strcmp(options.command, "metrics") == 0) {
        return sl_cli_command_metrics(&options);
    }
    if (strcmp(options.command, "capabilities") == 0) {
        return sl_cli_command_capabilities(&options);
    }
    if (strcmp(options.command, "deps") == 0) {
        return sl_cli_command_deps(&options);
    }
    if (strcmp(options.command, "run") == 0) {
        return sl_cli_command_run(&options);
    }
    if (strcmp(options.command, "build") == 0) {
        return sl_cli_command_build(&options);
    }
    if (strcmp(options.command, "package") == 0) {
        return sl_cli_command_package(&options);
    }
    if (strcmp(options.command, "dev") == 0) {
        return sl_cli_command_dev(&options);
    }
    if (strcmp(options.command, "create") == 0) {
        return sl_cli_command_create(&options);
    }
    if (strcmp(options.command, "doctor") == 0) {
        return sl_cli_command_doctor(&options);
    }
    if (strcmp(options.command, "db") == 0) {
        return sl_cli_command_db(&options);
    }
    if (strcmp(options.command, "audit") == 0) {
        return sl_cli_command_audit(&options);
    }
    if (strcmp(options.command, "openapi") == 0) {
        return sl_cli_command_openapi(&options);
    }

    sl_cli_write_error_with_value("sloppy: unknown command '", options.command, "'\n");
    return 1;
}
