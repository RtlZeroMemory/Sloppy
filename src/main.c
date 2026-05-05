/*
 * Sloppy CLI.
 *
 * EPIC-19 adds metadata-only introspection commands over plan-compatible JSON files.
 * EPIC-22 adds the dev-only `sloppy run` artifact path. EPIC-23 gives that path a small
 * native response writer and request context while it still avoids production HTTP,
 * package-manager, Node-compatibility, middleware, streaming, and hot reload.
 */
#include "sloppy/arena.h"
#include "sloppy/builder.h"
#include "sloppy/capability.h"
#include "sloppy/checked_math.h"
#include "sloppy/compiler.h"
#include "sloppy/data_postgres.h"
#include "sloppy/data_sqlserver.h"
#include "sloppy/diagnostics.h"
#include "sloppy/engine.h"
#include "sloppy/app_host.h"
#include "sloppy/http.h"
#include "sloppy/http_dispatch.h"
#include "sloppy/http_response.h"
#include "sloppy/http_transport.h"
#include "sloppy/plan.h"
#include "sloppy/platform.h"
#include "sloppy/platform_process.h"
#include "sloppy/route.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

#define SL_CLI_MAX_ROUTES 128U
#define SL_CLI_MAX_HANDLERS 128U
#define SL_CLI_MAX_MODULES 64U
#define SL_CLI_MAX_DEPS 16U
#define SL_CLI_MAX_PROVIDERS 32U
#define SL_CLI_MAX_CAPABILITIES 64U
#define SL_CLI_MAX_ROUTE_BINDINGS 16U
#define SL_CLI_MAX_ROUTE_EFFECTS 16U
#define SL_CLI_MAX_SCHEMAS 32U
#define SL_CLI_MAX_SCHEMA_PROPERTIES 32U
#define SL_CLI_MAX_DOCTOR_CHECKS 32U
#define SL_CLI_FILE_MAX_BYTES 65536U
#define SL_CLI_ARENA_BYTES 65536U
#define SL_RUN_FILE_MAX_BYTES 65536U
#define SL_RUN_ARENA_BYTES 65536U
#define SL_RUN_MAX_ROUTES SL_CLI_MAX_ROUTES
#define SL_RUN_MAX_CLIENTS 4U
#define SL_RUN_APP_SCOPE_MAX_CLEANUPS 16U
#define SL_RUN_REQUEST_SCOPE_MAX_CLEANUPS 16U
#define SL_RUN_REQUEST_MAX_BYTES 8192U
#define SL_RUN_RESPONSE_MAX_BYTES 16384U
#define SL_RUN_TRANSPORT_ARENA_BYTES 1048576U
#define SL_RUN_PLAN_INTERN_BASE_FIELDS 7U
#define SL_RUN_CONFIG_MAX_BYTES 8192U
#define SL_RUN_PATH_MAX_BYTES 1024U
#define SL_RUN_CONFIG_HOST_MAX_BYTES 128U
#define SL_RUN_DEFAULT_HOST "127.0.0.1"
#define SL_RUN_DEFAULT_PORT 5173U
#define SL_RUN_DEFAULT_ENVIRONMENT "Development"
#define SL_RUN_DEFAULT_SOURCE_OUT_DIR ".sloppy/cache/dev/source-input"
#define SL_RUN_DEFAULT_CONFIG_OUT_DIR ".sloppy"
#define SL_RUN_CONFIG_FILE "sloppy.json"
#ifndef SLOPPY_BOOTSTRAP_BUILD_DIR
#define SLOPPY_BOOTSTRAP_BUILD_DIR ""
#endif
#ifndef SLOPPY_COMPILER_BUILD_PATH
#define SLOPPY_COMPILER_BUILD_PATH ""
#endif

typedef enum SlCliFormat
{
    SL_CLI_FORMAT_TEXT,
    SL_CLI_FORMAT_JSON
} SlCliFormat;

typedef struct SlCliSpan
{
    const char* ptr;
    size_t length;
} SlCliSpan;

typedef struct SlCliRoute
{
    SlCliSpan method;
    SlCliSpan pattern;
    SlHandlerId handler_id;
    SlCliSpan name;
    SlCliSpan module;
    SlCliSpan capability;
    SlCliSpan completeness;
    SlCliSpan source_path;
    uint64_t source_line;
    uint64_t source_column;
    SlCliSpan response_kind;
    SlCliSpan response_helper;
    uint64_t response_status;
    SlCliSpan binding_kinds[SL_CLI_MAX_ROUTE_BINDINGS];
    SlCliSpan binding_names[SL_CLI_MAX_ROUTE_BINDINGS];
    SlCliSpan binding_schemas[SL_CLI_MAX_ROUTE_BINDINGS];
    size_t binding_count;
    SlCliSpan effect_providers[SL_CLI_MAX_ROUTE_EFFECTS];
    SlCliSpan effect_provider_kinds[SL_CLI_MAX_ROUTE_EFFECTS];
    SlCliSpan effect_capability_kinds[SL_CLI_MAX_ROUTE_EFFECTS];
    SlCliSpan effect_accesses[SL_CLI_MAX_ROUTE_EFFECTS];
    SlCliSpan effect_operations[SL_CLI_MAX_ROUTE_EFFECTS];
    SlCliSpan effect_reasons[SL_CLI_MAX_ROUTE_EFFECTS];
    size_t effect_count;
    size_t source_order;
} SlCliRoute;

typedef struct SlCliHandler
{
    SlHandlerId id;
    SlCliSpan display_name;
} SlCliHandler;

typedef struct SlCliModule
{
    SlCliSpan name;
    SlCliSpan dependencies[SL_CLI_MAX_DEPS];
    size_t dependency_count;
} SlCliModule;

typedef struct SlCliProvider
{
    SlCliSpan token;
    SlCliSpan provider;
    SlCliSpan capability;
    SlCliSpan required_access;
    SlCliSpan service;
} SlCliProvider;

typedef struct SlCliCapability
{
    SlCliSpan token;
    SlCliSpan kind;
    SlCliSpan access;
    SlCliSpan provider;
} SlCliCapability;

typedef struct SlCliDoctorCheck
{
    SlCliSpan id;
    SlCliSpan status;
    SlCliSpan message;
} SlCliDoctorCheck;

typedef struct SlCliSchemaProperty
{
    SlCliSpan name;
    SlCliSpan kind;
    SlCliSpan format;
    SlCliSpan item_kind;
    bool optional;
} SlCliSchemaProperty;

typedef struct SlCliSchema
{
    SlCliSpan name;
    SlCliSpan kind;
    SlCliSpan item_kind;
    SlCliSpan source_path;
    uint64_t source_line;
    uint64_t source_column;
    SlCliSchemaProperty properties[SL_CLI_MAX_SCHEMA_PROPERTIES];
    size_t property_count;
} SlCliSchema;

typedef struct SlCliMetadata
{
    SlCliRoute routes[SL_CLI_MAX_ROUTES];
    size_t route_count;
    SlCliHandler handlers[SL_CLI_MAX_HANDLERS];
    size_t handler_count;
    SlCliModule modules[SL_CLI_MAX_MODULES];
    size_t module_count;
    SlCliProvider providers[SL_CLI_MAX_PROVIDERS];
    size_t provider_count;
    SlCliCapability capabilities[SL_CLI_MAX_CAPABILITIES];
    size_t capability_count;
    SlCliSchema schemas[SL_CLI_MAX_SCHEMAS];
    size_t schema_count;
    SlCliDoctorCheck doctor_checks[SL_CLI_MAX_DOCTOR_CHECKS];
    size_t doctor_check_count;
    SlCliSpan completeness;
} SlCliMetadata;

typedef struct SlCliOptions
{
    const char* command;
    const char* plan_path;
    const char* output_path;
    const char* input_path;
    const char* artifacts_path;
    const char* stdlib_path;
    const char* host;
    const char* environment;
    const char* once_method;
    const char* once_target;
    uint16_t port;
    SlCliFormat format;
    bool host_explicit;
    bool port_explicit;
    bool environment_explicit;
    bool help;
} SlCliOptions;

static SlCliSpan sl_cli_span_cstr(const char* text)
{
    SlCliSpan span = {text, 0U};
    if (text != NULL) {
        span.length = strlen(text);
    }
    return span;
}

static SlStr sl_cli_span_str(SlCliSpan span)
{
    return sl_str_from_parts(span.ptr, span.length);
}

static SlCliSpan sl_cli_span_from_str(SlStr str)
{
    return (SlCliSpan){str.ptr, str.length};
}

static bool sl_cli_span_empty(SlCliSpan span)
{
    return span.ptr == NULL || span.length == 0U;
}

static bool sl_cli_span_equal(SlCliSpan left, SlCliSpan right)
{
    return sl_str_equal(sl_cli_span_str(left), sl_cli_span_str(right));
}

static bool sl_cli_span_equal_cstr(SlCliSpan left, const char* right)
{
    return sl_cli_span_equal(left, sl_cli_span_cstr(right));
}

static SlCliSpan sl_cli_json_span(yyjson_val* object, const char* name)
{
    yyjson_val* value = yyjson_obj_get(object, name);

    if (value == NULL || !yyjson_is_str(value)) {
        return (SlCliSpan){0};
    }

    return (SlCliSpan){yyjson_get_str(value), yyjson_get_len(value)};
}

static SlHandlerId sl_cli_json_handler_id(yyjson_val* object)
{
    yyjson_val* value = yyjson_obj_get(object, "handlerId");
    uint64_t id = 0U;

    if (value == NULL) {
        value = yyjson_obj_get(object, "handler");
    }
    if (value == NULL || !yyjson_is_uint(value)) {
        return SL_HANDLER_ID_INVALID;
    }

    id = yyjson_get_uint(value);
    if (id > UINT32_MAX) {
        return SL_HANDLER_ID_INVALID;
    }

    return (SlHandlerId)id;
}

static uint64_t sl_cli_json_uint_or_zero(yyjson_val* object, const char* name)
{
    yyjson_val* value = yyjson_obj_get(object, name);
    return value != NULL && yyjson_is_uint(value) ? yyjson_get_uint(value) : 0U;
}

static void sl_cli_parse_source(yyjson_val* object, SlCliSpan* path, uint64_t* line,
                                uint64_t* column)
{
    yyjson_val* source = yyjson_obj_get(object, "source");

    if (path != NULL) {
        *path = (SlCliSpan){0};
    }
    if (line != NULL) {
        *line = 0U;
    }
    if (column != NULL) {
        *column = 0U;
    }
    if (source == NULL || !yyjson_is_obj(source)) {
        return;
    }
    if (path != NULL) {
        *path = sl_cli_json_span(source, "path");
    }
    if (line != NULL) {
        *line = sl_cli_json_uint_or_zero(source, "line");
    }
    if (column != NULL) {
        *column = sl_cli_json_uint_or_zero(source, "column");
    }
}

static void sl_cli_print_version(void)
{
    (void)printf("Sloppy %s\n", SL_VERSION_STRING);
}

static void sl_cli_print_help(void)
{
    sl_cli_print_version();
    (void)printf("Foundation build with dev-only run MVP and metadata CLI introspection.\n\n");
    (void)printf("Usage:\n");
    (void)printf("  sloppy --help\n");
    (void)printf("  sloppy --version\n");
    (void)printf("  sloppy run [source.js|source.ts|--artifacts <dir>] [--stdlib <dir>]\n");
    (void)printf("             [--environment Development] [--host 127.0.0.1] [--port 5173]\n");
    (void)printf("             [--once METHOD TARGET]\n");
    (void)printf("  sloppy routes --plan <path> [--format text|json]\n");
    (void)printf("  sloppy capabilities --plan <path> [--format text|json]\n");
    (void)printf("  sloppy doctor [--plan <path>] [--format text|json]\n");
    (void)printf("  sloppy audit --plan <path> [--format text|json]\n");
    (void)printf("  sloppy openapi --plan <path> [--output <path>]\n");
}

static void sl_cli_print_command_help(const char* command)
{
    if (strcmp(command, "routes") == 0) {
        (void)printf("Usage: sloppy routes --plan <path> [--format text|json]\n");
        return;
    }
    if (strcmp(command, "capabilities") == 0) {
        (void)printf("Usage: sloppy capabilities --plan <path> [--format text|json]\n");
        return;
    }
    if (strcmp(command, "run") == 0) {
        (void)printf(
            "Usage: sloppy run [source.js|source.ts|--artifacts <dir>] [--stdlib <dir>]\n");
        (void)printf("                  [--environment Development] [--host 127.0.0.1]\n");
        (void)printf("                  [--port 5173] [--once METHOD TARGET]\n");
        (void)printf("\n");
        (void)printf("Source input compiles through sloppyc, validates artifacts, then runs the "
                     "artifact path.\n");
        (void)printf("Dev-only MVP. Runtime execution requires a V8-enabled build.\n");
        return;
    }
    if (strcmp(command, "doctor") == 0) {
        (void)printf("Usage: sloppy doctor [--plan <path>] [--format text|json]\n");
        return;
    }
    if (strcmp(command, "audit") == 0) {
        (void)printf("Usage: sloppy audit --plan <path> [--format text|json]\n");
        return;
    }
    if (strcmp(command, "openapi") == 0) {
        (void)printf("Usage: sloppy openapi --plan <path> [--output <path>]\n");
        return;
    }
    sl_cli_print_help();
}

static void sl_cli_write_cstr(FILE* file, const char* text)
{
    (void)fputs(text, file);
}

static void sl_cli_write_error_with_value(const char* prefix, const char* value, const char* suffix)
{
    sl_cli_write_cstr(stderr, prefix);
    if (value != NULL) {
        sl_cli_write_cstr(stderr, value);
    }
    sl_cli_write_cstr(stderr, suffix);
}

static bool sl_cli_parse_port(const char* text, uint16_t* out)
{
    uint32_t value = 0U;
    size_t index = 0U;

    if (text == NULL || text[0] == '\0' || out == NULL) {
        return false;
    }

    while (text[index] != '\0') {
        unsigned char ch = (unsigned char)text[index];
        if (ch < '0' || ch > '9') {
            return false;
        }
        value = (value * 10U) + (uint32_t)(ch - '0');
        if (value == 0U || value > UINT16_MAX) {
            return false;
        }
        index += 1U;
    }

    *out = (uint16_t)value;
    return true;
}

static int sl_cli_parse_run_path_option(int argc, char** argv, int* index, SlCliOptions* out)
{
    if (strcmp(argv[*index], "--artifacts") == 0) {
        if (*index + 1 >= argc) {
            sl_cli_write_cstr(stderr, "sloppy run: --artifacts requires a directory\n");
            return -1;
        }
        if (out->artifacts_path != NULL || out->input_path != NULL) {
            sl_cli_write_cstr(
                stderr,
                "sloppy run: expected either --artifacts <dir> or one positional artifact path\n");
            return -1;
        }
        out->artifacts_path = argv[*index + 1];
        *index += 2;
        return 1;
    }

    if (argv[*index][0] != '-') {
        if (out->input_path != NULL || out->artifacts_path != NULL) {
            sl_cli_write_cstr(
                stderr,
                "sloppy run: expected either --artifacts <dir> or one positional artifact path\n");
            return -1;
        }
        out->input_path = argv[*index];
        *index += 1;
        return 1;
    }

    return 0;
}

static int sl_cli_parse_run_option(int argc, char** argv, int* index, SlCliOptions* out)
{
    int path_result = 0;

    if (argc <= 0 || argv == NULL || index == NULL || out == NULL ||
        strcmp(out->command, "run") != 0)
    {
        return 0;
    }

    if (strcmp(argv[*index], "--host") == 0) {
        if (*index + 1 >= argc) {
            sl_cli_write_cstr(stderr, "sloppy run: --host requires an IPv4 address\n");
            return -1;
        }
        out->host = argv[*index + 1];
        out->host_explicit = true;
        *index += 2;
        return 1;
    }

    if (strcmp(argv[*index], "--environment") == 0) {
        if (*index + 1 >= argc || argv[*index + 1][0] == '\0') {
            sl_cli_write_cstr(stderr, "sloppy run: --environment requires a name\n");
            return -1;
        }
        out->environment = argv[*index + 1];
        out->environment_explicit = true;
        *index += 2;
        return 1;
    }

    if (strcmp(argv[*index], "--stdlib") == 0) {
        if (*index + 1 >= argc) {
            sl_cli_write_cstr(stderr,
                              "sloppy run: --stdlib requires a bootstrap stdlib directory\n");
            return -1;
        }
        out->stdlib_path = argv[*index + 1];
        *index += 2;
        return 1;
    }

    if (strcmp(argv[*index], "--port") == 0) {
        if (*index + 1 >= argc || !sl_cli_parse_port(argv[*index + 1], &out->port)) {
            sl_cli_write_cstr(stderr, "sloppy run: --port requires a value from 1 to 65535\n");
            return -1;
        }
        out->port_explicit = true;
        *index += 2;
        return 1;
    }

    if (strcmp(argv[*index], "--once") == 0) {
        if (*index + 2 >= argc) {
            sl_cli_write_cstr(stderr, "sloppy run: --once requires METHOD and TARGET\n");
            return -1;
        }
        out->once_method = argv[*index + 1];
        out->once_target = argv[*index + 2];
        *index += 3;
        return 1;
    }

    path_result = sl_cli_parse_run_path_option(argc, argv, index, out);
    if (path_result != 0) {
        return path_result;
    }

    return 0;
}

static int sl_cli_parse_common_option(int argc, char** argv, int* index, SlCliOptions* out)
{
    if (argc <= 0 || argv == NULL || index == NULL || out == NULL) {
        return 0;
    }

    if (strcmp(argv[*index], "--help") == 0 || strcmp(argv[*index], "-h") == 0) {
        out->help = true;
        *index += 1;
        return 1;
    }

    if (strcmp(argv[*index], "--format") == 0) {
        if (*index + 1 >= argc) {
            sl_cli_write_cstr(stderr, "sloppy: --format requires text or json\n");
            return -1;
        }
        if (strcmp(argv[*index + 1], "text") == 0) {
            out->format = SL_CLI_FORMAT_TEXT;
        }
        else if (strcmp(argv[*index + 1], "json") == 0) {
            out->format = SL_CLI_FORMAT_JSON;
        }
        else {
            sl_cli_write_error_with_value("sloppy: unsupported format '", argv[*index + 1], "'\n");
            return -1;
        }
        *index += 2;
        return 1;
    }

    if (strcmp(argv[*index], "--plan") == 0 || strcmp(argv[*index], "--app") == 0) {
        if (*index + 1 >= argc) {
            sl_cli_write_error_with_value("sloppy: ", argv[*index], " requires a path\n");
            return -1;
        }
        out->plan_path = argv[*index + 1];
        *index += 2;
        return 1;
    }

    if (strcmp(argv[*index], "--output") == 0) {
        if (*index + 1 >= argc) {
            sl_cli_write_cstr(stderr, "sloppy: --output requires a path\n");
            return -1;
        }
        out->output_path = argv[*index + 1];
        *index += 2;
        return 1;
    }

    return 0;
}

static int sl_cli_parse_options(int argc, char** argv, SlCliOptions* out)
{
    int index = 2;

    if (out == NULL) {
        return 1;
    }

    *out = (SlCliOptions){0};
    out->format = SL_CLI_FORMAT_TEXT;
    out->host = SL_RUN_DEFAULT_HOST;
    out->port = (uint16_t)SL_RUN_DEFAULT_PORT;

    if (argc <= 1) {
        out->help = true;
        return 0;
    }

    if (strcmp(argv[1], "--version") == 0) {
        out->command = "--version";
        return 0;
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        out->help = true;
        return 0;
    }

    out->command = argv[1];

    while (index < argc) {
        int common_parse = sl_cli_parse_common_option(argc, argv, &index, out);
        int run_parse = 0;
        if (common_parse < 0) {
            return 1;
        }
        if (common_parse > 0) {
            continue;
        }

        run_parse = sl_cli_parse_run_option(argc, argv, &index, out);
        if (run_parse < 0) {
            return 1;
        }
        if (run_parse == 0) {
            sl_cli_write_error_with_value("sloppy: unknown option '", argv[index], "'\n");
            return 1;
        }
    }

    return 0;
}

static int sl_read_file_with_messages(const char* path, unsigned char* buffer, size_t capacity,
                                      SlBytes* out, const char* not_found_prefix,
                                      const char* size_prefix)
{
    FILE* file = NULL;
    long size = 0L;
    size_t bytes_read = 0U;

    if (path == NULL || buffer == NULL || out == NULL || not_found_prefix == NULL ||
        size_prefix == NULL)
    {
        return 1;
    }

#ifdef _MSC_VER
    if (fopen_s(&file, path, "rb") != 0) {
        file = NULL;
    }
#else
    file = fopen(path, "rb");
#endif

    if (file == NULL) {
        sl_cli_write_error_with_value(not_found_prefix, path, "\n");
        return 1;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        (void)fclose(file);
        return 1;
    }

    size = ftell(file);
    if (size <= 0L || (size_t)size > capacity) {
        (void)fclose(file);
        sl_cli_write_error_with_value(size_prefix, path, "\n");
        return 1;
    }

    if (fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        return 1;
    }

    bytes_read = fread(buffer, 1U, (size_t)size, file);
    (void)fclose(file);
    if (bytes_read != (size_t)size) {
        return 1;
    }

    *out = sl_bytes_from_parts(buffer, bytes_read);
    return 0;
}

