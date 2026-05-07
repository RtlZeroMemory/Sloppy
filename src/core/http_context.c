/*
 * src/core/http_context.c
 *
 * Implements the query parsing helper for request context materialization. This is not a
 * full URL parser: it reads only the query component after `?`, splits on
 * `&`, supports `+` and `%XX` decoding, and stores arena-owned string views.
 *
 * Tests: tests/unit/core/test_http_context.c.
 */
#include "sloppy/http_context.h"

#include "sloppy/checked_math.h"

#include <stdbool.h>

static int sl_http_query_hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    return -1;
}

static SlStatus sl_http_query_decode(SlArena* arena, SlStr encoded, SlStr* out)
{
    void* memory = NULL;
    char* dst = NULL;
    size_t src_index = 0U;
    size_t dst_index = 0U;
    SlStatus status;

    if (arena == NULL || out == NULL || (encoded.ptr == NULL && encoded.length != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (encoded.length == 0U) {
        *out = sl_str_empty();
        return sl_status_ok();
    }

    status = sl_arena_alloc(arena, encoded.length, 1U, &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    dst = (char*)memory;
    while (src_index < encoded.length) {
        char ch = encoded.ptr[src_index];
        if (ch == '+') {
            dst[dst_index] = ' ';
            src_index += 1U;
            dst_index += 1U;
            continue;
        }

        if (ch == '%') {
            int high = 0;
            int low = 0;
            if (src_index + 2U >= encoded.length) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            high = sl_http_query_hex_value(encoded.ptr[src_index + 1U]);
            low = sl_http_query_hex_value(encoded.ptr[src_index + 2U]);
            if (high < 0 || low < 0) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            dst[dst_index] = (char)((high << 4) | low);
            src_index += 3U;
            dst_index += 1U;
            continue;
        }

        dst[dst_index] = ch;
        src_index += 1U;
        dst_index += 1U;
    }

    *out = sl_str_from_parts(dst, dst_index);
    return sl_status_ok();
}

static SlStr sl_http_query_slice(SlStr text, size_t start, size_t end)
{
    return sl_str_from_parts(text.ptr + start, end - start);
}

static size_t sl_http_query_pair_count(SlStr query)
{
    size_t index = 0U;
    size_t count = query.length == 0U ? 0U : 1U;

    for (index = 0U; index < query.length; index += 1U) {
        if (query.ptr[index] == '&') {
            count += 1U;
        }
    }

    return count;
}

static SlHttpQueryParam* sl_http_query_find_param(SlHttpQueryParam* params, size_t count,
                                                  SlStr name)
{
    size_t index = 0U;

    for (index = 0U; index < count; index += 1U) {
        if (sl_str_equal(params[index].name, name)) {
            return &params[index];
        }
    }

    return NULL;
}

static SlStatus sl_http_query_parse_pair(SlArena* arena, SlStr query, size_t pair_start,
                                         size_t pair_end, SlHttpQueryParam* params, size_t* count)
{
    size_t cursor = pair_start;
    size_t equals = pair_end;
    SlStr encoded_name = {0};
    SlStr encoded_value = {0};
    SlStr name = {0};
    SlStr value = {0};
    SlHttpQueryParam* existing = NULL;
    SlStatus status;

    if (arena == NULL || params == NULL || count == NULL || pair_start > pair_end ||
        pair_end > query.length)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    while (cursor < pair_end) {
        if (query.ptr[cursor] == '=') {
            equals = cursor;
            break;
        }
        cursor += 1U;
    }

    encoded_name = sl_http_query_slice(query, pair_start, equals);
    encoded_value =
        equals < pair_end ? sl_http_query_slice(query, equals + 1U, pair_end) : sl_str_empty();

    status = sl_http_query_decode(arena, encoded_name, &name);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_query_decode(arena, encoded_value, &value);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    existing = sl_http_query_find_param(params, *count, name);
    if (existing != NULL) {
        existing->value = value;
    }
    else {
        params[*count].name = name;
        params[*count].value = value;
        *count += 1U;
    }

    return sl_status_ok();
}

static SlStatus sl_http_query_alloc_params(SlArena* arena, size_t capacity,
                                           SlHttpQueryParam** out_params)
{
    size_t alloc_size = 0U;
    void* memory = NULL;
    SlStatus status;

    if (arena == NULL || out_params == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_checked_array_size(capacity, sizeof(SlHttpQueryParam), &alloc_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, alloc_size, _Alignof(SlHttpQueryParam), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out_params = (SlHttpQueryParam*)memory;
    return sl_status_ok();
}

SlStatus sl_http_query_parse(SlArena* arena, SlStr raw_target, SlHttpQuery* out_query)
{
    SlArenaMark mark = {0};
    size_t query_start = raw_target.length;
    size_t cursor = 0U;
    size_t count = 0U;
    size_t capacity = 0U;
    SlHttpQueryParam* params = NULL;
    SlStatus status;
    SlStatus reset_status;
    SlStr query = {0};

    if (arena == NULL || out_query == NULL || (raw_target.ptr == NULL && raw_target.length != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_query = (SlHttpQuery){0};
    for (cursor = 0U; cursor < raw_target.length; cursor += 1U) {
        if (raw_target.ptr[cursor] == '?') {
            query_start = cursor + 1U;
            break;
        }
    }

    if (query_start >= raw_target.length) {
        return sl_status_ok();
    }

    mark = sl_arena_mark(arena);
    query = sl_http_query_slice(raw_target, query_start, raw_target.length);
    capacity = sl_http_query_pair_count(query);
    if (capacity == 0U) {
        return sl_status_ok();
    }
    if (capacity > SL_HTTP_DEFAULT_MAX_QUERY_PARAMS) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    status = sl_http_query_alloc_params(arena, capacity, &params);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    cursor = 0U;
    while (cursor <= query.length) {
        size_t pair_start = cursor;
        size_t pair_end = cursor;

        while (pair_end < query.length && query.ptr[pair_end] != '&') {
            pair_end += 1U;
        }

        status = sl_http_query_parse_pair(arena, query, pair_start, pair_end, params, &count);
        if (!sl_status_is_ok(status)) {
            reset_status = sl_arena_reset_to(arena, mark);
            (void)reset_status;
            return status;
        }

        if (pair_end == query.length) {
            break;
        }
        cursor = pair_end + 1U;
    }

    out_query->params = params;
    out_query->param_count = count;
    return sl_status_ok();
}
