#include "sloppy/json_writer.h"

#include "sloppy/builder.h"
#include "sloppy/checked_math.h"
#include "sloppy/json_profile.h"

#include <math.h>
#include <stdbool.h>

static SlStatus sl_json_writer_reserve(SlJsonWriter* writer, size_t additional)
{
    if (writer == NULL || (writer->data == NULL && writer->capacity != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (additional > writer->capacity || writer->length > writer->capacity - additional) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }
    return sl_status_ok();
}

SlStatus sl_json_writer_init_fixed(SlJsonWriter* writer, unsigned char* buffer, size_t capacity)
{
    if (writer == NULL || (buffer == NULL && capacity != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *writer = (SlJsonWriter){.data = buffer, .capacity = capacity};
    return sl_status_ok();
}

SlBytes sl_json_writer_view(const SlJsonWriter* writer)
{
    if (writer == NULL) {
        return (SlBytes){0};
    }
    return sl_bytes_from_parts(writer->data, writer->length);
}

SlStatus sl_json_writer_write_bytes(SlJsonWriter* writer, SlBytes bytes)
{
    SlStatus status;

    if (writer == NULL || (bytes.ptr == NULL && bytes.length != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_json_writer_reserve(writer, bytes.length);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (bytes.length != 0U) {
        for (size_t index = 0U; index < bytes.length; index += 1U) {
            writer->data[writer->length + index] = bytes.ptr[index];
        }
    }
    writer->length += bytes.length;
    return sl_status_ok();
}

SlStatus sl_json_writer_write_str(SlJsonWriter* writer, SlStr text)
{
    return sl_json_writer_write_bytes(
        writer, sl_bytes_from_parts((const unsigned char*)text.ptr, text.length));
}

SlStatus sl_json_writer_write_char(SlJsonWriter* writer, char value)
{
    SlStatus status = sl_json_writer_reserve(writer, 1U);

    if (!sl_status_is_ok(status)) {
        return status;
    }
    writer->data[writer->length] = (unsigned char)value;
    writer->length += 1U;
    return sl_status_ok();
}

static bool sl_json_writer_string_needs_escape(SlStr text)
{
    for (size_t index = 0U; index < text.length; index += 1U) {
        unsigned char ch = (unsigned char)text.ptr[index];
        if (ch == '"' || ch == '\\' || ch < 0x20U) {
            return true;
        }
    }
    return false;
}

static bool sl_json_writer_named_escape_allowed(unsigned char ch)
{
    return ch == '\b' || ch == '\f' || ch == '\n' || ch == '\r' || ch == '\t';
}

static SlStatus sl_json_writer_escaped_string_length_with_mode(SlStr text, bool codepoint_controls,
                                                               size_t* out_length)
{
    size_t length = 2U;

    if (out_length == NULL || (text.ptr == NULL && text.length != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (size_t index = 0U; index < text.length; index += 1U) {
        unsigned char ch = (unsigned char)text.ptr[index];
        size_t additional = 1U;

        if (ch == '"' || ch == '\\' ||
            (sl_json_writer_named_escape_allowed(ch) && !codepoint_controls))
        {
            additional = 2U;
        }
        else if (ch < 0x20U) {
            additional = 6U;
        }
        SlStatus status = sl_checked_add_size(length, additional, &length);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    *out_length = length;
    return sl_status_ok();
}

SlStatus sl_json_writer_escaped_string_length(SlStr text, size_t* out_length)
{
    return sl_json_writer_escaped_string_length_with_mode(text, false, out_length);
}

SlStatus sl_json_writer_escaped_string_codepoint_controls_length(SlStr text, size_t* out_length)
{
    return sl_json_writer_escaped_string_length_with_mode(text, true, out_length);
}

static SlStatus sl_json_writer_sink_codepoint_escape(SlJsonWriterSink sink, void* user,
                                                     unsigned char ch)
{
    static const char hex[] = "0123456789abcdef";
    unsigned char escaped[6] = {'\\', 'u', '0', '0', 0, 0};

    if (sink == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    escaped[4] = (unsigned char)hex[(ch >> 4U) & 0x0FU];
    escaped[5] = (unsigned char)hex[ch & 0x0FU];
    return sink(user, sl_bytes_from_parts(escaped, sizeof(escaped)));
}

static char sl_json_writer_escape_letter(unsigned char ch)
{
    switch (ch) {
    case '\b':
        return 'b';
    case '\f':
        return 'f';
    case '\n':
        return 'n';
    case '\r':
        return 'r';
    case '\t':
        return 't';
    default:
        return '\0';
    }
}

static SlStatus sl_json_writer_sink_cstr(SlJsonWriterSink sink, void* user, const char* text,
                                         size_t length)
{
    if (sink == NULL || text == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sink(user, sl_bytes_from_parts((const unsigned char*)text, length));
}

static SlStatus sl_json_writer_sink_str(SlJsonWriterSink sink, void* user, SlStr text)
{
    if (sink == NULL || (text.ptr == NULL && text.length != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sink(user, sl_bytes_from_parts((const unsigned char*)text.ptr, text.length));
}

static SlStatus sl_json_writer_sink_escaped_string_with_mode(SlStr text, bool codepoint_controls,
                                                             bool profile_enabled,
                                                             SlJsonWriterSink sink, void* user)
{
    uint64_t scan_start =
        profile_enabled ? sl_json_profile_phase_begin(SL_JSON_PROFILE_PHASE_STRING_ESCAPE_SCAN)
                        : 0U;
    bool needs_escape = false;
    uint64_t write_start = 0U;
    SlStatus status;

    if (sink == NULL || (text.ptr == NULL && text.length != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    needs_escape = sl_json_writer_string_needs_escape(text);
    if (profile_enabled) {
        sl_json_profile_phase_end(SL_JSON_PROFILE_PHASE_STRING_ESCAPE_SCAN, scan_start);
        sl_json_profile_counter_add(needs_escape
                                        ? SL_JSON_PROFILE_COUNTER_STRINGS_ESCAPED
                                        : SL_JSON_PROFILE_COUNTER_STRINGS_FAST_PATH_NO_ESCAPE,
                                    1U);
    }

    write_start = profile_enabled
                      ? sl_json_profile_phase_begin(SL_JSON_PROFILE_PHASE_STRING_ESCAPE_WRITE)
                      : 0U;
    status = sl_json_writer_sink_cstr(sink, user, "\"", 1U);
    if (!sl_status_is_ok(status)) {
        goto done;
    }
    if (!needs_escape) {
        status = sl_json_writer_sink_str(sink, user, text);
        if (sl_status_is_ok(status)) {
            status = sl_json_writer_sink_cstr(sink, user, "\"", 1U);
        }
        goto done;
    }

    size_t run_start = 0U;
    for (size_t index = 0U; index < text.length; index += 1U) {
        unsigned char ch = (unsigned char)text.ptr[index];
        const char* escaped = NULL;
        size_t escaped_length = 0U;

        switch (ch) {
        case '"':
            escaped = "\\\"";
            escaped_length = 2U;
            break;
        case '\\':
            escaped = "\\\\";
            escaped_length = 2U;
            break;
        case '\b':
        case '\f':
        case '\n':
        case '\r':
        case '\t':
            if (!codepoint_controls) {
                char escape[2] = {'\\', sl_json_writer_escape_letter(ch)};
                if (run_start < index) {
                    status = sl_json_writer_sink_str(
                        sink, user, sl_str_from_parts(text.ptr + run_start, index - run_start));
                    if (!sl_status_is_ok(status)) {
                        goto done;
                    }
                }
                status =
                    sink(user, sl_bytes_from_parts((const unsigned char*)escape, sizeof(escape)));
                if (!sl_status_is_ok(status)) {
                    goto done;
                }
                run_start = index + 1U;
                continue;
            }
            break;
        default:
            if (ch >= 0x20U) {
                continue;
            }
            break;
        }

        if (run_start < index) {
            status = sl_json_writer_sink_str(
                sink, user, sl_str_from_parts(text.ptr + run_start, index - run_start));
            if (!sl_status_is_ok(status)) {
                goto done;
            }
        }
        if (escaped != NULL) {
            status = sl_json_writer_sink_cstr(sink, user, escaped, escaped_length);
        }
        else {
            status = sl_json_writer_sink_codepoint_escape(sink, user, ch);
        }
        if (!sl_status_is_ok(status)) {
            goto done;
        }
        run_start = index + 1U;
    }
    if (run_start < text.length) {
        status = sl_json_writer_sink_str(
            sink, user, sl_str_from_parts(text.ptr + run_start, text.length - run_start));
        if (!sl_status_is_ok(status)) {
            goto done;
        }
    }
    status = sl_json_writer_sink_cstr(sink, user, "\"", 1U);

done:
    if (profile_enabled) {
        sl_json_profile_phase_end(SL_JSON_PROFILE_PHASE_STRING_ESCAPE_WRITE, write_start);
    }
    return status;
}

static SlStatus sl_json_writer_string_builder_sink(void* user, SlBytes bytes)
{
    SlStringBuilder* builder = (SlStringBuilder*)user;

    if (builder == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_string_builder_append_str(builder,
                                        sl_str_from_parts((const char*)bytes.ptr, bytes.length));
}

static SlStatus sl_json_writer_append_escaped_string_with_mode(SlStringBuilder* builder, SlStr text,
                                                               bool codepoint_controls,
                                                               bool profile_enabled)
{
    if (builder == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_json_writer_sink_escaped_string_with_mode(
        text, codepoint_controls, profile_enabled, sl_json_writer_string_builder_sink, builder);
}

SlStatus sl_json_writer_write_escaped_string_to(SlStr text, SlJsonWriterSink sink, void* user)
{
    return sl_json_writer_sink_escaped_string_with_mode(text, false, false, sink, user);
}

SlStatus sl_json_writer_write_escaped_string_codepoint_controls_to(SlStr text,
                                                                   SlJsonWriterSink sink,
                                                                   void* user)
{
    return sl_json_writer_sink_escaped_string_with_mode(text, true, false, sink, user);
}

SlStatus sl_json_writer_append_escaped_string(SlStringBuilder* builder, SlStr text)
{
    return sl_json_writer_append_escaped_string_with_mode(builder, text, false, false);
}

SlStatus sl_json_writer_append_escaped_string_profiled(SlStringBuilder* builder, SlStr text)
{
    return sl_json_writer_append_escaped_string_with_mode(builder, text, false,
                                                          sl_json_profile_enabled());
}

SlStatus sl_json_writer_append_escaped_string_codepoint_controls(SlStringBuilder* builder,
                                                                 SlStr text)
{
    return sl_json_writer_append_escaped_string_with_mode(builder, text, true, false);
}

SlStatus sl_json_writer_append_escaped_string_bytes(SlByteBuilder* builder, SlStr text)
{
    SlStringBuilder string_builder = {0};
    SlStatus status;

    if (builder == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    string_builder.bytes = *builder;
    status = sl_json_writer_append_escaped_string(&string_builder, text);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *builder = string_builder.bytes;
    return sl_status_ok();
}

SlStatus sl_json_writer_write_string(SlJsonWriter* writer, SlStr text)
{
    SlStringBuilder builder = {0};
    char* remaining = NULL;
    SlStatus status;

    if (writer == NULL || (text.ptr == NULL && text.length != 0U) ||
        writer->length > writer->capacity || (writer->data == NULL && writer->capacity != 0U))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (writer->length < writer->capacity) {
        remaining = (char*)writer->data + writer->length;
    }
    status = sl_string_builder_init_fixed(&builder, remaining, writer->capacity - writer->length);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_json_writer_append_escaped_string_profiled(&builder, text);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    writer->length += sl_string_builder_view(&builder).length;
    return sl_status_ok();
}

SlStatus sl_json_writer_write_i64(SlJsonWriter* writer, int64_t value)
{
    char buffer[SL_STRING_FORMAT_I64_CAPACITY];
    SlStr formatted = {0};
    SlStatus status = sl_string_format_i64(buffer, sizeof(buffer), value, &formatted);

    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_json_writer_write_str(writer, formatted);
}

SlStatus sl_json_writer_write_f64(SlJsonWriter* writer, double value)
{
    char buffer[SL_STRING_FORMAT_F64_CAPACITY];
    SlStr formatted = {0};
    SlStatus status;

    if (!isfinite(value)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_string_format_f64(buffer, sizeof(buffer), value, &formatted);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_json_writer_write_str(writer, formatted);
}