static int sl_cli_read_file(const char* path, unsigned char* buffer, size_t capacity, SlBytes* out)
{
    return sl_read_file_with_messages(
        path, buffer, capacity, out,
        "sloppy: metadata path not found: ", "sloppy: metadata file is empty or too large: ");
}

static bool sl_cli_write_span(FILE* file, SlCliSpan span)
{
    return span.length == 0U || fwrite(span.ptr, 1U, span.length, file) == span.length;
}

static bool sl_cli_write_str(FILE* file, SlStr str)
{
    return str.length == 0U || fwrite(str.ptr, 1U, str.length, file) == str.length;
}

static void sl_cli_json_escape(FILE* file, SlCliSpan span)
{
    static const char hex[] = "0123456789abcdef";
    size_t index = 0U;

    (void)fputc('"', file);
    for (index = 0U; index < span.length; index += 1U) {
        unsigned char ch = (unsigned char)span.ptr[index];
        if (ch == '"' || ch == '\\') {
            (void)fputc('\\', file);
            (void)fputc((int)ch, file);
        }
        else if (ch == '\n') {
            (void)fputs("\\n", file);
        }
        else if (ch == '\r') {
            (void)fputs("\\r", file);
        }
        else if (ch == '\t') {
            (void)fputs("\\t", file);
        }
        else if (ch == '\b') {
            (void)fputs("\\b", file);
        }
        else if (ch == '\f') {
            (void)fputs("\\f", file);
        }
        else if (ch < 0x20U) {
            (void)fputs("\\u00", file);
            (void)fputc(hex[(ch >> 4U) & 0x0FU], file);
            (void)fputc(hex[ch & 0x0FU], file);
        }
        else {
            (void)fputc((int)ch, file);
        }
    }
    (void)fputc('"', file);
}

static SlStr sl_cli_redacted_span(SlArena* arena, SlCliSpan text)
{
    SlStr redacted = sl_diag_redacted();
    SlStatus status;

    if (arena == NULL) {
        return redacted;
    }

    status = sl_diag_redact_secrets(arena, sl_cli_span_str(text), &redacted);
    return sl_status_is_ok(status) ? redacted : sl_diag_redacted();
}

static void sl_cli_write_redacted(FILE* file, SlArena* arena, SlCliSpan text)
{
    SlArenaMark mark = {0U};
    SlStr redacted = sl_diag_redacted();
    bool should_reset = false;

    if (arena != NULL) {
        mark = sl_arena_mark(arena);
        should_reset = true;
        redacted = sl_cli_redacted_span(arena, text);
    }
    (void)sl_cli_write_str(file, redacted);
    if (should_reset) {
        (void)sl_arena_reset_to(arena, mark);
    }
}

static void sl_cli_json_redacted(FILE* file, SlArena* arena, SlCliSpan text)
{
    SlArenaMark mark = {0U};
    SlStr redacted = sl_diag_redacted();
    bool should_reset = false;

    if (arena != NULL) {
        mark = sl_arena_mark(arena);
        should_reset = true;
        redacted = sl_cli_redacted_span(arena, text);
    }
    sl_cli_json_escape(file, sl_cli_span_from_str(redacted));
    if (should_reset) {
        (void)sl_arena_reset_to(arena, mark);
    }
}

static void sl_cli_json_escape_lower(FILE* file, SlCliSpan span)
{
    size_t index = 0U;

    (void)fputc('"', file);
    for (index = 0U; index < span.length; index += 1U) {
        char ch = span.ptr[index];
        if (ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch - 'A' + 'a');
        }
        if (ch == '"' || ch == '\\') {
            (void)fputc('\\', file);
        }
        (void)fputc(ch, file);
    }
    (void)fputc('"', file);
}

static void sl_cli_write_u64(FILE* file, uint64_t value)
{
    char digits[32];
    size_t count = 0U;

    do {
        digits[count] = (char)('0' + (value % 10U));
        value /= 10U;
        count += 1U;
    } while (value > 0U && count < sizeof(digits));

    while (count > 0U) {
        count -= 1U;
        (void)fputc(digits[count], file);
    }
}

static int sl_cli_parse_handlers(yyjson_val* root, SlCliMetadata* metadata)
{
    yyjson_val* handlers = yyjson_obj_get(root, "handlers");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;

    if (handlers == NULL || !yyjson_is_arr(handlers)) {
        return 0;
    }

    yyjson_arr_iter_init(handlers, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        yyjson_val* id_value = yyjson_obj_get(value, "id");
        uint64_t id = 0U;

        if (!yyjson_is_obj(value) || id_value == NULL || !yyjson_is_uint(id_value)) {
            continue;
        }
        if (metadata->handler_count >= SL_CLI_MAX_HANDLERS) {
            sl_cli_write_cstr(stderr, "sloppy: too many handlers in metadata\n");
            return 1;
        }
        id = yyjson_get_uint(id_value);
        if (id > UINT32_MAX) {
            continue;
        }
        metadata->handlers[metadata->handler_count].id = (SlHandlerId)id;
        metadata->handlers[metadata->handler_count].display_name =
            sl_cli_json_span(value, "displayName");
        metadata->handler_count += 1U;
    }

    return 0;
}

static void sl_cli_parse_route_bindings(yyjson_val* value, SlCliRoute* route)
{
    yyjson_val* bindings = yyjson_obj_get(value, "bindings");
    yyjson_val* entry = NULL;
    yyjson_arr_iter iter;

    if (bindings == NULL || !yyjson_is_arr(bindings)) {
        return;
    }

    yyjson_arr_iter_init(bindings, &iter);
    while ((entry = yyjson_arr_iter_next(&iter)) != NULL) {
        if (yyjson_is_obj(entry) && route->binding_count < SL_CLI_MAX_ROUTE_BINDINGS) {
            route->binding_kinds[route->binding_count] = sl_cli_json_span(entry, "kind");
            route->binding_names[route->binding_count] = sl_cli_json_span(entry, "name");
            route->binding_schemas[route->binding_count] = sl_cli_json_span(entry, "schema");
            route->binding_count += 1U;
        }
    }
}

static void sl_cli_parse_route_effects(yyjson_val* value, SlCliRoute* route)
{
    yyjson_val* effects = yyjson_obj_get(value, "effects");
    yyjson_val* entry = NULL;
    yyjson_arr_iter iter;

    if (effects == NULL || !yyjson_is_arr(effects)) {
        return;
    }

    yyjson_arr_iter_init(effects, &iter);
    while ((entry = yyjson_arr_iter_next(&iter)) != NULL) {
        if (yyjson_is_obj(entry) && route->effect_count < SL_CLI_MAX_ROUTE_EFFECTS) {
            route->effect_providers[route->effect_count] = sl_cli_json_span(entry, "provider");
            route->effect_provider_kinds[route->effect_count] =
                sl_cli_json_span(entry, "providerKind");
            route->effect_capability_kinds[route->effect_count] =
                sl_cli_json_span(entry, "capabilityKind");
            route->effect_accesses[route->effect_count] = sl_cli_json_span(entry, "access");
            route->effect_operations[route->effect_count] = sl_cli_json_span(entry, "operation");
            route->effect_reasons[route->effect_count] = sl_cli_json_span(entry, "reason");
            route->effect_count += 1U;
        }
    }
}

static void sl_cli_parse_route_strong_metadata(yyjson_val* value, SlCliRoute* route)
{
    yyjson_val* completeness = yyjson_obj_get(value, "completeness");
    yyjson_val* response = yyjson_obj_get(value, "response");

    if (completeness != NULL && yyjson_is_obj(completeness)) {
        route->completeness = sl_cli_json_span(completeness, "status");
    }
    if (response != NULL && yyjson_is_obj(response)) {
        route->response_kind = sl_cli_json_span(response, "kind");
        route->response_helper = sl_cli_json_span(response, "helper");
        route->response_status = sl_cli_json_uint_or_zero(response, "status");
    }
    sl_cli_parse_route_bindings(value, route);
    sl_cli_parse_route_effects(value, route);
}

static int sl_cli_parse_routes(yyjson_val* root, SlCliMetadata* metadata)
{
    yyjson_val* routes = yyjson_obj_get(root, "routes");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;

    if (routes == NULL) {
        return 0;
    }
    if (!yyjson_is_arr(routes)) {
        sl_cli_write_cstr(stderr, "sloppy: malformed metadata: routes must be an array\n");
        return 1;
    }

    yyjson_arr_iter_init(routes, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        SlCliRoute route = {0};

        if (!yyjson_is_obj(value)) {
            sl_cli_write_cstr(stderr,
                              "sloppy: malformed metadata: route entries must be objects\n");
            return 1;
        }
        if (metadata->route_count >= SL_CLI_MAX_ROUTES) {
            sl_cli_write_cstr(stderr, "sloppy: too many routes in metadata\n");
            return 1;
        }

        route.method = sl_cli_json_span(value, "method");
        route.pattern = sl_cli_json_span(value, "pattern");
        route.name = sl_cli_json_span(value, "name");
        route.module = sl_cli_json_span(value, "module");
        route.capability = sl_cli_json_span(value, "capability");
        sl_cli_parse_route_strong_metadata(value, &route);
        route.handler_id = sl_cli_json_handler_id(value);
        sl_cli_parse_source(value, &route.source_path, &route.source_line, &route.source_column);
        route.source_order = metadata->route_count;

        if (sl_cli_span_empty(route.method) || sl_cli_span_empty(route.pattern) ||
            route.handler_id == SL_HANDLER_ID_INVALID)
        {
            sl_cli_write_cstr(stderr, "sloppy: malformed metadata: route requires method, pattern, "
                                      "and handlerId\n");
            return 1;
        }

        metadata->routes[metadata->route_count] = route;
        metadata->route_count += 1U;
    }

    return 0;
}

static int sl_cli_parse_modules(yyjson_val* root, SlCliMetadata* metadata)
{
    yyjson_val* modules = yyjson_obj_get(root, "modules");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;

    if (modules == NULL) {
        return 0;
    }
    if (!yyjson_is_arr(modules)) {
        sl_cli_write_cstr(stderr, "sloppy: malformed metadata: modules must be an array\n");
        return 1;
    }

    yyjson_arr_iter_init(modules, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        SlCliModule module = {0};
        yyjson_val* deps = NULL;
        yyjson_val* dep = NULL;
        yyjson_arr_iter dep_iter;

        if (!yyjson_is_obj(value)) {
            sl_cli_write_cstr(stderr, "sloppy: malformed module in metadata\n");
            return 1;
        }
        if (metadata->module_count >= SL_CLI_MAX_MODULES) {
            sl_cli_write_cstr(stderr, "sloppy: too many modules in metadata\n");
            return 1;
        }
        module.name = sl_cli_json_span(value, "name");
        deps = yyjson_obj_get(value, "dependencies");
        if (deps != NULL && yyjson_is_arr(deps)) {
            yyjson_arr_iter_init(deps, &dep_iter);
            while ((dep = yyjson_arr_iter_next(&dep_iter)) != NULL) {
                if (yyjson_is_str(dep) && module.dependency_count < SL_CLI_MAX_DEPS) {
                    module.dependencies[module.dependency_count] =
                        (SlCliSpan){yyjson_get_str(dep), yyjson_get_len(dep)};
                    module.dependency_count += 1U;
                }
            }
        }
        metadata->modules[metadata->module_count] = module;
        metadata->module_count += 1U;
    }

    return 0;
}

static int sl_cli_parse_providers(yyjson_val* root, SlCliMetadata* metadata)
{
    yyjson_val* providers = yyjson_obj_get(root, "dataProviders");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;

    if (providers == NULL) {
        return 0;
    }
    if (!yyjson_is_arr(providers)) {
        sl_cli_write_cstr(stderr, "sloppy: malformed metadata: dataProviders must be an array\n");
        return 1;
    }

    yyjson_arr_iter_init(providers, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        SlCliProvider provider = {0};

        if (!yyjson_is_obj(value)) {
            sl_cli_write_cstr(stderr, "sloppy: malformed data provider in metadata\n");
            return 1;
        }
        if (metadata->provider_count >= SL_CLI_MAX_PROVIDERS) {
            sl_cli_write_cstr(stderr, "sloppy: too many data providers in metadata\n");
            return 1;
        }
        provider.token = sl_cli_json_span(value, "token");
        provider.provider = sl_cli_json_span(value, "provider");
        provider.capability = sl_cli_json_span(value, "capability");
        provider.required_access = sl_cli_json_span(value, "requiredAccess");
        provider.service = sl_cli_json_span(value, "service");
        metadata->providers[metadata->provider_count] = provider;
        metadata->provider_count += 1U;
    }

    return 0;
}

static int sl_cli_parse_capabilities(yyjson_val* root, SlCliMetadata* metadata)
{
    yyjson_val* capabilities = yyjson_obj_get(root, "capabilities");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;

    if (capabilities == NULL) {
        return 0;
    }
    if (!yyjson_is_arr(capabilities)) {
        sl_cli_write_cstr(stderr, "sloppy: malformed metadata: capabilities must be an array\n");
        return 1;
    }

    yyjson_arr_iter_init(capabilities, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        SlCliCapability capability = {0};

        if (!yyjson_is_obj(value)) {
            sl_cli_write_cstr(stderr, "sloppy: malformed capability in metadata\n");
            return 1;
        }
        if (metadata->capability_count >= SL_CLI_MAX_CAPABILITIES) {
            sl_cli_write_cstr(stderr, "sloppy: too many capabilities in metadata\n");
            return 1;
        }
        capability.token = sl_cli_json_span(value, "token");
        capability.kind = sl_cli_json_span(value, "kind");
        capability.access = sl_cli_json_span(value, "access");
        capability.provider = sl_cli_json_span(value, "provider");
        metadata->capabilities[metadata->capability_count] = capability;
        metadata->capability_count += 1U;
    }

    return 0;
}

static void sl_cli_parse_schema_property(yyjson_val* property_value, SlCliSpan name,
                                         SlCliSchema* schema)
{
    SlCliSchemaProperty property = {0};
    yyjson_val* optional = NULL;
    yyjson_val* items = NULL;

    if (!yyjson_is_obj(property_value) || schema->property_count >= SL_CLI_MAX_SCHEMA_PROPERTIES) {
        return;
    }
    property.name = name;
    property.kind = sl_cli_json_span(property_value, "kind");
    property.format = sl_cli_json_span(property_value, "format");
    optional = yyjson_obj_get(property_value, "optional");
    property.optional = optional != NULL && yyjson_is_bool(optional) && yyjson_get_bool(optional);
    items = yyjson_obj_get(property_value, "items");
    if (items != NULL && yyjson_is_obj(items)) {
        property.item_kind = sl_cli_json_span(items, "kind");
    }
    schema->properties[schema->property_count] = property;
    schema->property_count += 1U;
}

static void sl_cli_parse_schema_properties(yyjson_val* schema_value, SlCliSchema* schema)
{
    yyjson_val* definition = yyjson_obj_get(schema_value, "definition");
    yyjson_val* properties = NULL;
    yyjson_val* items = NULL;
    yyjson_obj_iter iter;
    yyjson_val* key = NULL;

    if (definition == NULL || !yyjson_is_obj(definition)) {
        return;
    }
    schema->kind = sl_cli_json_span(definition, "kind");
    items = yyjson_obj_get(definition, "items");
    if (items != NULL && yyjson_is_obj(items)) {
        schema->item_kind = sl_cli_json_span(items, "kind");
    }
    properties = yyjson_obj_get(definition, "properties");
    if (properties == NULL || !yyjson_is_obj(properties)) {
        return;
    }
    iter = yyjson_obj_iter_with(properties);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL) {
        const char* property_name = yyjson_get_str(key);
        yyjson_val* property_value = NULL;

        if (property_name == NULL) {
            continue;
        }
        property_value = yyjson_obj_get(properties, property_name);
        sl_cli_parse_schema_property(property_value,
                                     (SlCliSpan){property_name, yyjson_get_len(key)}, schema);
    }
}

static int sl_cli_parse_schemas(yyjson_val* root, SlCliMetadata* metadata)
{
    yyjson_val* schemas = yyjson_obj_get(root, "schemas");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;

    if (schemas == NULL) {
        return 0;
    }
    if (!yyjson_is_arr(schemas)) {
        sl_cli_write_cstr(stderr, "sloppy: malformed metadata: schemas must be an array\n");
        return 1;
    }

    yyjson_arr_iter_init(schemas, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        SlCliSchema schema = {0};

        if (!yyjson_is_obj(value)) {
            sl_cli_write_cstr(stderr, "sloppy: malformed schema in metadata\n");
            return 1;
        }
        if (metadata->schema_count >= SL_CLI_MAX_SCHEMAS) {
            sl_cli_write_cstr(stderr, "sloppy: too many schemas in metadata\n");
            return 1;
        }
        schema.name = sl_cli_json_span(value, "name");
        sl_cli_parse_source(value, &schema.source_path, &schema.source_line, &schema.source_column);
        sl_cli_parse_schema_properties(value, &schema);
        metadata->schemas[metadata->schema_count] = schema;
        metadata->schema_count += 1U;
    }

    return 0;
}

static int sl_cli_parse_doctor_checks(yyjson_val* root, SlCliMetadata* metadata)
{
    yyjson_val* checks = yyjson_obj_get(root, "doctorChecks");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;

    if (checks == NULL) {
        return 0;
    }
    if (!yyjson_is_arr(checks)) {
        sl_cli_write_cstr(stderr, "sloppy: malformed metadata: doctorChecks must be an array\n");
        return 1;
    }

    yyjson_arr_iter_init(checks, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        SlCliDoctorCheck check = {0};

        if (!yyjson_is_obj(value)) {
            sl_cli_write_cstr(stderr, "sloppy: malformed doctor check in metadata\n");
            return 1;
        }
        if (metadata->doctor_check_count >= SL_CLI_MAX_DOCTOR_CHECKS) {
            sl_cli_write_cstr(stderr, "sloppy: too many doctor checks in metadata\n");
            return 1;
        }
        check.id = sl_cli_json_span(value, "id");
        check.status = sl_cli_json_span(value, "status");
        check.message = sl_cli_json_span(value, "message");
        if (sl_cli_span_empty(check.id) || sl_cli_span_empty(check.status)) {
            sl_cli_write_cstr(stderr, "sloppy: doctor check is missing id or status\n");
            return 1;
        }
        metadata->doctor_checks[metadata->doctor_check_count] = check;
        metadata->doctor_check_count += 1U;
    }

    return 0;
}

static int sl_cli_validate_native_plan(const char* path, SlBytes json, SlArena* arena)
{
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlPlanParseOptions options = {sl_str_from_cstr(path)};
    SlStatus status = sl_plan_parse_json(arena, json, &options, &plan, &diag);

    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy: invalid app plan metadata: ");
        if (!sl_str_is_empty(diag.message)) {
            sl_cli_write_span(stderr, sl_cli_span_from_str(diag.message));
        }
        else {
            sl_cli_write_cstr(stderr, "native Plan v1 validation failed");
        }
        sl_cli_write_cstr(stderr, "\n");
        return 1;
    }

    return 0;
}

static int sl_cli_load_metadata(const char* path, unsigned char* json_storage, SlArena* plan_arena,
                                bool validate_native_plan, yyjson_doc** out_doc,
                                SlCliMetadata* out_metadata)
{
    SlBytes json = {0};
    yyjson_read_err error = {0};
    yyjson_val* root = NULL;

    if (sl_cli_read_file(path, json_storage, SL_CLI_FILE_MAX_BYTES, &json) != 0) {
        return 1;
    }

    if (validate_native_plan && sl_cli_validate_native_plan(path, json, plan_arena) != 0) {
        return 1;
    }

    *out_doc = yyjson_read_opts((char*)json.ptr, json.length, 0U, NULL, &error);
    if (*out_doc == NULL) {
        sl_cli_write_error_with_value("sloppy: malformed metadata JSON: ", error.msg, "\n");
        return 1;
    }

    root = yyjson_doc_get_root(*out_doc);
    if (!yyjson_is_obj(root)) {
        sl_cli_write_cstr(stderr, "sloppy: malformed metadata: root must be an object\n");
        return 1;
    }

    *out_metadata = (SlCliMetadata){0};
    {
        yyjson_val* completeness = yyjson_obj_get(root, "completeness");
        if (completeness != NULL && yyjson_is_obj(completeness)) {
            out_metadata->completeness = sl_cli_json_span(completeness, "status");
        }
    }
    if (sl_cli_parse_handlers(root, out_metadata) != 0 ||
        sl_cli_parse_routes(root, out_metadata) != 0 ||
        sl_cli_parse_modules(root, out_metadata) != 0 ||
        sl_cli_parse_providers(root, out_metadata) != 0 ||
        sl_cli_parse_capabilities(root, out_metadata) != 0 ||
        sl_cli_parse_schemas(root, out_metadata) != 0 ||
        sl_cli_parse_doctor_checks(root, out_metadata) != 0)
    {
        return 1;
    }

    return 0;
}

