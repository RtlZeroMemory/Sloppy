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
 * - no platform, V8, terminal, JSON, source-map, or localization behavior appears here.
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
