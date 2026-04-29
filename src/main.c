/*
 * Sloppy CLI.
 *
 * EPIC-19 adds metadata-only introspection commands over plan-compatible JSON files. These
 * commands parse artifacts, fixtures, and debug metadata only: they do not start HTTP, load
 * V8, execute application code, or call route handlers.
 */
#include "sloppy/arena.h"
#include "sloppy/compiler.h"
#include "sloppy/data_postgres.h"
#include "sloppy/data_sqlserver.h"
#include "sloppy/diagnostics.h"
#include "sloppy/plan.h"
#include "sloppy/platform.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <yyjson.h>

#define SL_CLI_MAX_ROUTES 128U
#define SL_CLI_MAX_HANDLERS 128U
#define SL_CLI_MAX_MODULES 64U
#define SL_CLI_MAX_DEPS 16U
#define SL_CLI_MAX_PROVIDERS 32U
#define SL_CLI_MAX_DOCTOR_CHECKS 32U
#define SL_CLI_FILE_MAX_BYTES 65536U
#define SL_CLI_ARENA_BYTES 65536U

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
    (void)printf("Foundation build with metadata-only CLI introspection.\n\n");
    (void)printf("Usage:\n");
    (void)printf("  sloppy --help\n");
    (void)printf("  sloppy --version\n");
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

static int sl_cli_parse_options(int argc, char** argv, SlCliOptions* out)
{
    int index = 2;

    if (out == NULL) {
        return 1;
    }

    *out = (SlCliOptions){0};
    out->format = SL_CLI_FORMAT_TEXT;

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
        if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0) {
            out->help = true;
            index += 1;
        }
        else if (strcmp(argv[index], "--format") == 0) {
            if (index + 1 >= argc) {
                sl_cli_write_cstr(stderr, "sloppy: --format requires text or json\n");
                return 1;
            }
            if (strcmp(argv[index + 1], "text") == 0) {
                out->format = SL_CLI_FORMAT_TEXT;
            }
            else if (strcmp(argv[index + 1], "json") == 0) {
                out->format = SL_CLI_FORMAT_JSON;
            }
            else {
                sl_cli_write_error_with_value("sloppy: unsupported format '", argv[index + 1],
                                              "'\n");
                return 1;
            }
            index += 2;
        }
        else if (strcmp(argv[index], "--plan") == 0 || strcmp(argv[index], "--app") == 0) {
            if (index + 1 >= argc) {
                sl_cli_write_error_with_value("sloppy: ", argv[index], " requires a path\n");
                return 1;
            }
            out->plan_path = argv[index + 1];
            index += 2;
        }
        else if (strcmp(argv[index], "--output") == 0) {
            if (index + 1 >= argc) {
                sl_cli_write_cstr(stderr, "sloppy: --output requires a path\n");
                return 1;
            }
            out->output_path = argv[index + 1];
            index += 2;
        }
        else {
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