static const SlCliHandler* sl_cli_find_handler(const SlCliMetadata* metadata, SlHandlerId id)
{
    size_t index = 0U;

    for (index = 0U; index < metadata->handler_count; index += 1U) {
        if (metadata->handlers[index].id == id) {
            return &metadata->handlers[index];
        }
    }

    return NULL;
}

typedef struct SlRunApp
{
    unsigned char plan_json_storage[SL_RUN_FILE_MAX_BYTES];
    unsigned char metadata_json_storage[SL_RUN_FILE_MAX_BYTES];
    unsigned char bootstrap_js_storage[SL_RUN_FILE_MAX_BYTES];
    unsigned char app_js_storage[SL_RUN_FILE_MAX_BYTES];
    unsigned char plan_arena_storage[SL_RUN_ARENA_BYTES];
    unsigned char route_arena_storage[SL_RUN_ARENA_BYTES];
    unsigned char engine_arena_storage[SL_RUN_ARENA_BYTES];
    SlScopeCleanup app_cleanups[SL_RUN_APP_SCOPE_MAX_CLEANUPS];
    SlArena plan_arena;
    SlArena route_arena;
    SlArena engine_arena;
    SlPlan plan;
    SlInternTable plan_metadata_interns;
    SlRuntimeFeatureSet runtime_features;
    SlCapabilityRegistry capability_registry;
    SlBytes app_js_bytes;
    SlBytes source_map_bytes;
    SlEngine* engine;
    SlAppLifecycle lifecycle;
    SlHttpRouteTable route_table;
    char config_host[SL_RUN_CONFIG_HOST_MAX_BYTES];
    uint16_t config_port;
    uint64_t config_keep_alive_idle_timeout_ms;
    uint64_t config_max_requests_per_connection;
    uint64_t next_request_id;
    bool config_keep_alive_enabled;
    bool config_has_host;
    bool config_has_port;
    bool config_has_keep_alive_enabled;
    bool config_has_keep_alive_idle_timeout_ms;
    bool config_has_max_requests_per_connection;
} SlRunApp;

static bool sl_run_copy_json_string(char* buffer, size_t capacity, yyjson_val* value);

static bool sl_run_span_ends_with(SlCliSpan span, const char* suffix)
{
    size_t suffix_length = strlen(suffix);

    if (span.length < suffix_length) {
        return false;
    }

    return memcmp(span.ptr + span.length - suffix_length, suffix, suffix_length) == 0;
}

static bool sl_run_path_component_is_safe(SlCliSpan component)
{
    if (component.length == 0U) {
        return false;
    }
    if (component.length == 1U && component.ptr[0] == '.') {
        return false;
    }
    return !(component.length == 2U && component.ptr[0] == '.' && component.ptr[1] == '.');
}

static bool sl_run_relative_artifact_path_is_safe(SlCliSpan leaf)
{
    size_t index = 0U;
    size_t component_start = 0U;

    if (leaf.ptr == NULL || leaf.length == 0U || leaf.ptr[0] == '/' || leaf.ptr[0] == '\\') {
        return false;
    }

    for (index = 0U; index < leaf.length; index += 1U) {
        char ch = leaf.ptr[index];
        if (ch == ':') {
            return false;
        }
        if (ch == '/' || ch == '\\') {
            SlCliSpan component = {leaf.ptr + component_start, index - component_start};
            if (!sl_run_path_component_is_safe(component)) {
                return false;
            }
            component_start = index + 1U;
        }
    }

    return sl_run_path_component_is_safe(
        (SlCliSpan){leaf.ptr + component_start, leaf.length - component_start});
}

static bool sl_run_join_path(char* buffer, size_t capacity, const char* dir, SlCliSpan leaf)
{
    SlStringBuilder builder = {0};
    SlStr joined = {0};
    size_t dir_length = 0U;
    size_t index = 0U;
    bool needs_separator = true;
    SlStatus status;

    if (buffer == NULL || capacity == 0U || dir == NULL || leaf.ptr == NULL || leaf.length == 0U) {
        return false;
    }

    if (!sl_run_relative_artifact_path_is_safe(leaf)) {
        return false;
    }

    dir_length = strlen(dir);
    if (dir_length == 0U) {
        return false;
    }

    needs_separator = dir[dir_length - 1U] != '/' && dir[dir_length - 1U] != '\\';

    status = sl_string_builder_init_fixed(&builder, buffer, capacity);
    if (!sl_status_is_ok(status)) {
        return false;
    }
    status = sl_string_builder_append_cstr(&builder, dir);
    if (!sl_status_is_ok(status)) {
        return false;
    }
    if (needs_separator) {
        status = sl_string_builder_append_char(&builder, '/');
        if (!sl_status_is_ok(status)) {
            return false;
        }
    }
    for (index = 0U; index < leaf.length; index += 1U) {
        char ch = leaf.ptr[index];
        if (ch == '\\') {
            ch = '/';
        }
        status = sl_string_builder_append_char(&builder, ch);
        if (!sl_status_is_ok(status)) {
            return false;
        }
    }

    status = sl_string_builder_view_with_nul(&builder, &joined);
    return sl_status_is_ok(status) && joined.ptr == buffer;
}

static SlCliSpan sl_run_str_span(SlStr str)
{
    return (SlCliSpan){str.ptr, str.length};
}

static bool sl_run_artifact_file_path(char* buffer, size_t capacity, const char* dir,
                                      const char* leaf)
{
    return sl_run_join_path(buffer, capacity, dir, sl_cli_span_cstr(leaf));
}

static int sl_run_read_file(const char* path, unsigned char* buffer, size_t capacity, SlBytes* out)
{
    return sl_read_file_with_messages(path, buffer, capacity, out,
                                      "sloppy run: artifact path not found: ",
                                      "sloppy run: artifact file is empty or too large: ");
}

static int sl_run_read_stdlib_file(const char* path, unsigned char* buffer, size_t capacity,
                                   SlBytes* out)
{
    return sl_read_file_with_messages(
        path, buffer, capacity, out,
        "sloppy run: stdlib asset missing: ", "sloppy run: stdlib asset is empty or too large: ");
}

static bool sl_run_json_string_equals_cstr(yyjson_val* value, const char* expected)
{
    const char* text = NULL;
    size_t expected_length = 0U;
    size_t text_length = 0U;

    if (value == NULL || expected == NULL || !yyjson_is_str(value)) {
        return false;
    }

    text = yyjson_get_str(value);
    text_length = yyjson_get_len(value);
    expected_length = strlen(expected);

    return text != NULL && text_length == expected_length &&
           memcmp(text, expected, expected_length) == 0;
}

static bool sl_run_manifest_array_contains_asset(yyjson_val* array, const char* required_asset)
{
    yyjson_arr_iter iter;
    yyjson_val* value = NULL;

    if (array == NULL || required_asset == NULL || !yyjson_is_arr(array)) {
        return false;
    }

    yyjson_arr_iter_init(array, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        if (sl_run_json_string_equals_cstr(value, required_asset)) {
            return true;
        }
        if (yyjson_is_obj(value) &&
            sl_run_json_string_equals_cstr(yyjson_obj_get(value, "path"), required_asset))
        {
            return true;
        }
    }

    return false;
}

static bool sl_run_manifest_is_compatible(SlBytes manifest, const char* required_stdlib_version,
                                          const char* required_asset)
{
    yyjson_read_err error = {0};
    yyjson_doc* doc = NULL;
    yyjson_val* root = NULL;
    bool compatible = false;

    if (manifest.ptr == NULL || manifest.length == 0U || required_stdlib_version == NULL ||
        required_asset == NULL)
    {
        return false;
    }

    doc = yyjson_read_opts((char*)manifest.ptr, manifest.length, 0U, NULL, &error);
    if (doc == NULL) {
        return false;
    }

    root = yyjson_doc_get_root(doc);
    if (yyjson_is_obj(root) &&
        sl_run_json_string_equals_cstr(yyjson_obj_get(root, "stdlibVersion"),
                                       required_stdlib_version) &&
        (sl_run_manifest_array_contains_asset(yyjson_obj_get(root, "modules"), required_asset) ||
         sl_run_manifest_array_contains_asset(yyjson_obj_get(root, "assets"), required_asset)))
    {
        compatible = true;
    }

    yyjson_doc_free(doc);
    return compatible;
}

static bool sl_run_plan_config_parse_port(yyjson_val* value, uint16_t* out)
{
    uint64_t port = 0U;

    if (value == NULL || out == NULL || !yyjson_is_uint(value)) {
        return false;
    }

    port = yyjson_get_uint(value);
    if (port == 0U || port > UINT16_MAX) {
        return false;
    }

    *out = (uint16_t)port;
    return true;
}

static bool sl_run_plan_config_parse_u64(yyjson_val* value, uint64_t* out)
{
    if (value == NULL || out == NULL || !yyjson_is_uint(value)) {
        return false;
    }
    *out = yyjson_get_uint(value);
    return *out != 0U;
}

static bool sl_run_plan_config_parse_bool(yyjson_val* value, bool* out)
{
    if (value == NULL || out == NULL || !yyjson_is_bool(value)) {
        return false;
    }
    *out = yyjson_get_bool(value);
    return true;
}

static int sl_run_apply_config_metadata_entry(SlRunApp* app, yyjson_val* key, yyjson_val* value)
{
    if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:Host")) {
        if (!sl_run_copy_json_string(app->config_host, sizeof(app->config_host), value) ||
            app->config_host[0] == '\0')
        {
            sl_cli_write_cstr(stderr,
                              "sloppy run: app.plan.json Sloppy:Server:Host must be a string\n");
            return 1;
        }
        app->config_has_host = true;
    }
    else if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:Port")) {
        if (!sl_run_plan_config_parse_port(value, &app->config_port)) {
            sl_cli_write_cstr(
                stderr, "sloppy run: app.plan.json Sloppy:Server:Port must be an integer port\n");
            return 1;
        }
        app->config_has_port = true;
    }
    else if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:KeepAliveEnabled")) {
        if (!sl_run_plan_config_parse_bool(value, &app->config_keep_alive_enabled)) {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Server:KeepAliveEnabled must be a boolean\n");
            return 1;
        }
        app->config_has_keep_alive_enabled = true;
    }
    else if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:KeepAliveIdleTimeoutMs")) {
        if (!sl_run_plan_config_parse_u64(value, &app->config_keep_alive_idle_timeout_ms)) {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Server:KeepAliveIdleTimeoutMs must be a positive "
                                      "integer\n");
            return 1;
        }
        app->config_has_keep_alive_idle_timeout_ms = true;
    }
    else if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:MaxRequestsPerConnection")) {
        if (!sl_run_plan_config_parse_u64(value, &app->config_max_requests_per_connection)) {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Server:MaxRequestsPerConnection must be a positive "
                                      "integer\n");
            return 1;
        }
        app->config_has_max_requests_per_connection = true;
    }

    return 0;
}

static int sl_run_load_config_metadata(SlRunApp* app, SlBytes json, const char* plan_path)
{
    yyjson_read_err error = {0};
    yyjson_doc* doc = NULL;
    yyjson_val* root = NULL;
    yyjson_val* configuration = NULL;
    yyjson_val* keys = NULL;
    yyjson_arr_iter iter;
    yyjson_val* entry = NULL;
    int result = 0;

    if (app == NULL || json.ptr == NULL || json.length == 0U) {
        return 1;
    }

    doc = yyjson_read_opts((char*)json.ptr, json.length, 0U, NULL, &error);
    if (doc == NULL) {
        sl_cli_write_error_with_value("sloppy run: malformed app.plan.json: ", plan_path, "\n");
        return 1;
    }

    root = yyjson_doc_get_root(doc);
    configuration = yyjson_is_obj(root) ? yyjson_obj_get(root, "configuration") : NULL;
    keys = yyjson_is_obj(configuration) ? yyjson_obj_get(configuration, "keys") : NULL;
    if (keys == NULL) {
        yyjson_doc_free(doc);
        return 0;
    }
    if (!yyjson_is_arr(keys)) {
        sl_cli_write_cstr(stderr,
                          "sloppy run: app.plan.json configuration.keys must be an array\n");
        yyjson_doc_free(doc);
        return 1;
    }

    yyjson_arr_iter_init(keys, &iter);
    while ((entry = yyjson_arr_iter_next(&iter)) != NULL) {
        yyjson_val* key = NULL;
        yyjson_val* value = NULL;

        if (!yyjson_is_obj(entry)) {
            sl_cli_write_cstr(
                stderr, "sloppy run: app.plan.json configuration.keys entries must be objects\n");
            result = 1;
            break;
        }

        key = yyjson_obj_get(entry, "key");
        value = yyjson_obj_get(entry, "value");
        if (sl_run_apply_config_metadata_entry(app, key, value) != 0) {
            result = 1;
            break;
        }
    }

    yyjson_doc_free(doc);
    return result;
}

typedef struct SlRunSha256
{
    uint32_t state[8];
    uint64_t bit_count;
    unsigned char buffer[64];
    size_t buffer_len;
} SlRunSha256;

static uint32_t sl_run_rotr32(uint32_t value, uint32_t shift)
{
    return (value >> shift) | (value << (32U - shift));
}

static void sl_run_sha256_transform(SlRunSha256* ctx, const unsigned char block[64])
{
    static const uint32_t k[64] = {
        UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
        UINT32_C(0x3956c25b), UINT32_C(0x59f111f1), UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
        UINT32_C(0xd807aa98), UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
        UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
        UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786), UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
        UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
        UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
        UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147), UINT32_C(0x06ca6351), UINT32_C(0x14292967),
        UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
        UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
        UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b), UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
        UINT32_C(0xd192e819), UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
        UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
        UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a), UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
        UINT32_C(0x748f82ee), UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
        UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2)};
    uint32_t w[64];
    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];
    size_t index = 0U;

    for (index = 0U; index < 16U; index += 1U) {
        size_t offset = index * 4U;
        w[index] = ((uint32_t)block[offset] << 24U) | ((uint32_t)block[offset + 1U] << 16U) |
                   ((uint32_t)block[offset + 2U] << 8U) | (uint32_t)block[offset + 3U];
    }
    for (index = 16U; index < 64U; index += 1U) {
        uint32_t s0 = sl_run_rotr32(w[index - 15U], 7U) ^ sl_run_rotr32(w[index - 15U], 18U) ^
                      (w[index - 15U] >> 3U);
        uint32_t s1 = sl_run_rotr32(w[index - 2U], 17U) ^ sl_run_rotr32(w[index - 2U], 19U) ^
                      (w[index - 2U] >> 10U);
        w[index] = w[index - 16U] + s0 + w[index - 7U] + s1;
    }

    for (index = 0U; index < 64U; index += 1U) {
        uint32_t s1 = sl_run_rotr32(e, 6U) ^ sl_run_rotr32(e, 11U) ^ sl_run_rotr32(e, 25U);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + k[index] + w[index];
        uint32_t s0 = sl_run_rotr32(a, 2U) ^ sl_run_rotr32(a, 13U) ^ sl_run_rotr32(a, 22U);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sl_run_sha256_init(SlRunSha256* ctx)
{
    *ctx = (SlRunSha256){0};
    ctx->state[0] = UINT32_C(0x6a09e667);
    ctx->state[1] = UINT32_C(0xbb67ae85);
    ctx->state[2] = UINT32_C(0x3c6ef372);
    ctx->state[3] = UINT32_C(0xa54ff53a);
    ctx->state[4] = UINT32_C(0x510e527f);
    ctx->state[5] = UINT32_C(0x9b05688c);
    ctx->state[6] = UINT32_C(0x1f83d9ab);
    ctx->state[7] = UINT32_C(0x5be0cd19);
}

static void sl_run_sha256_update(SlRunSha256* ctx, const unsigned char* bytes, size_t length)
{
    size_t index = 0U;

    ctx->bit_count += (uint64_t)length * 8U;
    while (index < length) {
        size_t available = 64U - ctx->buffer_len;
        size_t chunk = length - index < available ? length - index : available;
        for (size_t chunk_index = 0U; chunk_index < chunk; chunk_index += 1U) {
            ctx->buffer[ctx->buffer_len + chunk_index] = bytes[index + chunk_index];
        }
        ctx->buffer_len += chunk;
        index += chunk;
        if (ctx->buffer_len == 64U) {
            sl_run_sha256_transform(ctx, ctx->buffer);
            ctx->buffer_len = 0U;
        }
    }
}

static void sl_run_sha256_finish(SlRunSha256* ctx, unsigned char out[32])
{
    uint64_t bit_count = ctx->bit_count;
    size_t index = 0U;

    ctx->buffer[ctx->buffer_len] = 0x80U;
    ctx->buffer_len += 1U;
    if (ctx->buffer_len > 56U) {
        while (ctx->buffer_len < 64U) {
            ctx->buffer[ctx->buffer_len] = 0U;
            ctx->buffer_len += 1U;
        }
        sl_run_sha256_transform(ctx, ctx->buffer);
        ctx->buffer_len = 0U;
    }
    while (ctx->buffer_len < 56U) {
        ctx->buffer[ctx->buffer_len] = 0U;
        ctx->buffer_len += 1U;
    }
    for (index = 0U; index < 8U; index += 1U) {
        ctx->buffer[63U - index] = (unsigned char)(bit_count >> (index * 8U));
    }
    sl_run_sha256_transform(ctx, ctx->buffer);

    for (index = 0U; index < 8U; index += 1U) {
        size_t out_index = index * 4U;
        out[out_index] = (unsigned char)(ctx->state[index] >> 24U);
        out[out_index + 1U] = (unsigned char)(ctx->state[index] >> 16U);
        out[out_index + 2U] = (unsigned char)(ctx->state[index] >> 8U);
        out[out_index + 3U] = (unsigned char)ctx->state[index];
    }
}

static bool sl_run_hash_matches(SlStr expected, SlBytes bytes)
{
    static const char hex[] = "0123456789abcdef";
    unsigned char digest[32];
    char actual[71];
    SlRunSha256 ctx;
    size_t index = 0U;

    if (expected.length != 71U || expected.ptr == NULL ||
        memcmp(expected.ptr, "sha256:", sizeof("sha256:") - 1U) != 0)
    {
        return false;
    }

    sl_run_sha256_init(&ctx);
    sl_run_sha256_update(&ctx, bytes.ptr, bytes.length);
    sl_run_sha256_finish(&ctx, digest);

    actual[0] = 's';
    actual[1] = 'h';
    actual[2] = 'a';
    actual[3] = '2';
    actual[4] = '5';
    actual[5] = '6';
    actual[6] = ':';
    for (index = 0U; index < sizeof(digest); index += 1U) {
        actual[7U + (index * 2U)] = hex[(digest[index] >> 4U) & 0x0FU];
        actual[7U + (index * 2U) + 1U] = hex[digest[index] & 0x0FU];
    }

    return memcmp(expected.ptr, actual, sizeof(actual)) == 0;
}

static void sl_run_print_diag(const char* prefix, SlArena* arena, const SlDiag* diag)
{
    SlArenaMark mark = {0U};
    SlStr rendered = {0};
    bool should_reset = false;

    sl_cli_write_cstr(stderr, prefix);
    if (arena != NULL) {
        mark = sl_arena_mark(arena);
        should_reset = true;
    }
    if (diag != NULL && arena != NULL &&
        sl_status_is_ok(sl_diag_render_text(arena, diag, &rendered)))
    {
        (void)sl_cli_write_str(stderr, rendered);
        if (should_reset) {
            (void)sl_arena_reset_to(arena, mark);
        }
        return;
    }
    if (should_reset) {
        (void)sl_arena_reset_to(arena, mark);
    }
    if (diag != NULL && !sl_str_is_empty(diag->message)) {
        (void)sl_cli_write_str(stderr, diag->message);
        sl_cli_write_cstr(stderr, "\n");
        return;
    }
    sl_cli_write_cstr(stderr, "operation failed\n");
}

static SlEngineOptions sl_run_v8_options(const SlRunApp* app, const char* app_js_path)
{
    SlEngineOptions options = {0};

    options.kind = SL_ENGINE_KIND_V8;
    options.runtime_name = sl_str_from_cstr("sloppy-run-mvp");
    options.runtime_version = sl_str_from_cstr(SL_VERSION_STRING);
    options.target_platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    options.target_engine = sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8);
    /*
     * These are borrowed from SlRunApp; the app must stay alive for the engine lifetime.
     */
    options.plan = app == NULL ? NULL : &app->plan;
    options.capabilities = app == NULL ? NULL : &app->capability_registry;
    options.runtime_features = app == NULL ? NULL : &app->runtime_features;
    options.source_map = app == NULL ? (SlBytes){0} : app->source_map_bytes;
    options.source_map_source_name =
        app_js_path == NULL ? sl_str_empty() : sl_str_from_cstr(app_js_path);
    return options;
}

