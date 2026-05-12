#include "sloppy/json_writer.h"

#include "sloppy/builder.h"
#include "sloppy/json_profile.h"

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

SlStatus sl_json_writer_write_string(SlJsonWriter* writer, SlStr text)
{
    static const char hex[] = "0123456789abcdef";
    size_t index = 0U;
    bool profile_enabled = sl_json_profile_enabled();
    uint64_t scan_start =
        profile_enabled ? sl_json_profile_phase_begin(SL_JSON_PROFILE_PHASE_STRING_ESCAPE_SCAN)
                        : 0U;
    uint64_t write_start = 0U;
    bool needs_escape = false;
    SlStatus status;

    if (writer == NULL || (text.ptr == NULL && text.length != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (index = 0U; index < text.length; index += 1U) {
        unsigned char ch = (unsigned char)text.ptr[index];
        if (ch == '"' || ch == '\\' || ch < 0x20U) {
            needs_escape = true;
            break;
        }
    }
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
    status = sl_json_writer_write_char(writer, '"');
    if (!sl_status_is_ok(status)) {
        if (profile_enabled) {
            sl_json_profile_phase_end(SL_JSON_PROFILE_PHASE_STRING_ESCAPE_WRITE, write_start);
        }
        return status;
    }
    if (!needs_escape) {
        status = sl_json_writer_write_str(writer, text);
        if (sl_status_is_ok(status)) {
            status = sl_json_writer_write_char(writer, '"');
        }
        if (profile_enabled) {
            sl_json_profile_phase_end(SL_JSON_PROFILE_PHASE_STRING_ESCAPE_WRITE, write_start);
        }
        return status;
    }

    for (index = 0U; index < text.length; index += 1U) {
        unsigned char ch = (unsigned char)text.ptr[index];
        char escaped[6] = {'\\', 'u', '0', '0', '0', '0'};

        if (ch == '"' || ch == '\\') {
            status = sl_json_writer_write_char(writer, '\\');
            if (sl_status_is_ok(status)) {
                status = sl_json_writer_write_char(writer, (char)ch);
            }
        }
        else if (ch == '\b') {
            status = sl_json_writer_write_str(writer, sl_str_from_cstr("\\b"));
        }
        else if (ch == '\f') {
            status = sl_json_writer_write_str(writer, sl_str_from_cstr("\\f"));
        }
        else if (ch == '\n') {
            status = sl_json_writer_write_str(writer, sl_str_from_cstr("\\n"));
        }
        else if (ch == '\r') {
            status = sl_json_writer_write_str(writer, sl_str_from_cstr("\\r"));
        }
        else if (ch == '\t') {
            status = sl_json_writer_write_str(writer, sl_str_from_cstr("\\t"));
        }
        else if (ch < 0x20U) {
            escaped[4] = hex[(ch >> 4U) & 0x0FU];
            escaped[5] = hex[ch & 0x0FU];
            status = sl_json_writer_write_bytes(
                writer, sl_bytes_from_parts((const unsigned char*)escaped, sizeof(escaped)));
        }
        else {
            status = sl_json_writer_write_char(writer, (char)ch);
        }
        if (!sl_status_is_ok(status)) {
            if (profile_enabled) {
                sl_json_profile_phase_end(SL_JSON_PROFILE_PHASE_STRING_ESCAPE_WRITE, write_start);
            }
            return status;
        }
    }
    status = sl_json_writer_write_char(writer, '"');
    if (profile_enabled) {
        sl_json_profile_phase_end(SL_JSON_PROFILE_PHASE_STRING_ESCAPE_WRITE, write_start);
    }
    return status;
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
    SlStatus status = sl_string_format_f64(buffer, sizeof(buffer), value, &formatted);

    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_json_writer_write_str(writer, formatted);
}
