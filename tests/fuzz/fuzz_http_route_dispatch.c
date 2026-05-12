#include "sloppy/http_dispatch.h"

#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define FUZZ_HTTP_ROUTE_DISPATCH_ARENA_SIZE 131072U
#define FUZZ_HTTP_ROUTE_DISPATCH_MAX_PATH 256U

static SlEngineOptions fuzz_noop_options(void)
{
    SlEngineOptions options = {0};

    options.kind = SL_ENGINE_KIND_NONE;
    options.runtime_name = sl_str_from_cstr("http-route-dispatch-fuzz");
    options.runtime_version = sl_str_from_cstr("0.3.0-fuzz");
    options.target_platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    options.target_engine = sl_str_from_cstr("none");
    return options;
}

static const char* fuzz_method(uint8_t byte)
{
    switch (byte % 6U) {
    case 0U:
        return "GET";
    case 1U:
        return "HEAD";
    case 2U:
        return "POST";
    case 3U:
        return "PUT";
    case 4U:
        return "DELETE";
    default:
        return "OPTIONS";
    }
}

static size_t fuzz_copy_path(const uint8_t* data, size_t size, char* out, size_t capacity)
{
    size_t input_index = 1U;
    size_t output_index = 0U;

    if (out == NULL || capacity == 0U) {
        return 0U;
    }

    out[output_index] = '/';
    output_index += 1U;
    if (data != NULL && size > 1U && data[1U] == '/') {
        input_index = 2U;
    }
    while (data != NULL && input_index < size && output_index + 1U < capacity) {
        unsigned char byte = data[input_index];
        if (byte == '\r' || byte == '\n' || byte == ' ' || byte == '\t' || byte == '?') {
            byte = 'x';
        }
        if (byte < 0x20U || byte > 0x7eU) {
            byte = (unsigned char)('a' + (byte % 26U));
        }
        out[output_index] = (char)byte;
        output_index += 1U;
        input_index += 1U;
    }
    out[output_index] = '\0';
    return output_index;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    unsigned char storage[FUZZ_HTTP_ROUTE_DISPATCH_ARENA_SIZE];
    unsigned char engine_storage[4096U];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlEngineOptions options = fuzz_noop_options();
    SlPlanHandler handlers[4] = {0};
    SlPlanRoute routes[6] = {0};
    SlPlan plan = {0};
    SlHttpRouteTable table = {0};
    SlHttpRequestHead request = {0};
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlStr allow = {0};
    char path[FUZZ_HTTP_ROUTE_DISPATCH_MAX_PATH + 1U];
    char request_text[FUZZ_HTTP_ROUTE_DISPATCH_MAX_PATH + 96U];
    int written = 0;

    if (data == NULL || size == 0U ||
        !sl_status_is_ok(sl_arena_init(&arena, storage, sizeof(storage))) ||
        !sl_status_is_ok(sl_arena_init(&engine_arena, engine_storage, sizeof(engine_storage))) ||
        !sl_status_is_ok(sl_engine_create(&options, &engine_arena, &engine)))
    {
        return 0;
    }

    handlers[0].id = 1U;
    handlers[0].export_name = sl_str_from_cstr("__sloppy_handler_1");
    handlers[1].id = 2U;
    handlers[1].export_name = sl_str_from_cstr("__sloppy_handler_2");
    handlers[2].id = 3U;
    handlers[2].export_name = sl_str_from_cstr("__sloppy_handler_3");
    handlers[3].id = 4U;
    handlers[3].export_name = sl_str_from_cstr("__sloppy_handler_4");
    routes[0].method = sl_str_from_cstr("GET");
    routes[0].pattern = sl_str_from_cstr("/health");
    routes[0].handler_id = 1U;
    routes[1].method = sl_str_from_cstr("GET");
    routes[1].pattern = sl_str_from_cstr("/users/{id:int}");
    routes[1].handler_id = 2U;
    routes[2].method = sl_str_from_cstr("POST");
    routes[2].pattern = sl_str_from_cstr("/users/{id:int}/posts/{slug:alpha}");
    routes[2].handler_id = 3U;
    routes[3].method = sl_str_from_cstr("PUT");
    routes[3].pattern = sl_str_from_cstr("/{tenant}/settings");
    routes[3].handler_id = 4U;
    routes[4].method = sl_str_from_cstr("DELETE");
    routes[4].pattern = sl_str_from_cstr("/files/{id:uuid}");
    routes[4].handler_id = 4U;
    routes[5].method = sl_str_from_cstr("OPTIONS");
    routes[5].pattern = sl_str_from_cstr("/users/{id:int}");
    routes[5].handler_id = 1U;
    plan.version = SL_PLAN_CURRENT_VERSION;
    plan.handlers = handlers;
    plan.handler_count = 4U;
    plan.routes = routes;
    plan.route_count = 6U;

    if (!sl_status_is_ok(sl_http_route_table_build(&arena, &plan, &table, &diag))) {
        sl_engine_destroy(engine);
        return 0;
    }

    fuzz_copy_path(data, size, path, sizeof(path));
    written = snprintf(request_text, sizeof(request_text), "%s %s HTTP/1.1\r\nHost: fuzz\r\n\r\n",
                       fuzz_method(data[0]), path);
    if (written > 0 && (size_t)written < sizeof(request_text) &&
        sl_status_is_ok(sl_http_parse_request_head(
            &arena, sl_bytes_from_parts((const unsigned char*)request_text, (size_t)written), NULL,
            &request, NULL)))
    {
        sl_http_dispatch_allow_header_for_path(&arena, &table.dispatch, request.path, &allow);
        sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch, &request, &result,
                                      &diag);
    }

    sl_engine_destroy(engine);
    return 0;
}