static void sl_run_engine_cleanup(void* payload, void* user)
{
    SlEngine** engine = (SlEngine**)payload;

    (void)user;
    if (engine != NULL && *engine != NULL) {
        sl_engine_destroy(*engine);
        *engine = NULL;
    }
}

static int sl_run_load_plan(SlRunApp* app, const char* plan_path)
{
    SlBytes json = {0};
    SlPlanParseOptions parse_options = {0};
    SlDiag diag = {0};
    SlStatus status;

    if (sl_run_read_file(plan_path, app->plan_json_storage, sizeof(app->plan_json_storage),
                         &json) != 0)
    {
        return 1;
    }

    parse_options.source_name = sl_str_from_cstr(plan_path);
    status = sl_plan_parse_json(&app->plan_arena, json, &parse_options, &app->plan, &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag("sloppy run: malformed app.plan.json: ", &app->engine_arena, &diag);
        return 1;
    }

    if (!sl_str_equal(app->plan.target.engine, sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8))) {
        sl_cli_write_cstr(stderr, "sloppy run: app.plan.json target.engine must be v8\n");
        return 1;
    }
    if (!sl_str_equal(app->plan.target.platform,
                      sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64)))
    {
        sl_cli_write_cstr(stderr, "sloppy run: app.plan.json target.platform is unsupported\n");
        return 1;
    }
    if (!sl_str_equal(app->plan.runtime_min_version,
                      sl_str_from_cstr(SL_PLAN_RUNTIME_MIN_VERSION_0_1_0)))
    {
        sl_cli_write_cstr(stderr, "sloppy run: unsupported app.plan.json runtimeMinimumVersion\n");
        return 1;
    }

    if (sl_run_load_config_metadata(app, json, plan_path) != 0) {
        return 1;
    }

    return 0;
}

