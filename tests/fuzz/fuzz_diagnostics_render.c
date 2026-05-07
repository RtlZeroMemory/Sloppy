#include "sloppy/diagnostics.h"

#include "fuzz_support.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define FUZZ_DIAG_ARENA_SIZE 65536U

static SlStr slice_to_str(const uint8_t* data, size_t size, size_t offset, size_t length)
{
    if (offset >= size) {
        return sl_str_empty();
    }

    if (length > size - offset) {
        length = size - offset;
    }

    return sl_str_from_parts((const char*)data + offset, length);
}

static bool contains_secret_marker(SlStr text)
{
    static const char marker[] = "SECRET_SHOULD_NOT_APPEAR";
    size_t index = 0U;
    size_t marker_index = 0U;
    const size_t marker_length = sizeof(marker) - 1U;

    if (text.ptr == NULL || text.length < marker_length) {
        return false;
    }

    for (index = 0U; index <= text.length - marker_length; index += 1U) {
        for (marker_index = 0U; marker_index < marker_length; marker_index += 1U) {
            if (text.ptr[index + marker_index] != marker[marker_index]) {
                break;
            }
        }
        if (marker_index == marker_length) {
            return true;
        }
    }

    return false;
}

static SlStr sanitize_secret_marker(SlArena* arena, SlStr text)
{
    SlStr redacted = {0};

    if (!contains_secret_marker(text)) {
        return text;
    }

    if (sl_status_is_ok(sl_diag_redact_secrets(arena, text, &redacted)) &&
        !contains_secret_marker(redacted))
    {
        return redacted;
    }

    return sl_diag_redacted();
}

static void assert_no_secret_marker(SlStr rendered)
{
    if (contains_secret_marker(rendered)) {
        abort();
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    unsigned char arena_storage[FUZZ_DIAG_ARENA_SIZE];
    SlArena arena = {0};
    SlDiagBuilder builder = {0};
    SlDiag diag = {0};
    SlDiagSource source = {0};
    SlStr rendered = {0};
    SlDiagSeverity severity = SL_DIAG_SEVERITY_ERROR;
    SlDiagCode code = SL_DIAG_INVALID_ARGUMENT;
    SlStr message = {0};
    SlStr path = {0};
    SlStr hint = {0};
    SlStr source_text = {0};

    if (data == NULL || size == 0U) {
        return 0;
    }

    if (!sl_status_is_ok(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)))) {
        return 0;
    }

    severity = (SlDiagSeverity)(data[0] % 4U);
    code = (SlDiagCode)(data[0] % ((uint8_t)SL_DIAG_WORKER_STALE_HANDLE + 1U));
    message = slice_to_str(data, size, 0U, size);
    path = slice_to_str(data, size, 1U, size > 1U ? size - 1U : 0U);
    hint = slice_to_str(data, size, 2U, size > 2U ? size - 2U : 0U);
    source_text = slice_to_str(data, size, 3U, size > 3U ? size - 3U : 0U);

    if (message.length == 0U) {
        message = sl_str_from_cstr("fuzz diagnostic");
    }
    message = sanitize_secret_marker(&arena, message);
    path = sanitize_secret_marker(&arena, path);
    hint = sanitize_secret_marker(&arena, hint);
    source_text = sanitize_secret_marker(&arena, source_text);

    if (!sl_status_is_ok(sl_diag_builder_init(&builder, &arena, severity, code, message))) {
        return 0;
    }

    (void)sl_diag_builder_set_primary_span(&builder, sl_source_span_make(path, 1U, 1U, 1U));
    if (hint.length > 0U) {
        (void)sl_diag_builder_add_hint(&builder, hint);
    }

    if (!sl_status_is_ok(sl_diag_builder_finish(&builder, &diag))) {
        return 0;
    }

    (void)sl_diag_render_text(&arena, &diag, &rendered);
    assert_no_secret_marker(rendered);
    (void)sl_diag_render_json(&arena, &diag, &rendered);
    assert_no_secret_marker(rendered);

    source.path = path;
    source.text = source_text;
    (void)sl_diag_render_text_with_source(&arena, &diag, &source, &rendered);
    assert_no_secret_marker(rendered);
    (void)sl_diag_render_json_with_source(&arena, &diag, &source, &rendered);
    assert_no_secret_marker(rendered);

    return 0;
}
