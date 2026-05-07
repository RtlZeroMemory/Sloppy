/*
 * src/core/route.c
 *
 * Implements Sloppy's native route pattern parser and matcher. It parses one path pattern,
 * matches one path against that pattern, and captures route parameters for dispatch.
 *
 * Safety invariants:
 * - parsing is iterative and bounded by SL_ROUTE_MAX_SEGMENTS / SL_ROUTE_MAX_PARAMS;
 * - parsed pattern data is copied into the caller-provided arena;
 * - match parameter values are borrowed slices of the caller-provided path;
 * - no route table, trie, HTTP parser, libuv, llhttp, OS API, V8 type, or JS API appears
 *   in this module.
 *
 * Tests: tests/unit/core/test_route.c.
 */
#include "sloppy/route.h"

#include "sloppy/checked_math.h"

typedef struct SlRouteParseContext
{
    SlArena* arena;
    SlDiag* out_diag;
    SlDiagCode diag_code;
    SlStr diag_message;
    SlStr diag_hint;
} SlRouteParseContext;

static SlStr sl_route_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static bool sl_route_str_valid(SlStr str)
{
    return str.length == 0U || str.ptr != NULL;
}

static SlStatus sl_route_copy_str(SlArena* arena, SlStr src, SlStr* out)
{
    SlOwnedStr owned = {0};
    SlStatus status;

    if (arena == NULL || out == NULL || !sl_route_str_valid(src)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_str_copy_to_arena(arena, src, &owned);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out = sl_owned_str_as_view(owned);
    return sl_status_ok();
}

static SlStatus sl_route_set_diag(SlRouteParseContext* ctx, SlDiagCode code, SlStr message,
                                  SlStr hint)
{
    if (ctx == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    ctx->diag_code = code;
    ctx->diag_message = message;
    ctx->diag_hint = hint;
    return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
}

static SlStatus sl_route_invalid_pattern(SlRouteParseContext* ctx, SlStr message, SlStr hint)
{
    return sl_route_set_diag(ctx, SL_DIAG_INVALID_ROUTE_PATTERN, message, hint);
}

static SlStatus sl_route_finish_diag(SlRouteParseContext* ctx)
{
    SlDiagBuilder builder;
    SlStatus status;

    if (ctx == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (ctx->out_diag == NULL || ctx->diag_code == SL_DIAG_NONE) {
        return sl_status_ok();
    }

    *ctx->out_diag = (SlDiag){0};

    status = sl_diag_builder_init(&builder, ctx->arena, SL_DIAG_SEVERITY_ERROR, ctx->diag_code,
                                  ctx->diag_message);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (!sl_str_is_empty(ctx->diag_hint)) {
        status = sl_diag_builder_add_hint(&builder, ctx->diag_hint);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    return sl_diag_builder_finish(&builder, ctx->out_diag);
}

static bool sl_route_contains_byte(SlStr str, char byte)
{
    size_t index = 0U;

    if (!sl_route_str_valid(str)) {
        return false;
    }

    for (index = 0U; index < str.length; index += 1U) {
        if (str.ptr[index] == byte) {
            return true;
        }
    }

    return false;
}

static bool sl_route_is_name_start(char byte)
{
    return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || byte == '_';
}

static bool sl_route_is_name_continue(char byte)
{
    return sl_route_is_name_start(byte) || (byte >= '0' && byte <= '9');
}

static bool sl_route_param_name_valid(SlStr name)
{
    size_t index = 0U;

    if (name.length == 0U || name.ptr == NULL || !sl_route_is_name_start(name.ptr[0])) {
        return false;
    }

    for (index = 1U; index < name.length; index += 1U) {
        if (!sl_route_is_name_continue(name.ptr[index])) {
            return false;
        }
    }

    return true;
}

static bool sl_route_name_duplicate(const SlRouteSegment* segments, size_t segment_count,
                                    SlStr name)
{
    size_t index = 0U;

    for (index = 0U; index < segment_count; index += 1U) {
        if (segments[index].kind == SL_ROUTE_SEGMENT_PARAM &&
            sl_str_equal(segments[index].param_name, name))
        {
            return true;
        }
    }

    return false;
}

static SlStatus sl_route_parse_param_type(SlRouteParseContext* ctx, SlStr type,
                                          SlRouteParamKind* out)
{
    if (ctx == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (sl_str_equal(type, sl_route_literal("int", sizeof("int") - 1U))) {
        *out = SL_ROUTE_PARAM_INT;
        return sl_status_ok();
    }

    if (sl_str_equal(type, sl_route_literal("str", sizeof("str") - 1U))) {
        *out = SL_ROUTE_PARAM_STRING;
        return sl_status_ok();
    }

    return sl_route_invalid_pattern(
        ctx,
        sl_route_literal("unknown route parameter type",
                         sizeof("unknown route parameter type") - 1U),
        sl_route_literal("supported route parameter types are int and str",
                         sizeof("supported route parameter types are int and str") - 1U));
}

static SlStatus sl_route_parse_param_segment(SlRouteParseContext* ctx, SlStr segment,
                                             SlRouteSegment* segments, size_t segment_count,
                                             SlRouteSegment* out)
{
    SlStr inner = {0};
    SlStr name = {0};
    SlStr type = {0};
    SlRouteParamKind kind = SL_ROUTE_PARAM_STRING;
    size_t index = 0U;
    size_t colon = 0U;
    bool has_colon = false;
    SlStatus status;

    if (ctx == NULL || segments == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (segment.length < 3U || segment.ptr[0] != '{' || segment.ptr[segment.length - 1U] != '}') {
        return sl_route_invalid_pattern(
            ctx,
            sl_route_literal("malformed route parameter", sizeof("malformed route parameter") - 1U),
            sl_route_literal(
                "parameters must occupy a whole segment like {id} or {id:int}",
                sizeof("parameters must occupy a whole segment like {id} or {id:int}") - 1U));
    }

    inner = sl_str_from_parts(segment.ptr + 1U, segment.length - 2U);
    for (index = 0U; index < inner.length; index += 1U) {
        if (inner.ptr[index] == '{' || inner.ptr[index] == '}') {
            return sl_route_invalid_pattern(
                ctx,
                sl_route_literal("malformed route parameter",
                                 sizeof("malformed route parameter") - 1U),
                sl_route_literal("nested braces are not supported in route parameters",
                                 sizeof("nested braces are not supported in route parameters") -
                                     1U));
        }

        if (inner.ptr[index] == ':') {
            if (has_colon) {
                return sl_route_invalid_pattern(
                    ctx,
                    sl_route_literal("malformed route parameter",
                                     sizeof("malformed route parameter") - 1U),
                    sl_route_literal("route parameters support at most one type separator",
                                     sizeof("route parameters support at most one type separator") -
                                         1U));
            }

            has_colon = true;
            colon = index;
        }
    }

    name = has_colon ? sl_str_from_parts(inner.ptr, colon) : inner;
    if (!sl_route_param_name_valid(name)) {
        return sl_route_invalid_pattern(
            ctx,
            sl_route_literal("invalid route parameter name",
                             sizeof("invalid route parameter name") - 1U),
            sl_route_literal("parameter names must start with a letter or underscore",
                             sizeof("parameter names must start with a letter or underscore") -
                                 1U));
    }

    if (has_colon) {
        if (colon + 1U >= inner.length) {
            return sl_route_invalid_pattern(
                ctx,
                sl_route_literal("malformed route parameter",
                                 sizeof("malformed route parameter") - 1U),
                sl_route_literal("route parameter type must be non-empty",
                                 sizeof("route parameter type must be non-empty") - 1U));
        }

        type = sl_str_from_parts(inner.ptr + colon + 1U, inner.length - colon - 1U);
        status = sl_route_parse_param_type(ctx, type, &kind);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    if (sl_route_name_duplicate(segments, segment_count, name)) {
        return sl_route_set_diag(
            ctx, SL_DIAG_DUPLICATE_ROUTE_PARAM,
            sl_route_literal("duplicate route parameter", sizeof("duplicate route parameter") - 1U),
            sl_route_literal("route parameter names must be unique within one pattern",
                             sizeof("route parameter names must be unique within one pattern") -
                                 1U));
    }

    out->kind = SL_ROUTE_SEGMENT_PARAM;
    out->text = sl_str_empty();
    out->param_kind = kind;
    return sl_route_copy_str(ctx->arena, name, &out->param_name);
}

static SlStatus sl_route_parse_static_segment(SlRouteParseContext* ctx, SlStr segment,
                                              SlRouteSegment* out)
{
    SlStatus status;

    if (ctx == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (sl_route_contains_byte(segment, '{') || sl_route_contains_byte(segment, '}')) {
        return sl_route_invalid_pattern(
            ctx,
            sl_route_literal("malformed route parameter", sizeof("malformed route parameter") - 1U),
            sl_route_literal(
                "braces are only valid around a whole route parameter segment",
                sizeof("braces are only valid around a whole route parameter segment") - 1U));
    }

    out->kind = SL_ROUTE_SEGMENT_STATIC;
    out->param_name = sl_str_empty();
    out->param_kind = SL_ROUTE_PARAM_STRING;
    status = sl_route_copy_str(ctx->arena, segment, &out->text);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_status_ok();
}

static SlStatus sl_route_parse_segment(SlRouteParseContext* ctx, SlStr segment,
                                       SlRouteSegment* segments, size_t segment_count,
                                       SlRouteSegment* out)
{
    if (ctx == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (segment.length == 0U) {
        return sl_route_invalid_pattern(
            ctx, sl_route_literal("empty route segment", sizeof("empty route segment") - 1U),
            sl_route_literal(
                "empty segments are not supported except for the root pattern",
                sizeof("empty segments are not supported except for the root pattern") - 1U));
    }

    if (segment.ptr[0] == '{' || segment.ptr[segment.length - 1U] == '}') {
        return sl_route_parse_param_segment(ctx, segment, segments, segment_count, out);
    }

    return sl_route_parse_static_segment(ctx, segment, out);
}

static SlStatus sl_route_finalize_pattern(SlRouteParseContext* ctx, SlStr pattern_text,
                                          SlRouteSegment* parsed_segments, size_t segment_count,
                                          size_t param_count, SlRoutePattern* out_pattern)
{
    SlRouteSegment* segments = NULL;
    void* ptr = NULL;
    size_t alloc_size = 0U;
    size_t index = 0U;
    SlStatus status;

    if (ctx == NULL || out_pattern == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_route_copy_str(ctx->arena, pattern_text, &out_pattern->source);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (segment_count != 0U) {
        status = sl_checked_array_size(segment_count, sizeof(SlRouteSegment), &alloc_size);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        status = sl_arena_alloc(ctx->arena, alloc_size, _Alignof(SlRouteSegment), &ptr);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        segments = (SlRouteSegment*)ptr;
        for (index = 0U; index < segment_count; index += 1U) {
            segments[index] = parsed_segments[index];
        }
    }

    out_pattern->segments = segments;
    out_pattern->segment_count = segment_count;
    out_pattern->param_count = param_count;
    return sl_status_ok();
}

static SlStatus sl_route_parse_pattern_segments(SlRouteParseContext* ctx, SlStr pattern_text,
                                                SlRouteSegment* parsed_segments,
                                                size_t* out_segment_count, size_t* out_param_count)
{
    size_t segment_count = 0U;
    size_t param_count = 0U;
    size_t pos = 1U;
    SlStatus status;

    if (ctx == NULL || parsed_segments == NULL || out_segment_count == NULL ||
        out_param_count == NULL)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (pattern_text.ptr[0] != '/') {
        return sl_route_invalid_pattern(
            ctx,
            sl_route_literal("route pattern must start with slash",
                             sizeof("route pattern must start with slash") - 1U),
            sl_route_literal("route patterns must be absolute paths such as /users/{id}",
                             sizeof("route patterns must be absolute paths such as /users/{id}") -
                                 1U));
    }

    if (sl_route_contains_byte(pattern_text, '?')) {
        return sl_route_invalid_pattern(
            ctx,
            sl_route_literal("route pattern must not include query string",
                             sizeof("route pattern must not include query string") - 1U),
            sl_route_literal("query strings are not part of route pattern matching",
                             sizeof("query strings are not part of route pattern matching") - 1U));
    }

    if (pattern_text.length == 1U) {
        *out_segment_count = 0U;
        *out_param_count = 0U;
        return sl_status_ok();
    }

    while (pos <= pattern_text.length) {
        size_t segment_start = pos;
        size_t segment_end = pos;
        SlStr segment = {0};

        while (segment_end < pattern_text.length && pattern_text.ptr[segment_end] != '/') {
            segment_end += 1U;
        }

        if (segment_count >= SL_ROUTE_MAX_SEGMENTS) {
            return sl_route_invalid_pattern(
                ctx,
                sl_route_literal("route pattern has too many segments",
                                 sizeof("route pattern has too many segments") - 1U),
                sl_route_literal(
                    "split large route shapes before registering native routes",
                    sizeof("split large route shapes before registering native routes") - 1U));
        }

        segment = sl_str_from_parts(pattern_text.ptr + segment_start, segment_end - segment_start);
        parsed_segments[segment_count] = (SlRouteSegment){0};
        status = sl_route_parse_segment(ctx, segment, parsed_segments, segment_count,
                                        &parsed_segments[segment_count]);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        if (parsed_segments[segment_count].kind == SL_ROUTE_SEGMENT_PARAM) {
            if (param_count >= SL_ROUTE_MAX_PARAMS) {
                return sl_route_invalid_pattern(
                    ctx,
                    sl_route_literal("route pattern has too many parameters",
                                     sizeof("route pattern has too many parameters") - 1U),
                    sl_route_literal(
                        "keep one route pattern within the supported parameter bound",
                        sizeof("keep one route pattern within the supported parameter bound") -
                            1U));
            }
            param_count += 1U;
        }

        segment_count += 1U;

        if (segment_end == pattern_text.length) {
            break;
        }

        pos = segment_end + 1U;
    }

    *out_segment_count = segment_count;
    *out_param_count = param_count;
    return sl_status_ok();
}

SlStatus sl_route_pattern_parse(SlArena* arena, SlStr pattern_text, SlRoutePattern* out_pattern,
                                SlDiag* out_diag)
{
    SlRouteParseContext ctx = {0};
    SlArenaMark mark = {0};
    SlRouteSegment parsed_segments[SL_ROUTE_MAX_SEGMENTS];
    size_t segment_count = 0U;
    size_t param_count = 0U;
    SlStatus status;
    SlStatus reset_status;
    SlStatus diag_status;

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }

    if (out_pattern == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_pattern = (SlRoutePattern){0};

    if (arena == NULL || !sl_route_str_valid(pattern_text)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    ctx.arena = arena;
    ctx.out_diag = out_diag;
    ctx.diag_code = SL_DIAG_NONE;
    mark = sl_arena_mark(arena);

    if (pattern_text.length == 0U) {
        status = sl_route_invalid_pattern(
            &ctx,
            sl_route_literal("route pattern must not be empty",
                             sizeof("route pattern must not be empty") - 1U),
            sl_route_literal(
                "route patterns must be absolute paths such as / or /users/{id}",
                sizeof("route patterns must be absolute paths such as / or /users/{id}") - 1U));
        goto failure;
    }

    status = sl_route_parse_pattern_segments(&ctx, pattern_text, parsed_segments, &segment_count,
                                             &param_count);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }

    status = sl_route_finalize_pattern(&ctx, pattern_text, parsed_segments, segment_count,
                                       param_count, out_pattern);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }

    return sl_status_ok();

failure:
    *out_pattern = (SlRoutePattern){0};
    reset_status = sl_arena_reset_to(arena, mark);
    if (!sl_status_is_ok(reset_status)) {
        return reset_status;
    }

    diag_status = sl_route_finish_diag(&ctx);
    if (!sl_status_is_ok(diag_status)) {
        return diag_status;
    }

    return status;
}

static bool sl_route_path_has_query(SlStr path)
{
    return sl_route_contains_byte(path, '?');
}

static bool sl_route_segment_is_ascii_digits(SlStr segment)
{
    size_t index = 0U;

    if (segment.length == 0U || segment.ptr == NULL) {
        return false;
    }

    for (index = 0U; index < segment.length; index += 1U) {
        if (segment.ptr[index] < '0' || segment.ptr[index] > '9') {
            return false;
        }
    }

    return true;
}

static bool sl_route_match_param(SlRouteParamKind kind, SlStr segment)
{
    if (segment.length == 0U || segment.ptr == NULL) {
        return false;
    }

    if (kind == SL_ROUTE_PARAM_INT) {
        return sl_route_segment_is_ascii_digits(segment);
    }

    return true;
}

static bool sl_route_segment_kind_known(SlRouteSegmentKind kind)
{
    return kind == SL_ROUTE_SEGMENT_STATIC || kind == SL_ROUTE_SEGMENT_PARAM;
}

static bool sl_route_param_kind_known(SlRouteParamKind kind)
{
    return kind == SL_ROUTE_PARAM_STRING || kind == SL_ROUTE_PARAM_INT;
}

static SlStatus sl_route_pattern_validate_for_match(const SlRoutePattern* pattern,
                                                    size_t* out_param_count)
{
    size_t index = 0U;
    size_t actual_param_count = 0U;

    if (pattern == NULL || out_param_count == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_param_count = 0U;
    if (!sl_route_str_valid(pattern->source) || pattern->source.length == 0U ||
        pattern->source.ptr[0] != '/' || sl_route_contains_byte(pattern->source, '?'))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (pattern->segment_count > SL_ROUTE_MAX_SEGMENTS ||
        pattern->param_count > SL_ROUTE_MAX_PARAMS)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (pattern->segment_count == 0U) {
        if (pattern->param_count != 0U ||
            !sl_str_equal(pattern->source, sl_route_literal("/", sizeof("/") - 1U)))
        {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }

        return sl_status_ok();
    }

    if (pattern->segment_count != 0U && pattern->segments == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (index = 0U; index < pattern->segment_count; index += 1U) {
        const SlRouteSegment* segment = &pattern->segments[index];

        if (!sl_route_segment_kind_known(segment->kind)) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }

        if (segment->kind == SL_ROUTE_SEGMENT_STATIC) {
            if (!sl_route_str_valid(segment->text) || segment->text.length == 0U) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            continue;
        }

        if (!sl_route_param_kind_known(segment->param_kind) ||
            !sl_route_param_name_valid(segment->param_name))
        {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }

        actual_param_count += 1U;
        if (actual_param_count > pattern->param_count || actual_param_count > SL_ROUTE_MAX_PARAMS) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
    }

    *out_param_count = actual_param_count;
    return sl_status_ok();
}

static SlStatus sl_route_copy_match_params(SlArena* arena, const SlRouteParam* captures,
                                           size_t capture_count, SlRouteParam** out)
{
    void* ptr = NULL;
    size_t alloc_size = 0U;
    size_t index = 0U;
    SlRouteParam* params = NULL;
    SlStatus status;

    if (arena == NULL || captures == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out = NULL;
    if (capture_count == 0U) {
        return sl_status_ok();
    }

    status = sl_checked_array_size(capture_count, sizeof(SlRouteParam), &alloc_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_arena_alloc(arena, alloc_size, _Alignof(SlRouteParam), &ptr);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    params = (SlRouteParam*)ptr;
    for (index = 0U; index < capture_count; index += 1U) {
        params[index] = captures[index];
    }

    *out = params;
    return sl_status_ok();
}

static SlStatus sl_route_match_one_segment(const SlRouteSegment* pattern_segment,
                                           SlStr path_segment, SlRouteParam* params,
                                           size_t* param_index, bool* out_matched)
{
    if (pattern_segment == NULL || param_index == NULL || out_matched == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_matched = false;
    if (path_segment.length == 0U) {
        return sl_status_ok();
    }

    if (pattern_segment->kind == SL_ROUTE_SEGMENT_STATIC) {
        *out_matched = sl_str_equal(pattern_segment->text, path_segment);
        return sl_status_ok();
    }

    if (params == NULL || !sl_route_match_param(pattern_segment->param_kind, path_segment)) {
        return params == NULL ? sl_status_from_code(SL_STATUS_INVALID_ARGUMENT) : sl_status_ok();
    }

    params[*param_index].name = pattern_segment->param_name;
    params[*param_index].value = path_segment;
    params[*param_index].kind = pattern_segment->param_kind;
    *param_index += 1U;
    *out_matched = true;
    return sl_status_ok();
}

static SlStatus sl_route_match_segments(const SlRoutePattern* pattern, SlStr path,
                                        SlRouteParam* captures, size_t* out_capture_count,
                                        bool* out_matched)
{
    size_t pattern_index = 0U;
    size_t path_pos = 1U;
    size_t param_index = 0U;
    SlStatus status;

    if (pattern == NULL || captures == NULL || out_capture_count == NULL || out_matched == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_capture_count = 0U;
    *out_matched = false;

    while (pattern_index < pattern->segment_count) {
        const SlRouteSegment* pattern_segment = &pattern->segments[pattern_index];
        size_t segment_start = path_pos;
        size_t segment_end = path_pos;
        SlStr path_segment = {0};
        bool segment_matched = false;

        if (path_pos >= path.length) {
            return sl_status_ok();
        }

        while (segment_end < path.length && path.ptr[segment_end] != '/') {
            segment_end += 1U;
        }

        path_segment = sl_str_from_parts(path.ptr + segment_start, segment_end - segment_start);
        status = sl_route_match_one_segment(pattern_segment, path_segment, captures, &param_index,
                                            &segment_matched);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        if (!segment_matched) {
            return sl_status_ok();
        }

        pattern_index += 1U;

        if (segment_end == path.length) {
            path_pos = segment_end;
        }
        else {
            path_pos = segment_end + 1U;
        }
    }

    *out_matched = path_pos == path.length && path.ptr[path.length - 1U] != '/';
    *out_capture_count = *out_matched ? param_index : 0U;
    return sl_status_ok();
}

SlStatus sl_route_pattern_match(SlArena* arena, const SlRoutePattern* pattern, SlStr path,
                                SlRouteMatch* out_match)
{
    SlRouteParam captures[SL_ROUTE_MAX_PARAMS];
    SlRouteParam* copied_params = NULL;
    size_t actual_param_count = 0U;
    size_t capture_count = 0U;
    bool matched = false;
    SlStatus status;

    if (out_match == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_match = (SlRouteMatch){0};

    if (arena == NULL || pattern == NULL || !sl_route_str_valid(path) || path.length == 0U ||
        path.ptr[0] != '/' || sl_route_path_has_query(path))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_route_pattern_validate_for_match(pattern, &actual_param_count);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (pattern->segment_count == 0U) {
        out_match->matched = path.length == 1U;
        out_match->params = NULL;
        out_match->param_count = 0U;
        return sl_status_ok();
    }

    (void)actual_param_count;
    status = sl_route_match_segments(pattern, path, captures, &capture_count, &matched);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (!matched) {
        return sl_status_ok();
    }

    status = sl_route_copy_match_params(arena, captures, capture_count, &copied_params);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    out_match->matched = true;
    out_match->params = copied_params;
    out_match->param_count = capture_count;
    return sl_status_ok();
}