static SlStatus sl_run_plan_intern_capacity(const SlPlan* plan, size_t* out_capacity)
{
    size_t capacity = SL_RUN_PLAN_INTERN_BASE_FIELDS;
    size_t addend = 0U;
    SlStatus status;

    if (plan == NULL || out_capacity == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_checked_mul_size(plan->handler_count, 2U, &addend);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_checked_add_size(capacity, addend, &capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_checked_mul_size(plan->route_count, 3U, &addend);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_checked_add_size(capacity, addend, &capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_checked_mul_size(plan->data_provider_count, 4U, &addend);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_checked_add_size(capacity, addend, &capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_checked_mul_size(plan->capability_count, 4U, &addend);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_checked_add_size(capacity, addend, &capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_checked_add_size(capacity, plan->required_feature_count, &capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out_capacity = capacity;
    return sl_status_ok();
}

static int sl_run_intern_plan_metadata(SlRunApp* app)
{
    SlPlan interned_plan = {0};
    SlInternTable intern_table = {0};
    size_t capacity = 0U;
    SlStatus status;

    if (app == NULL) {
        return 1;
    }

    status = sl_run_plan_intern_capacity(&app->plan, &capacity);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy run: failed to size stable app plan metadata table\n");
        return 1;
    }

    intern_table = app->plan_metadata_interns;
    status = sl_plan_intern_metadata(&app->plan_arena, &app->plan, capacity, capacity,
                                     &interned_plan, &intern_table);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy run: failed to intern stable app plan metadata\n");
        return 1;
    }

    app->plan = interned_plan;
    app->plan_metadata_interns = intern_table;
    return 0;
}

static int sl_run_validate_startup(SlRunApp* app)
{
    SlAppHostStartupValidation validation = {0};
    SlDiag diag = {0};
    SlStatus status;

    if (app == NULL) {
        return 1;
    }

    validation.diag_arena = &app->plan_arena;
    validation.require_runnable_route = true;
    validation.max_runnable_routes = SL_RUN_MAX_ROUTES;

    status = sl_app_host_validate_startup(&app->plan, &validation, &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag("sloppy run: app graph startup validation failed: ", &app->engine_arena,
                          &diag);
        return 1;
    }

    return 0;
}

static int sl_run_validate_runtime_features(SlRunApp* app)
{
    SlAppHostStartupValidation validation = {0};
    SlDiag diag = {0};
    SlStatus status;

    if (app == NULL) {
        return 1;
    }

    validation.diag_arena = &app->plan_arena;
    validation.require_runnable_route = true;
    validation.max_runnable_routes = SL_RUN_MAX_ROUTES;
    validation.validate_runtime_features = true;
    validation.out_runtime_features = &app->runtime_features;

    status = sl_app_host_validate_startup(&app->plan, &validation, &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag("sloppy run: runtime feature activation failed: ", &app->engine_arena,
                          &diag);
        return 1;
    }

    return 0;
}

static int sl_run_init_capability_registry(SlRunApp* app)
{
    SlStatus status;

    if (app == NULL) {
        return 1;
    }

    status = sl_capability_registry_init_from_plan(&app->plan, &app->capability_registry);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy run: capability registry initialization failed\n");
        return 1;
    }

    return 0;
}

static int sl_run_prepare_routes(SlRunApp* app)
{
    SlDiag diag = {0};
    SlStatus status;

    if (app->plan.route_count > SL_RUN_MAX_ROUTES) {
        sl_cli_write_cstr(
            stderr, "sloppy run: app.plan.json contains more GET routes than the dev runtime can "
                    "materialize\n");
        return 1;
    }

    status = sl_http_route_table_build(&app->route_arena, &app->plan, &app->route_table, &diag);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy run: failed to build HTTP route table\n");
        return 1;
    }

    if (app->route_table.route_count == 0U) {
        sl_cli_write_cstr(stderr,
                          "sloppy run: app.plan.json does not contain GET route metadata\n");
        return 1;
    }

    return 0;
}

static int sl_run_load_bootstrap_runtime(SlRunApp* app, const char* stdlib_root)
{
    char bootstrap_path[1024];
    char manifest_path[1024];
    SlBytes manifest = {0};
    SlBytes js = {0};
    SlStr source = {0};
    SlDiag diag = {0};
    SlStatus status;

    if (stdlib_root == NULL || stdlib_root[0] == '\0') {
        sl_cli_write_cstr(stderr, "sloppy run: bootstrap stdlib root is not configured\n");
        return 1;
    }

    if (!sl_run_artifact_file_path(bootstrap_path, sizeof(bootstrap_path), stdlib_root,
                                   "internal/runtime-classic.js"))
    {
        sl_cli_write_cstr(stderr, "sloppy run: invalid bootstrap stdlib directory\n");
        return 1;
    }

    if (!sl_run_artifact_file_path(manifest_path, sizeof(manifest_path), stdlib_root,
                                   "bootstrap.manifest.json"))
    {
        sl_cli_write_cstr(stderr, "sloppy run: invalid bootstrap stdlib directory\n");
        return 1;
    }

    if (sl_run_read_stdlib_file(manifest_path, app->bootstrap_js_storage,
                                sizeof(app->bootstrap_js_storage), &manifest) != 0)
    {
        return 1;
    }

    if (!sl_run_manifest_is_compatible(manifest, "0.1.0", "sloppy/internal/runtime-classic.js")) {
        sl_cli_write_cstr(
            stderr, "sloppy run: bootstrap stdlib manifest is incompatible with this runtime\n");
        return 1;
    }

    if (sl_run_read_stdlib_file(bootstrap_path, app->bootstrap_js_storage,
                                sizeof(app->bootstrap_js_storage), &js) != 0)
    {
        return 1;
    }

    source = sl_str_from_parts((const char*)js.ptr, js.length);
    status = sl_engine_eval_source(app->engine, sl_str_from_cstr(bootstrap_path), source, &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag("sloppy run: failed to evaluate bootstrap stdlib: ", &app->engine_arena,
                          &diag);
        return 1;
    }

    return 0;
}

static int sl_run_load_engine(SlRunApp* app, const char* stdlib_root, const char* app_js_path)
{
    SlStr source = {0};
    SlEngineOptions options = sl_run_v8_options(app, app_js_path);
    SlDiag diag = {0};
    SlStatus status;

    status = sl_engine_create(&options, &app->engine_arena, &app->engine);
    if (sl_status_code(status) == SL_STATUS_UNSUPPORTED) {
        sl_cli_write_cstr(stderr, "sloppy run: sloppy run requires V8-enabled build\n");
        return 1;
    }
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy run: failed to create V8 engine\n");
        return 1;
    }

    /* app->lifecycle runs LIFO; register sl_run_engine_cleanup first so engine teardown runs last.
     */
    status = sl_app_lifecycle_add_cleanup(&app->lifecycle, sl_run_engine_cleanup,
                                          (void*)&app->engine, NULL, &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag(
            "sloppy run: failed to register engine shutdown cleanup: ", &app->engine_arena, &diag);
        sl_engine_destroy(app->engine);
        app->engine = NULL;
        return 1;
    }

    if (sl_run_load_bootstrap_runtime(app, stdlib_root) != 0) {
        return 1;
    }

    source = sl_str_from_parts((const char*)app->app_js_bytes.ptr, app->app_js_bytes.length);
    status = sl_engine_eval_source(app->engine, sl_str_from_cstr(app_js_path), source, &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag("sloppy run: failed to evaluate app.js: ", &app->engine_arena, &diag);
        return 1;
    }

    status = sl_engine_validate_registered_handlers(app->engine, &app->plan, &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag("sloppy run: registered handler validation failed: ", &app->engine_arena,
                          &diag);
        return 1;
    }

    return 0;
}

static int sl_run_load_app(const char* artifacts_path, const char* stdlib_path, SlRunApp* app)
{
    char plan_path[1024];
    char app_js_path[1024];
    char source_map_path[1024];
    SlBytes app_js = {0};
    SlBytes source_map = {0};
    SlDiag diag = {0};

    if (artifacts_path == NULL || app == NULL) {
        return 1;
    }

    *app = (SlRunApp){0};
    if (!sl_status_is_ok(sl_arena_init(&app->plan_arena, app->plan_arena_storage,
                                       sizeof(app->plan_arena_storage))) ||
        !sl_status_is_ok(sl_arena_init(&app->route_arena, app->route_arena_storage,
                                       sizeof(app->route_arena_storage))) ||
        !sl_status_is_ok(sl_arena_init(&app->engine_arena, app->engine_arena_storage,
                                       sizeof(app->engine_arena_storage))))
    {
        return 1;
    }

    if (!sl_status_is_ok(sl_app_lifecycle_start(
            &app->lifecycle, app->app_cleanups,
            sizeof(app->app_cleanups) / sizeof(app->app_cleanups[0]), &diag)))
    {
        sl_run_print_diag("sloppy run: failed to start app lifecycle: ", &app->engine_arena, &diag);
        return 1;
    }

    if (!sl_run_artifact_file_path(plan_path, sizeof(plan_path), artifacts_path, "app.plan.json")) {
        sl_cli_write_cstr(stderr, "sloppy run: invalid artifacts directory\n");
        return 1;
    }

    if (sl_run_load_plan(app, plan_path) != 0 || sl_run_validate_startup(app) != 0 ||
        sl_run_intern_plan_metadata(app) != 0 || sl_run_init_capability_registry(app) != 0 ||
        sl_run_prepare_routes(app) != 0)
    {
        return 1;
    }

    if (!sl_run_join_path(app_js_path, sizeof(app_js_path), artifacts_path,
                          sl_run_str_span(app->plan.bundle.path)))
    {
        sl_cli_write_cstr(stderr, "sloppy run: invalid bundle path in app.plan.json\n");
        return 1;
    }

    if (!sl_run_join_path(source_map_path, sizeof(source_map_path), artifacts_path,
                          sl_run_str_span(app->plan.source_map.path)))
    {
        sl_cli_write_cstr(stderr, "sloppy run: invalid source map path in app.plan.json\n");
        return 1;
    }

    if (sl_run_read_file(app_js_path, app->app_js_storage, sizeof(app->app_js_storage), &app_js) !=
        0)
    {
        return 1;
    }
    if (sl_run_read_file(source_map_path, app->metadata_json_storage,
                         sizeof(app->metadata_json_storage), &source_map) != 0)
    {
        return 1;
    }
    if (!sl_run_hash_matches(app->plan.bundle.hash, app_js)) {
        sl_cli_write_cstr(stderr, "sloppy run: bundle hash mismatch in app.plan.json\n");
        return 1;
    }
    if (!sl_run_hash_matches(app->plan.source_map.hash, source_map)) {
        sl_cli_write_cstr(stderr, "sloppy run: source map hash mismatch in app.plan.json\n");
        return 1;
    }
    app->app_js_bytes = app_js;
    app->source_map_bytes = source_map;

    if (sl_run_validate_runtime_features(app) != 0) {
        return 1;
    }

    return sl_run_load_engine(app, stdlib_path, app_js_path);
}

static int sl_run_write_response(char* buffer, size_t capacity, unsigned status,
                                 const char* content_type, SlStr body)
{
    SlHttpResponse response = {0};
    SlBytes bytes = {0};
    SlStatus write_status;

    if (buffer == NULL || content_type == NULL || status > UINT16_MAX) {
        return -1;
    }

    response = sl_http_response_text((uint16_t)status, body);
    response.content_type = sl_str_from_cstr(content_type);
    write_status = sl_http_response_write(&response, (unsigned char*)buffer, capacity, &bytes);
    if (!sl_status_is_ok(write_status) || bytes.length > (size_t)INT32_MAX) {
        return -1;
    }

    return (int)bytes.length;
}

static SlHttpMethod sl_run_method_from_cstr(const char* method)
{
    if (method == NULL) {
        return SL_HTTP_METHOD_UNKNOWN;
    }
    if (strcmp(method, "GET") == 0) {
        return SL_HTTP_METHOD_GET;
    }
    if (strcmp(method, "POST") == 0) {
        return SL_HTTP_METHOD_POST;
    }
    if (strcmp(method, "PUT") == 0) {
        return SL_HTTP_METHOD_PUT;
    }
    if (strcmp(method, "DELETE") == 0) {
        return SL_HTTP_METHOD_DELETE;
    }
    if (strcmp(method, "PATCH") == 0) {
        return SL_HTTP_METHOD_PATCH;
    }
    if (strcmp(method, "OPTIONS") == 0) {
        return SL_HTTP_METHOD_OPTIONS;
    }
    if (strcmp(method, "HEAD") == 0) {
        return SL_HTTP_METHOD_HEAD;
    }
    return SL_HTTP_METHOD_UNKNOWN;
}

static SlStr sl_run_target_path(const char* target)
{
    size_t index = 0U;

    if (target == NULL) {
        return sl_str_empty();
    }

    while (target[index] != '\0' && target[index] != '?') {
        index += 1U;
    }

    return sl_str_from_parts(target, index);
}

typedef struct SlRunDispatchContext
{
    SlRunApp* app;
    SlArena* arena;
    const SlHttpRequestHead* request;
    SlHttpResponse* response;
} SlRunDispatchContext;

static SlStatus sl_run_dispatch_with_request_scope(SlAppRequestScope* request_scope, void* user,
                                                   SlDiag* out_diag)
{
    SlRunDispatchContext* context = (SlRunDispatchContext*)user;
    SlEngineResult result = {0};
    SlStatus status;

    /* The scope is represented by the callback boundary; this slice registers no cleanups here. */
    (void)request_scope;
    if (context == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_http_dispatch_request_head(context->arena, context->app->engine,
                                           &context->app->plan, &context->app->route_table.dispatch,
                                           context->request, &result, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (context->response == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *context->response = result.response;
    return sl_status_ok();
}

static SlStatus sl_run_dispatch_response_with_storage(SlRunApp* app,
                                                      const SlHttpRequestHead* request,
                                                      SlArena* dispatch_arena,
                                                      SlHttpResponse* out_response,
                                                      SlDiag* out_diag)
{
    SlScopeCleanup request_cleanups[SL_RUN_REQUEST_SCOPE_MAX_CLEANUPS];
    SlRunDispatchContext dispatch_context = {0};

    if (app == NULL || request == NULL || dispatch_arena == NULL || out_response == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    dispatch_context.app = app;
    dispatch_context.arena = dispatch_arena;
    dispatch_context.request = request;
    dispatch_context.response = out_response;

    if (app->next_request_id == UINT64_MAX) {
        return sl_status_from_code(SL_STATUS_OVERFLOW);
    }
    /* Request ID 0 is invalid; request scopes receive monotonic IDs or fail on overflow. */
    app->next_request_id += 1U;
    return sl_app_request_scope_execute_for_app(
        &app->lifecycle, app->next_request_id, request_cleanups,
        sizeof(request_cleanups) / sizeof(request_cleanups[0]), sl_run_dispatch_with_request_scope,
        &dispatch_context, out_diag);
}

static bool sl_run_dispatch_failure_is_cli_mapped(SlStatus status, const SlDiag* diag)
{
    if (diag == NULL) {
        return false;
    }

    if (sl_status_code(status) == SL_STATUS_UNSUPPORTED &&
        (diag->code == SL_DIAG_HTTP_UNSUPPORTED_METHOD ||
         diag->code == SL_DIAG_HTTP_UNSUPPORTED_BODY ||
         diag->code == SL_DIAG_HTTP_UNSUPPORTED_MEDIA_TYPE))
    {
        return true;
    }
    if (sl_status_code(status) == SL_STATUS_CAPACITY_EXCEEDED &&
        diag->code == SL_DIAG_HTTP_BODY_LIMIT)
    {
        return true;
    }
    if (sl_status_code(status) == SL_STATUS_INVALID_ARGUMENT &&
        diag->code == SL_DIAG_MALFORMED_JSON)
    {
        return true;
    }
    return sl_status_code(status) == SL_STATUS_OUT_OF_RANGE &&
           diag->code == SL_DIAG_HTTP_ROUTE_NOT_FOUND;
}

static SlStatus sl_run_dispatch_transport_status(SlStatus status, const SlDiag* diag)
{
    return sl_run_dispatch_failure_is_cli_mapped(status, diag)
               ? status
               : sl_status_from_code(SL_STATUS_INTERNAL);
}

static int sl_run_dispatch_head_with_storage(SlRunApp* app, const SlHttpRequestHead* request,
                                             char* response, size_t response_capacity,
                                             unsigned char* dispatch_storage,
                                             size_t dispatch_storage_size)
{
    SlArena dispatch_arena = {0};
    SlHttpResponse result = {0};
    SlBytes response_bytes = {0};
    SlDiag diag = {0};
    SlStatus status;

    if (app == NULL || request == NULL || response == NULL || dispatch_storage == NULL ||
        !sl_status_is_ok(sl_arena_init(&dispatch_arena, dispatch_storage, dispatch_storage_size)))
    {
        return -1;
    }

    status = sl_run_dispatch_response_with_storage(app, request, &dispatch_arena, &result, &diag);
    if (sl_status_is_ok(status)) {
        status = sl_http_response_write(&result, (unsigned char*)response, response_capacity,
                                        &response_bytes);
        if (!sl_status_is_ok(status) || response_bytes.length > (size_t)INT32_MAX) {
            status = sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
        }
        else {
            return (int)response_bytes.length;
        }
    }

    if (sl_status_code(status) == SL_STATUS_UNSUPPORTED &&
        diag.code == SL_DIAG_HTTP_UNSUPPORTED_METHOD)
    {
        return sl_run_write_response(response, response_capacity, 405U, "text/plain; charset=utf-8",
                                     sl_str_from_cstr("Method Not Allowed\n"));
    }

    if (sl_status_code(status) == SL_STATUS_UNSUPPORTED &&
        diag.code == SL_DIAG_HTTP_UNSUPPORTED_BODY)
    {
        return sl_run_write_response(response, response_capacity, 501U, "text/plain; charset=utf-8",
                                     sl_str_from_cstr("Request body framing is not supported\n"));
    }

    if (sl_status_code(status) == SL_STATUS_CAPACITY_EXCEEDED &&
        diag.code == SL_DIAG_HTTP_BODY_LIMIT)
    {
        return sl_run_write_response(response, response_capacity, 413U, "text/plain; charset=utf-8",
                                     sl_str_from_cstr("Payload Too Large\n"));
    }

    if (sl_status_code(status) == SL_STATUS_UNSUPPORTED &&
        diag.code == SL_DIAG_HTTP_UNSUPPORTED_MEDIA_TYPE)
    {
        return sl_run_write_response(response, response_capacity, 415U, "text/plain; charset=utf-8",
                                     sl_str_from_cstr("Unsupported Media Type\n"));
    }

    if (sl_status_code(status) == SL_STATUS_INVALID_ARGUMENT && diag.code == SL_DIAG_MALFORMED_JSON)
    {
        return sl_run_write_response(response, response_capacity, 400U, "text/plain; charset=utf-8",
                                     sl_str_from_cstr("Malformed JSON\n"));
    }

    if (sl_status_code(status) == SL_STATUS_OUT_OF_RANGE &&
        diag.code == SL_DIAG_HTTP_ROUTE_NOT_FOUND)
    {
        return sl_run_write_response(response, response_capacity, 404U, "text/plain; charset=utf-8",
                                     sl_str_from_cstr("Not Found\n"));
    }

    (void)diag;
    return sl_run_write_response(response, response_capacity, 500U, "text/plain; charset=utf-8",
                                 sl_str_from_cstr("Sloppy handler failed\n"));
}

static int sl_run_dispatch_head(SlRunApp* app, const SlHttpRequestHead* request, char* response,
                                size_t response_capacity)
{
    unsigned char dispatch_storage[SL_RUN_ARENA_BYTES];

    return sl_run_dispatch_head_with_storage(app, request, response, response_capacity,
                                             dispatch_storage, sizeof(dispatch_storage));
}

static int sl_run_once(SlRunApp* app, const char* method, const char* target)
{
    unsigned char request_storage[SL_RUN_ARENA_BYTES];
    SlArena request_arena = {0};
    SlHttpRequestHead request = {0};
    char response[SL_RUN_RESPONSE_MAX_BYTES];
    int response_length;

    if (target == NULL || target[0] != '/') {
        sl_cli_write_cstr(stderr, "sloppy run: --once target must start with /\n");
        return 1;
    }

    if (!sl_status_is_ok(sl_arena_init(&request_arena, request_storage, sizeof(request_storage)))) {
        return 1;
    }

    request.method = sl_run_method_from_cstr(method);
    request.raw_target = sl_str_from_cstr(target);
    request.path = sl_run_target_path(target);
    if (request.method == SL_HTTP_METHOD_UNKNOWN) {
        sl_cli_write_cstr(stderr, "sloppy run: --once method is unsupported by the MVP parser\n");
        return 1;
    }

    response_length = sl_run_dispatch_head(app, &request, response, sizeof(response));
    if (response_length < 0) {
        sl_cli_write_cstr(stderr, "sloppy run: failed to write response\n");
        return 1;
    }

    (void)fwrite(response, 1U, (size_t)response_length, stdout);
    return 0;
}

static SlStatus sl_run_transport_dispatch(SlHttpTransportConnection* connection, SlArena* arena,
                                          const SlHttpRequestLifecycle* request,
                                          SlHttpResponse* out_response, SlDiag* out_diag,
                                          void* user)
{
    SlRunApp* app = (SlRunApp*)user;
    SlStatus status;
    SlStatus transport_status;

    (void)connection;
    if (app == NULL || arena == NULL || request == NULL || out_response == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status =
        sl_run_dispatch_response_with_storage(app, &request->head, arena, out_response, out_diag);
    if (sl_status_is_ok(status)) {
        return status;
    }

    transport_status = sl_run_dispatch_transport_status(status, out_diag);
    if (sl_status_code(transport_status) == SL_STATUS_INTERNAL && out_diag != NULL) {
        sl_run_print_diag("sloppy run: HTTP dispatch failed\n", arena, out_diag);
    }
    return transport_status;
}

static SlHttpTransportConfig sl_run_transport_config(const char* host, uint16_t port, SlRunApp* app)
{
    SlHttpTransportConfig config = {0};

    config.host = sl_str_from_cstr(host);
    config.port = port;
    config.max_connections = SL_RUN_MAX_CLIENTS;
    config.max_active_requests = SL_RUN_MAX_CLIENTS;
    config.connection_capacity = SL_RUN_MAX_CLIENTS;
    config.backlog = 16;
    config.max_request_head_bytes = SL_RUN_REQUEST_MAX_BYTES;
    config.request_arena_bytes = SL_RUN_ARENA_BYTES;
    config.read_chunk_bytes = SL_HTTP_TRANSPORT_DEFAULT_READ_CHUNK_BYTES;
    config.max_response_bytes = SL_RUN_RESPONSE_MAX_BYTES;
    config.parse.max_headers = SL_HTTP_DEFAULT_MAX_HEADERS;
    config.parse.max_target_length = SL_HTTP_DEFAULT_MAX_TARGET_LENGTH;
    config.parse.max_header_name_length = SL_HTTP_DEFAULT_MAX_HEADER_NAME_LENGTH;
    config.parse.max_header_value_length = SL_HTTP_DEFAULT_MAX_HEADER_VALUE_LENGTH;
    config.parse.max_total_header_bytes = SL_HTTP_DEFAULT_MAX_TOTAL_HEADER_BYTES;
    config.parse.max_body_length = SL_RUN_REQUEST_MAX_BYTES;
    if (app != NULL && app->config_has_keep_alive_enabled) {
        config.keep_alive_disabled = !app->config_keep_alive_enabled;
    }
    if (app != NULL && app->config_has_keep_alive_idle_timeout_ms) {
        config.keep_alive_idle_timeout_ms = app->config_keep_alive_idle_timeout_ms;
    }
    if (app != NULL && app->config_has_max_requests_per_connection) {
        config.max_requests_per_connection = (size_t)app->config_max_requests_per_connection;
    }
    config.dispatch = sl_run_transport_dispatch;
    config.dispatch_user = app;
    return config;
}

static int sl_run_server(SlRunApp* app, const char* host, uint16_t port)
{
    unsigned char transport_storage[SL_RUN_TRANSPORT_ARENA_BYTES];
    SlArena transport_arena = {0};
    SlHttpTransportServer server = {0};
    SlHttpTransportConfig config = sl_run_transport_config(host, port, app);
    SlDiag diag = {0};
    SlStatus status;
    int result = 0;

    if (!sl_status_is_ok(
            sl_arena_init(&transport_arena, transport_storage, sizeof(transport_storage))))
    {
        sl_cli_write_cstr(stderr, "sloppy run: failed to initialize dev server storage\n");
        return 1;
    }

    status = sl_http_transport_server_init(&server, &transport_arena, &config, &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag("sloppy run: failed to initialize dev server: ", &transport_arena, &diag);
        return 1;
    }
    status = sl_http_transport_server_listen(&server, &diag);
    if (!sl_status_is_ok(status)) {
        if (diag.code == SL_DIAG_HTTP_TRANSPORT_CONFIG) {
            sl_cli_write_cstr(stderr, "sloppy run: --host must be an IPv4 address\n");
        }
        else {
            sl_cli_write_cstr(stderr, "sloppy run: failed to listen on requested host/port\n");
        }
        (void)sl_http_transport_server_dispose(&server, NULL);
        return 1;
    }

    (void)printf("Sloppy dev server listening on http://%s:%u\n", host, (unsigned)port);
    (void)printf("Dev-only MVP: no TLS, no streaming, no middleware.\n");
    status = sl_http_transport_server_run(&server, &diag);
    if (!sl_status_is_ok(status)) {
        result = 1;
    }
    (void)sl_http_transport_server_dispose(&server, NULL);
    return result;
}

static int sl_run_shutdown_app(SlRunApp* app)
{
    SlDiag diag = {0};
    SlStatus status;

    if (app == NULL) {
        return 1;
    }

    status = sl_app_lifecycle_shutdown(&app->lifecycle, &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag("sloppy run: app shutdown failed: ", &app->engine_arena, &diag);
        return 1;
    }

    return 0;
}

typedef struct SlRunSourceConfig
{
    char entry[SL_RUN_PATH_MAX_BYTES];
    char out_dir[SL_RUN_PATH_MAX_BYTES];
    char environment[128];
} SlRunSourceConfig;

static bool sl_run_source_input_extension_supported(const char* path)
{
    SlCliSpan input = sl_cli_span_cstr(path);

    return sl_run_span_ends_with(input, ".js") || sl_run_span_ends_with(input, ".mjs") ||
           sl_run_span_ends_with(input, ".ts") || sl_run_span_ends_with(input, ".mts");
}

static bool sl_run_file_exists(const char* path)
{
    FILE* file = NULL;

    if (path == NULL || path[0] == '\0') {
        return false;
    }

#ifdef _MSC_VER
    if (fopen_s(&file, path, "rb") != 0) {
        file = NULL;
    }
#else
    file = fopen(path, "rb");
#endif

    if (file == NULL) {
        return false;
    }
    (void)fclose(file);
    return true;
}

static bool sl_run_copy_json_string(char* buffer, size_t capacity, yyjson_val* value)
{
    SlStringBuilder builder = {0};
    SlStr view = {0};
    SlStr text = {0};
    size_t index = 0U;
    SlStatus status;

    if (buffer == NULL || capacity == 0U || value == NULL || !yyjson_is_str(value)) {
        return false;
    }

    status = sl_string_builder_init_fixed(&builder, buffer, capacity);
    if (!sl_status_is_ok(status)) {
        return false;
    }

    text = sl_str_from_parts(yyjson_get_str(value), yyjson_get_len(value));
    for (index = 0U; index < text.length; index += 1U) {
        status = sl_string_builder_append_char(&builder, text.ptr[index]);
        if (!sl_status_is_ok(status)) {
            return false;
        }
    }

    status = sl_string_builder_view_with_nul(&builder, &view);
    return sl_status_is_ok(status) && view.ptr == buffer && view.length == text.length;
}

static int sl_run_reject_unknown_project_config_fields(yyjson_val* root)
{
    yyjson_obj_iter iter;
    yyjson_val* key = NULL;

    iter = yyjson_obj_iter_with(root);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL) {
        const char* field = yyjson_get_str(key);
        if (field == NULL || (strcmp(field, "entry") != 0 && strcmp(field, "outDir") != 0 &&
                              strcmp(field, "environment") != 0))
        {
            sl_cli_write_cstr(stderr,
                              "sloppy run: invalid sloppy.json: unsupported field; supported "
                              "fields are entry, outDir, and environment\n");
            return 1;
        }
    }
    return 0;
}

static int sl_run_read_required_config_string(yyjson_val* root, const char* field, char* buffer,
                                              size_t capacity, const char* missing_message,
                                              const char* invalid_message)
{
    yyjson_val* value = yyjson_obj_get(root, field);

    if (value == NULL) {
        sl_cli_write_cstr(stderr, missing_message);
        return 1;
    }

    if (!yyjson_is_str(value) || yyjson_get_len(value) == 0U) {
        sl_cli_write_cstr(stderr, invalid_message);
        return 1;
    }

    if (!sl_run_copy_json_string(buffer, capacity, value)) {
        sl_cli_write_cstr(stderr, invalid_message);
        return 1;
    }
    return 0;
}

static int sl_run_read_optional_config_string(yyjson_val* root, const char* field, char* buffer,
                                              size_t capacity, const char* default_value,
                                              const char* invalid_message)
{
    yyjson_val* value = yyjson_obj_get(root, field);

    if (value == NULL) {
        SlStringBuilder builder = {0};
        SlStr view = {0};
        if (!sl_status_is_ok(sl_string_builder_init_fixed(&builder, buffer, capacity)) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(&builder, default_value)) ||
            !sl_status_is_ok(sl_string_builder_view_with_nul(&builder, &view)))
        {
            sl_cli_write_cstr(stderr, invalid_message);
            return 1;
        }
        return 0;
    }

    if (!yyjson_is_str(value) || yyjson_get_len(value) == 0U ||
        !sl_run_copy_json_string(buffer, capacity, value))
    {
        sl_cli_write_cstr(stderr, invalid_message);
        return 1;
    }
    return 0;
}

static int sl_run_parse_project_config(SlRunSourceConfig* out)
{
    unsigned char json_storage[SL_RUN_CONFIG_MAX_BYTES];
    SlBytes json = {0};
    yyjson_doc* doc = NULL;
    yyjson_read_err error = {0};
    yyjson_val* root = NULL;
    int result = 1;

    if (out == NULL) {
        return 1;
    }
    *out = (SlRunSourceConfig){0};

    if (sl_read_file_with_messages(SL_RUN_CONFIG_FILE, json_storage, sizeof(json_storage), &json,
                                   "sloppy run: project config not found: ",
                                   "sloppy run: project config is empty or too large: ") != 0)
    {
        return 1;
    }

    doc = yyjson_read_opts((char*)json.ptr, json.length, 0U, NULL, &error);
    if (doc == NULL) {
        sl_cli_write_cstr(stderr, "sloppy run: invalid sloppy.json: malformed JSON\n");
        return 1;
    }

    root = yyjson_doc_get_root(doc);
    if (root == NULL || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        sl_cli_write_cstr(stderr, "sloppy run: invalid sloppy.json: root must be an object\n");
        return 1;
    }

    if (sl_run_reject_unknown_project_config_fields(root) == 0 &&
        sl_run_read_required_config_string(
            root, "entry", out->entry, sizeof(out->entry),
            "sloppy run: missing entry in sloppy.json\n",
            "sloppy run: invalid sloppy.json: entry must be a non-empty string\n") == 0 &&
        sl_run_read_optional_config_string(
            root, "outDir", out->out_dir, sizeof(out->out_dir), SL_RUN_DEFAULT_CONFIG_OUT_DIR,
            "sloppy run: invalid sloppy.json: outDir must be a string\n") == 0 &&
        sl_run_read_optional_config_string(
            root, "environment", out->environment, sizeof(out->environment), "Development",
            "sloppy run: invalid sloppy.json: environment must be a string\n") == 0)
    {
        result = 0;
    }

    yyjson_doc_free(doc);
    return result;
}

static const char* sl_run_getenv(const char* name)
{
    const char* value = NULL;
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#ifdef _MSC_VER
#pragma warning(suppress : 4996)
#endif
    value = getenv(name);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    return value;
}

static const char* sl_run_resolve_compiler_path(void)
{
    const char* explicit_path = sl_run_getenv("SLOPPY_SLOPPYC");
    const char* built_path = SLOPPY_COMPILER_BUILD_PATH;

    if (explicit_path != NULL && explicit_path[0] != '\0') {
        if (!sl_run_file_exists(explicit_path)) {
            sl_cli_write_error_with_value("sloppy run: compiler unavailable: ", explicit_path,
                                          "\n");
            return NULL;
        }
        return explicit_path;
    }

    if (built_path[0] != '\0' && sl_run_file_exists(built_path)) {
        return built_path;
    }

    return "sloppyc";
}

static int sl_run_compile_source(const char* source_path, const char* out_dir,
                                 const SlCliOptions* options, const char* environment)
{
    const char* compiler_path = NULL;
    char* compiler_argv[14];
    char port_text[6];
    size_t arg_count = 0U;
    SlPlatformProcessArgs process_args = {0};
    SlStatus status;
    int exit_code = 1;

    if (source_path == NULL || out_dir == NULL || source_path[0] == '\0' || out_dir[0] == '\0') {
        sl_cli_write_cstr(stderr, "sloppy run: source-input handoff is missing source or outDir\n");
        return 1;
    }

    if (!sl_run_file_exists(source_path)) {
        sl_cli_write_error_with_value("sloppy run: missing source file: ", source_path, "\n");
        return 1;
    }

    compiler_path = sl_run_resolve_compiler_path();
    if (compiler_path == NULL) {
        return 1;
    }

    compiler_argv[arg_count++] = (char*)compiler_path;
    compiler_argv[arg_count++] = "build";
    compiler_argv[arg_count++] = (char*)source_path;
    compiler_argv[arg_count++] = "--out";
    compiler_argv[arg_count++] = (char*)out_dir;
    if (environment != NULL && environment[0] != '\0') {
        compiler_argv[arg_count++] = "--environment";
        compiler_argv[arg_count++] = (char*)environment;
    }
    if (options != NULL && options->host_explicit) {
        compiler_argv[arg_count++] = "--host";
        compiler_argv[arg_count++] = (char*)options->host;
    }
    if (options != NULL && options->port_explicit) {
        SlStringBuilder port_builder = {0};
        SlStr port_view = {0};
        if (!sl_status_is_ok(
                sl_string_builder_init_fixed(&port_builder, port_text, sizeof(port_text))) ||
            !sl_status_is_ok(sl_string_builder_append_u64(&port_builder, options->port)) ||
            !sl_status_is_ok(sl_string_builder_view_with_nul(&port_builder, &port_view)))
        {
            sl_cli_write_cstr(stderr, "sloppy run: failed to format compiler port override\n");
            return 1;
        }
        (void)port_view;
        compiler_argv[arg_count++] = "--port";
        compiler_argv[arg_count++] = port_text;
    }
    compiler_argv[arg_count] = NULL;

    process_args.file = compiler_path;
    process_args.argv = compiler_argv;
    status = sl_platform_process_run(&process_args, &exit_code);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_error_with_value("sloppy run: compiler unavailable: ", compiler_path, "\n");
        return 1;
    }
    if (exit_code != 0) {
        sl_cli_write_cstr(
            stderr, "sloppy run: compiler handoff failed at the source-input command boundary\n");
        return 1;
    }

    return 0;
}

static int sl_run_prepare_source_input(const SlCliOptions* options, char* artifacts_path,
                                       size_t artifacts_path_capacity)
{
    SlRunSourceConfig config = {0};
    const char* source_path = NULL;
    const char* out_dir = NULL;
    const char* environment = NULL;

    if (options == NULL || artifacts_path == NULL || artifacts_path_capacity == 0U) {
        return 1;
    }

    if (options->input_path != NULL) {
        source_path = options->input_path;
        out_dir = SL_RUN_DEFAULT_SOURCE_OUT_DIR;
        environment =
            options->environment_explicit ? options->environment : SL_RUN_DEFAULT_ENVIRONMENT;
    }
    else {
        if (sl_run_parse_project_config(&config) != 0) {
            return 1;
        }
        source_path = config.entry;
        out_dir = config.out_dir;
        environment = options->environment_explicit ? options->environment : config.environment;
    }

    if (!sl_run_source_input_extension_supported(source_path)) {
        sl_cli_write_error_with_value("sloppy run: unsupported source input: ", source_path, "\n");
        return 1;
    }

    if (sl_run_compile_source(source_path, out_dir, options, environment) != 0) {
        return 1;
    }

    {
        SlStringBuilder builder = {0};
        SlStr view = {0};
        if (!sl_status_is_ok(
                sl_string_builder_init_fixed(&builder, artifacts_path, artifacts_path_capacity)) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(&builder, out_dir)) ||
            !sl_status_is_ok(sl_string_builder_view_with_nul(&builder, &view)))
        {
            sl_cli_write_cstr(stderr,
                              "sloppy run: source-input artifact directory path is too long\n");
            return 1;
        }
    }

    return 0;
}

static int sl_cli_command_run(const SlCliOptions* options)
{
    SlRunApp app = {0};
    const char* artifacts_path = options->artifacts_path;
    const char* stdlib_path = options->stdlib_path;
    const char* run_host = options->host;
    uint16_t run_port = options->port;
    char source_artifacts_path[SL_RUN_PATH_MAX_BYTES];
    int result = 0;

    if (options->environment_explicit && artifacts_path == NULL && options->input_path != NULL &&
        !sl_run_source_input_extension_supported(options->input_path))
    {
        sl_cli_write_cstr(
            stderr, "sloppy run: --environment only applies to source input or sloppy.json\n");
        return 1;
    }
    if (options->environment_explicit && artifacts_path != NULL) {
        sl_cli_write_cstr(
            stderr, "sloppy run: --environment only applies to source input or sloppy.json\n");
        return 1;
    }

    if (artifacts_path == NULL) {
        if (options->input_path == NULL ||
            sl_run_source_input_extension_supported(options->input_path))
        {
            if (sl_run_prepare_source_input(options, source_artifacts_path,
                                            sizeof(source_artifacts_path)) != 0)
            {
                return 1;
            }
            artifacts_path = source_artifacts_path;
        }
        else {
            artifacts_path = options->input_path;
        }
    }

    if (artifacts_path == NULL) {
        sl_cli_write_cstr(stderr,
                          "sloppy run: expected source input, sloppy.json, or --artifacts <dir>\n");
        return 1;
    }

    if (stdlib_path == NULL) {
        stdlib_path = SLOPPY_BOOTSTRAP_BUILD_DIR;
    }

    if (sl_run_load_app(artifacts_path, stdlib_path, &app) != 0) {
        (void)sl_run_shutdown_app(&app);
        return 1;
    }

    if (options->once_method != NULL) {
        result = sl_run_once(&app, options->once_method, options->once_target);
    }
    else {
        if (!options->host_explicit && app.config_has_host) {
            run_host = app.config_host;
        }
        if (!options->port_explicit && app.config_has_port) {
            run_port = app.config_port;
        }
        result = sl_run_server(&app, run_host, run_port);
    }

    if (sl_run_shutdown_app(&app) != 0 && result == 0) {
        result = 1;
    }
    return result;
}

static const SlCliModule* sl_cli_find_module(const SlCliMetadata* metadata, SlCliSpan name)
{
    size_t index = 0U;

    for (index = 0U; index < metadata->module_count; index += 1U) {
        if (sl_cli_span_equal(metadata->modules[index].name, name)) {
            return &metadata->modules[index];
        }
    }

    return NULL;
}

static const SlCliProvider* sl_cli_find_provider(const SlCliMetadata* metadata, SlCliSpan token)
{
    size_t index = 0U;

    for (index = 0U; index < metadata->provider_count; index += 1U) {
        if (sl_cli_span_equal(metadata->providers[index].token, token)) {
            return &metadata->providers[index];
        }
    }

    return NULL;
}

static const SlCliCapability* sl_cli_find_capability(const SlCliMetadata* metadata, SlCliSpan token)
{
    size_t index = 0U;

    for (index = 0U; index < metadata->capability_count; index += 1U) {
        if (sl_cli_span_equal(metadata->capabilities[index].token, token)) {
            return &metadata->capabilities[index];
        }
    }

    return NULL;
}

static bool sl_cli_capability_allows(SlCliSpan access, SlCliSpan operation)
{
    if (sl_cli_span_equal_cstr(operation, "read")) {
        return sl_cli_span_equal_cstr(access, "read") ||
               sl_cli_span_equal_cstr(access, "readwrite");
    }
    if (sl_cli_span_equal_cstr(operation, "write")) {
        return sl_cli_span_equal_cstr(access, "write") ||
               sl_cli_span_equal_cstr(access, "readwrite");
    }
    if (sl_cli_span_equal_cstr(operation, "connect")) {
        return sl_cli_span_equal_cstr(access, "connect") ||
               sl_cli_span_equal_cstr(access, "connect-listen");
    }
    if (sl_cli_span_equal_cstr(operation, "listen")) {
        return sl_cli_span_equal_cstr(access, "listen") ||
               sl_cli_span_equal_cstr(access, "connect-listen");
    }
    return false;
}

static int sl_cli_route_compare(const SlCliRoute* left, const SlCliRoute* right)
{
    int result = strncmp(left->method.ptr, right->method.ptr,
                         left->method.length < right->method.length ? left->method.length
                                                                    : right->method.length);
    if (result == 0 && left->method.length != right->method.length) {
        return left->method.length < right->method.length ? -1 : 1;
    }
    result = strncmp(left->pattern.ptr, right->pattern.ptr,
                     left->pattern.length < right->pattern.length ? left->pattern.length
                                                                  : right->pattern.length);
    if (result == 0 && left->pattern.length != right->pattern.length) {
        return left->pattern.length < right->pattern.length ? -1 : 1;
    }
    if (left->handler_id == right->handler_id) {
        return 0;
    }
    return left->handler_id < right->handler_id ? -1 : 1;
}

static void sl_cli_sort_routes(SlCliMetadata* metadata)
{
    size_t outer = 0U;
    size_t inner = 0U;

    for (outer = 0U; outer < metadata->route_count; outer += 1U) {
        for (inner = outer + 1U; inner < metadata->route_count; inner += 1U) {
            if (sl_cli_route_compare(&metadata->routes[inner], &metadata->routes[outer]) < 0) {
                SlCliRoute temp = metadata->routes[outer];
                metadata->routes[outer] = metadata->routes[inner];
                metadata->routes[inner] = temp;
            }
        }
    }
}

static void sl_cli_routes_emit_json_bindings(const SlCliRoute* route)
{
    size_t binding = 0U;

    (void)printf(", \"bindings\": [");
    for (binding = 0U; binding < route->binding_count; binding += 1U) {
        (void)printf("%s{ \"kind\": ", binding == 0U ? "" : ", ");
        sl_cli_json_escape(stdout, route->binding_kinds[binding]);
        (void)printf(", \"name\": ");
        sl_cli_json_escape(stdout, route->binding_names[binding]);
        (void)printf(", \"schema\": ");
        sl_cli_json_escape(stdout, route->binding_schemas[binding]);
        (void)printf(" }");
    }
    (void)printf("]");
}

static void sl_cli_routes_emit_json_route(const SlCliRoute* route, size_t index)
{
    (void)printf("%s\n    { \"method\": ", index == 0U ? "" : ",");
    sl_cli_json_escape(stdout, route->method);
    (void)printf(", \"pattern\": ");
    sl_cli_json_escape(stdout, route->pattern);
    (void)printf(", \"handlerId\": %u, \"name\": ", route->handler_id);
    sl_cli_json_escape(stdout, route->name);
    (void)printf(", \"module\": ");
    sl_cli_json_escape(stdout, route->module);
    (void)printf(", \"sourceOrder\": %zu", route->source_order);
    (void)printf(", \"source\": { \"path\": ");
    sl_cli_json_escape(stdout, route->source_path);
    (void)printf(", \"line\": %llu, \"column\": %llu }", (unsigned long long)route->source_line,
                 (unsigned long long)route->source_column);
    (void)printf(", \"completeness\": ");
    sl_cli_json_escape(stdout, route->completeness);
    sl_cli_routes_emit_json_bindings(route);
    (void)printf(", \"response\": { \"kind\": ");
    sl_cli_json_escape(stdout, route->response_kind);
    (void)printf(", \"helper\": ");
    sl_cli_json_escape(stdout, route->response_helper);
    (void)printf(", \"status\": %llu }", (unsigned long long)route->response_status);
    (void)printf(" }");
}

static void sl_cli_routes_emit_text_bindings(const SlCliRoute* route)
{
    size_t binding = 0U;

    if (route->binding_count == 0U) {
        (void)printf("-");
        return;
    }
    for (binding = 0U; binding < route->binding_count; binding += 1U) {
        (void)printf("%s%.*s", binding == 0U ? "" : ",", (int)route->binding_kinds[binding].length,
                     route->binding_kinds[binding].ptr);
        if (!sl_cli_span_empty(route->binding_names[binding])) {
            (void)printf(":%.*s", (int)route->binding_names[binding].length,
                         route->binding_names[binding].ptr);
        }
        if (!sl_cli_span_empty(route->binding_schemas[binding])) {
            (void)printf("(%.*s)", (int)route->binding_schemas[binding].length,
                         route->binding_schemas[binding].ptr);
        }
    }
}

static void sl_cli_routes_emit_text_response(const SlCliRoute* route)
{
    if (!sl_cli_span_empty(route->response_kind)) {
        (void)printf("%llu/%.*s/%.*s", (unsigned long long)route->response_status,
                     (int)route->response_kind.length, route->response_kind.ptr,
                     (int)route->response_helper.length, route->response_helper.ptr);
        return;
    }
    (void)printf("unknown");
}

static void sl_cli_routes_emit_text_route(const SlCliRoute* route)
{
    (void)printf("%-5zu  %-6.*s  %-19.*s  %-7u  %-8.*s  %-6.*s  ", route->source_order,
                 (int)route->method.length, route->method.ptr, (int)route->pattern.length,
                 route->pattern.ptr, route->handler_id, (int)route->completeness.length,
                 route->completeness.ptr, (int)route->module.length, route->module.ptr);
    if (!sl_cli_span_empty(route->source_path)) {
        (void)printf("%.*s:%llu:%llu", (int)route->source_path.length, route->source_path.ptr,
                     (unsigned long long)route->source_line,
                     (unsigned long long)route->source_column);
    }
    (void)printf("  ");
    sl_cli_routes_emit_text_bindings(route);
    (void)printf("  ");
    sl_cli_routes_emit_text_response(route);
    (void)printf("  %.*s\n", (int)route->name.length, route->name.ptr);
}

static int sl_cli_command_routes(const SlCliOptions* options)
{
    unsigned char json_storage[SL_CLI_FILE_MAX_BYTES];
    unsigned char plan_arena_storage[SL_CLI_ARENA_BYTES];
    SlArena plan_arena;
    yyjson_doc* doc = NULL;
    SlCliMetadata metadata = {0};
    size_t index = 0U;

    if (options->plan_path == NULL) {
        sl_cli_write_cstr(stderr, "sloppy routes: --plan <path> is required\n");
        return 1;
    }
    if (!sl_status_is_ok(
            sl_arena_init(&plan_arena, plan_arena_storage, sizeof(plan_arena_storage))))
    {
        sl_cli_write_cstr(stderr, "sloppy routes: failed to initialize plan validation arena\n");
        return 1;
    }
    if (sl_cli_load_metadata(options->plan_path, json_storage, &plan_arena, true, &doc,
                             &metadata) != 0)
    {
        if (doc != NULL) {
            yyjson_doc_free(doc);
        }
        return 1;
    }
    sl_cli_sort_routes(&metadata);

    if (options->format == SL_CLI_FORMAT_JSON) {
        (void)printf("{\n  \"routes\": [");
        for (index = 0U; index < metadata.route_count; index += 1U) {
            sl_cli_routes_emit_json_route(&metadata.routes[index], index);
        }
        (void)printf("\n  ]\n}\n");
    }
    else {
        (void)printf("ORDER  METHOD  PATTERN              HANDLER  COMPLETE  MODULE  SOURCE  "
                     "BINDINGS  RESPONSE  NAME\n");
        if (metadata.route_count == 0U) {
            (void)printf("No routes.\n");
        }
        for (index = 0U; index < metadata.route_count; index += 1U) {
            sl_cli_routes_emit_text_route(&metadata.routes[index]);
        }
    }

    yyjson_doc_free(doc);
    return 0;
}

static void sl_cli_capabilities_emit_json_effect(const SlCliRoute* route, size_t effect_index,
                                                 size_t finding_count)
{
    (void)printf("%s\n    { \"route\": { \"method\": ", finding_count == 0U ? "" : ",");
    sl_cli_json_escape(stdout, route->method);
    (void)printf(", \"pattern\": ");
    sl_cli_json_escape(stdout, route->pattern);
    (void)printf(" }, \"provider\": ");
    sl_cli_json_escape(stdout, route->effect_providers[effect_index]);
    (void)printf(", \"providerKind\": ");
    sl_cli_json_escape(stdout, route->effect_provider_kinds[effect_index]);
    (void)printf(", \"capabilityKind\": ");
    sl_cli_json_escape(stdout, route->effect_capability_kinds[effect_index]);
    (void)printf(", \"access\": ");
    sl_cli_json_escape(stdout, route->effect_accesses[effect_index]);
    (void)printf(", \"inference\": \"generated\", \"reason\": ");
    sl_cli_json_escape(stdout, route->effect_reasons[effect_index]);
    (void)printf(", \"operation\": ");
    sl_cli_json_escape(stdout, route->effect_operations[effect_index]);
    (void)printf(", \"source\": { \"path\": ");
    sl_cli_json_escape(stdout, route->source_path);
    (void)printf(", \"line\": %llu, \"column\": %llu } }", (unsigned long long)route->source_line,
                 (unsigned long long)route->source_column);
}

static void sl_cli_capabilities_emit_text_effect(const SlCliRoute* route, size_t effect_index)
{
    (void)printf(
        "%.*s %.*s  %.*s  %.*s  %.*s  generated:%.*s  ", (int)route->method.length,
        route->method.ptr, (int)route->pattern.length, route->pattern.ptr,
        (int)route->effect_providers[effect_index].length,
        route->effect_providers[effect_index].ptr,
        (int)route->effect_provider_kinds[effect_index].length,
        route->effect_provider_kinds[effect_index].ptr,
        (int)route->effect_accesses[effect_index].length, route->effect_accesses[effect_index].ptr,
        (int)route->effect_reasons[effect_index].length, route->effect_reasons[effect_index].ptr);
    if (!sl_cli_span_empty(route->source_path)) {
        (void)printf("%.*s:%llu:%llu", (int)route->source_path.length, route->source_path.ptr,
                     (unsigned long long)route->source_line,
                     (unsigned long long)route->source_column);
    }
    (void)printf("\n");
}

static void sl_cli_capabilities_emit_route_effects(const SlCliOptions* options,
                                                   const SlCliRoute* route, size_t* finding_count)
{
    size_t effect_index = 0U;

    for (effect_index = 0U; effect_index < route->effect_count; effect_index += 1U) {
        if (options->format == SL_CLI_FORMAT_JSON) {
            sl_cli_capabilities_emit_json_effect(route, effect_index, *finding_count);
        }
        else {
            sl_cli_capabilities_emit_text_effect(route, effect_index);
        }
        *finding_count += 1U;
    }
}

static int sl_cli_command_capabilities(const SlCliOptions* options)
{
    unsigned char json_storage[SL_CLI_FILE_MAX_BYTES];
    unsigned char plan_arena_storage[SL_CLI_ARENA_BYTES];
    SlArena plan_arena;
    yyjson_doc* doc = NULL;
    SlCliMetadata metadata = {0};
    size_t route_index = 0U;
    size_t finding_count = 0U;

    if (options->plan_path == NULL) {
        sl_cli_write_cstr(stderr, "sloppy capabilities: --plan <path> is required\n");
        return 1;
    }
    if (!sl_status_is_ok(
            sl_arena_init(&plan_arena, plan_arena_storage, sizeof(plan_arena_storage))))
    {
        sl_cli_write_cstr(stderr,
                          "sloppy capabilities: failed to initialize plan validation arena\n");
        return 1;
    }
    if (sl_cli_load_metadata(options->plan_path, json_storage, &plan_arena, true, &doc,
                             &metadata) != 0)
    {
        if (doc != NULL) {
            yyjson_doc_free(doc);
        }
        return 1;
    }
    sl_cli_sort_routes(&metadata);

    if (options->format == SL_CLI_FORMAT_JSON) {
        (void)printf("{\n  \"capabilities\": [");
    }
    else {
        (void)printf("ROUTE  PROVIDER  KIND  ACCESS  REASON  SOURCE\n");
    }
    for (route_index = 0U; route_index < metadata.route_count; route_index += 1U) {
        sl_cli_capabilities_emit_route_effects(options, &metadata.routes[route_index],
                                               &finding_count);
    }
    if (finding_count == 0U && options->format == SL_CLI_FORMAT_TEXT) {
        (void)printf("No inferred route capabilities.\n");
    }
    if (options->format == SL_CLI_FORMAT_JSON) {
        (void)printf("\n  ]\n}\n");
    }

    yyjson_doc_free(doc);
    return 0;
}

static void sl_cli_doctor_emit_text(SlArena* arena, SlCliSpan id, SlCliSpan status,
                                    SlCliSpan message)
{
    (void)printf("[");
    sl_cli_write_span(stdout, status);
    (void)printf("] ");
    sl_cli_write_span(stdout, id);
    if (!sl_cli_span_empty(message)) {
        (void)printf(": ");
        sl_cli_write_redacted(stdout, arena, message);
    }
    (void)printf("\n");
}

static void sl_cli_doctor_emit_json(SlArena* arena, SlCliSpan id, SlCliSpan status,
                                    SlCliSpan message, bool comma)
{
    (void)printf("%s\n    { \"id\": ", comma ? "," : "");
    sl_cli_json_escape(stdout, id);
    (void)printf(", \"status\": ");
    sl_cli_json_escape(stdout, status);
    (void)printf(", \"message\": ");
    sl_cli_json_redacted(stdout, arena, message);
    (void)printf(" }");
}

static bool sl_cli_path_exists(const char* path)
{
    FILE* file = NULL;

    if (path == NULL || path[0] == '\0') {
        return false;
    }
#ifdef _MSC_VER
    if (fopen_s(&file, path, "rb") != 0) {
        file = NULL;
    }
#else
    file = fopen(path, "rb");
#endif
    if (file == NULL) {
        return false;
    }
    (void)fclose(file);
    return true;
}

static void sl_cli_doctor_emit(const SlCliOptions* options, SlArena* arena, SlCliSpan id,
                               SlCliSpan status, SlCliSpan message, bool* emitted)
{
    if (options->format == SL_CLI_FORMAT_JSON) {
        sl_cli_doctor_emit_json(arena, id, status, message, *emitted);
        *emitted = true;
        return;
    }

    sl_cli_doctor_emit_text(arena, id, status, message);
}

static void sl_cli_doctor_emit_fs_capabilities(const SlCliOptions* options, SlArena* arena,
                                               const SlCliMetadata* metadata, bool* emitted)
{
    bool has_filesystem_capability = false;
    bool has_watch_capability = false;

    for (size_t index = 0U; index < metadata->capability_count; index += 1U) {
        if (sl_cli_span_equal_cstr(metadata->capabilities[index].kind, "filesystem")) {
            has_filesystem_capability = true;
            if (sl_cli_span_equal_cstr(metadata->capabilities[index].access, "watch")) {
                has_watch_capability = true;
            }
        }
    }
    if (has_filesystem_capability) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("stdlib.fs.capabilities"), sl_cli_span_cstr("ok"),
            sl_cli_span_cstr(
                "filesystem capability metadata is visible to sloppy/fs policy checks"),
            emitted);
    }
    if (has_watch_capability) {
        sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("stdlib.fs.watch"),
                           sl_cli_span_cstr("ok"),
                           sl_cli_span_cstr("fs.watch capability metadata is present"), emitted);
    }
}

