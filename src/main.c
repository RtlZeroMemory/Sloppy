/*
 * Sloppy CLI.
 *
 * EPIC-19 adds metadata-only introspection commands over plan-compatible JSON files.
 * EPIC-22 adds the dev-only `sloppy run` artifact path. The run path is intentionally
 * narrow: it loads EPIC-21 artifacts, requires V8, dispatches GET routes, writes a tiny
 * response, and avoids production HTTP, package-manager, Node-compatibility, middleware,
 * body parsing, and hot-reload behavior.
 */
#include "sloppy/arena.h"
#include "sloppy/compiler.h"
#include "sloppy/data_postgres.h"
#include "sloppy/data_sqlserver.h"
#include "sloppy/diagnostics.h"
#include "sloppy/engine.h"
#include "sloppy/http.h"
#include "sloppy/http_dispatch.h"
#include "sloppy/plan.h"
#include "sloppy/platform.h"
#include "sloppy/route.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <uv.h>
#include <yyjson.h>

#define SL_CLI_MAX_ROUTES 128U
#define SL_CLI_MAX_HANDLERS 128U
#define SL_CLI_MAX_MODULES 64U
#define SL_CLI_MAX_DEPS 16U
#define SL_CLI_MAX_PROVIDERS 32U
#define SL_CLI_MAX_DOCTOR_CHECKS 32U
#define SL_CLI_FILE_MAX_BYTES 65536U
#define SL_CLI_ARENA_BYTES 65536U
#define SL_RUN_FILE_MAX_BYTES 65536U
#define SL_RUN_ARENA_BYTES 65536U
#define SL_RUN_MAX_CLIENTS 16U
#define SL_RUN_REQUEST_MAX_BYTES 8192U
#define SL_RUN_RESPONSE_MAX_BYTES 16384U
#define SL_RUN_DEFAULT_HOST "127.0.0.1"
#define SL_RUN_DEFAULT_PORT 5173U

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
    SlCliSpan service;
} SlCliProvider;

typedef struct SlCliDoctorCheck
{
    SlCliSpan id;
    SlCliSpan status;
    SlCliSpan message;
} SlCliDoctorCheck;

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
    SlCliDoctorCheck doctor_checks[SL_CLI_MAX_DOCTOR_CHECKS];
    size_t doctor_check_count;
} SlCliMetadata;

