/*
 * src/core/diagnostics.c
 *
 * Implements Sloppy's first structured diagnostics core: stable code names, severities,
 * source spans, bounded related spans/hints, an arena-copying builder, and deterministic
 * plain-text rendering.
 *
 * Safety invariants:
 * - diagnostics created through the builder copy text into caller-provided arena memory;
 * - related spans and hints are fixed-size arrays with named bounds;
 * - renderer output is computed before allocation so it performs one arena allocation;
 * - source-frame and JSON renderers stay deterministic and do not inspect the filesystem;
 * - no platform, V8, terminal, source-map, or localization behavior appears here.
 *
 * Tests: tests/unit/core/test_diagnostics.c.
 */
#include "sloppy/diagnostics.h"

#include "sloppy/checked_math.h"

static SlStr sl_diag_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static bool sl_diag_str_is_valid(SlStr str)
{
    return str.length == 0U || str.ptr != NULL;
}

static SlStatus sl_diag_copy_str(SlArena* arena, SlStr src, SlStr* out)
{
    void* ptr = NULL;
    char* dst = NULL;
    size_t index = 0U;

    if (arena == NULL || out == NULL || !sl_diag_str_is_valid(src)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (src.length == 0U) {
        *out = sl_str_empty();
        return sl_status_ok();
    }

    SlStatus status = sl_arena_alloc(arena, src.length, 1U, &ptr);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    dst = (char*)ptr;
    for (index = 0U; index < src.length; index += 1U) {
        dst[index] = src.ptr[index];
    }

    *out = sl_str_from_parts(dst, src.length);
    return sl_status_ok();
}

static SlStatus sl_diag_copy_span(SlArena* arena, SlSourceSpan src, SlSourceSpan* out)
{
    SlSourceSpan copy = src;
    SlStatus status = sl_diag_copy_str(arena, src.path, &copy.path);

    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out = copy;
    return sl_status_ok();
}

static size_t sl_diag_decimal_len(size_t value)
{
    size_t length = 1U;

    while (value >= 10U) {
        value /= 10U;
        length += 1U;
    }

    return length;
}

static void sl_diag_write_str(char* buffer, size_t* offset, SlStr str)
{
    size_t index = 0U;

    for (index = 0U; index < str.length; index += 1U) {
        buffer[*offset + index] = str.ptr[index];
    }

    *offset += str.length;
}

static void sl_diag_write_size(char* buffer, size_t* offset, size_t value)
{
    char digits[32];
    size_t count = 0U;
    size_t index = 0U;

    do {
        digits[count] = (char)('0' + (value % 10U));
        value /= 10U;
        count += 1U;
    } while (value != 0U);

    for (index = 0U; index < count; index += 1U) {
        buffer[*offset + index] = digits[count - index - 1U];
    }

    *offset += count;
}

static void sl_diag_write_char_repeat(char* buffer, size_t* offset, char value, size_t count)
{
    size_t index = 0U;

    for (index = 0U; index < count; index += 1U) {
        buffer[*offset + index] = value;
    }
    *offset += count;
}

static SlStatus sl_diag_add_len(size_t* total, size_t addend)
{
    return sl_checked_add_size(*total, addend, total);
}

static SlStatus sl_diag_add_str_len(size_t* total, SlStr str)
{
    if (!sl_diag_str_is_valid(str)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_diag_add_len(total, str.length);
}

static SlStr sl_diag_render_path(SlSourceSpan span)
{
    if (span.path.length == 0U) {
        return sl_diag_literal("<unknown>", sizeof("<unknown>") - 1U);
    }

    return span.path;
}

static SlStatus sl_diag_span_render_len(size_t* total, SlSourceSpan span)
{
    SlStatus status = sl_diag_add_str_len(total, sl_diag_render_path(span));

    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (!span.has_location) {
        return sl_status_ok();
    }

    status = sl_diag_add_len(total, 1U + sl_diag_decimal_len(span.line));
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_add_len(total, 1U + sl_diag_decimal_len(span.column));
    if (!sl_status_is_ok(status) || span.length == 0U) {
        return status;
    }

    return sl_diag_add_len(total, 7U + sl_diag_decimal_len(span.length));
}

static void sl_diag_write_span(char* buffer, size_t* offset, SlSourceSpan span)
{
    sl_diag_write_str(buffer, offset, sl_diag_render_path(span));

    if (span.has_location) {
        buffer[*offset] = ':';
        *offset += 1U;
        sl_diag_write_size(buffer, offset, span.line);
        buffer[*offset] = ':';
        *offset += 1U;
        sl_diag_write_size(buffer, offset, span.column);

        if (span.length != 0U) {
            sl_diag_write_str(buffer, offset, sl_diag_literal(" (len ", 6U));
            sl_diag_write_size(buffer, offset, span.length);
            buffer[*offset] = ')';
            *offset += 1U;
        }
    }
}

static SlStr sl_diag_first_hint(const SlDiag* diag)
{
    if (diag == NULL || diag->hint_count == 0U) {
        return sl_str_empty();
    }
    return diag->hints[0];
}

static bool sl_diag_has_span(SlSourceSpan span)
{
    return span.has_location || span.path.length != 0U;
}

static SlStatus sl_diag_header_render_len(size_t* total, const SlDiag* diag)
{
    SlStatus status;

    status = sl_diag_add_str_len(total, sl_diag_severity_name(diag->severity));
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_add_len(total, 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_add_str_len(total, sl_diag_code_name(diag->code));
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_add_len(total, 2U);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_add_str_len(total, diag->message);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_diag_add_len(total, 1U);
}

static SlStatus sl_diag_primary_render_len(size_t* total, SlSourceSpan span)
{
    SlStatus status;

    if (!sl_diag_has_span(span)) {
        return sl_status_ok();
    }

    status = sl_diag_add_len(total, 7U);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_span_render_len(total, span);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_diag_add_len(total, 1U);
}

static SlStatus sl_diag_related_render_len(size_t* total, const SlDiag* diag)
{
    SlStatus status;
    size_t index = 0U;

    if (diag->related_count == 0U) {
        return sl_status_ok();
    }

    status = sl_diag_add_len(total, 12U);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < diag->related_count; index += 1U) {
        if (!sl_diag_str_is_valid(diag->related[index].message)) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }

        status = sl_diag_add_len(total, 4U);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        status = sl_diag_span_render_len(total, diag->related[index].span);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        status = sl_diag_add_len(total, 2U);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        status = sl_diag_add_str_len(total, diag->related[index].message);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        status = sl_diag_add_len(total, 1U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    return sl_status_ok();
}

static SlStatus sl_diag_hints_render_len(size_t* total, const SlDiag* diag)
{
    SlStatus status;
    size_t index = 0U;

    if (diag->hint_count == 0U) {
        return sl_status_ok();
    }

    status = sl_diag_add_len(total, 9U);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < diag->hint_count; index += 1U) {
        if (!sl_diag_str_is_valid(diag->hints[index])) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }

        status = sl_diag_add_len(total, 4U);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        status = sl_diag_add_str_len(total, diag->hints[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        status = sl_diag_add_len(total, 1U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    return sl_status_ok();
}

static SlStatus sl_diag_render_len(const SlDiag* diag, size_t* out)
{
    SlStatus status;
    size_t total = 0U;

    if (diag == NULL || out == NULL || !sl_diag_str_is_valid(diag->message) ||
        diag->related_count > SL_DIAG_MAX_RELATED || diag->hint_count > SL_DIAG_MAX_HINTS)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_diag_header_render_len(&total, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_primary_render_len(&total, diag->primary_span);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_related_render_len(&total, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_hints_render_len(&total, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out = total;
    return sl_status_ok();
}

SlStr sl_diag_severity_name(SlDiagSeverity severity)
{
    switch (severity) {
    case SL_DIAG_SEVERITY_NOTE:
        return sl_diag_literal("note", sizeof("note") - 1U);
    case SL_DIAG_SEVERITY_WARNING:
        return sl_diag_literal("warning", sizeof("warning") - 1U);
    case SL_DIAG_SEVERITY_ERROR:
        return sl_diag_literal("error", sizeof("error") - 1U);
    case SL_DIAG_SEVERITY_FATAL:
        return sl_diag_literal("fatal", sizeof("fatal") - 1U);
    default:
        return sl_diag_literal("unknown", sizeof("unknown") - 1U);
    }
}

SlStr sl_diag_code_name(SlDiagCode code)
{
    switch (code) {
    case SL_DIAG_NONE:
        return sl_diag_literal("SLOPPY_NONE", sizeof("SLOPPY_NONE") - 1U);
    case SL_DIAG_INVALID_ARGUMENT:
        return sl_diag_literal("SLOPPY_E_INVALID_ARGUMENT",
                               sizeof("SLOPPY_E_INVALID_ARGUMENT") - 1U);
    case SL_DIAG_OUT_OF_MEMORY:
        return sl_diag_literal("SLOPPY_E_OUT_OF_MEMORY", sizeof("SLOPPY_E_OUT_OF_MEMORY") - 1U);
    case SL_DIAG_OVERFLOW:
        return sl_diag_literal("SLOPPY_E_OVERFLOW", sizeof("SLOPPY_E_OVERFLOW") - 1U);
    case SL_DIAG_INVALID_PLAN_VERSION:
        return sl_diag_literal("SLOPPY_E_INVALID_PLAN_VERSION",
                               sizeof("SLOPPY_E_INVALID_PLAN_VERSION") - 1U);
    case SL_DIAG_MISSING_SERVICE:
        return sl_diag_literal("SLOPPY_E_MISSING_SERVICE", sizeof("SLOPPY_E_MISSING_SERVICE") - 1U);
    case SL_DIAG_PERMISSION_DENIED:
        return sl_diag_literal("SLOPPY_E_PERMISSION_DENIED",
                               sizeof("SLOPPY_E_PERMISSION_DENIED") - 1U);
    case SL_DIAG_INTERNAL_ERROR:
        return sl_diag_literal("SLOPPY_E_INTERNAL", sizeof("SLOPPY_E_INTERNAL") - 1U);
    case SL_DIAG_INVALID_PLAN_FIELD:
        return sl_diag_literal("SLOPPY_E_INVALID_PLAN_FIELD",
                               sizeof("SLOPPY_E_INVALID_PLAN_FIELD") - 1U);
    case SL_DIAG_DUPLICATE_HANDLER_ID:
        return sl_diag_literal("SLOPPY_E_DUPLICATE_HANDLER_ID",
                               sizeof("SLOPPY_E_DUPLICATE_HANDLER_ID") - 1U);
    case SL_DIAG_MALFORMED_JSON:
        return sl_diag_literal("SLOPPY_E_MALFORMED_JSON", sizeof("SLOPPY_E_MALFORMED_JSON") - 1U);
    case SL_DIAG_UNSUPPORTED_ENGINE:
        return sl_diag_literal("SLOPPY_E_UNSUPPORTED_ENGINE",
                               sizeof("SLOPPY_E_UNSUPPORTED_ENGINE") - 1U);
    case SL_DIAG_ENGINE_EXCEPTION:
        return sl_diag_literal("SLOPPY_E_ENGINE_EXCEPTION",
                               sizeof("SLOPPY_E_ENGINE_EXCEPTION") - 1U);
    case SL_DIAG_ENGINE_COMPILE_ERROR:
        return sl_diag_literal("SLOPPY_E_ENGINE_COMPILE_ERROR",
                               sizeof("SLOPPY_E_ENGINE_COMPILE_ERROR") - 1U);
    case SL_DIAG_ENGINE_CALL_ERROR:
        return sl_diag_literal("SLOPPY_E_ENGINE_CALL_ERROR",
                               sizeof("SLOPPY_E_ENGINE_CALL_ERROR") - 1U);
    case SL_DIAG_ENGINE_PROMISE_REJECTION:
        return sl_diag_literal("SLOPPY_E_ENGINE_PROMISE_REJECTION",
                               sizeof("SLOPPY_E_ENGINE_PROMISE_REJECTION") - 1U);
    case SL_DIAG_ENGINE_PROMISE_PENDING:
        return sl_diag_literal("SLOPPY_E_ENGINE_PROMISE_PENDING",
                               sizeof("SLOPPY_E_ENGINE_PROMISE_PENDING") - 1U);
    case SL_DIAG_ENGINE_CANCELLED:
        return sl_diag_literal("SLOPPY_E_ENGINE_CANCELLED",
                               sizeof("SLOPPY_E_ENGINE_CANCELLED") - 1U);
    case SL_DIAG_ENGINE_BACKPRESSURE:
        return sl_diag_literal("SLOPPY_E_ENGINE_BACKPRESSURE",
                               sizeof("SLOPPY_E_ENGINE_BACKPRESSURE") - 1U);
    case SL_DIAG_INVALID_ROUTE_PATTERN:
        return sl_diag_literal("SLOPPY_E_INVALID_ROUTE_PATTERN",
                               sizeof("SLOPPY_E_INVALID_ROUTE_PATTERN") - 1U);
    case SL_DIAG_DUPLICATE_ROUTE_PARAM:
        return sl_diag_literal("SLOPPY_E_DUPLICATE_ROUTE_PARAM",
                               sizeof("SLOPPY_E_DUPLICATE_ROUTE_PARAM") - 1U);
    case SL_DIAG_INVALID_HTTP_REQUEST:
        return sl_diag_literal("SLOPPY_E_INVALID_HTTP_REQUEST",
                               sizeof("SLOPPY_E_INVALID_HTTP_REQUEST") - 1U);
    case SL_DIAG_HTTP_HEADER_LIMIT:
        return sl_diag_literal("SLOPPY_E_HTTP_HEADER_LIMIT",
                               sizeof("SLOPPY_E_HTTP_HEADER_LIMIT") - 1U);
    case SL_DIAG_HTTP_UNSUPPORTED_METHOD:
        return sl_diag_literal("SLOPPY_E_HTTP_UNSUPPORTED_METHOD",
                               sizeof("SLOPPY_E_HTTP_UNSUPPORTED_METHOD") - 1U);
    case SL_DIAG_HTTP_ROUTE_NOT_FOUND:
        return sl_diag_literal("SLOPPY_E_HTTP_ROUTE_NOT_FOUND",
                               sizeof("SLOPPY_E_HTTP_ROUTE_NOT_FOUND") - 1U);
    case SL_DIAG_SQLITE_PROVIDER_ERROR:
        return sl_diag_literal("SLOPPY_E_SQLITE_PROVIDER", sizeof("SLOPPY_E_SQLITE_PROVIDER") - 1U);
    case SL_DIAG_DATABASE_UNSUPPORTED_VALUE:
        return sl_diag_literal("SLOPPY_E_DATABASE_UNSUPPORTED_VALUE",
                               sizeof("SLOPPY_E_DATABASE_UNSUPPORTED_VALUE") - 1U);
    case SL_DIAG_POSTGRES_PROVIDER_ERROR:
        return sl_diag_literal("SLOPPY_E_POSTGRES_PROVIDER",
                               sizeof("SLOPPY_E_POSTGRES_PROVIDER") - 1U);
    case SL_DIAG_POSTGRES_POOL_EXHAUSTED:
        return sl_diag_literal("SLOPPY_E_POSTGRES_POOL_EXHAUSTED",
                               sizeof("SLOPPY_E_POSTGRES_POOL_EXHAUSTED") - 1U);
    case SL_DIAG_SQLSERVER_PROVIDER_ERROR:
        return sl_diag_literal("SLOPPY_E_SQLSERVER_PROVIDER",
                               sizeof("SLOPPY_E_SQLSERVER_PROVIDER") - 1U);
    case SL_DIAG_SQLSERVER_POOL_EXHAUSTED:
        return sl_diag_literal("SLOPPY_E_SQLSERVER_POOL_EXHAUSTED",
                               sizeof("SLOPPY_E_SQLSERVER_POOL_EXHAUSTED") - 1U);
    case SL_DIAG_RESOURCE_INVALID_ID:
        return sl_diag_literal("SLOPPY_E_RESOURCE_INVALID_ID",
                               sizeof("SLOPPY_E_RESOURCE_INVALID_ID") - 1U);
    case SL_DIAG_RESOURCE_STALE_ID:
        return sl_diag_literal("SLOPPY_E_RESOURCE_STALE_ID",
                               sizeof("SLOPPY_E_RESOURCE_STALE_ID") - 1U);
    case SL_DIAG_RESOURCE_WRONG_KIND:
        return sl_diag_literal("SLOPPY_E_RESOURCE_WRONG_KIND",
                               sizeof("SLOPPY_E_RESOURCE_WRONG_KIND") - 1U);
    case SL_DIAG_RESOURCE_CLOSED:
        return sl_diag_literal("SLOPPY_E_RESOURCE_CLOSED", sizeof("SLOPPY_E_RESOURCE_CLOSED") - 1U);
    case SL_DIAG_RESOURCE_TABLE_EXHAUSTED:
        return sl_diag_literal("SLOPPY_E_RESOURCE_TABLE_EXHAUSTED",
                               sizeof("SLOPPY_E_RESOURCE_TABLE_EXHAUSTED") - 1U);
    case SL_DIAG_DUPLICATE_ROUTE:
        return sl_diag_literal("SLOPPY_E_DUPLICATE_ROUTE", sizeof("SLOPPY_E_DUPLICATE_ROUTE") - 1U);
    case SL_DIAG_HTTP_UNSUPPORTED_BODY:
        return sl_diag_literal("SLOPPY_E_HTTP_UNSUPPORTED_BODY",
                               sizeof("SLOPPY_E_HTTP_UNSUPPORTED_BODY") - 1U);
    case SL_DIAG_INVALID_HTTP_RESULT:
        return sl_diag_literal("SLOPPY_E_INVALID_HTTP_RESULT",
                               sizeof("SLOPPY_E_INVALID_HTTP_RESULT") - 1U);
    default:
        return sl_diag_literal("SLOPPY_E_UNKNOWN", sizeof("SLOPPY_E_UNKNOWN") - 1U);
    }
}

SlStr sl_diag_redacted(void)
{
    return sl_diag_literal("<redacted>", sizeof("<redacted>") - 1U);
}

SlSourceSpan sl_source_span_unknown(void)
{
    SlSourceSpan span = {0};
    span.path = sl_str_empty();
    return span;
}

SlSourceSpan sl_source_span_make(SlStr path, size_t line, size_t column, size_t length)
{
    SlSourceSpan span = {0};

    span.path = path;
    span.line = line;
    span.column = column;
    span.length = length;
    span.has_location = line != 0U && column != 0U;

    return span;
}

SlStatus sl_diag_builder_init(SlDiagBuilder* builder, SlArena* arena, SlDiagSeverity severity,
                              SlDiagCode code, SlStr message)
{
    SlDiag diag = {0};
    SlStr copied_message = {0};
    SlStatus status;

    if (builder == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_diag_copy_str(arena, message, &copied_message);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    diag.severity = severity;
    diag.code = code;
    diag.message = copied_message;
    diag.primary_span = sl_source_span_unknown();

    builder->arena = arena;
    builder->diag = diag;
    return sl_status_ok();
}

SlStatus sl_diag_builder_set_primary_span(SlDiagBuilder* builder, SlSourceSpan span)
{
    SlSourceSpan copied_span;
    SlStatus status;

    if (builder == NULL || builder->arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_diag_copy_span(builder->arena, span, &copied_span);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    builder->diag.primary_span = copied_span;
    return sl_status_ok();
}

SlStatus sl_diag_builder_add_related(SlDiagBuilder* builder, SlSourceSpan span, SlStr message)
{
    SlDiagRelated related;
    SlArenaMark mark;
    SlStatus status;

    if (builder == NULL || builder->arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (builder->diag.related_count >= SL_DIAG_MAX_RELATED) {
        return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
    }

    mark = sl_arena_mark(builder->arena);
    status = sl_diag_copy_span(builder->arena, span, &related.span);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_copy_str(builder->arena, message, &related.message);
    if (!sl_status_is_ok(status)) {
        SlStatus reset_status = sl_arena_reset_to(builder->arena, mark);
        if (!sl_status_is_ok(reset_status)) {
            return reset_status;
        }

        return status;
    }

    builder->diag.related[builder->diag.related_count] = related;
    builder->diag.related_count += 1U;
    return sl_status_ok();
}

SlStatus sl_diag_builder_add_hint(SlDiagBuilder* builder, SlStr hint)
{
    SlStr copied_hint;
    SlStatus status;

    if (builder == NULL || builder->arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (builder->diag.hint_count >= SL_DIAG_MAX_HINTS) {
        return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
    }

    status = sl_diag_copy_str(builder->arena, hint, &copied_hint);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    builder->diag.hints[builder->diag.hint_count] = copied_hint;
    builder->diag.hint_count += 1U;
    return sl_status_ok();
}

SlStatus sl_diag_builder_finish(SlDiagBuilder* builder, SlDiag* out)
{
    if (builder == NULL || builder->arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out = builder->diag;
    return sl_status_ok();
}

SlStatus sl_diag_render_text(SlArena* arena, const SlDiag* diag, SlStr* out)
{
    SlStatus status;
    size_t length = 0U;
    size_t offset = 0U;
    size_t index = 0U;
    void* ptr = NULL;
    char* buffer = NULL;

    if (arena == NULL || diag == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_diag_render_len(diag, &length);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_arena_alloc(arena, length, 1U, &ptr);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    buffer = (char*)ptr;
    sl_diag_write_str(buffer, &offset, sl_diag_severity_name(diag->severity));
    buffer[offset] = ' ';
    offset += 1U;
    sl_diag_write_str(buffer, &offset, sl_diag_code_name(diag->code));
    sl_diag_write_str(buffer, &offset, sl_diag_literal(": ", 2U));
    sl_diag_write_str(buffer, &offset, diag->message);
    buffer[offset] = '\n';
    offset += 1U;

    if (diag->primary_span.has_location || diag->primary_span.path.length != 0U) {
        sl_diag_write_str(buffer, &offset, sl_diag_literal("\n  at ", 6U));
        sl_diag_write_span(buffer, &offset, diag->primary_span);
        buffer[offset] = '\n';
        offset += 1U;
    }

    if (diag->related_count != 0U) {
        sl_diag_write_str(buffer, &offset, sl_diag_literal("\n  related:\n", 12U));
        for (index = 0U; index < diag->related_count; index += 1U) {
            sl_diag_write_str(buffer, &offset, sl_diag_literal("    ", 4U));
            sl_diag_write_span(buffer, &offset, diag->related[index].span);
            sl_diag_write_str(buffer, &offset, sl_diag_literal(": ", 2U));
            sl_diag_write_str(buffer, &offset, diag->related[index].message);
            buffer[offset] = '\n';
            offset += 1U;
        }
    }

    if (diag->hint_count != 0U) {
        sl_diag_write_str(buffer, &offset, sl_diag_literal("\n  help:\n", 9U));
        for (index = 0U; index < diag->hint_count; index += 1U) {
            sl_diag_write_str(buffer, &offset, sl_diag_literal("    ", 4U));
            sl_diag_write_str(buffer, &offset, diag->hints[index]);
            buffer[offset] = '\n';
            offset += 1U;
        }
    }

    *out = sl_str_from_parts(buffer, offset);
    return sl_status_ok();
}

static bool sl_diag_source_matches(const SlDiag* diag, const SlDiagSource* source)
{
    if (diag == NULL || source == NULL || !diag->primary_span.has_location ||
        sl_str_is_empty(source->text))
    {
        return false;
    }
    if (sl_str_is_empty(diag->primary_span.path) || sl_str_is_empty(source->path)) {
        return true;
    }
    return sl_str_equal(diag->primary_span.path, source->path);
}

static bool sl_diag_source_line(SlStr source, size_t line, SlStr* out)
{
    size_t current_line = 1U;
    size_t start = 0U;
    size_t index = 0U;

    if (out == NULL || line == 0U || !sl_diag_str_is_valid(source)) {
        return false;
    }
    while (index <= source.length) {
        if (index == source.length || source.ptr[index] == '\n') {
            size_t end = index;
            if (end > start && source.ptr[end - 1U] == '\r') {
                end -= 1U;
            }
            if (current_line == line) {
                *out = sl_str_from_parts(source.ptr + start, end - start);
                return true;
            }
            current_line += 1U;
            start = index + 1U;
        }
        index += 1U;
    }
    return false;
}

static SlStatus sl_diag_frame_render_len(size_t* total, const SlDiag* diag, SlStr line)
{
    SlStatus status;
    const size_t line_number = diag->primary_span.line;
    const size_t underline = diag->primary_span.length == 0U ? 1U : diag->primary_span.length;
    SlStr hint = sl_diag_first_hint(diag);

    status = sl_diag_add_len(total, 6U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_span_render_len(total, diag->primary_span);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_add_len(total, 9U + sl_diag_decimal_len(line_number) + line.length);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_add_len(total, 7U + diag->primary_span.column - 1U + underline);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!sl_str_is_empty(hint)) {
        status = sl_diag_add_len(total, 1U + hint.length);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_diag_add_len(total, 1U);
}

static void sl_diag_write_frame(char* buffer, size_t* offset, const SlDiag* diag, SlStr line)
{
    const size_t underline = diag->primary_span.length == 0U ? 1U : diag->primary_span.length;
    SlStr hint = sl_diag_first_hint(diag);

    sl_diag_write_str(buffer, offset, sl_diag_literal("  --> ", 6U));
    sl_diag_write_span(buffer, offset, diag->primary_span);
    buffer[*offset] = '\n';
    *offset += 1U;
    sl_diag_write_str(buffer, offset, sl_diag_literal("   |\n", 5U));
    buffer[*offset] = ' ';
    *offset += 1U;
    sl_diag_write_size(buffer, offset, diag->primary_span.line);
    sl_diag_write_str(buffer, offset, sl_diag_literal(" | ", 3U));
    sl_diag_write_str(buffer, offset, line);
    buffer[*offset] = '\n';
    *offset += 1U;
    sl_diag_write_str(buffer, offset, sl_diag_literal("   | ", 5U));
    sl_diag_write_char_repeat(buffer, offset, ' ', diag->primary_span.column - 1U);
    sl_diag_write_char_repeat(buffer, offset, '^', underline);
    if (!sl_str_is_empty(hint)) {
        buffer[*offset] = ' ';
        *offset += 1U;
        sl_diag_write_str(buffer, offset, hint);
    }
    buffer[*offset] = '\n';
    *offset += 1U;
}

SlStatus sl_diag_render_text_with_source(SlArena* arena, const SlDiag* diag,
                                         const SlDiagSource* source, SlStr* out)
{
    SlStatus status;
    SlStr line = sl_str_empty();
    size_t length = 0U;
    size_t offset = 0U;
    void* ptr = NULL;
    char* buffer = NULL;

    if (!sl_diag_source_matches(diag, source) ||
        !sl_diag_source_line(source->text, diag->primary_span.line, &line))
    {
        return sl_diag_render_text(arena, diag, out);
    }

    status = sl_diag_header_render_len(&length, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_add_len(&length, 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_frame_render_len(&length, diag, line);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, length, 1U, &ptr);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    buffer = (char*)ptr;
    sl_diag_write_str(buffer, &offset, sl_diag_severity_name(diag->severity));
    buffer[offset] = ' ';
    offset += 1U;
    sl_diag_write_str(buffer, &offset, sl_diag_code_name(diag->code));
    sl_diag_write_str(buffer, &offset, sl_diag_literal(": ", 2U));
    sl_diag_write_str(buffer, &offset, diag->message);
    sl_diag_write_str(buffer, &offset, sl_diag_literal("\n\n", 2U));
    sl_diag_write_frame(buffer, &offset, diag, line);

    *out = sl_str_from_parts(buffer, offset);
    return sl_status_ok();
}

static size_t sl_diag_json_escaped_len(SlStr value)
{
    size_t length = 2U;
    size_t index = 0U;

    for (index = 0U; index < value.length; index += 1U) {
        unsigned char ch = (unsigned char)value.ptr[index];
        if (ch == '"' || ch == '\\' || ch == '\n' || ch == '\r' || ch == '\t') {
            length += 2U;
        }
        else if (ch < 0x20U) {
            length += 6U;
        }
        else {
            length += 1U;
        }
    }
    return length;
}

static char sl_diag_json_escape_letter(unsigned char ch)
{
    if (ch == '\n') {
        return 'n';
    }
    if (ch == '\r') {
        return 'r';
    }
    return 't';
}

static void sl_diag_json_write_escaped(char* buffer, size_t* offset, SlStr value)
{
    static const char hex[] = "0123456789abcdef";
    size_t index = 0U;

    buffer[*offset] = '"';
    *offset += 1U;
    for (index = 0U; index < value.length; index += 1U) {
        unsigned char ch = (unsigned char)value.ptr[index];
        if (ch == '"' || ch == '\\') {
            buffer[*offset] = '\\';
            buffer[*offset + 1U] = (char)ch;
            *offset += 2U;
        }
        else if (ch == '\n' || ch == '\r' || ch == '\t') {
            buffer[*offset] = '\\';
            buffer[*offset + 1U] = sl_diag_json_escape_letter(ch);
            *offset += 2U;
        }
        else if (ch < 0x20U) {
            sl_diag_write_str(buffer, offset, sl_diag_literal("\\u00", 4U));
            buffer[*offset] = hex[(ch >> 4U) & 0xFU];
            buffer[*offset + 1U] = hex[ch & 0xFU];
            *offset += 2U;
        }
        else {
            buffer[*offset] = (char)ch;
            *offset += 1U;
        }
    }
    buffer[*offset] = '"';
    *offset += 1U;
}

static SlStatus sl_diag_json_span_len(size_t* total, SlSourceSpan span)
{
    SlStatus status = sl_diag_add_len(total, 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!sl_str_is_empty(span.path)) {
        status =
            sl_diag_add_len(total, sizeof("\"file\":") - 1U + sl_diag_json_escaped_len(span.path));
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    if (span.has_location) {
        if (!sl_str_is_empty(span.path)) {
            status = sl_diag_add_len(total, 1U);
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
        status = sl_diag_add_len(total, sizeof("\"line\":") - 1U + sl_diag_decimal_len(span.line) +
                                            1U + sizeof("\"column\":") - 1U +
                                            sl_diag_decimal_len(span.column));
        if (!sl_status_is_ok(status) || span.length == 0U) {
            return status;
        }
        status = sl_diag_add_len(total,
                                 1U + sizeof("\"span\":") - 1U + sl_diag_decimal_len(span.length));
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_diag_add_len(total, 1U);
}

static void sl_diag_json_write_span(char* buffer, size_t* offset, SlSourceSpan span)
{
    bool need_comma = false;

    buffer[*offset] = '{';
    *offset += 1U;
    if (!sl_str_is_empty(span.path)) {
        sl_diag_write_str(buffer, offset, sl_diag_literal("\"file\":", sizeof("\"file\":") - 1U));
        sl_diag_json_write_escaped(buffer, offset, span.path);
        need_comma = true;
    }
    if (span.has_location) {
        if (need_comma) {
            buffer[*offset] = ',';
            *offset += 1U;
        }
        sl_diag_write_str(buffer, offset, sl_diag_literal("\"line\":", sizeof("\"line\":") - 1U));
        sl_diag_write_size(buffer, offset, span.line);
        sl_diag_write_str(buffer, offset,
                          sl_diag_literal(",\"column\":", sizeof(",\"column\":") - 1U));
        sl_diag_write_size(buffer, offset, span.column);
        if (span.length != 0U) {
            sl_diag_write_str(buffer, offset,
                              sl_diag_literal(",\"span\":", sizeof(",\"span\":") - 1U));
            sl_diag_write_size(buffer, offset, span.length);
        }
    }
    buffer[*offset] = '}';
    *offset += 1U;
}

static SlStatus sl_diag_json_base_len(size_t* total, const SlDiag* diag)
{
    return sl_diag_add_len(
        total, sizeof("{\"code\":") - 1U + sl_diag_json_escaped_len(sl_diag_code_name(diag->code)) +
                   sizeof(",\"severity\":") - 1U +
                   sl_diag_json_escaped_len(sl_diag_severity_name(diag->severity)) +
                   sizeof(",\"message\":") - 1U + sl_diag_json_escaped_len(diag->message));
}

static SlStatus sl_diag_json_primary_len(size_t* total, const SlDiag* diag)
{
    SlStatus status;

    if (!sl_diag_has_span(diag->primary_span)) {
        return sl_status_ok();
    }
    status = sl_diag_add_len(total, sizeof(",\"primary\":") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_diag_json_span_len(total, diag->primary_span);
}

static SlStatus sl_diag_json_related_len(size_t* total, const SlDiag* diag)
{
    SlStatus status;
    size_t index = 0U;

    if (diag->related_count == 0U) {
        return sl_status_ok();
    }
    status = sl_diag_add_len(total, sizeof(",\"related\":[") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (index = 0U; index < diag->related_count; index += 1U) {
        if (!sl_diag_str_is_valid(diag->related[index].message)) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        if (index != 0U) {
            status = sl_diag_add_len(total, 1U);
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
        status = sl_diag_add_len(total, sizeof("{\"message\":") - 1U +
                                            sl_diag_json_escaped_len(diag->related[index].message) +
                                            sizeof(",\"span\":") - 1U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_diag_json_span_len(total, diag->related[index].span);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_diag_add_len(total, 1U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_diag_add_len(total, 1U);
}

static SlStatus sl_diag_json_hints_len(size_t* total, const SlDiag* diag)
{
    SlStatus status;
    size_t index = 0U;

    if (diag->hint_count == 0U) {
        return sl_status_ok();
    }
    status = sl_diag_add_len(total, sizeof(",\"hints\":[") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (index = 0U; index < diag->hint_count; index += 1U) {
        if (!sl_diag_str_is_valid(diag->hints[index])) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        status = sl_diag_add_len(total, (index == 0U ? 0U : 1U) +
                                            sl_diag_json_escaped_len(diag->hints[index]));
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_diag_add_len(total, 1U);
}

static SlStatus sl_diag_json_render_len(size_t* total, const SlDiag* diag)
{
    SlStatus status;

    if (total == NULL || diag == NULL || !sl_diag_str_is_valid(diag->message) ||
        diag->related_count > SL_DIAG_MAX_RELATED || diag->hint_count > SL_DIAG_MAX_HINTS)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_diag_json_base_len(total, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_json_primary_len(total, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_json_related_len(total, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_json_hints_len(total, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_diag_add_len(total, 2U);
}

static void sl_diag_json_write_base(char* buffer, size_t* offset, const SlDiag* diag)
{
    sl_diag_write_str(buffer, offset, sl_diag_literal("{\"code\":", sizeof("{\"code\":") - 1U));
    sl_diag_json_write_escaped(buffer, offset, sl_diag_code_name(diag->code));
    sl_diag_write_str(buffer, offset,
                      sl_diag_literal(",\"severity\":", sizeof(",\"severity\":") - 1U));
    sl_diag_json_write_escaped(buffer, offset, sl_diag_severity_name(diag->severity));
    sl_diag_write_str(buffer, offset,
                      sl_diag_literal(",\"message\":", sizeof(",\"message\":") - 1U));
    sl_diag_json_write_escaped(buffer, offset, diag->message);
}

static void sl_diag_json_write_primary(char* buffer, size_t* offset, const SlDiag* diag)
{
    if (sl_diag_has_span(diag->primary_span)) {
        sl_diag_write_str(buffer, offset,
                          sl_diag_literal(",\"primary\":", sizeof(",\"primary\":") - 1U));
        sl_diag_json_write_span(buffer, offset, diag->primary_span);
    }
}

static void sl_diag_json_write_related(char* buffer, size_t* offset, const SlDiag* diag)
{
    size_t index = 0U;

    if (diag->related_count != 0U) {
        sl_diag_write_str(buffer, offset,
                          sl_diag_literal(",\"related\":[", sizeof(",\"related\":[") - 1U));
        for (index = 0U; index < diag->related_count; index += 1U) {
            if (index != 0U) {
                buffer[*offset] = ',';
                *offset += 1U;
            }
            sl_diag_write_str(buffer, offset,
                              sl_diag_literal("{\"message\":", sizeof("{\"message\":") - 1U));
            sl_diag_json_write_escaped(buffer, offset, diag->related[index].message);
            sl_diag_write_str(buffer, offset,
                              sl_diag_literal(",\"span\":", sizeof(",\"span\":") - 1U));
            sl_diag_json_write_span(buffer, offset, diag->related[index].span);
            buffer[*offset] = '}';
            *offset += 1U;
        }
        buffer[*offset] = ']';
        *offset += 1U;
    }
}

static void sl_diag_json_write_hints(char* buffer, size_t* offset, const SlDiag* diag)
{
    size_t index = 0U;

    if (diag->hint_count != 0U) {
        sl_diag_write_str(buffer, offset,
                          sl_diag_literal(",\"hints\":[", sizeof(",\"hints\":[") - 1U));
        for (index = 0U; index < diag->hint_count; index += 1U) {
            if (index != 0U) {
                buffer[*offset] = ',';
                *offset += 1U;
            }
            sl_diag_json_write_escaped(buffer, offset, diag->hints[index]);
        }
        buffer[*offset] = ']';
        *offset += 1U;
    }
}

SlStatus sl_diag_render_json(SlArena* arena, const SlDiag* diag, SlStr* out)
{
    SlStatus status;
    size_t length = 0U;
    size_t offset = 0U;
    void* ptr = NULL;
    char* buffer = NULL;

    if (arena == NULL || diag == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_diag_json_render_len(&length, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, length, 1U, &ptr);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    buffer = (char*)ptr;
    sl_diag_json_write_base(buffer, &offset, diag);
    sl_diag_json_write_primary(buffer, &offset, diag);
    sl_diag_json_write_related(buffer, &offset, diag);
    sl_diag_json_write_hints(buffer, &offset, diag);
    sl_diag_write_str(buffer, &offset, sl_diag_literal("}\n", 2U));
    *out = sl_str_from_parts(buffer, offset);
    return sl_status_ok();
}

static bool sl_diag_ascii_equal_ci(char actual, char expected_lower)
{
    if (actual >= 'A' && actual <= 'Z') {
        actual = (char)(actual - 'A' + 'a');
    }
    return actual == expected_lower;
}

static bool sl_diag_secret_word_at(SlStr text, size_t index, const char* word)
{
    size_t offset = 0U;

    while (word[offset] != '\0') {
        if (index + offset >= text.length ||
            !sl_diag_ascii_equal_ci(text.ptr[index + offset], word[offset]))
        {
            return false;
        }
        offset += 1U;
    }
    return true;
}

static bool sl_diag_secret_key_at(SlStr text, size_t index, size_t* out_separator)
{
    static const char* keys[] = {"password", "pwd", "token", "secret", "api_key", "apikey"};
    size_t key_index = 0U;

    for (key_index = 0U; key_index < sizeof(keys) / sizeof(keys[0]); key_index += 1U) {
        size_t end = index;
        if (!sl_diag_secret_word_at(text, index, keys[key_index])) {
            continue;
        }
        while (end < text.length && text.ptr[end] != '=' && text.ptr[end] != ':') {
            if ((text.ptr[end] == ' ' || text.ptr[end] == '\t') && end > index) {
                end += 1U;
                continue;
            }
            if (text.ptr[end] == ';' || text.ptr[end] == '&' || text.ptr[end] == '\n' ||
                text.ptr[end] == '\r')
            {
                break;
            }
            end += 1U;
        }
        if (end < text.length && (text.ptr[end] == '=' || text.ptr[end] == ':')) {
            if (out_separator != NULL) {
                *out_separator = end;
            }
            return true;
        }
    }
    return false;
}

static size_t sl_diag_redact_value(char* dst, size_t length, size_t index)
{
    while (index < length &&
           (dst[index] == ' ' || dst[index] == '\t' || dst[index] == '=' || dst[index] == ':'))
    {
        index += 1U;
    }
    if (index < length && (dst[index] == '\'' || dst[index] == '"' || dst[index] == '{')) {
        char quote = dst[index];
        if (dst[index] == '{') {
            quote = '}';
        }
        dst[index] = '*';
        index += 1U;
        while (index < length) {
            if (dst[index] == quote) {
                dst[index] = '*';
                return index + 1U;
            }
            dst[index] = '*';
            index += 1U;
        }
        return index;
    }
    while (index < length && dst[index] != ';' && dst[index] != '&' && dst[index] != ' ' &&
           dst[index] != '\t' && dst[index] != '\n' && dst[index] != '\r')
    {
        dst[index] = '*';
        index += 1U;
    }
    return index;
}

static void sl_diag_redact_uri_userinfo(char* dst, SlStr input)
{
    size_t index = 0U;

    while (index + 3U < input.length) {
        if (input.ptr[index] == ':' && input.ptr[index + 1U] == '/' && input.ptr[index + 2U] == '/')
        {
            size_t cursor = index + 3U;
            size_t colon = input.length;
            size_t at = input.length;

            while (cursor < input.length && input.ptr[cursor] != '/' && input.ptr[cursor] != ' ' &&
                   input.ptr[cursor] != '\t' && input.ptr[cursor] != '\n' &&
                   input.ptr[cursor] != '\r')
            {
                if (input.ptr[cursor] == ':' && colon == input.length) {
                    colon = cursor;
                }
                if (input.ptr[cursor] == '@') {
                    at = cursor;
                    break;
                }
                cursor += 1U;
            }
            if (colon < at && at < input.length) {
                for (cursor = colon + 1U; cursor < at; cursor += 1U) {
                    dst[cursor] = '*';
                }
            }
        }
        index += 1U;
    }
}

SlStatus sl_diag_redact_secrets(SlArena* arena, SlStr input, SlStr* out)
{
    void* ptr = NULL;
    char* dst = NULL;
    size_t index = 0U;
    SlStatus status;

    if (arena == NULL || out == NULL || !sl_diag_str_is_valid(input)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (input.length == 0U) {
        *out = sl_str_empty();
        return sl_status_ok();
    }
    status = sl_arena_alloc(arena, input.length, 1U, &ptr);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    dst = (char*)ptr;
    for (index = 0U; index < input.length; index += 1U) {
        dst[index] = input.ptr[index];
    }
    for (index = 0U; index < input.length; index += 1U) {
        size_t separator = 0U;
        if (sl_diag_secret_key_at(input, index, &separator)) {
            index = sl_diag_redact_value(dst, input.length, separator);
        }
    }
    sl_diag_redact_uri_userinfo(dst, input);
    *out = sl_str_from_parts(dst, input.length);
    return sl_status_ok();
}