static void sl_cli_doctor_emit_plan_metadata(const SlCliOptions* options, SlArena* arena,
                                             const SlCliMetadata* metadata, bool* emitted)
{
    sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("app.plan.routes"),
                       metadata->route_count > 0U ? sl_cli_span_cstr("ok")
                                                  : sl_cli_span_cstr("warn"),
                       metadata->route_count > 0U ? sl_cli_span_cstr("route metadata present")
                                                  : sl_cli_span_cstr("no route metadata present"),
                       emitted);
    sl_cli_doctor_emit(
        options, arena, sl_cli_span_cstr("app.plan.providers"),
        metadata->provider_count > 0U ? sl_cli_span_cstr("ok") : sl_cli_span_cstr("warn"),
        metadata->provider_count > 0U ? sl_cli_span_cstr("provider metadata present")
                                      : sl_cli_span_cstr("provider metadata not present"),
        emitted);
    sl_cli_doctor_emit(
        options, arena, sl_cli_span_cstr("app.plan.capabilities"),
        metadata->capability_count > 0U ? sl_cli_span_cstr("ok") : sl_cli_span_cstr("warn"),
        metadata->capability_count > 0U ? sl_cli_span_cstr("capability metadata present")
                                        : sl_cli_span_cstr("capability metadata not present"),
        emitted);
    sl_cli_doctor_emit_fs_capabilities(options, arena, metadata, emitted);
    if (!sl_cli_span_empty(metadata->completeness)) {
        SlCliSpan status = sl_cli_span_cstr("warn");
        SlCliSpan message =
            sl_cli_span_cstr("Plan completeness is partial, runtime-only, or invalid");

        if (sl_cli_span_equal_cstr(metadata->completeness, "complete")) {
            status = sl_cli_span_cstr("ok");
            message = sl_cli_span_cstr("Plan completeness is complete");
        }
        else if (sl_cli_span_equal_cstr(metadata->completeness, "invalid")) {
            status = sl_cli_span_cstr("error");
        }
        sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("app.plan.completeness"), status,
                           message, emitted);
    }
    for (size_t index = 0U; index < metadata->route_count; index += 1U) {
        const SlCliRoute* route = &metadata->routes[index];
        if (!sl_cli_span_empty(route->completeness) &&
            !sl_cli_span_equal_cstr(route->completeness, "complete"))
        {
            sl_cli_doctor_emit(
                options, arena, sl_cli_span_cstr("route.completeness"),
                sl_cli_span_equal_cstr(route->completeness, "invalid") ? sl_cli_span_cstr("error")
                                                                       : sl_cli_span_cstr("warn"),
                sl_cli_span_cstr("route has partial/runtime-only/invalid Plan metadata"), emitted);
        }
        if (sl_cli_span_empty(route->response_kind)) {
            sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("route.response"),
                               sl_cli_span_cstr("warn"),
                               sl_cli_span_cstr("route response metadata is unknown"), emitted);
        }
        for (size_t binding = 0U; binding < route->binding_count; binding += 1U) {
            if (sl_cli_span_equal_cstr(route->binding_kinds[binding], "body.json") &&
                sl_cli_span_empty(route->binding_schemas[binding]))
            {
                sl_cli_doctor_emit(
                    options, arena, sl_cli_span_cstr("route.validation"), sl_cli_span_cstr("warn"),
                    sl_cli_span_cstr("JSON body binding has no schema metadata"), emitted);
            }
        }
    }
    {
        bool emitted_response_candidate = false;
        bool emitted_provider_candidate = false;

        for (size_t index = 0U; index < metadata->route_count; index += 1U) {
            const SlCliRoute* route = &metadata->routes[index];
            if (!emitted_response_candidate && sl_cli_span_equal_cstr(route->response_kind, "json"))
            {
                sl_cli_doctor_emit(
                    options, arena, sl_cli_span_cstr("optimization.native-json-candidate"),
                    sl_cli_span_cstr("ok"),
                    sl_cli_span_cstr("route has response metadata for future native JSON analysis"),
                    emitted);
                emitted_response_candidate = true;
            }
            if (!emitted_provider_candidate && route->effect_count > 0U) {
                sl_cli_doctor_emit(
                    options, arena, sl_cli_span_cstr("optimization.provider-route-candidate"),
                    sl_cli_span_cstr("ok"),
                    sl_cli_span_cstr(
                        "route has provider/effect metadata for future route analysis"),
                    emitted);
                emitted_provider_candidate = true;
            }
        }
    }
}