typedef struct SlCliOptions
{
    const char* command;
    const char* plan_path;
    const char* output_path;
    const char* input_path;
    const char* artifacts_path;
    const char* host;
    const char* once_method;
    const char* once_target;
    uint16_t port;
    SlCliFormat format;
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
    (void)printf("  sloppy run <artifact-dir> [--host 127.0.0.1] [--port 5173]\n");
    (void)printf("  sloppy run --artifacts <dir> [--once METHOD TARGET]\n");
    (void)printf("  sloppy routes --plan <path> [--format text|json]\n");
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
    if (strcmp(command, "run") == 0) {
        (void)printf("Usage: sloppy run <artifact-dir> [--host 127.0.0.1] [--port 5173]\n");
        (void)printf("       sloppy run --artifacts <dir> [--once METHOD TARGET]\n");
        (void)printf("\n");
        (void)printf("Dev-only MVP. Requires a V8-enabled build and .sloppy-style artifacts.\n");
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

static int sl_cli_parse_run_option(int argc, char** argv, int* index, SlCliOptions* out)
{
    if (argc <= 0 || argv == NULL || index == NULL || out == NULL ||
        strcmp(out->command, "run") != 0)
    {
        return 0;
    }

    if (strcmp(argv[*index], "--artifacts") == 0) {
        if (*index + 1 >= argc) {
            sl_cli_write_cstr(stderr, "sloppy run: --artifacts requires a directory\n");
            return -1;
        }
        out->artifacts_path = argv[*index + 1];
        *index += 2;
        return 1;
    }

    if (strcmp(argv[*index], "--host") == 0) {
        if (*index + 1 >= argc) {
            sl_cli_write_cstr(stderr, "sloppy run: --host requires an IPv4 address\n");
            return -1;
        }
        out->host = argv[*index + 1];
        *index += 2;
        return 1;
    }

    if (strcmp(argv[*index], "--port") == 0) {
        if (*index + 1 >= argc || !sl_cli_parse_port(argv[*index + 1], &out->port)) {
            sl_cli_write_cstr(stderr, "sloppy run: --port requires a value from 1 to 65535\n");
            return -1;
        }
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

    if (argv[*index][0] != '-') {
        if (out->input_path != NULL) {
            sl_cli_write_cstr(stderr, "sloppy run: expected only one input or artifact path\n");
            return -1;
        }
        out->input_path = argv[*index];
        *index += 1;
        return 1;
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

static int sl_cli_read_file(const char* path, unsigned char* buffer, size_t capacity, SlBytes* out)
{
    FILE* file = NULL;
    long size = 0L;
    size_t bytes_read = 0U;

    if (path == NULL || buffer == NULL || out == NULL) {
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
        sl_cli_write_error_with_value("sloppy: metadata path not found: ", path, "\n");
        return 1;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        (void)fclose(file);
        return 1;
    }

    size = ftell(file);
    if (size <= 0L || (size_t)size > capacity) {
        (void)fclose(file);
        sl_cli_write_error_with_value("sloppy: metadata file is empty or too large: ", path, "\n");
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

static bool sl_cli_write_span(FILE* file, SlCliSpan span)
{
    return span.length == 0U || fwrite(span.ptr, 1U, span.length, file) == span.length;
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

static bool sl_cli_secret_key_at(SlCliSpan text, size_t index)
{
    static const char* keys[] = {"password=", "pwd=", "access token=", "token=", "secret="};
    size_t key_index = 0U;

    for (key_index = 0U; key_index < sizeof(keys) / sizeof(keys[0]); key_index += 1U) {
        const char* key = keys[key_index];
        size_t length = strlen(key);
        size_t offset = 0U;
        bool equal = true;

        if (index + length > text.length) {
            continue;
        }
        for (offset = 0U; offset < length; offset += 1U) {
            char actual = text.ptr[index + offset];
            char expected = key[offset];
            if (actual >= 'A' && actual <= 'Z') {
                actual = (char)(actual - 'A' + 'a');
            }
            if (actual != expected) {
                equal = false;
                break;
            }
        }
        if (equal) {
            return true;
        }
    }

    return false;
}

static void sl_cli_write_redacted(FILE* file, SlCliSpan text)
{
    size_t index = 0U;

    while (index < text.length) {
        if (sl_cli_secret_key_at(text, index)) {
            while (index < text.length && text.ptr[index] != '=') {
                (void)fputc(text.ptr[index], file);
                index += 1U;
            }
            if (index < text.length) {
                (void)fputc('=', file);
                index += 1U;
            }
            (void)fputs("<redacted>", file);
            while (index < text.length && text.ptr[index] != ';' && text.ptr[index] != ' ' &&
                   text.ptr[index] != '&' && text.ptr[index] != '\n' && text.ptr[index] != '\r')
            {
                index += 1U;
            }
        }
        else {
            (void)fputc(text.ptr[index], file);
            index += 1U;
        }
    }
}

static SlCliSpan sl_cli_redact_to_buffer(char* buffer, size_t capacity, SlCliSpan text)
{
    size_t index = 0U;
    size_t out = 0U;

    while (index < text.length && out + 1U < capacity) {
        if (sl_cli_secret_key_at(text, index)) {
            while (index < text.length && text.ptr[index] != '=' && out + 1U < capacity) {
                buffer[out] = text.ptr[index];
                out += 1U;
                index += 1U;
            }
            if (index < text.length && out + 1U < capacity) {
                buffer[out] = '=';
                out += 1U;
                index += 1U;
            }
            if (out + sizeof("<redacted>") < capacity) {
                size_t redacted_index = 0U;
                for (redacted_index = 0U; redacted_index < sizeof("<redacted>") - 1U;
                     redacted_index += 1U)
                {
                    buffer[out] = "<redacted>"[redacted_index];
                    out += 1U;
                }
            }
            while (index < text.length && text.ptr[index] != ';' && text.ptr[index] != ' ' &&
                   text.ptr[index] != '&' && text.ptr[index] != '\n' && text.ptr[index] != '\r')
            {
                index += 1U;
            }
        }
        else {
            buffer[out] = text.ptr[index];
            out += 1U;
            index += 1U;
        }
    }

    buffer[out] = '\0';
    return (SlCliSpan){buffer, out};
}

static void sl_cli_json_redacted(FILE* file, SlCliSpan text)
{
    char buffer[4096];
    SlCliSpan redacted = sl_cli_redact_to_buffer(buffer, sizeof(buffer), text);

    sl_cli_json_escape(file, redacted);
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
        route.handler_id = sl_cli_json_handler_id(value);

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
        provider.service = sl_cli_json_span(value, "service");
        metadata->providers[metadata->provider_count] = provider;
        metadata->provider_count += 1U;
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

static int sl_cli_load_metadata(const char* path, unsigned char* json_storage, yyjson_doc** out_doc,
                                SlCliMetadata* out_metadata)
{
    SlBytes json = {0};
    yyjson_read_err error = {0};
    yyjson_val* root = NULL;

    if (sl_cli_read_file(path, json_storage, SL_CLI_FILE_MAX_BYTES, &json) != 0) {
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
    if (sl_cli_parse_handlers(root, out_metadata) != 0 ||
        sl_cli_parse_routes(root, out_metadata) != 0 ||
        sl_cli_parse_modules(root, out_metadata) != 0 ||
        sl_cli_parse_providers(root, out_metadata) != 0 ||
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

typedef struct SlRunRoute
{
    SlRoutePattern pattern;
    SlHttpRouteBinding binding;
} SlRunRoute;

typedef struct SlRunApp
{
    unsigned char plan_json_storage[SL_RUN_FILE_MAX_BYTES];
    unsigned char metadata_json_storage[SL_RUN_FILE_MAX_BYTES];
    unsigned char app_js_storage[SL_RUN_FILE_MAX_BYTES];
    unsigned char plan_arena_storage[SL_RUN_ARENA_BYTES];
    unsigned char route_arena_storage[SL_RUN_ARENA_BYTES];
    unsigned char engine_arena_storage[SL_RUN_ARENA_BYTES];
    SlArena plan_arena;
    SlArena route_arena;
    SlArena engine_arena;
    SlPlan plan;
    SlEngine* engine;
    SlRunRoute routes[SL_CLI_MAX_ROUTES];
    SlHttpRouteBinding bindings[SL_CLI_MAX_ROUTES];
    SlHttpDispatchTable dispatch_table;
} SlRunApp;

typedef struct SlRunServer SlRunServer;

typedef struct SlRunClient
{
    uv_tcp_t handle;
    SlRunServer* server;
    char request[SL_RUN_REQUEST_MAX_BYTES];
    size_t request_length;
    char response[SL_RUN_RESPONSE_MAX_BYTES];
    uv_write_t write_request;
    bool active;
} SlRunClient;

struct SlRunServer
{
    SlRunApp* app;
    uv_loop_t loop;
    uv_tcp_t listener;
    SlRunClient clients[SL_RUN_MAX_CLIENTS];
};

static bool sl_run_span_ends_with(SlCliSpan span, const char* suffix)
{
    size_t suffix_length = strlen(suffix);

    if (span.length < suffix_length) {
        return false;
    }

    return memcmp(span.ptr + span.length - suffix_length, suffix, suffix_length) == 0;
}

static bool sl_run_join_path(char* buffer, size_t capacity, const char* dir, SlCliSpan leaf)
{
    size_t dir_length = 0U;
    size_t out = 0U;
    size_t index = 0U;
    bool needs_separator = true;

    if (buffer == NULL || capacity == 0U || dir == NULL || leaf.ptr == NULL || leaf.length == 0U) {
        return false;
    }

    if (leaf.ptr[0] == '/' || leaf.ptr[0] == '\\') {
        return false;
    }
    for (index = 0U; index < leaf.length; index += 1U) {
        if (leaf.ptr[index] == ':') {
            return false;
        }
    }

    dir_length = strlen(dir);
    if (dir_length == 0U) {
        return false;
    }

    needs_separator = dir[dir_length - 1U] != '/' && dir[dir_length - 1U] != '\\';
    if (dir_length + (needs_separator ? 1U : 0U) + leaf.length + 1U > capacity) {
        return false;
    }

    for (index = 0U; index < dir_length; index += 1U) {
        buffer[out] = dir[index];
        out += 1U;
    }
    if (needs_separator) {
        buffer[out] = '/';
        out += 1U;
    }
    for (index = 0U; index < leaf.length; index += 1U) {
        if (leaf.ptr[index] == '\\') {
            buffer[out] = '/';
        }
        else {
            buffer[out] = leaf.ptr[index];
        }
        out += 1U;
    }
    buffer[out] = '\0';
    return true;
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
    FILE* file = NULL;
    long size = 0L;
    size_t bytes_read = 0U;

    if (path == NULL || buffer == NULL || out == NULL) {
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
        sl_cli_write_error_with_value("sloppy run: artifact path not found: ", path, "\n");
        return 1;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        (void)fclose(file);
        return 1;
    }

    size = ftell(file);
    if (size <= 0L || (size_t)size > capacity) {
        (void)fclose(file);
        sl_cli_write_error_with_value("sloppy run: artifact file is empty or too large: ", path,
                                      "\n");
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

static void sl_run_print_diag(const char* prefix, const SlDiag* diag)
{
    sl_cli_write_cstr(stderr, prefix);
    if (diag != NULL && !sl_str_is_empty(diag->message)) {
        sl_cli_write_span(stderr, (SlCliSpan){diag->message.ptr, diag->message.length});
        sl_cli_write_cstr(stderr, "\n");
        return;
    }
    sl_cli_write_cstr(stderr, "operation failed\n");
}

static SlEngineOptions sl_run_v8_options(void)
{
    SlEngineOptions options = {0};

    options.kind = SL_ENGINE_KIND_V8;
    options.runtime_name = sl_str_from_cstr("sloppy-run-mvp");
    options.runtime_version = sl_str_from_cstr(SL_VERSION_STRING);
    options.target_platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    options.target_engine = sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8);
    return options;
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
        sl_run_print_diag("sloppy run: malformed app.plan.json: ", &diag);
        return 1;
    }

    if (!sl_str_equal(app->plan.target.engine, sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8))) {
        sl_cli_write_cstr(stderr, "sloppy run: app.plan.json target.engine must be v8\n");
        return 1;
    }

    return 0;
}

static int sl_run_prepare_routes(SlRunApp* app, const SlCliMetadata* metadata)
{
    size_t index = 0U;
    size_t count = 0U;

    for (index = 0U; index < metadata->route_count; index += 1U) {
        const SlCliRoute* route = &metadata->routes[index];
        const SlPlanHandler* handler = NULL;
        SlStatus status;

        if (!sl_cli_span_equal_cstr(route->method, "GET")) {
            continue;
        }

        status = sl_plan_find_handler_by_id(&app->plan, route->handler_id, &handler);
        if (!sl_status_is_ok(status)) {
            sl_cli_write_cstr(stderr,
                              "sloppy run: route metadata references a missing plan handler\n");
            return 1;
        }

        status = sl_route_pattern_parse(&app->route_arena, sl_cli_span_str(route->pattern),
                                        &app->routes[count].pattern, NULL);
        if (!sl_status_is_ok(status)) {
            sl_cli_write_cstr(stderr, "sloppy run: route metadata contains an invalid pattern: ");
            sl_cli_write_span(stderr, route->pattern);
            sl_cli_write_cstr(stderr, "\n");
            return 1;
        }

        app->routes[count].binding.method = SL_HTTP_METHOD_GET;
        app->routes[count].binding.pattern = &app->routes[count].pattern;
        app->routes[count].binding.handler_id = route->handler_id;
        app->bindings[count] = app->routes[count].binding;
        count += 1U;
    }

    if (count == 0U) {
        sl_cli_write_cstr(stderr,
                          "sloppy run: app.plan.json does not contain GET route metadata\n");
        return 1;
    }

    app->dispatch_table.routes = app->bindings;
    app->dispatch_table.route_count = count;
    return 0;
}

static int sl_run_load_routes(SlRunApp* app, const char* plan_path)
{
    yyjson_doc* doc = NULL;
    SlCliMetadata metadata = {0};

    if (sl_cli_load_metadata(plan_path, app->metadata_json_storage, &doc, &metadata) != 0) {
        if (doc != NULL) {
            yyjson_doc_free(doc);
        }
        return 1;
    }

    if (sl_run_prepare_routes(app, &metadata) != 0) {
        yyjson_doc_free(doc);
        return 1;
    }

    yyjson_doc_free(doc);
    return 0;
}

static int sl_run_load_engine(SlRunApp* app, const char* app_js_path)
{
    SlBytes js = {0};
    SlStr source = {0};
    SlEngineOptions options = sl_run_v8_options();
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

    if (sl_run_read_file(app_js_path, app->app_js_storage, sizeof(app->app_js_storage), &js) != 0) {
        return 1;
    }

    source = sl_str_from_parts((const char*)js.ptr, js.length);
    status = sl_engine_eval_source(app->engine, sl_str_from_cstr(app_js_path), source, &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag("sloppy run: failed to evaluate app.js: ", &diag);
        return 1;
    }

    return 0;
}

static int sl_run_load_app(const char* artifacts_path, SlRunApp* app)
{
    char plan_path[1024];
    char app_js_path[1024];

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

    if (!sl_run_artifact_file_path(plan_path, sizeof(plan_path), artifacts_path, "app.plan.json")) {
        sl_cli_write_cstr(stderr, "sloppy run: invalid artifacts directory\n");
        return 1;
    }

    if (sl_run_load_plan(app, plan_path) != 0 || sl_run_load_routes(app, plan_path) != 0) {
        return 1;
    }

    if (!sl_run_join_path(app_js_path, sizeof(app_js_path), artifacts_path,
                          sl_run_str_span(app->plan.bundle.path)))
    {
        sl_cli_write_cstr(stderr, "sloppy run: invalid bundle path in app.plan.json\n");
        return 1;
    }

    return sl_run_load_engine(app, app_js_path);
}

static bool sl_run_text_looks_json(SlStr text)
{
    size_t index = 0U;

    while (index < text.length && (text.ptr[index] == ' ' || text.ptr[index] == '\t' ||
                                   text.ptr[index] == '\r' || text.ptr[index] == '\n'))
    {
        index += 1U;
    }

    if (index >= text.length) {
        return false;
    }

    return text.ptr[index] == '{' || text.ptr[index] == '[';
}

static const char* sl_run_status_reason(unsigned status)
{
    switch (status) {
    case 200U:
        return "OK";
    case 404U:
        return "Not Found";
    case 405U:
        return "Method Not Allowed";
    case 500U:
        return "Internal Server Error";
    default:
        return "OK";
    }
}

static bool sl_run_append_byte(char* buffer, size_t capacity, size_t* length, char byte)
{
    if (buffer == NULL || length == NULL || *length + 1U >= capacity) {
        return false;
    }

    buffer[*length] = byte;
    *length += 1U;
    buffer[*length] = '\0';
    return true;
}

static bool sl_run_append_cstr(char* buffer, size_t capacity, size_t* length, const char* text)
{
    size_t index = 0U;

    if (text == NULL) {
        return false;
    }

    while (text[index] != '\0') {
        if (!sl_run_append_byte(buffer, capacity, length, text[index])) {
            return false;
        }
        index += 1U;
    }

    return true;
}

static bool sl_run_append_uint(char* buffer, size_t capacity, size_t* length, unsigned value)
{
    char digits[10];
    size_t count = 0U;

    do {
        digits[count] = (char)('0' + (value % 10U));
        value /= 10U;
        count += 1U;
    } while (value != 0U && count < sizeof(digits));

    while (count > 0U) {
        count -= 1U;
        if (!sl_run_append_byte(buffer, capacity, length, digits[count])) {
            return false;
        }
    }

    return true;
}

static bool sl_run_append_size(char* buffer, size_t capacity, size_t* length, size_t value)
{
    char digits[32];
    size_t count = 0U;

    do {
        digits[count] = (char)('0' + (value % 10U));
        value /= 10U;
        count += 1U;
    } while (value != 0U && count < sizeof(digits));

    while (count > 0U) {
        count -= 1U;
        if (!sl_run_append_byte(buffer, capacity, length, digits[count])) {
            return false;
        }
    }

    return true;
}

static bool sl_run_append_str(char* buffer, size_t capacity, size_t* length, SlStr text)
{
    size_t index = 0U;

    if (text.ptr == NULL && text.length != 0U) {
        return false;
    }

    for (index = 0U; index < text.length; index += 1U) {
        if (!sl_run_append_byte(buffer, capacity, length, text.ptr[index])) {
            return false;
        }
    }

    return true;
}

static int sl_run_write_response(char* buffer, size_t capacity, unsigned status,
                                 const char* content_type, SlStr body)
{
    size_t length = 0U;

    if (buffer == NULL || content_type == NULL || (body.ptr == NULL && body.length != 0U)) {
        return -1;
    }

    buffer[0] = '\0';
    if (!sl_run_append_cstr(buffer, capacity, &length, "HTTP/1.1 ") ||
        !sl_run_append_uint(buffer, capacity, &length, status) ||
        !sl_run_append_cstr(buffer, capacity, &length, " ") ||
        !sl_run_append_cstr(buffer, capacity, &length, sl_run_status_reason(status)) ||
        !sl_run_append_cstr(buffer, capacity, &length, "\r\nConnection: close\r\nContent-Type: ") ||
        !sl_run_append_cstr(buffer, capacity, &length, content_type) ||
        !sl_run_append_cstr(buffer, capacity, &length, "\r\nContent-Length: ") ||
        !sl_run_append_size(buffer, capacity, &length, body.length) ||
        !sl_run_append_cstr(buffer, capacity, &length, "\r\n\r\n") ||
        !sl_run_append_str(buffer, capacity, &length, body))
    {
        return -1;
    }

    return length > (size_t)INT32_MAX ? -1 : (int)length;
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

static int sl_run_dispatch_head(SlRunApp* app, const SlHttpRequestHead* request, char* response,
                                size_t response_capacity)
{
    unsigned char dispatch_storage[SL_RUN_ARENA_BYTES];
    SlArena dispatch_arena = {0};
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlStatus status;
    const char* content_type = "text/plain; charset=utf-8";

    if (app == NULL || request == NULL || response == NULL ||
        !sl_status_is_ok(
            sl_arena_init(&dispatch_arena, dispatch_storage, sizeof(dispatch_storage))))
    {
        return -1;
    }

    status = sl_http_dispatch_request_head(&dispatch_arena, app->engine, &app->plan,
                                           &app->dispatch_table, request, &result, &diag);
    if (sl_status_is_ok(status) && result.kind == SL_ENGINE_RESULT_TEXT) {
        if (sl_run_text_looks_json(result.text)) {
            content_type = "application/json; charset=utf-8";
        }
        return sl_run_write_response(response, response_capacity, 200U, content_type, result.text);
    }

    if (sl_status_code(status) == SL_STATUS_UNSUPPORTED) {
        return sl_run_write_response(response, response_capacity, 405U, "text/plain; charset=utf-8",
                                     sl_str_from_cstr("Method Not Allowed\n"));
    }

    if (sl_status_code(status) == SL_STATUS_OUT_OF_RANGE) {
        return sl_run_write_response(response, response_capacity, 404U, "text/plain; charset=utf-8",
                                     sl_str_from_cstr("Not Found\n"));
    }

    (void)diag;
    return sl_run_write_response(response, response_capacity, 500U, "text/plain; charset=utf-8",
                                 sl_str_from_cstr("Sloppy handler failed\n"));
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

static bool sl_run_request_complete(const SlRunClient* client)
{
    size_t index = 0U;

    if (client == NULL || client->request_length < 4U) {
        return false;
    }

    for (index = 3U; index < client->request_length; index += 1U) {
        if (client->request[index - 3U] == '\r' && client->request[index - 2U] == '\n' &&
            client->request[index - 1U] == '\r' && client->request[index] == '\n')
        {
            return true;
        }
    }

    return false;
}

static void sl_run_client_close_cb(uv_handle_t* handle)
{
    SlRunClient* client = (SlRunClient*)handle->data;

    if (client != NULL) {
        client->active = false;
        client->request_length = 0U;
    }
}

static void sl_run_client_write_cb(uv_write_t* request, int status)
{
    SlRunClient* client = (SlRunClient*)request->data;

    (void)status;
    if (client != NULL) {
        uv_close((uv_handle_t*)&client->handle, sl_run_client_close_cb);
    }
}

static void sl_run_client_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
    SlRunClient* client = (SlRunClient*)handle->data;
    size_t remaining = 0U;

    (void)suggested_size;
    if (client == NULL || client->request_length >= sizeof(client->request)) {
        *buf = uv_buf_init(NULL, 0U);
        return;
    }

    remaining = sizeof(client->request) - client->request_length;
    *buf = uv_buf_init(client->request + client->request_length, (unsigned int)remaining);
}

static void sl_run_client_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
    SlRunClient* client = (SlRunClient*)stream->data;
    unsigned char request_arena_storage[SL_RUN_ARENA_BYTES];
    SlArena request_arena = {0};
    SlHttpRequestHead request = {0};
    SlDiag diag = {0};
    int response_length = 0;
    SlStatus status;
    uv_buf_t response_buf;

    (void)buf;
    if (client == NULL) {
        return;
    }

    if (nread < 0) {
        uv_close((uv_handle_t*)&client->handle, sl_run_client_close_cb);
        return;
    }

    client->request_length += (size_t)nread;
    if (!sl_run_request_complete(client)) {
        if (client->request_length >= sizeof(client->request)) {
            response_length = sl_run_write_response(client->response, sizeof(client->response),
                                                    500U, "text/plain; charset=utf-8",
                                                    sl_str_from_cstr("Request head too large\n"));
            response_buf = uv_buf_init(client->response, (unsigned int)response_length);
            client->write_request.data = client;
            (void)uv_write(&client->write_request, stream, &response_buf, 1U,
                           sl_run_client_write_cb);
        }
        return;
    }

    if (!sl_status_is_ok(
            sl_arena_init(&request_arena, request_arena_storage, sizeof(request_arena_storage))))
    {
        return;
    }

    status = sl_http_parse_request_head(
        &request_arena,
        sl_bytes_from_parts((const unsigned char*)client->request, client->request_length), NULL,
        &request, &diag);
    if (sl_status_is_ok(status)) {
        response_length = sl_run_dispatch_head(client->server->app, &request, client->response,
                                               sizeof(client->response));
    }
    else {
        response_length = sl_run_write_response(client->response, sizeof(client->response), 500U,
                                                "text/plain; charset=utf-8",
                                                sl_str_from_cstr("Malformed HTTP request\n"));
    }

    if (response_length < 0) {
        uv_close((uv_handle_t*)&client->handle, sl_run_client_close_cb);
        return;
    }

    response_buf = uv_buf_init(client->response, (unsigned int)response_length);
    client->write_request.data = client;
    (void)uv_write(&client->write_request, stream, &response_buf, 1U, sl_run_client_write_cb);
}

static SlRunClient* sl_run_server_acquire_client(SlRunServer* server)
{
    size_t index = 0U;

    for (index = 0U; index < SL_RUN_MAX_CLIENTS; index += 1U) {
        if (!server->clients[index].active) {
            server->clients[index] = (SlRunClient){0};
            server->clients[index].active = true;
            server->clients[index].server = server;
            return &server->clients[index];
        }
    }

    return NULL;
}

static void sl_run_server_connection_cb(uv_stream_t* listener, int status)
{
    SlRunServer* server = (SlRunServer*)listener->data;
    SlRunClient* client = NULL;

    if (status < 0 || server == NULL) {
        return;
    }

    client = sl_run_server_acquire_client(server);
    if (client == NULL) {
        return;
    }

    if (uv_tcp_init(&server->loop, &client->handle) != 0) {
        client->active = false;
        return;
    }

    client->handle.data = client;
    if (uv_accept(listener, (uv_stream_t*)&client->handle) != 0) {
        uv_close((uv_handle_t*)&client->handle, sl_run_client_close_cb);
        return;
    }

    (void)uv_read_start((uv_stream_t*)&client->handle, sl_run_client_alloc_cb,
                        sl_run_client_read_cb);
}

static int sl_run_server(SlRunApp* app, const char* host, uint16_t port)
{
    SlRunServer server = {0};
    struct sockaddr_in address;
    int rc = 0;

    server.app = app;
    rc = uv_loop_init(&server.loop);
    if (rc != 0) {
        sl_cli_write_cstr(stderr, "sloppy run: failed to initialize dev server loop\n");
        return 1;
    }

    rc = uv_ip4_addr(host, (int)port, &address);
    if (rc != 0) {
        sl_cli_write_cstr(stderr, "sloppy run: --host must be an IPv4 address\n");
        (void)uv_loop_close(&server.loop);
        return 1;
    }

    rc = uv_tcp_init(&server.loop, &server.listener);
    if (rc == 0) {
        server.listener.data = &server;
        rc = uv_tcp_bind(&server.listener, (const struct sockaddr*)&address, 0U);
    }
    if (rc == 0) {
        rc = uv_listen((uv_stream_t*)&server.listener, 16, sl_run_server_connection_cb);
    }

    if (rc != 0) {
        sl_cli_write_cstr(stderr, "sloppy run: failed to listen on requested host/port\n");
        uv_close((uv_handle_t*)&server.listener, NULL);
        (void)uv_run(&server.loop, UV_RUN_DEFAULT);
        (void)uv_loop_close(&server.loop);
        return 1;
    }

    (void)printf("Sloppy dev server listening on http://%s:%u\n", host, (unsigned)port);
    (void)printf("Dev-only MVP: no TLS, no body parsing, no streaming, no middleware.\n");
    rc = uv_run(&server.loop, UV_RUN_DEFAULT);
    (void)uv_loop_close(&server.loop);
    return rc == 0 ? 0 : 1;
}

static int sl_cli_command_run(const SlCliOptions* options)
{
    SlRunApp app;
    const char* artifacts_path = options->artifacts_path;
    int result = 0;

    if (artifacts_path == NULL) {
        artifacts_path = options->input_path;
    }

    if (artifacts_path == NULL) {
        sl_cli_write_cstr(stderr, "sloppy run: expected <artifact-dir> or --artifacts <dir>\n");
        return 1;
    }

    if (options->artifacts_path == NULL && options->input_path != NULL) {
        SlCliSpan input = sl_cli_span_cstr(options->input_path);
        if (sl_run_span_ends_with(input, ".js") || sl_run_span_ends_with(input, ".mjs")) {
            sl_cli_write_cstr(stderr,
                              "sloppy run: source input handoff to sloppyc is deferred; use "
                              "--artifacts <dir>\n");
            return 1;
        }
    }

    if (sl_run_load_app(artifacts_path, &app) != 0) {
        sl_engine_destroy(app.engine);
        return 1;
    }

    if (options->once_method != NULL) {
        result = sl_run_once(&app, options->once_method, options->once_target);
    }
    else {
        result = sl_run_server(&app, options->host, options->port);
    }

    sl_engine_destroy(app.engine);
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

static int sl_cli_command_routes(const SlCliOptions* options)
{
    unsigned char json_storage[SL_CLI_FILE_MAX_BYTES];
    yyjson_doc* doc = NULL;
    SlCliMetadata metadata = {0};
    size_t index = 0U;

    if (options->plan_path == NULL) {
        sl_cli_write_cstr(stderr, "sloppy routes: --plan <path> is required\n");
        return 1;
    }
    if (sl_cli_load_metadata(options->plan_path, json_storage, &doc, &metadata) != 0) {
        if (doc != NULL) {
            yyjson_doc_free(doc);
        }
        return 1;
    }
    sl_cli_sort_routes(&metadata);

    if (options->format == SL_CLI_FORMAT_JSON) {
        (void)printf("{\n  \"routes\": [");
        for (index = 0U; index < metadata.route_count; index += 1U) {
            SlCliRoute* route = &metadata.routes[index];
            (void)printf("%s\n    { \"method\": ", index == 0U ? "" : ",");
            sl_cli_json_escape(stdout, route->method);
            (void)printf(", \"pattern\": ");
            sl_cli_json_escape(stdout, route->pattern);
            (void)printf(", \"handlerId\": %u, \"name\": ", route->handler_id);
            sl_cli_json_escape(stdout, route->name);
            (void)printf(", \"module\": ");
            sl_cli_json_escape(stdout, route->module);
            (void)printf(" }");
        }
        (void)printf("\n  ]\n}\n");
    }
    else {
        (void)printf("METHOD  PATTERN              HANDLER  NAME\n");
        for (index = 0U; index < metadata.route_count; index += 1U) {
            SlCliRoute* route = &metadata.routes[index];
            (void)printf("%-6.*s  %-19.*s  %-7u  %.*s\n", (int)route->method.length,
                         route->method.ptr, (int)route->pattern.length, route->pattern.ptr,
                         route->handler_id, (int)route->name.length, route->name.ptr);
        }
    }

    yyjson_doc_free(doc);
    return 0;
}

static void sl_cli_doctor_emit_text(SlCliSpan id, SlCliSpan status, SlCliSpan message)
{
    (void)printf("[");
    sl_cli_write_span(stdout, status);
    (void)printf("] ");
    sl_cli_write_span(stdout, id);
    if (!sl_cli_span_empty(message)) {
        (void)printf(": ");
        sl_cli_write_redacted(stdout, message);
    }
    (void)printf("\n");
}

static void sl_cli_doctor_emit_json(SlCliSpan id, SlCliSpan status, SlCliSpan message, bool comma)
{
    (void)printf("%s\n    { \"id\": ", comma ? "," : "");
    sl_cli_json_escape(stdout, id);
    (void)printf(", \"status\": ");
    sl_cli_json_escape(stdout, status);
    (void)printf(", \"message\": ");
    sl_cli_json_redacted(stdout, message);
    (void)printf(" }");
}

static int sl_cli_command_doctor(const SlCliOptions* options)
{
    unsigned char json_storage[SL_CLI_FILE_MAX_BYTES];
    yyjson_doc* doc = NULL;
    SlCliMetadata metadata = {0};
    size_t index = 0U;
    bool emitted = false;

    if (options->plan_path != NULL &&
        sl_cli_load_metadata(options->plan_path, json_storage, &doc, &metadata) != 0)
    {
        if (doc != NULL) {
            yyjson_doc_free(doc);
        }
        return 1;
    }

    if (options->format == SL_CLI_FORMAT_JSON) {
        (void)printf("{\n  \"checks\": [");
        sl_cli_doctor_emit_json(sl_cli_span_cstr("bootstrap.assets"), sl_cli_span_cstr("ok"),
                                sl_cli_span_cstr("bootstrap assets found"), false);
        emitted = true;
        if (options->plan_path != NULL) {
            sl_cli_doctor_emit_json(sl_cli_span_cstr("app.plan.parse"), sl_cli_span_cstr("ok"),
                                    sl_cli_span_cstr("app plan metadata parsed"), emitted);
            emitted = true;
        }
        for (index = 0U; index < metadata.doctor_check_count; index += 1U) {
            sl_cli_doctor_emit_json(metadata.doctor_checks[index].id,
                                    metadata.doctor_checks[index].status,
                                    metadata.doctor_checks[index].message, emitted);
            emitted = true;
        }
        (void)printf("\n  ]\n}\n");
    }
    else {
        (void)printf("Sloppy Doctor\n\n");
        sl_cli_doctor_emit_text(sl_cli_span_cstr("bootstrap.assets"), sl_cli_span_cstr("ok"),
                                sl_cli_span_cstr("bootstrap assets found"));
        if (options->plan_path != NULL) {
            sl_cli_doctor_emit_text(sl_cli_span_cstr("app.plan.parse"), sl_cli_span_cstr("ok"),
                                    sl_cli_span_cstr("app plan metadata parsed"));
        }
        for (index = 0U; index < metadata.doctor_check_count; index += 1U) {
            sl_cli_doctor_emit_text(metadata.doctor_checks[index].id,
                                    metadata.doctor_checks[index].status,
                                    metadata.doctor_checks[index].message);
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
                              const char* message, const char* path, size_t* findings)
{
    if (options->format == SL_CLI_FORMAT_JSON) {
        sl_cli_audit_json(sl_cli_span_cstr(severity), code, message, path, *findings > 0U);
    }
    else {
        sl_cli_audit_text(sl_cli_span_cstr(severity), code, message, path);
    }
    *findings += 1U;
}

static void sl_cli_audit_routes(const SlCliOptions* options, const SlCliMetadata* metadata,
                                size_t* findings)
{
    size_t outer = 0U;
    size_t inner = 0U;

    for (outer = 0U; outer < metadata->route_count; outer += 1U) {
        if (sl_cli_find_handler(metadata, metadata->routes[outer].handler_id) == NULL) {
            sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_MISSING_HANDLER",
                              "route references a missing handler id", "routes", findings);
        }
        for (inner = outer + 1U; inner < metadata->route_count; inner += 1U) {
            if (!sl_cli_span_empty(metadata->routes[outer].name) &&
                sl_cli_span_equal(metadata->routes[outer].name, metadata->routes[inner].name))
            {
                sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_DUPLICATE_ROUTE_NAME",
                                  "duplicate route name", "routes", findings);
            }
            if (sl_cli_span_equal(metadata->routes[outer].method, metadata->routes[inner].method) &&
                sl_cli_span_equal(metadata->routes[outer].pattern, metadata->routes[inner].pattern))
            {
                sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_DUPLICATE_ROUTE",
                                  "duplicate route method and pattern", "routes", findings);
            }
        }
    }
}

static void sl_cli_audit_modules(const SlCliOptions* options, const SlCliMetadata* metadata,
                                 size_t* findings)
{
    size_t outer = 0U;
    size_t inner = 0U;

    for (outer = 0U; outer < metadata->module_count; outer += 1U) {
        for (inner = 0U; inner < metadata->modules[outer].dependency_count; inner += 1U) {
            const SlCliModule* dep =
                sl_cli_find_module(metadata, metadata->modules[outer].dependencies[inner]);
            if (dep == NULL) {
                sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_MISSING_MODULE_DEPENDENCY",
                                  "module dependency is missing", "modules", findings);
            }
            else {
                size_t dep_index = 0U;
                for (dep_index = 0U; dep_index < dep->dependency_count; dep_index += 1U) {
                    if (sl_cli_span_equal(dep->dependencies[dep_index],
                                          metadata->modules[outer].name))
                    {
                        sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_MODULE_CYCLE",
                                          "module dependency cycle detected", "modules", findings);
                    }
                }
            }
        }
    }
}

static void sl_cli_audit_providers(const SlCliOptions* options, const SlCliMetadata* metadata,
                                   size_t* findings)
{
    size_t index = 0U;

    for (index = 0U; index < metadata->provider_count; index += 1U) {
        if (sl_cli_span_empty(metadata->providers[index].token) ||
            sl_cli_span_empty(metadata->providers[index].provider) ||
            sl_cli_span_empty(metadata->providers[index].service))
        {
            sl_cli_audit_emit(options, "warn", "SLOPPY_AUDIT_PROVIDER_INCOMPLETE",
                              "provider metadata is missing token, provider, or service",
                              "dataProviders", findings);
        }
    }
}

static int sl_cli_command_audit(const SlCliOptions* options)
{
    unsigned char json_storage[SL_CLI_FILE_MAX_BYTES];
    yyjson_doc* doc = NULL;
    SlCliMetadata metadata = {0};
    size_t findings = 0U;

    if (options->plan_path == NULL) {
        sl_cli_write_cstr(stderr, "sloppy audit: --plan <path> is required\n");
        return 1;
    }
    if (sl_cli_load_metadata(options->plan_path, json_storage, &doc, &metadata) != 0) {
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

    sl_cli_audit_routes(options, &metadata, &findings);
    sl_cli_audit_modules(options, &metadata, &findings);
    sl_cli_audit_providers(options, &metadata, &findings);

    if (findings == 0U && options->format == SL_CLI_FORMAT_TEXT) {
        (void)printf("No findings.\n");
    }
    if (options->format == SL_CLI_FORMAT_JSON) {
        (void)printf("\n  ]\n}\n");
    }

    yyjson_doc_free(doc);
    return 0;
}

static bool sl_cli_openapi_path(char* buffer, size_t capacity, SlCliSpan pattern,
                                SlCliSpan* openapi_path)
{
    size_t index = 0U;
    size_t out = 0U;

    while (index < pattern.length) {
        if (out + 1U >= capacity) {
            return false;
        }
        if (pattern.ptr[index] == '{') {
            buffer[out] = pattern.ptr[index];
            out += 1U;
            index += 1U;
            while (index < pattern.length && pattern.ptr[index] != '}' &&
                   pattern.ptr[index] != ':' && out + 1U < capacity)
            {
                buffer[out] = pattern.ptr[index];
                out += 1U;
                index += 1U;
            }
            if (out + 1U >= capacity) {
                return false;
            }
            while (index < pattern.length && pattern.ptr[index] != '}') {
                index += 1U;
            }
            if (index >= pattern.length) {
                return false;
            }
        }
        buffer[out] = pattern.ptr[index];
        out += 1U;
        index += 1U;
    }

    buffer[out] = '\0';
    *openapi_path = (SlCliSpan){buffer, out};
    return true;
}

static void sl_cli_openapi_parameters(FILE* file, SlCliSpan pattern)
{
    size_t index = 0U;
    bool first = true;

    sl_cli_write_cstr(file, "[");
    while (index < pattern.length) {
        if (pattern.ptr[index] == '{') {
            size_t name_start = index + 1U;
            size_t name_end = name_start;
            size_t type_start = 0U;
            size_t type_end = 0U;
            SlCliSpan type = {0};

            while (name_end < pattern.length && pattern.ptr[name_end] != '}' &&
                   pattern.ptr[name_end] != ':')
            {
                name_end += 1U;
            }
            if (name_end < pattern.length && pattern.ptr[name_end] == ':') {
                type_start = name_end + 1U;
                type_end = type_start;
                while (type_end < pattern.length && pattern.ptr[type_end] != '}') {
                    type_end += 1U;
                }
                type = (SlCliSpan){&pattern.ptr[type_start], type_end - type_start};
            }
            if (!first) {
                sl_cli_write_cstr(file, ",");
            }
            sl_cli_write_cstr(file, "\n          { \"name\": ");
            sl_cli_json_escape(file, (SlCliSpan){&pattern.ptr[name_start], name_end - name_start});
            sl_cli_write_cstr(file,
                              ", \"in\": \"path\", \"required\": true, \"schema\": { \"type\": ");
            sl_cli_json_escape(file, sl_cli_span_equal_cstr(type, "int")
                                         ? sl_cli_span_cstr("integer")
                                         : sl_cli_span_cstr("string"));
            sl_cli_write_cstr(file, " } }");
            first = false;
        }
        index += 1U;
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

static void sl_cli_openapi_emit_operation(FILE* out, const SlCliRoute* route)
{
    sl_cli_json_escape_lower(out, route->method);
    sl_cli_write_cstr(out, ": {\n        \"operationId\": ");
    sl_cli_json_escape(out, route->name);
    sl_cli_write_cstr(out, ",\n        \"parameters\": ");
    sl_cli_openapi_parameters(out, route->pattern);
    sl_cli_write_cstr(out, ",\n"
                           "        \"responses\": {\n"
                           "          \"200\": { \"description\": \"OK\" }\n"
                           "        }\n"
                           "      }");
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
        sl_cli_openapi_emit_operation(out, &metadata->routes[operation]);
    }
    sl_cli_write_cstr(out, "\n    }");
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
    sl_cli_write_cstr(out, "\n  }\n}\n");
}

static int sl_cli_command_openapi(const SlCliOptions* options)
{
    unsigned char json_storage[SL_CLI_FILE_MAX_BYTES];
    yyjson_doc* doc = NULL;
    SlCliMetadata metadata = {0};
    char path_buffers[SL_CLI_MAX_ROUTES][512];
    SlCliSpan openapi_paths[SL_CLI_MAX_ROUTES] = {0};
    FILE* out = stdout;

    if (options->plan_path == NULL) {
        sl_cli_write_cstr(stderr, "sloppy openapi: --plan <path> is required\n");
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
    if (sl_cli_load_metadata(options->plan_path, json_storage, &doc, &metadata) != 0) {
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