static void sl_cli_doctor_emit_environment(const SlCliOptions* options, SlArena* arena,
                                           bool* emitted)
{
    const char* bootstrap_asset = SLOPPY_BOOTSTRAP_BUILD_DIR "/internal/runtime-classic.js";

    sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("bootstrap.assets"),
                       sl_cli_path_exists(bootstrap_asset) ? sl_cli_span_cstr("ok")
                                                           : sl_cli_span_cstr("warn"),
                       sl_cli_path_exists(bootstrap_asset)
                           ? sl_cli_span_cstr("bootstrap runtime asset found")
                           : sl_cli_span_cstr("bootstrap runtime asset not found in build layout"),
                       emitted);
#ifdef SLOPPY_ENABLE_V8_BRIDGE
    sl_cli_doctor_emit(
        options, arena, sl_cli_span_cstr("engine.v8"), sl_cli_span_cstr("ok"),
        sl_cli_span_cstr("V8 bridge compiled; runtime success still requires V8 tests"), emitted);
#else
    sl_cli_doctor_emit(
        options, arena, sl_cli_span_cstr("engine.v8"), sl_cli_span_cstr("warn"),
        sl_cli_span_cstr("V8 bridge disabled in this build; V8 runtime tests not run"), emitted);
#endif
    sl_cli_doctor_emit(
        options, arena, sl_cli_span_cstr("providers.live"), sl_cli_span_cstr("warn"),
        sl_cli_span_cstr(
            "live provider checks are not configured by default and no live DB was contacted"),
        emitted);
    sl_cli_doctor_emit(
        options, arena, sl_cli_span_cstr("package.runtime"), sl_cli_span_cstr("warn"),
        sl_cli_span_cstr("local CLI checks do not prove package release readiness"), emitted);
}

static int sl_cli_command_doctor(const SlCliOptions* options)
{
    unsigned char json_storage[SL_CLI_FILE_MAX_BYTES];
    unsigned char plan_arena_storage[SL_CLI_ARENA_BYTES];
    unsigned char diag_arena_storage[SL_CLI_ARENA_BYTES];
    SlArena plan_arena;
    SlArena diag_arena;
    yyjson_doc* doc = NULL;
    SlCliMetadata metadata = {0};
    size_t index = 0U;
    bool emitted = false;

    if (!sl_status_is_ok(
            sl_arena_init(&plan_arena, plan_arena_storage, sizeof(plan_arena_storage))) ||
        !sl_status_is_ok(
            sl_arena_init(&diag_arena, diag_arena_storage, sizeof(diag_arena_storage))))
    {
        sl_cli_write_cstr(stderr, "sloppy doctor: failed to initialize validation arena\n");
        return 1;
    }

    if (options->plan_path != NULL && sl_cli_load_metadata(options->plan_path, json_storage,
                                                           &plan_arena, true, &doc, &metadata) != 0)
    {
        if (doc != NULL) {
            yyjson_doc_free(doc);
        }
        return 1;
    }

    if (options->format == SL_CLI_FORMAT_JSON) {
        (void)printf("{\n  \"checks\": [");
        sl_cli_doctor_emit_environment(options, &diag_arena, &emitted);
        if (options->plan_path != NULL) {
            sl_cli_doctor_emit(
                options, &diag_arena, sl_cli_span_cstr("app.plan.parse"), sl_cli_span_cstr("ok"),
                sl_cli_span_cstr("app plan parsed by native Plan v1 parser"), &emitted);
            sl_cli_doctor_emit_plan_metadata(options, &diag_arena, &metadata, &emitted);
        }
        for (index = 0U; index < metadata.doctor_check_count; index += 1U) {
            sl_cli_doctor_emit(options, &diag_arena, metadata.doctor_checks[index].id,
                               metadata.doctor_checks[index].status,
                               metadata.doctor_checks[index].message, &emitted);
        }
        (void)printf("\n  ]\n}\n");
    }
    else {
        (void)printf("Sloppy Doctor\n\n");
        sl_cli_doctor_emit_environment(options, &diag_arena, &emitted);
        if (options->plan_path != NULL) {
            sl_cli_doctor_emit(
                options, &diag_arena, sl_cli_span_cstr("app.plan.parse"), sl_cli_span_cstr("ok"),
                sl_cli_span_cstr("app plan parsed by native Plan v1 parser"), &emitted);
            sl_cli_doctor_emit_plan_metadata(options, &diag_arena, &metadata, &emitted);
        }
        for (index = 0U; index < metadata.doctor_check_count; index += 1U) {
            sl_cli_doctor_emit(options, &diag_arena, metadata.doctor_checks[index].id,
                               metadata.doctor_checks[index].status,
                               metadata.doctor_checks[index].message, &emitted);
        }
    }

    if (doc != NULL) {
        yyjson_doc_free(doc);
    }
    return 0;
}

static void sl_cli_audit_text(SlCliSpan severity, const char* code, const char* message,
                              const char* path)
{
    (void)printf("[%.*s] %s %s (%s)\n", (int)severity.length, severity.ptr, code, message, path);
}

static void sl_cli_audit_json(SlCliSpan severity, const char* code, const char* message,
                              const char* path, bool comma)
{
    (void)printf("%s\n    { \"severity\": ", comma ? "," : "");
    sl_cli_json_escape(stdout, severity);
    (void)printf(", \"code\": \"%s\", \"message\": ", code);
    sl_cli_json_escape(stdout, sl_cli_span_cstr(message));
    (void)printf(", \"path\": \"%s\" }", path);
}

static void sl_cli_audit_emit(const SlCliOptions* options, const char* severity, const char* code,
                              const char* message, const char* path, size_t* findings,
                              size_t* errors)
{
    if (options->format == SL_CLI_FORMAT_JSON) {
        sl_cli_audit_json(sl_cli_span_cstr(severity), code, message, path, *findings > 0U);
    }
    else {
        sl_cli_audit_text(sl_cli_span_cstr(severity), code, message, path);
    }
    *findings += 1U;
    if (errors != NULL && strcmp(severity, "error") == 0) {
        *errors += 1U;
    }
}

static void sl_cli_audit_routes(const SlCliOptions* options, const SlCliMetadata* metadata,
                                size_t* findings, size_t* errors)
{
    size_t outer = 0U;
    size_t inner = 0U;

    for (outer = 0U; outer < metadata->route_count; outer += 1U) {
        if (sl_cli_find_handler(metadata, metadata->routes[outer].handler_id) == NULL) {
            sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_MISSING_HANDLER",
                              "route references a missing handler id", "routes", findings, errors);
        }
        if (!sl_cli_span_empty(metadata->routes[outer].capability) &&
            sl_cli_find_capability(metadata, metadata->routes[outer].capability) == NULL)
        {
            sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_ROUTE_CAPABILITY_MISSING",
                              "route references an undeclared capability", "routes", findings,
                              errors);
        }
        for (inner = outer + 1U; inner < metadata->route_count; inner += 1U) {
            if (!sl_cli_span_empty(metadata->routes[outer].name) &&
                sl_cli_span_equal(metadata->routes[outer].name, metadata->routes[inner].name))
            {
                sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_DUPLICATE_ROUTE_NAME",
                                  "duplicate route name", "routes", findings, errors);
            }
            if (sl_cli_span_equal(metadata->routes[outer].method, metadata->routes[inner].method) &&
                sl_cli_span_equal(metadata->routes[outer].pattern, metadata->routes[inner].pattern))
            {
                sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_DUPLICATE_ROUTE",
                                  "duplicate route method and pattern", "routes", findings, errors);
            }
        }
        if (!sl_cli_span_empty(metadata->routes[outer].completeness) &&
            !sl_cli_span_equal_cstr(metadata->routes[outer].completeness, "complete"))
        {
            sl_cli_audit_emit(
                options,
                sl_cli_span_equal_cstr(metadata->routes[outer].completeness, "invalid") ? "error"
                                                                                        : "warn",
                "SLOPPY_AUDIT_ROUTE_COMPLETENESS", "route Plan completeness is not complete",
                "routes", findings, errors);
        }
        if (metadata->routes[outer].binding_count > 0U) {
            size_t binding = 0U;
            for (binding = 0U; binding < metadata->routes[outer].binding_count; binding += 1U) {
                if (sl_cli_span_equal_cstr(metadata->routes[outer].binding_kinds[binding],
                                           "body.json") &&
                    sl_cli_span_empty(metadata->routes[outer].binding_schemas[binding]))
                {
                    sl_cli_audit_emit(options, "warn", "SLOPPY_AUDIT_BODY_SCHEMA_MISSING",
                                      "JSON body binding has no schema metadata", "validation",
                                      findings, errors);
                }
            }
        }
        if (!sl_cli_span_empty(metadata->routes[outer].completeness) &&
            sl_cli_span_empty(metadata->routes[outer].response_kind))
        {
            sl_cli_audit_emit(options, "warn", "SLOPPY_AUDIT_RESPONSE_UNKNOWN",
                              "route response metadata is unknown", "response", findings, errors);
        }
        if (sl_cli_span_equal_cstr(metadata->routes[outer].response_kind, "json")) {
            sl_cli_audit_emit(
                options, "note", "SLOPPY_AUDIT_OPT_NATIVE_JSON_CANDIDATE",
                "route response metadata is available for future native JSON analysis",
                "optimization", findings, errors);
        }
        if (metadata->routes[outer].effect_count > 0U) {
            sl_cli_audit_emit(
                options, "note", "SLOPPY_AUDIT_OPT_PROVIDER_ROUTE_CANDIDATE",
                "route provider/effect metadata is available for future route analysis",
                "optimization", findings, errors);
        }
    }
}

static void sl_cli_audit_modules(const SlCliOptions* options, const SlCliMetadata* metadata,
                                 size_t* findings, size_t* errors)
{
    size_t outer = 0U;
    size_t inner = 0U;

    for (outer = 0U; outer < metadata->module_count; outer += 1U) {
        for (inner = 0U; inner < metadata->modules[outer].dependency_count; inner += 1U) {
            const SlCliModule* dep =
                sl_cli_find_module(metadata, metadata->modules[outer].dependencies[inner]);
            if (dep == NULL) {
                sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_MISSING_MODULE_DEPENDENCY",
                                  "module dependency is missing", "modules", findings, errors);
            }
            else {
                size_t dep_index = 0U;
                for (dep_index = 0U; dep_index < dep->dependency_count; dep_index += 1U) {
                    if (sl_cli_span_equal(dep->dependencies[dep_index],
                                          metadata->modules[outer].name))
                    {
                        sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_MODULE_CYCLE",
                                          "module dependency cycle detected", "modules", findings,
                                          errors);
                    }
                }
            }
        }
    }
}

static void sl_cli_audit_providers(const SlCliOptions* options, const SlCliMetadata* metadata,
                                   size_t* findings, size_t* errors)
{
    size_t index = 0U;

    for (index = 0U; index < metadata->provider_count; index += 1U) {
        if (sl_cli_span_empty(metadata->providers[index].token) ||
            sl_cli_span_empty(metadata->providers[index].provider) ||
            sl_cli_span_empty(metadata->providers[index].service))
        {
            sl_cli_audit_emit(options, "warn", "SLOPPY_AUDIT_PROVIDER_INCOMPLETE",
                              "provider metadata is missing token, provider, or service",
                              "dataProviders", findings, errors);
        }
        if (!sl_cli_span_empty(metadata->providers[index].capability)) {
            const SlCliCapability* capability =
                sl_cli_find_capability(metadata, metadata->providers[index].capability);
            if (capability == NULL) {
                sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_PROVIDER_CAPABILITY_MISSING",
                                  "data provider references an undeclared capability",
                                  "dataProviders", findings, errors);
            }
            else if (!sl_cli_span_equal_cstr(capability->kind, "database")) {
                sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_PROVIDER_CAPABILITY_KIND",
                                  "data provider capability is not a database capability",
                                  "dataProviders", findings, errors);
            }
            else {
                if (!sl_cli_span_empty(capability->provider) &&
                    !sl_cli_span_equal(capability->provider, metadata->providers[index].token))
                {
                    sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_PROVIDER_MISMATCH",
                                      "capability provider reference does not match data provider",
                                      "capabilities", findings, errors);
                }
                if (!sl_cli_span_empty(metadata->providers[index].required_access) &&
                    !sl_cli_capability_allows(capability->access,
                                              metadata->providers[index].required_access))
                {
                    sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_CAPABILITY_INSUFFICIENT",
                                      "capability access is insufficient for provider operation",
                                      "capabilities", findings, errors);
                }
            }
        }
    }
}

static void sl_cli_audit_capabilities(const SlCliOptions* options, const SlCliMetadata* metadata,
                                      size_t* findings, size_t* errors)
{
    size_t outer = 0U;
    size_t inner = 0U;
    bool has_filesystem = false;
    bool has_network = false;

    for (outer = 0U; outer < metadata->capability_count; outer += 1U) {
        if (sl_cli_span_empty(metadata->capabilities[outer].token) ||
            sl_cli_span_empty(metadata->capabilities[outer].kind) ||
            sl_cli_span_empty(metadata->capabilities[outer].access))
        {
            sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_CAPABILITY_INCOMPLETE",
                              "capability metadata is missing token, kind, or access",
                              "capabilities", findings, errors);
        }
        for (inner = outer + 1U; inner < metadata->capability_count; inner += 1U) {
            if (!sl_cli_span_empty(metadata->capabilities[outer].token) &&
                sl_cli_span_equal(metadata->capabilities[outer].token,
                                  metadata->capabilities[inner].token))
            {
                sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_DUPLICATE_CAPABILITY",
                                  "duplicate capability token", "capabilities", findings, errors);
            }
        }
        if (sl_cli_span_equal_cstr(metadata->capabilities[outer].kind, "database")) {
            if (sl_cli_span_empty(metadata->capabilities[outer].provider)) {
                sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_CAPABILITY_PROVIDER_REQUIRED",
                                  "database capability is missing required provider reference",
                                  "capabilities", findings, errors);
            }
            else if (sl_cli_find_provider(metadata, metadata->capabilities[outer].provider) == NULL)
            {
                sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_CAPABILITY_PROVIDER_MISSING",
                                  "database capability references an undeclared provider",
                                  "capabilities", findings, errors);
            }
        }
        else if ((sl_cli_span_equal_cstr(metadata->capabilities[outer].kind, "filesystem") ||
                  sl_cli_span_equal_cstr(metadata->capabilities[outer].kind, "network")) &&
                 !sl_cli_span_empty(metadata->capabilities[outer].provider))
        {
            sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_CAPABILITY_PROVIDER_FORBIDDEN",
                              "filesystem/network capabilities must not declare providers",
                              "capabilities", findings, errors);
        }
        if (sl_cli_span_equal_cstr(metadata->capabilities[outer].kind, "filesystem")) {
            has_filesystem = true;
        }
        if (sl_cli_span_equal_cstr(metadata->capabilities[outer].kind, "network")) {
            has_network = true;
        }
    }
    if (has_filesystem) {
        sl_cli_audit_emit(options, "note", "SLOPPY_AUDIT_FILESYSTEM_POLICY_VISIBLE",
                          "filesystem capabilities are policy-visible for sloppy/fs; no OS "
                          "sandbox is implemented",
                          "capabilities", findings, errors);
    }
    if (has_network) {
        sl_cli_audit_emit(options, "note", "SLOPPY_AUDIT_NETWORK_SKELETON",
                          "network capabilities are metadata/check-only; no network API or OS "
                          "sandbox is implemented",
                          "capabilities", findings, errors);
    }
}

static int sl_cli_command_audit(const SlCliOptions* options)
{
    unsigned char json_storage[SL_CLI_FILE_MAX_BYTES];
    yyjson_doc* doc = NULL;
    SlCliMetadata metadata = {0};
    size_t findings = 0U;
    size_t errors = 0U;

    if (options->plan_path == NULL) {
        sl_cli_write_cstr(stderr, "sloppy audit: --plan <path> is required\n");
        return 1;
    }
    if (sl_cli_load_metadata(options->plan_path, json_storage, NULL, false, &doc, &metadata) != 0) {
        if (doc != NULL) {
            yyjson_doc_free(doc);
        }
        return 1;
    }

    if (options->format == SL_CLI_FORMAT_JSON) {
        (void)printf("{\n  \"findings\": [");
    }
    else {
        (void)printf("Sloppy Audit\n\n");
    }

    if (!sl_cli_span_empty(metadata.completeness) &&
        sl_cli_span_equal_cstr(metadata.completeness, "invalid"))
    {
        sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_PLAN_INVALID",
                          "Plan completeness is invalid", "completeness", &findings, &errors);
    }
    sl_cli_audit_routes(options, &metadata, &findings, &errors);
    sl_cli_audit_modules(options, &metadata, &findings, &errors);
    sl_cli_audit_providers(options, &metadata, &findings, &errors);
    sl_cli_audit_capabilities(options, &metadata, &findings, &errors);

    if (findings == 0U && options->format == SL_CLI_FORMAT_TEXT) {
        (void)printf("No findings.\n");
    }
    if (options->format == SL_CLI_FORMAT_JSON) {
        (void)printf("\n  ]\n}\n");
    }

    yyjson_doc_free(doc);
    return errors > 0U ? 1 : 0;
}

static bool sl_cli_openapi_path(char* buffer, size_t capacity, SlCliSpan pattern,
                                SlCliSpan* openapi_path)
{
    SlStringBuilder builder = {0};
    SlStr view = {0};
    size_t index = 0U;
    SlStatus status;

    if (buffer == NULL || capacity == 0U || openapi_path == NULL ||
        (pattern.ptr == NULL && pattern.length != 0U))
    {
        return false;
    }

    status = sl_string_builder_init_fixed(&builder, buffer, capacity);
    if (!sl_status_is_ok(status)) {
        return false;
    }

    while (index < pattern.length) {
        if (pattern.ptr[index] == '{') {
            status = sl_string_builder_append_char(&builder, pattern.ptr[index]);
            if (!sl_status_is_ok(status)) {
                return false;
            }
            index += 1U;

            while (index < pattern.length && pattern.ptr[index] != '}' && pattern.ptr[index] != ':')
            {
                status = sl_string_builder_append_char(&builder, pattern.ptr[index]);
                if (!sl_status_is_ok(status)) {
                    return false;
                }
                index += 1U;
            }

            while (index < pattern.length && pattern.ptr[index] != '}') {
                index += 1U;
            }
            if (index >= pattern.length) {
                return false;
            }
        }
        status = sl_string_builder_append_char(&builder, pattern.ptr[index]);
        if (!sl_status_is_ok(status)) {
            return false;
        }
        index += 1U;
    }

    status = sl_string_builder_view_with_nul(&builder, &view);
    if (!sl_status_is_ok(status)) {
        return false;
    }

    *openapi_path = sl_cli_span_from_str(view);
    return true;
}

static SlCliSpan sl_cli_openapi_type_for_kind(SlCliSpan kind)
{
    if (sl_cli_span_equal_cstr(kind, "int") || sl_cli_span_equal_cstr(kind, "integer")) {
        return sl_cli_span_cstr("integer");
    }
    if (sl_cli_span_equal_cstr(kind, "number")) {
        return sl_cli_span_cstr("number");
    }
    if (sl_cli_span_equal_cstr(kind, "boolean") || sl_cli_span_equal_cstr(kind, "bool")) {
        return sl_cli_span_cstr("boolean");
    }
    if (sl_cli_span_equal_cstr(kind, "array")) {
        return sl_cli_span_cstr("array");
    }
    if (sl_cli_span_equal_cstr(kind, "object")) {
        return sl_cli_span_cstr("object");
    }
    return sl_cli_span_cstr("string");
}

static void sl_cli_openapi_emit_parameter(FILE* file, SlCliSpan name, SlCliSpan location,
                                          SlCliSpan type, bool required, bool* first)
{
    if (!*first) {
        sl_cli_write_cstr(file, ",");
    }
    sl_cli_write_cstr(file, "\n          { \"name\": ");
    sl_cli_json_escape(file, name);
    sl_cli_write_cstr(file, ", \"in\": ");
    sl_cli_json_escape(file, location);
    sl_cli_write_cstr(file, ", \"required\": ");
    sl_cli_write_cstr(file, required ? "true" : "false");
    sl_cli_write_cstr(file, ", \"schema\": { \"type\": ");
    sl_cli_json_escape(file, sl_cli_openapi_type_for_kind(type));
    sl_cli_write_cstr(file, " } }");
    *first = false;
}

static void sl_cli_openapi_emit_field_prefix(FILE* out, bool* first)
{
    sl_cli_write_cstr(out, *first ? "" : ",");
    sl_cli_write_cstr(out, "\n        ");
    *first = false;
}

static const SlCliSchema* sl_cli_openapi_find_schema(const SlCliMetadata* metadata, SlCliSpan name)
{
    size_t index = 0U;

    if (sl_cli_span_empty(name)) {
        return NULL;
    }
    for (index = 0U; index < metadata->schema_count; index += 1U) {
        if (sl_cli_span_equal(metadata->schemas[index].name, name)) {
            return &metadata->schemas[index];
        }
    }
    return NULL;
}

static bool sl_cli_openapi_pattern_has_typed_parameter(SlCliSpan pattern)
{
    size_t index = 0U;

    while (index < pattern.length) {
        if (pattern.ptr[index] == '{') {
            while (index < pattern.length && pattern.ptr[index] != '}') {
                if (pattern.ptr[index] == ':') {
                    return true;
                }
                index += 1U;
            }
        }
        index += 1U;
    }

    return false;
}

static bool sl_cli_openapi_route_has_validation(const SlCliRoute* route)
{
    size_t index = 0U;

    if (sl_cli_openapi_pattern_has_typed_parameter(route->pattern)) {
        return true;
    }
    for (index = 0U; index < route->binding_count; index += 1U) {
        if (sl_cli_span_equal_cstr(route->binding_kinds[index], "body.json")) {
            return true;
        }
        if ((sl_cli_span_equal_cstr(route->binding_kinds[index], "query") ||
             sl_cli_span_equal_cstr(route->binding_kinds[index], "header")) &&
            !sl_cli_span_empty(route->binding_schemas[index]))
        {
            return true;
        }
    }

    return false;
}

static void sl_cli_openapi_parameters(FILE* file, const SlCliRoute* route)
{
    size_t index = 0U;
    bool first = true;

    sl_cli_write_cstr(file, "[");
    while (index < route->pattern.length) {
        if (route->pattern.ptr[index] == '{') {
            size_t name_start = index + 1U;
            size_t name_end = name_start;
            size_t type_start = 0U;
            size_t type_end = 0U;
            SlCliSpan type = {0};

            while (name_end < route->pattern.length && route->pattern.ptr[name_end] != '}' &&
                   route->pattern.ptr[name_end] != ':')
            {
                name_end += 1U;
            }
            if (name_end < route->pattern.length && route->pattern.ptr[name_end] == ':') {
                type_start = name_end + 1U;
                type_end = type_start;
                while (type_end < route->pattern.length && route->pattern.ptr[type_end] != '}') {
                    type_end += 1U;
                }
                type = (SlCliSpan){&route->pattern.ptr[type_start], type_end - type_start};
            }
            sl_cli_openapi_emit_parameter(
                file, (SlCliSpan){&route->pattern.ptr[name_start], name_end - name_start},
                sl_cli_span_cstr("path"), type, true, &first);
        }
        index += 1U;
    }
    for (index = 0U; index < route->binding_count; index += 1U) {
        if (sl_cli_span_equal_cstr(route->binding_kinds[index], "query")) {
            sl_cli_openapi_emit_parameter(file, route->binding_names[index],
                                          sl_cli_span_cstr("query"), route->binding_schemas[index],
                                          false, &first);
        }
        else if (sl_cli_span_equal_cstr(route->binding_kinds[index], "header")) {
            sl_cli_openapi_emit_parameter(file, route->binding_names[index],
                                          sl_cli_span_cstr("header"), route->binding_schemas[index],
                                          false, &first);
        }
    }
    if (!first) {
        sl_cli_write_cstr(file, "\n        ");
    }
    sl_cli_write_cstr(file, "]");
}

static int sl_cli_openapi_prepare_paths(const SlCliMetadata* metadata,
                                        char path_buffers[SL_CLI_MAX_ROUTES][512],
                                        SlCliSpan openapi_paths[SL_CLI_MAX_ROUTES])
{
    size_t index = 0U;

    for (index = 0U; index < metadata->route_count; index += 1U) {
        if (!sl_cli_openapi_path(path_buffers[index], sizeof(path_buffers[index]),
                                 metadata->routes[index].pattern, &openapi_paths[index]))
        {
            sl_cli_write_cstr(stderr, "sloppy openapi: malformed route pattern in metadata: ");
            sl_cli_write_span(stderr, metadata->routes[index].pattern);
            sl_cli_write_cstr(stderr, "\n");
            return 1;
        }
    }

    return 0;
}

static bool sl_cli_openapi_path_seen(const SlCliSpan openapi_paths[SL_CLI_MAX_ROUTES],
                                     size_t current)
{
    size_t index = 0U;

    for (index = 0U; index < current; index += 1U) {
        if (sl_cli_span_equal(openapi_paths[index], openapi_paths[current])) {
            return true;
        }
    }

    return false;
}

static void sl_cli_openapi_emit_capabilities(FILE* out, const SlCliRoute* route)
{
    size_t index = 0U;

    sl_cli_write_cstr(out, "[");
    for (index = 0U; index < route->effect_count; index += 1U) {
        sl_cli_write_cstr(out, index == 0U ? "" : ", ");
        sl_cli_write_cstr(out, "{ \"provider\": ");
        sl_cli_json_escape(out, route->effect_providers[index]);
        sl_cli_write_cstr(out, ", \"providerKind\": ");
        sl_cli_json_escape(out, route->effect_provider_kinds[index]);
        sl_cli_write_cstr(out, ", \"access\": ");
        sl_cli_json_escape(out, route->effect_accesses[index]);
        sl_cli_write_cstr(out, ", \"operation\": ");
        sl_cli_json_escape(out, route->effect_operations[index]);
        sl_cli_write_cstr(out, " }");
    }
    sl_cli_write_cstr(out, "]");
}

static void sl_cli_openapi_emit_request_body(FILE* out, const SlCliMetadata* metadata,
                                             const SlCliRoute* route)
{
    size_t index = 0U;

    for (index = 0U; index < route->binding_count; index += 1U) {
        if (!sl_cli_span_equal_cstr(route->binding_kinds[index], "body.json")) {
            continue;
        }
        sl_cli_write_cstr(out, ",\n        \"requestBody\": {\n"
                               "          \"required\": true,\n"
                               "          \"content\": { \"application/json\": { \"schema\": ");
        if (sl_cli_openapi_find_schema(metadata, route->binding_schemas[index]) != NULL) {
            sl_cli_write_cstr(out, "{ \"$ref\": \"#/components/schemas/");
            sl_cli_write_span(out, route->binding_schemas[index]);
            sl_cli_write_cstr(out, "\" }");
        }
        else {
            sl_cli_write_cstr(out, "{ \"x-slop-partial\": \"request body schema unknown\" }");
        }
        sl_cli_write_cstr(out, " } }\n        }");
        return;
    }
}

static void sl_cli_openapi_emit_responses(FILE* out, const SlCliRoute* route)
{
    uint64_t status = route->response_status == 0U ? 200U : route->response_status;

    sl_cli_write_cstr(out, ",\n        \"responses\": {\n          \"");
    sl_cli_write_u64(out, status);
    sl_cli_write_cstr(out, "\": { \"description\": ");
    if (sl_cli_span_empty(route->response_kind)) {
        sl_cli_json_escape(out, sl_cli_span_cstr("response metadata unknown"));
        sl_cli_write_cstr(out, ", \"x-slop-partial\": \"response metadata unknown\" }");
    }
    else {
        sl_cli_json_escape(out, route->response_helper);
        if (sl_cli_span_equal_cstr(route->response_kind, "json")) {
            sl_cli_write_cstr(out,
                              ", \"content\": { \"application/json\": { \"schema\": { "
                              "\"x-slop-partial\": \"response schema shape not declared\" } } }");
        }
        sl_cli_write_cstr(out, " }");
    }
    if (sl_cli_openapi_route_has_validation(route)) {
        sl_cli_write_cstr(out, ",\n"
                               "          \"400\": { \"$ref\": "
                               "\"#/components/responses/SlopValidationProblem\" }\n");
    }
    else {
        sl_cli_write_cstr(out, "\n");
    }
    sl_cli_write_cstr(out, "        }");
}

static void sl_cli_openapi_emit_candidates(FILE* out, const SlCliMetadata* metadata,
                                           const SlCliRoute* route)
{
    bool first = true;

    sl_cli_write_cstr(out, "[");
    if (sl_cli_span_equal_cstr(route->response_kind, "json")) {
        sl_cli_write_cstr(out, "\"native-json-serialization\"");
        first = false;
    }
    for (size_t index = 0U; index < route->binding_count; index += 1U) {
        if (sl_cli_span_equal_cstr(route->binding_kinds[index], "body.json") &&
            sl_cli_openapi_find_schema(metadata, route->binding_schemas[index]) != NULL)
        {
            sl_cli_write_cstr(out, first ? "" : ", ");
            sl_cli_write_cstr(out, "\"native-body-validation\"");
            first = false;
        }
    }
    if (route->effect_count > 0U) {
        sl_cli_write_cstr(out, first ? "" : ", ");
        sl_cli_write_cstr(out, "\"provider-aware-route\"");
        first = false;
    }
    if (sl_cli_span_equal_cstr(route->completeness, "complete")) {
        sl_cli_write_cstr(out, first ? "" : ", ");
        sl_cli_write_cstr(out, "\"static-route-dispatch\"");
    }
    sl_cli_write_cstr(out, "]");
}

static void sl_cli_openapi_emit_operation(FILE* out, const SlCliMetadata* metadata,
                                          const SlCliRoute* route)
{
    bool first = true;

    sl_cli_json_escape_lower(out, route->method);
    sl_cli_write_cstr(out, ": {");
    if (!sl_cli_span_empty(route->name)) {
        sl_cli_openapi_emit_field_prefix(out, &first);
        sl_cli_write_cstr(out, "\"operationId\": ");
        sl_cli_json_escape(out, route->name);
    }
    if (!sl_cli_span_empty(route->source_path)) {
        sl_cli_openapi_emit_field_prefix(out, &first);
        sl_cli_write_cstr(out, "\"x-slop-source\": { \"path\": ");
        sl_cli_json_escape(out, route->source_path);
        sl_cli_write_cstr(out, ", \"line\": ");
        sl_cli_write_u64(out, route->source_line);
        sl_cli_write_cstr(out, ", \"column\": ");
        sl_cli_write_u64(out, route->source_column);
        sl_cli_write_cstr(out, " }");
    }
    sl_cli_openapi_emit_field_prefix(out, &first);
    sl_cli_write_cstr(out, "\"x-slop-completeness\": ");
    sl_cli_json_escape(out, route->completeness);
    sl_cli_write_cstr(out, ",\n        \"x-slop-capabilities\": ");
    sl_cli_openapi_emit_capabilities(out, route);
    sl_cli_write_cstr(out, ",\n        \"x-slop-optimization-candidates\": ");
    sl_cli_openapi_emit_candidates(out, metadata, route);
    sl_cli_write_cstr(out, ",\n        \"parameters\": ");
    sl_cli_openapi_parameters(out, route);
    sl_cli_openapi_emit_request_body(out, metadata, route);
    sl_cli_openapi_emit_responses(out, route);
    sl_cli_write_cstr(out, "\n      }");
}

static void sl_cli_openapi_emit_path(FILE* out, const SlCliMetadata* metadata,
                                     const SlCliSpan openapi_paths[SL_CLI_MAX_ROUTES],
                                     size_t path_index)
{
    size_t operation = 0U;
    bool first_method = true;

    sl_cli_write_cstr(out, "\n    ");
    sl_cli_json_escape(out, openapi_paths[path_index]);
    sl_cli_write_cstr(out, ": {");
    for (operation = 0U; operation < metadata->route_count; operation += 1U) {
        if (!sl_cli_span_equal(openapi_paths[operation], openapi_paths[path_index])) {
            continue;
        }
        if (!first_method) {
            sl_cli_write_cstr(out, ",");
        }
        first_method = false;
        sl_cli_write_cstr(out, "\n      ");
        sl_cli_openapi_emit_operation(out, metadata, &metadata->routes[operation]);
    }
    sl_cli_write_cstr(out, "\n    }");
}

static void sl_cli_openapi_emit_property_schema(FILE* out, const SlCliSchemaProperty* property)
{
    sl_cli_write_cstr(out, "{ \"type\": ");
    sl_cli_json_escape(out, sl_cli_openapi_type_for_kind(property->kind));
    if (!sl_cli_span_empty(property->format)) {
        sl_cli_write_cstr(out, ", \"format\": ");
        sl_cli_json_escape(out, property->format);
    }
    if (sl_cli_span_equal_cstr(property->kind, "array")) {
        if (sl_cli_span_empty(property->item_kind)) {
            sl_cli_write_cstr(out, ", \"items\": { \"x-slop-partial\": "
                                   "\"array item schema unknown\" }");
        }
        else {
            sl_cli_write_cstr(out, ", \"items\": { \"type\": ");
            sl_cli_json_escape(out, sl_cli_openapi_type_for_kind(property->item_kind));
            sl_cli_write_cstr(out, " }");
        }
    }
    sl_cli_write_cstr(out, " }");
}

static void sl_cli_openapi_emit_schema(FILE* out, const SlCliSchema* schema, bool comma)
{
    size_t index = 0U;
    bool first_required = true;
    bool is_object =
        sl_cli_span_empty(schema->kind) || sl_cli_span_equal_cstr(schema->kind, "object");

    sl_cli_write_cstr(out, comma ? ",\n      " : "\n      ");
    sl_cli_json_escape(out, schema->name);
    sl_cli_write_cstr(out, ": {\n        \"type\": ");
    sl_cli_json_escape(out, is_object ? sl_cli_span_cstr("object")
                                      : sl_cli_openapi_type_for_kind(schema->kind));
    if (sl_cli_span_equal_cstr(schema->kind, "array")) {
        if (sl_cli_span_empty(schema->item_kind)) {
            sl_cli_write_cstr(out, ",\n        \"items\": { \"x-slop-partial\": "
                                   "\"array item schema unknown\" }");
        }
        else {
            sl_cli_write_cstr(out, ",\n        \"items\": { \"type\": ");
            sl_cli_json_escape(out, sl_cli_openapi_type_for_kind(schema->item_kind));
            sl_cli_write_cstr(out, " }");
        }
    }
    if (!sl_cli_span_empty(schema->source_path)) {
        sl_cli_write_cstr(out, ",\n        \"x-slop-source\": { \"path\": ");
        sl_cli_json_escape(out, schema->source_path);
        sl_cli_write_cstr(out, ", \"line\": ");
        sl_cli_write_u64(out, schema->source_line);
        sl_cli_write_cstr(out, ", \"column\": ");
        sl_cli_write_u64(out, schema->source_column);
        sl_cli_write_cstr(out, " }");
    }
    if (!is_object) {
        sl_cli_write_cstr(out, "\n      }");
        return;
    }
    sl_cli_write_cstr(out, ",\n        \"properties\": {");
    for (index = 0U; index < schema->property_count; index += 1U) {
        sl_cli_write_cstr(out, index == 0U ? "\n          " : ",\n          ");
        sl_cli_json_escape(out, schema->properties[index].name);
        sl_cli_write_cstr(out, ": ");
        sl_cli_openapi_emit_property_schema(out, &schema->properties[index]);
    }
    if (schema->property_count > 0U) {
        sl_cli_write_cstr(out, "\n        ");
    }
    sl_cli_write_cstr(out, "}");
    for (index = 0U; index < schema->property_count; index += 1U) {
        if (schema->properties[index].optional) {
            continue;
        }
        if (first_required) {
            sl_cli_write_cstr(out, ",\n        \"required\": [");
        }
        else {
            sl_cli_write_cstr(out, ", ");
        }
        sl_cli_json_escape(out, schema->properties[index].name);
        first_required = false;
    }
    if (!first_required) {
        sl_cli_write_cstr(out, "]");
    }
    sl_cli_write_cstr(out, "\n      }");
}

static void sl_cli_openapi_emit_components(FILE* out, const SlCliMetadata* metadata)
{
    size_t index = 0U;

    sl_cli_write_cstr(out, ",\n  \"components\": {\n"
                           "    \"responses\": {\n"
                           "      \"SlopValidationProblem\": {\n"
                           "        \"description\": \"Slop validation problem response\",\n"
                           "        \"content\": { \"application/problem+json\": { \"schema\": { "
                           "\"$ref\": \"#/components/schemas/SlopValidationProblem\" } } }\n"
                           "      }\n"
                           "    },\n"
                           "    \"schemas\": {\n"
                           "      \"SlopValidationProblem\": {\n"
                           "        \"type\": \"object\",\n"
                           "        \"properties\": {\n"
                           "          \"status\": { \"type\": \"integer\" },\n"
                           "          \"title\": { \"type\": \"string\" },\n"
                           "          \"detail\": { \"type\": \"string\" }\n"
                           "        }\n"
                           "      }");
    for (index = 0U; index < metadata->schema_count; index += 1U) {
        sl_cli_openapi_emit_schema(out, &metadata->schemas[index], true);
    }
    sl_cli_write_cstr(out, "\n    }\n  }");
}

static void sl_cli_openapi_emit_document(FILE* out, const SlCliMetadata* metadata,
                                         const SlCliSpan openapi_paths[SL_CLI_MAX_ROUTES])
{
    size_t index = 0U;
    bool first_path = true;

    sl_cli_write_cstr(out, "{\n"
                           "  \"openapi\": \"3.0.3\",\n"
                           "  \"info\": {\n"
                           "    \"title\": \"Sloppy API\",\n"
                           "    \"version\": \"0.0.0\"\n"
                           "  },\n"
                           "  \"x-slop-openapi-policy\": {\n"
                           "    \"status\": \"plan-supported-subset\",\n"
                           "    \"unknownMetadata\": \"explicit-partial-markers\",\n"
                           "    \"optimizations\": \"reported-only\"\n"
                           "  },\n"
                           "  \"paths\": {");
    for (index = 0U; index < metadata->route_count; index += 1U) {
        if (sl_cli_openapi_path_seen(openapi_paths, index)) {
            continue;
        }
        if (!first_path) {
            sl_cli_write_cstr(out, ",");
        }
        first_path = false;
        sl_cli_openapi_emit_path(out, metadata, openapi_paths, index);
    }
    sl_cli_write_cstr(out, "\n  }");
    sl_cli_openapi_emit_components(out, metadata);
    sl_cli_write_cstr(out, "\n}\n");
}

static int sl_cli_command_openapi(const SlCliOptions* options)
{
    unsigned char json_storage[SL_CLI_FILE_MAX_BYTES];
    unsigned char plan_arena_storage[SL_CLI_ARENA_BYTES];
    SlArena plan_arena;
    yyjson_doc* doc = NULL;
    SlCliMetadata metadata = {0};
    char path_buffers[SL_CLI_MAX_ROUTES][512];
    SlCliSpan openapi_paths[SL_CLI_MAX_ROUTES] = {0};
    FILE* out = stdout;

    if (options->plan_path == NULL) {
        sl_cli_write_cstr(stderr, "sloppy openapi: --plan <path> is required\n");
        return 1;
    }
    if (!sl_status_is_ok(
            sl_arena_init(&plan_arena, plan_arena_storage, sizeof(plan_arena_storage))))
    {
        sl_cli_write_cstr(stderr, "sloppy openapi: failed to initialize plan validation arena\n");
        return 1;
    }
    if (options->output_path != NULL) {
#ifdef _MSC_VER
        if (fopen_s(&out, options->output_path, "wb") != 0) {
            out = NULL;
        }
#else
        out = fopen(options->output_path, "wb");
#endif
        if (out == NULL) {
            sl_cli_write_error_with_value(
                "sloppy openapi: failed to open output path: ", options->output_path, "\n");
            return 1;
        }
    }
    if (sl_cli_load_metadata(options->plan_path, json_storage, &plan_arena, true, &doc,
                             &metadata) != 0)
    {
        if (doc != NULL) {
            yyjson_doc_free(doc);
        }
        if (out != stdout) {
            (void)fclose(out);
        }
        return 1;
    }
    sl_cli_sort_routes(&metadata);
    if (sl_cli_openapi_prepare_paths(&metadata, path_buffers, openapi_paths) != 0) {
        yyjson_doc_free(doc);
        if (out != stdout) {
            (void)fclose(out);
        }
        return 1;
    }
    sl_cli_openapi_emit_document(out, &metadata, openapi_paths);

    yyjson_doc_free(doc);
    if (out != stdout) {
        (void)fclose(out);
    }
    return 0;
}

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
    if (strcmp(options.command, "capabilities") == 0) {
        return sl_cli_command_capabilities(&options);
    }
    if (strcmp(options.command, "run") == 0) {
        return sl_cli_command_run(&options);
    }
    if (strcmp(options.command, "doctor") == 0) {
        return sl_cli_command_doctor(&options);
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
