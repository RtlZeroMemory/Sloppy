#include "sloppy/logging.h"

#include "sloppy/builder.h"
#include "sloppy/container.h"
#include "sloppy/platform_thread.h"
#include "sloppy/platform_time.h"

#include <math.h>
#include <yyjson.h>

typedef SlStatus (*SlLogSinkWriteFn)(SlLogSink* sink, const SlLogEvent* event);
typedef SlStatus (*SlLogSinkFlushFn)(SlLogSink* sink);
typedef void (*SlLogSinkCloseFn)(SlLogSink* sink);

typedef struct SlLogMemorySinkState
{
    SlRingQueue events;
    uint64_t overwritten_events;
} SlLogMemorySinkState;

typedef struct SlLogConsoleSinkState
{
    SlLogConsoleFormat format;
    SlLogBytesWriteFn writer;
    void* writer_user;
} SlLogConsoleSinkState;

typedef struct SlLogFileSinkState
{
    SlFsFileHandle* file;
    unsigned char* buffer;
    size_t capacity;
    size_t length;
} SlLogFileSinkState;

struct SlLogSink
{
    SlLogSinkKind kind;
    void* state;
    SlPlatformMutex* mutex;
    SlLogSinkWriteFn write;
    SlLogSinkFlushFn flush;
    SlLogSinkCloseFn close;
    SlLogCustomSinkWriteFn custom_write;
    SlLogCustomSinkFlushFn custom_flush;
    SlLogCustomSinkCloseFn custom_close;
    uint64_t write_count;
    uint64_t failure_count;
    bool closed;
};

struct SlLogRuntime
{
    SlArena* arena;
    SlLogRuntimeConfig config;
    SlRingQueue queue;
    SlLogSink** sinks;
    size_t sink_capacity;
    size_t sink_count;
    SlPlatformMutex* mutex;
    SlPlatformMutex* sink_mutex;
    SlPlatformCond* cond;
    SlPlatformThread* thread;
    size_t in_flight_events;
    uint64_t next_sequence;
    uint64_t submitted_events;
    uint64_t dispatched_events;
    uint64_t dropped_new_events;
    uint64_t dropped_oldest_events;
    uint64_t sink_failures;
    bool accepting;
    bool stop_requested;
    bool dispatcher_started;
    bool shutdown;
};

/*
 * Lock order for logging runtime code:
 * runtime->mutex protects queue/runtime counters;
 * runtime->sink_mutex protects sink-list publication/iteration;
 * sink->mutex protects per-sink state and sink counters.
 *
 * Code may take sink_mutex before sink->mutex. Snapshot APIs only take sink->mutex.
 * Runtime code must not hold runtime->mutex while invoking sink callbacks.
 */

static SlStr sl_log_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static bool sl_log_text_valid(SlStr text)
{
    return text.length == 0U || text.ptr != NULL;
}

static SlStatus sl_log_copy_to_fixed(char* buffer, size_t capacity, size_t* out_length, SlStr text)
{
    size_t index = 0U;

    if (buffer == NULL || capacity == 0U || out_length == NULL || !sl_log_text_valid(text)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (text.length >= capacity) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }
    for (index = 0U; index < text.length; index += 1U) {
        buffer[index] = text.ptr[index];
    }
    buffer[text.length] = '\0';
    *out_length = text.length;
    return sl_status_ok();
}

static SlStr sl_log_short_text_view(const SlLogShortText* text)
{
    return text == NULL ? sl_str_empty() : sl_str_from_parts(text->bytes, text->length);
}

static SlStr sl_log_value_text_view(const SlLogText* text)
{
    return text == NULL ? sl_str_empty() : sl_str_from_parts(text->bytes, text->length);
}

static SlStr sl_log_message_text_view(const SlLogMessageText* text)
{
    return text == NULL ? sl_str_empty() : sl_str_from_parts(text->bytes, text->length);
}

static SlStr sl_log_category_text_view(const SlLogCategoryText* text)
{
    return text == NULL ? sl_str_empty() : sl_str_from_parts(text->bytes, text->length);
}

static SlStr sl_log_request_id_text_view(const SlLogRequestIdText* text)
{
    return text == NULL ? sl_str_empty() : sl_str_from_parts(text->bytes, text->length);
}

static SlStr sl_log_route_name_text_view(const SlLogRouteNameText* text)
{
    return text == NULL ? sl_str_empty() : sl_str_from_parts(text->bytes, text->length);
}

static SlStr sl_log_route_pattern_text_view(const SlLogRoutePatternText* text)
{
    return text == NULL ? sl_str_empty() : sl_str_from_parts(text->bytes, text->length);
}

SlStr sl_log_text_view(const char* bytes, size_t length)
{
    return sl_str_from_parts(bytes, length);
}

SlStr sl_log_event_category(const SlLogEvent* event)
{
    return event == NULL ? sl_str_empty() : sl_log_category_text_view(&event->category);
}

SlStr sl_log_event_message(const SlLogEvent* event)
{
    return event == NULL ? sl_str_empty() : sl_log_message_text_view(&event->message);
}

SlStr sl_log_event_request_id(const SlLogEvent* event)
{
    return event == NULL ? sl_str_empty() : sl_log_request_id_text_view(&event->request_id);
}

SlStr sl_log_event_route_name(const SlLogEvent* event)
{
    return event == NULL ? sl_str_empty() : sl_log_route_name_text_view(&event->route_name);
}

SlStr sl_log_event_route_pattern(const SlLogEvent* event)
{
    return event == NULL ? sl_str_empty() : sl_log_route_pattern_text_view(&event->route_pattern);
}

SlStr sl_log_field_key(const SlLogField* field)
{
    return field == NULL ? sl_str_empty() : sl_log_short_text_view(&field->key);
}

SlStr sl_log_field_text_value(const SlLogField* field)
{
    return field == NULL ? sl_str_empty() : sl_log_value_text_view(&field->value);
}

SlLogRuntimeConfig sl_log_runtime_config_default(void)
{
    SlLogRuntimeConfig config = {0};
    config.minimum_level = SL_LOG_LEVEL_INFO;
    config.queue_capacity = SL_LOG_DEFAULT_QUEUE_CAPACITY;
    config.sink_capacity = SL_LOG_DEFAULT_SINK_CAPACITY;
    config.backpressure_policy = SL_LOG_BACKPRESSURE_DROP_NEW;
    return config;
}

SlStr sl_log_level_name(SlLogLevel level)
{
    switch (level) {
    case SL_LOG_LEVEL_TRACE:
        return sl_log_literal("trace", sizeof("trace") - 1U);
    case SL_LOG_LEVEL_DEBUG:
        return sl_log_literal("debug", sizeof("debug") - 1U);
    case SL_LOG_LEVEL_INFO:
        return sl_log_literal("info", sizeof("info") - 1U);
    case SL_LOG_LEVEL_WARN:
        return sl_log_literal("warn", sizeof("warn") - 1U);
    case SL_LOG_LEVEL_ERROR:
        return sl_log_literal("error", sizeof("error") - 1U);
    case SL_LOG_LEVEL_OFF:
        return sl_log_literal("off", sizeof("off") - 1U);
    default:
        return sl_str_empty();
    }
}

SlStatus sl_log_level_from_str(SlStr text, SlLogLevel* out_level)
{
    if (out_level == NULL || !sl_log_text_valid(text)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (sl_str_equal_ci_ascii(text, sl_log_literal("trace", sizeof("trace") - 1U))) {
        *out_level = SL_LOG_LEVEL_TRACE;
        return sl_status_ok();
    }
    if (sl_str_equal_ci_ascii(text, sl_log_literal("debug", sizeof("debug") - 1U))) {
        *out_level = SL_LOG_LEVEL_DEBUG;
        return sl_status_ok();
    }
    if (sl_str_equal_ci_ascii(text, sl_log_literal("info", sizeof("info") - 1U))) {
        *out_level = SL_LOG_LEVEL_INFO;
        return sl_status_ok();
    }
    if (sl_str_equal_ci_ascii(text, sl_log_literal("warn", sizeof("warn") - 1U)) ||
        sl_str_equal_ci_ascii(text, sl_log_literal("warning", sizeof("warning") - 1U)))
    {
        *out_level = SL_LOG_LEVEL_WARN;
        return sl_status_ok();
    }
    if (sl_str_equal_ci_ascii(text, sl_log_literal("error", sizeof("error") - 1U))) {
        *out_level = SL_LOG_LEVEL_ERROR;
        return sl_status_ok();
    }
    if (sl_str_equal_ci_ascii(text, sl_log_literal("off", sizeof("off") - 1U))) {
        *out_level = SL_LOG_LEVEL_OFF;
        return sl_status_ok();
    }
    return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
}

bool sl_log_level_enabled(SlLogLevel minimum_level, SlLogLevel level)
{
    if (minimum_level == SL_LOG_LEVEL_OFF) {
        return false;
    }
    if (level < SL_LOG_LEVEL_TRACE || level > SL_LOG_LEVEL_ERROR) {
        return false;
    }
    return level >= minimum_level;
}

static char sl_log_ascii_lower(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

static bool sl_log_normalized_equals(SlStr key, const char* normalized)
{
    size_t index = 0U;
    size_t out_index = 0U;

    if (!sl_log_text_valid(key) || normalized == NULL) {
        return false;
    }
    for (index = 0U; index < key.length; index += 1U) {
        char ch = sl_log_ascii_lower(key.ptr[index]);
        if (ch == '_' || ch == '-' || ch == '.' || ch == ':') {
            continue;
        }
        if (normalized[out_index] == '\0' || ch != normalized[out_index]) {
            return false;
        }
        out_index += 1U;
    }
    return normalized[out_index] == '\0';
}

static bool sl_log_normalized_ends_with(SlStr key, const char* suffix)
{
    char normalized_key[SL_LOG_MAX_FIELD_KEY_BYTES];
    size_t index = 0U;
    size_t length = 0U;
    size_t suffix_length = 0U;

    if (!sl_log_text_valid(key) || suffix == NULL) {
        return false;
    }
    while (suffix[suffix_length] != '\0') {
        suffix_length += 1U;
    }
    for (index = 0U; index < key.length && length < sizeof(normalized_key); index += 1U) {
        char ch = sl_log_ascii_lower(key.ptr[index]);
        if (ch == '_' || ch == '-' || ch == '.' || ch == ':') {
            continue;
        }
        normalized_key[length] = ch;
        length += 1U;
    }
    if (suffix_length > length) {
        return false;
    }
    return sl_str_ends_with(sl_str_from_parts(normalized_key, length), sl_str_from_cstr(suffix));
}

bool sl_log_key_is_sensitive(SlStr key, const SlStr* extra_keys, size_t extra_key_count)
{
    static const char* const exact_keys[] = {
        "password",        "passwd",    "pwd",    "secret",       "token",      "authorization",
        "cookie",          "setcookie", "apikey", "clientsecret", "privatekey", "passphrase",
        "connectionstring"};
    static const char* const suffix_keys[] = {
        "password", "secret",       "token",      "authorization", "cookie",          "setcookie",
        "apikey",   "clientsecret", "privatekey", "passphrase",    "connectionstring"};
    size_t index = 0U;

    if (!sl_log_text_valid(key) || key.length == 0U) {
        return false;
    }
    for (index = 0U; index < sizeof(exact_keys) / sizeof(exact_keys[0]); index += 1U) {
        if (sl_log_normalized_equals(key, exact_keys[index])) {
            return true;
        }
    }
    for (index = 0U; index < sizeof(suffix_keys) / sizeof(suffix_keys[0]); index += 1U) {
        if (sl_log_normalized_ends_with(key, suffix_keys[index])) {
            return true;
        }
    }
    for (index = 0U; index < extra_key_count; index += 1U) {
        if (extra_keys != NULL && sl_str_equal_ci_ascii(key, extra_keys[index])) {
            return true;
        }
    }
    return false;
}

SlStatus sl_log_event_apply_redaction(SlLogEvent* event, const SlStr* extra_keys,
                                      size_t extra_key_count)
{
    size_t index = 0U;

    if (event == NULL || (extra_key_count != 0U && extra_keys == NULL)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    event->redacted_count = 0U;
    for (index = 0U; index < event->field_count; index += 1U) {
        SlLogField* field = &event->fields[index];
        if (sl_log_key_is_sensitive(sl_log_field_key(field), extra_keys, extra_key_count)) {
            SlStatus status = sl_log_copy_to_fixed(
                field->value.bytes, sizeof(field->value.bytes), &field->value.length,
                sl_log_literal("[REDACTED]", sizeof("[REDACTED]") - 1U));
            if (!sl_status_is_ok(status)) {
                return status;
            }
            field->kind = SL_LOG_FIELD_STRING;
            field->redacted = true;
            event->redacted_count += 1U;
        }
        else {
            field->redacted = false;
        }
    }
    return sl_status_ok();
}

SlStatus sl_log_event_builder_init(SlLogEventBuilder* builder, SlLogLevel level, SlStr message)
{
    SlStatus status;

    if (builder == NULL || level < SL_LOG_LEVEL_TRACE || level > SL_LOG_LEVEL_ERROR ||
        !sl_log_text_valid(message))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *builder = (SlLogEventBuilder){0};
    builder->event.level = level;
    status =
        sl_log_copy_to_fixed(builder->event.message.bytes, sizeof(builder->event.message.bytes),
                             &builder->event.message.length, message);
    if (!sl_status_is_ok(status)) {
        *builder = (SlLogEventBuilder){0};
        return status;
    }
    return sl_status_ok();
}

SlStatus sl_log_event_builder_set_category(SlLogEventBuilder* builder, SlStr category)
{
    if (builder == NULL || builder->finished || !sl_log_text_valid(category)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_log_copy_to_fixed(builder->event.category.bytes,
                                sizeof(builder->event.category.bytes),
                                &builder->event.category.length, category);
}

SlStatus sl_log_event_builder_set_request_id(SlLogEventBuilder* builder, SlStr request_id)
{
    if (builder == NULL || builder->finished || !sl_log_text_valid(request_id)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_log_copy_to_fixed(builder->event.request_id.bytes,
                                sizeof(builder->event.request_id.bytes),
                                &builder->event.request_id.length, request_id);
}

SlStatus sl_log_event_builder_set_route(SlLogEventBuilder* builder, SlStr route_name,
                                        SlStr route_pattern)
{
    SlStatus status;

    if (builder == NULL || builder->finished || !sl_log_text_valid(route_name) ||
        !sl_log_text_valid(route_pattern))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_log_copy_to_fixed(builder->event.route_name.bytes,
                                  sizeof(builder->event.route_name.bytes),
                                  &builder->event.route_name.length, route_name);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_log_copy_to_fixed(builder->event.route_pattern.bytes,
                                sizeof(builder->event.route_pattern.bytes),
                                &builder->event.route_pattern.length, route_pattern);
}

static SlStatus sl_log_event_builder_prepare_field(SlLogEventBuilder* builder, SlStr key,
                                                   SlLogField** out_field)
{
    SlLogField* field = NULL;
    SlStatus status;

    if (out_field != NULL) {
        *out_field = NULL;
    }
    if (builder == NULL || out_field == NULL || builder->finished || !sl_log_text_valid(key) ||
        key.length == 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (builder->event.field_count >= SL_LOG_MAX_FIELDS) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }
    field = &builder->event.fields[builder->event.field_count];
    *field = (SlLogField){0};
    status =
        sl_log_copy_to_fixed(field->key.bytes, sizeof(field->key.bytes), &field->key.length, key);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    builder->event.field_count += 1U;
    *out_field = field;
    return sl_status_ok();
}

SlStatus sl_log_event_builder_add_null(SlLogEventBuilder* builder, SlStr key)
{
    SlLogField* field = NULL;
    SlStatus status = sl_log_event_builder_prepare_field(builder, key, &field);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (field == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    field->kind = SL_LOG_FIELD_NULL;
    return sl_status_ok();
}

SlStatus sl_log_event_builder_add_bool(SlLogEventBuilder* builder, SlStr key, bool value)
{
    SlLogField* field = NULL;
    SlStatus status = sl_log_event_builder_prepare_field(builder, key, &field);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (field == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    field->kind = SL_LOG_FIELD_BOOL;
    field->bool_value = value;
    return sl_status_ok();
}

SlStatus sl_log_event_builder_add_i64(SlLogEventBuilder* builder, SlStr key, int64_t value)
{
    SlLogField* field = NULL;
    SlStatus status = sl_log_event_builder_prepare_field(builder, key, &field);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (field == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    field->kind = SL_LOG_FIELD_I64;
    field->i64_value = value;
    return sl_status_ok();
}

SlStatus sl_log_event_builder_add_f64(SlLogEventBuilder* builder, SlStr key, double value)
{
    SlLogField* field = NULL;
    SlStatus status;

    if (!isfinite(value)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_log_event_builder_prepare_field(builder, key, &field);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (field == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    field->kind = SL_LOG_FIELD_F64;
    field->f64_value = value;
    return sl_status_ok();
}

SlStatus sl_log_event_builder_add_string(SlLogEventBuilder* builder, SlStr key, SlStr value)
{
    SlLogField* field = NULL;
    SlStatus status = sl_log_event_builder_prepare_field(builder, key, &field);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (field == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    status = sl_log_copy_to_fixed(field->value.bytes, sizeof(field->value.bytes),
                                  &field->value.length, value);
    if (!sl_status_is_ok(status)) {
        builder->event.field_count -= 1U;
        return status;
    }
    field->kind = SL_LOG_FIELD_STRING;
    return sl_status_ok();
}

static bool sl_log_json_payload_valid(SlStr value)
{
    yyjson_read_err error = {0};
    yyjson_doc* doc = NULL;
    yyjson_val* root = NULL;

    if (!sl_log_text_valid(value) || value.length == 0U) {
        return false;
    }

    doc = yyjson_read_opts((char*)value.ptr, value.length, 0U, NULL, &error);
    if (doc == NULL) {
        return false;
    }
    root = yyjson_doc_get_root(doc);
    if (root == NULL) {
        yyjson_doc_free(doc);
        return false;
    }
    yyjson_doc_free(doc);
    return true;
}

SlStatus sl_log_event_builder_add_json(SlLogEventBuilder* builder, SlStr key, SlStr value)
{
    SlLogField* field = NULL;
    SlStatus status = sl_log_event_builder_prepare_field(builder, key, &field);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (field == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    status = sl_log_copy_to_fixed(field->value.bytes, sizeof(field->value.bytes),
                                  &field->value.length, value);
    if (!sl_status_is_ok(status)) {
        builder->event.field_count -= 1U;
        return status;
    }
    if (!sl_log_json_payload_valid(sl_log_field_text_value(field))) {
        builder->event.field_count -= 1U;
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    field->kind = SL_LOG_FIELD_JSON;
    return sl_status_ok();
}

SlStatus sl_log_event_builder_finish(SlLogEventBuilder* builder, SlLogEvent* out_event)
{
    if (builder == NULL || out_event == NULL || builder->finished) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    builder->finished = true;
    *out_event = builder->event;
    return sl_status_ok();
}

static SlStatus sl_log_json_append_escaped(SlStringBuilder* builder, SlStr text)
{
    size_t index = 0U;
    static const char hex[] = "0123456789abcdef";

    if (builder == NULL || !sl_log_text_valid(text)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    SlStatus status = sl_string_builder_append_char(builder, '"');
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (index = 0U; index < text.length; index += 1U) {
        unsigned char ch = (unsigned char)text.ptr[index];
        switch (ch) {
        case '"':
            status = sl_string_builder_append_cstr(builder, "\\\"");
            break;
        case '\\':
            status = sl_string_builder_append_cstr(builder, "\\\\");
            break;
        case '\b':
            status = sl_string_builder_append_cstr(builder, "\\b");
            break;
        case '\f':
            status = sl_string_builder_append_cstr(builder, "\\f");
            break;
        case '\n':
            status = sl_string_builder_append_cstr(builder, "\\n");
            break;
        case '\r':
            status = sl_string_builder_append_cstr(builder, "\\r");
            break;
        case '\t':
            status = sl_string_builder_append_cstr(builder, "\\t");
            break;
        default:
            if (ch < 0x20U) {
                status = sl_string_builder_append_cstr(builder, "\\u00");
                if (sl_status_is_ok(status)) {
                    status = sl_string_builder_append_char(builder, hex[(ch >> 4U) & 0x0FU]);
                }
                if (sl_status_is_ok(status)) {
                    status = sl_string_builder_append_char(builder, hex[ch & 0x0FU]);
                }
            }
            else {
                status = sl_string_builder_append_char(builder, (char)ch);
            }
            break;
        }
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_string_builder_append_char(builder, '"');
}

static SlStatus sl_log_json_append_field_value(SlStringBuilder* builder, const SlLogField* field)
{
    if (builder == NULL || field == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (field->redacted) {
        return sl_log_json_append_escaped(builder, sl_log_field_text_value(field));
    }
    switch (field->kind) {
    case SL_LOG_FIELD_NULL:
        return sl_string_builder_append_cstr(builder, "null");
    case SL_LOG_FIELD_BOOL:
        return sl_string_builder_append_cstr(builder, field->bool_value ? "true" : "false");
    case SL_LOG_FIELD_I64:
        return sl_string_builder_append_i64(builder, field->i64_value);
    case SL_LOG_FIELD_F64:
        return sl_string_builder_append_f64(builder, field->f64_value);
    case SL_LOG_FIELD_STRING:
        return sl_log_json_append_escaped(builder, sl_log_field_text_value(field));
    case SL_LOG_FIELD_JSON:
        return sl_string_builder_append_str(builder, sl_log_field_text_value(field));
    default:
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
}

SlStatus sl_log_event_serialize_jsonl(const SlLogEvent* event, char* buffer, size_t capacity,
                                      SlBytes* out_bytes)
{
    SlStringBuilder builder = {0};
    SlStatus status;
    size_t index = 0U;
    bool first = true;
    SlStr view = {0};

    if (event == NULL || buffer == NULL || capacity == 0U || out_bytes == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_bytes = sl_bytes_empty();
    status = sl_string_builder_init_fixed(&builder, buffer, capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_string_builder_append_cstr(&builder, "{\"tsNs\":");
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_u64(&builder, event->timestamp_ns);
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_cstr(&builder, ",\"level\":");
    }
    if (sl_status_is_ok(status)) {
        status = sl_log_json_append_escaped(&builder, sl_log_level_name(event->level));
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_cstr(&builder, ",\"category\":");
    }
    if (sl_status_is_ok(status)) {
        status = sl_log_json_append_escaped(&builder, sl_log_event_category(event));
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_cstr(&builder, ",\"message\":");
    }
    if (sl_status_is_ok(status)) {
        status = sl_log_json_append_escaped(&builder, sl_log_event_message(event));
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_cstr(&builder, ",\"seq\":");
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_u64(&builder, event->sequence);
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (event->request_id.length != 0U) {
        status = sl_string_builder_append_cstr(&builder, ",\"requestId\":");
        if (sl_status_is_ok(status)) {
            status = sl_log_json_append_escaped(&builder, sl_log_event_request_id(event));
        }
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    if (event->route_name.length != 0U) {
        status = sl_string_builder_append_cstr(&builder, ",\"routeName\":");
        if (sl_status_is_ok(status)) {
            status = sl_log_json_append_escaped(&builder, sl_log_event_route_name(event));
        }
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    if (event->route_pattern.length != 0U) {
        status = sl_string_builder_append_cstr(&builder, ",\"routePattern\":");
        if (sl_status_is_ok(status)) {
            status = sl_log_json_append_escaped(&builder, sl_log_event_route_pattern(event));
        }
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    status = sl_string_builder_append_cstr(&builder, ",\"fields\":{");
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (index = 0U; index < event->field_count; index += 1U) {
        const SlLogField* field = &event->fields[index];
        if (!first) {
            status = sl_string_builder_append_char(&builder, ',');
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
        first = false;
        status = sl_log_json_append_escaped(&builder, sl_log_field_key(field));
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_char(&builder, ':');
        }
        if (sl_status_is_ok(status)) {
            status = sl_log_json_append_field_value(&builder, field);
        }
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    status = sl_string_builder_append_cstr(&builder, "},\"redacted\":");
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_size(&builder, event->redacted_count);
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_cstr(&builder, "}\n");
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }

    view = sl_string_builder_view(&builder);
    *out_bytes = sl_bytes_from_parts((const unsigned char*)view.ptr, view.length);
    return sl_status_ok();
}

static SlStatus sl_log_dispatch_to_sink(SlLogSink* sink, const SlLogEvent* event)
{
    SlStatus status;

    if (sink == NULL || sink->mutex == NULL || sink->write == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    sl_platform_mutex_lock(sink->mutex);
    if (sink->closed) {
        sl_platform_mutex_unlock(sink->mutex);
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    status = sink->write(sink, event);
    if (sl_status_is_ok(status)) {
        sink->write_count += 1U;
    }
    else {
        sink->failure_count += 1U;
    }
    sl_platform_mutex_unlock(sink->mutex);
    return status;
}

static SlStatus sl_log_flush_sink(SlLogSink* sink, uint64_t* out_sink_failure)
{
    SlStatus status = sl_status_ok();

    if (out_sink_failure != NULL) {
        *out_sink_failure = 0U;
    }
    if (sink == NULL) {
        return sl_status_ok();
    }
    if (sink->mutex == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    sl_platform_mutex_lock(sink->mutex);
    if (sink->flush != NULL && !sink->closed) {
        status = sink->flush(sink);
        if (!sl_status_is_ok(status)) {
            sink->failure_count += 1U;
            if (out_sink_failure != NULL) {
                *out_sink_failure = 1U;
            }
        }
    }
    sl_platform_mutex_unlock(sink->mutex);
    return status;
}

static void sl_log_close_sink(SlLogSink* sink)
{
    SlLogSinkCloseFn close_fn = NULL;

    if (sink == NULL || sink->mutex == NULL) {
        return;
    }
    sl_platform_mutex_lock(sink->mutex);
    if (sink->closed) {
        sl_platform_mutex_unlock(sink->mutex);
        return;
    }
    close_fn = sink->close;
    sink->closed = true;
    sl_platform_mutex_unlock(sink->mutex);

    if (close_fn != NULL) {
        close_fn(sink);
    }
}

static void sl_log_runtime_dispatch_event(SlLogRuntime* runtime, const SlLogEvent* event)
{
    size_t index = 0U;
    uint64_t sink_failures = 0U;

    if (runtime == NULL || event == NULL) {
        return;
    }

    sl_platform_mutex_lock(runtime->sink_mutex);
    for (index = 0U; index < runtime->sink_count; index += 1U) {
        if (!sl_status_is_ok(sl_log_dispatch_to_sink(runtime->sinks[index], event))) {
            sink_failures += 1U;
        }
    }
    sl_platform_mutex_unlock(runtime->sink_mutex);

    sl_platform_mutex_lock(runtime->mutex);
    runtime->sink_failures += sink_failures;
    runtime->dispatched_events += 1U;
    sl_platform_cond_broadcast(runtime->cond);
    sl_platform_mutex_unlock(runtime->mutex);
}

static bool sl_log_runtime_pop_event_locked(SlLogRuntime* runtime, SlLogEvent* out_event)
{
    return runtime != NULL && out_event != NULL &&
           sl_ring_queue_pop_front(&runtime->queue, out_event);
}

static void sl_log_runtime_dispatch_main(void* user)
{
    SlLogRuntime* runtime = (SlLogRuntime*)user;

    if (runtime == NULL) {
        return;
    }

    for (;;) {
        SlLogEvent event = {0};
        bool has_event = false;

        sl_platform_mutex_lock(runtime->mutex);
        while (sl_ring_queue_is_empty(&runtime->queue) && !runtime->stop_requested) {
            sl_platform_cond_wait(runtime->cond, runtime->mutex);
        }
        if (sl_ring_queue_is_empty(&runtime->queue) && runtime->stop_requested) {
            sl_platform_cond_broadcast(runtime->cond);
            sl_platform_mutex_unlock(runtime->mutex);
            break;
        }
        has_event = sl_log_runtime_pop_event_locked(runtime, &event);
        if (has_event) {
            runtime->in_flight_events += 1U;
        }
        sl_platform_cond_broadcast(runtime->cond);
        sl_platform_mutex_unlock(runtime->mutex);

        if (has_event) {
            sl_log_runtime_dispatch_event(runtime, &event);
            sl_platform_mutex_lock(runtime->mutex);
            if (runtime->in_flight_events > 0U) {
                runtime->in_flight_events -= 1U;
            }
            sl_platform_cond_broadcast(runtime->cond);
            sl_platform_mutex_unlock(runtime->mutex);
        }
    }
}

SlStatus sl_log_runtime_create(SlArena* arena, const SlLogRuntimeConfig* config,
                               SlLogRuntime** out_runtime)
{
    SlLogRuntimeConfig resolved = sl_log_runtime_config_default();
    SlSlice queue_storage = {0};
    SlSlice sink_storage = {0};
    void* runtime_memory = NULL;
    SlLogRuntime* runtime = NULL;
    SlStatus status;

    if (arena == NULL || out_runtime == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_runtime = NULL;
    if (config != NULL) {
        resolved = *config;
    }
    if (resolved.queue_capacity == 0U || resolved.sink_capacity == 0U ||
        resolved.minimum_level < SL_LOG_LEVEL_TRACE || resolved.minimum_level > SL_LOG_LEVEL_OFF ||
        (resolved.redaction_key_count != 0U && resolved.redaction_keys == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_arena_alloc(arena, sizeof(SlLogRuntime), _Alignof(SlLogRuntime), &runtime_memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    runtime = (SlLogRuntime*)runtime_memory;
    *runtime = (SlLogRuntime){0};
    runtime->arena = arena;
    runtime->config = resolved;
    runtime->sink_capacity = resolved.sink_capacity;
    runtime->accepting = true;

    status = sl_arena_array_alloc(arena, resolved.queue_capacity, sizeof(SlLogEvent),
                                  _Alignof(SlLogEvent), &queue_storage);
    if (sl_status_is_ok(status)) {
        status = sl_ring_queue_init(&runtime->queue, queue_storage.ptr, sizeof(SlLogEvent),
                                    resolved.queue_capacity);
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_arena_array_alloc(arena, resolved.sink_capacity, sizeof(SlLogSink*),
                                  _Alignof(SlLogSink*), &sink_storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    runtime->sinks = (SlLogSink**)sink_storage.ptr;

    status = sl_platform_mutex_create(arena, &runtime->mutex);
    if (sl_status_is_ok(status)) {
        status = sl_platform_mutex_create(arena, &runtime->sink_mutex);
    }
    if (sl_status_is_ok(status)) {
        status = sl_platform_cond_create(arena, &runtime->cond);
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out_runtime = runtime;
    return sl_status_ok();
}

SlStatus sl_log_runtime_add_sink(SlLogRuntime* runtime, SlLogSink* sink)
{
    if (runtime == NULL || sink == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    sl_platform_mutex_lock(runtime->mutex);
    if (runtime->dispatcher_started || runtime->stop_requested || runtime->shutdown) {
        sl_platform_mutex_unlock(runtime->mutex);
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    if (runtime->sink_count >= runtime->sink_capacity) {
        sl_platform_mutex_unlock(runtime->mutex);
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    sl_platform_mutex_lock(runtime->sink_mutex);
    runtime->sinks[runtime->sink_count] = sink;
    runtime->sink_count += 1U;
    sl_platform_mutex_unlock(runtime->sink_mutex);
    sl_platform_mutex_unlock(runtime->mutex);
    return sl_status_ok();
}

SlStatus sl_log_runtime_start(SlLogRuntime* runtime)
{
    SlStatus status;

    if (runtime == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    sl_platform_mutex_lock(runtime->mutex);
    if (runtime->shutdown || runtime->stop_requested) {
        sl_platform_mutex_unlock(runtime->mutex);
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    if (runtime->dispatcher_started) {
        sl_platform_mutex_unlock(runtime->mutex);
        return sl_status_ok();
    }
    status = sl_platform_thread_start(runtime->arena, sl_log_runtime_dispatch_main, runtime,
                                      &runtime->thread);
    if (!sl_status_is_ok(status)) {
        sl_platform_mutex_unlock(runtime->mutex);
        return status;
    }
    runtime->dispatcher_started = true;
    sl_platform_mutex_unlock(runtime->mutex);
    return sl_status_ok();
}

bool sl_log_runtime_is_enabled(const SlLogRuntime* runtime, SlLogLevel level)
{
    bool enabled = false;

    if (runtime == NULL) {
        return false;
    }
    sl_platform_mutex_lock(runtime->mutex);
    enabled = runtime->accepting && !runtime->shutdown &&
              sl_log_level_enabled(runtime->config.minimum_level, level);
    sl_platform_mutex_unlock(runtime->mutex);
    return enabled;
}

SlStatus sl_log_runtime_submit(SlLogRuntime* runtime, const SlLogEvent* event)
{
    SlLogEvent queued = {0};
    SlLogLevel minimum_level = SL_LOG_LEVEL_OFF;
    SlStatus status;

    if (runtime == NULL || event == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    sl_platform_mutex_lock(runtime->mutex);
    if (!runtime->accepting || runtime->stop_requested || runtime->shutdown) {
        sl_platform_mutex_unlock(runtime->mutex);
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    minimum_level = runtime->config.minimum_level;
    sl_platform_mutex_unlock(runtime->mutex);

    if (!sl_log_level_enabled(minimum_level, event->level)) {
        return sl_status_ok();
    }

    queued = *event;
    status = sl_log_event_apply_redaction(&queued, runtime->config.redaction_keys,
                                          runtime->config.redaction_key_count);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    (void)sl_platform_monotonic_time_ns(&queued.timestamp_ns);

    sl_platform_mutex_lock(runtime->mutex);
    if (!runtime->accepting || runtime->stop_requested || runtime->shutdown) {
        sl_platform_mutex_unlock(runtime->mutex);
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    runtime->next_sequence += 1U;
    queued.sequence = runtime->next_sequence;
    if (sl_ring_queue_is_full(&runtime->queue)) {
        if (runtime->config.backpressure_policy == SL_LOG_BACKPRESSURE_DROP_OLDEST) {
            (void)sl_ring_queue_discard_front(&runtime->queue);
            runtime->dropped_oldest_events += 1U;
        }
        else {
            runtime->dropped_new_events += 1U;
            sl_platform_cond_broadcast(runtime->cond);
            sl_platform_mutex_unlock(runtime->mutex);
            return sl_status_ok();
        }
    }
    status = sl_ring_queue_push(&runtime->queue, &queued);
    if (sl_status_is_ok(status)) {
        runtime->submitted_events += 1U;
        sl_platform_cond_signal(runtime->cond);
    }
    sl_platform_mutex_unlock(runtime->mutex);
    return status;
}

SlStatus sl_log_runtime_drain(SlLogRuntime* runtime)
{
    if (runtime == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    for (;;) {
        SlLogEvent event = {0};
        bool has_event = false;

        sl_platform_mutex_lock(runtime->mutex);
        if (runtime->dispatcher_started) {
            sl_platform_mutex_unlock(runtime->mutex);
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        has_event = sl_log_runtime_pop_event_locked(runtime, &event);
        if (has_event) {
            runtime->in_flight_events += 1U;
        }
        sl_platform_mutex_unlock(runtime->mutex);
        if (!has_event) {
            break;
        }
        sl_log_runtime_dispatch_event(runtime, &event);
        sl_platform_mutex_lock(runtime->mutex);
        if (runtime->in_flight_events > 0U) {
            runtime->in_flight_events -= 1U;
        }
        sl_platform_cond_broadcast(runtime->cond);
        sl_platform_mutex_unlock(runtime->mutex);
    }
    return sl_status_ok();
}

SlStatus sl_log_runtime_flush(SlLogRuntime* runtime)
{
    size_t index = 0U;
    SlStatus first_failure = sl_status_ok();
    uint64_t sink_failures = 0U;
    bool dispatcher_started = false;

    if (runtime == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    sl_platform_mutex_lock(runtime->mutex);
    dispatcher_started = runtime->dispatcher_started;
    sl_platform_mutex_unlock(runtime->mutex);

    if (!dispatcher_started) {
        SlStatus status = sl_log_runtime_drain(runtime);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    else {
        sl_platform_mutex_lock(runtime->mutex);
        while (!sl_ring_queue_is_empty(&runtime->queue) || runtime->in_flight_events != 0U) {
            sl_platform_cond_wait(runtime->cond, runtime->mutex);
        }
        sl_platform_mutex_unlock(runtime->mutex);
    }

    sl_platform_mutex_lock(runtime->sink_mutex);
    for (index = 0U; index < runtime->sink_count; index += 1U) {
        uint64_t sink_failure = 0U;
        SlStatus status = sl_log_flush_sink(runtime->sinks[index], &sink_failure);
        if (!sl_status_is_ok(status) && sl_status_is_ok(first_failure)) {
            first_failure = status;
        }
        sink_failures += sink_failure;
    }
    sl_platform_mutex_unlock(runtime->sink_mutex);

    if (sink_failures != 0U) {
        sl_platform_mutex_lock(runtime->mutex);
        runtime->sink_failures += sink_failures;
        sl_platform_mutex_unlock(runtime->mutex);
    }
    return first_failure;
}

SlStatus sl_log_runtime_shutdown(SlLogRuntime* runtime)
{
    size_t index = 0U;
    SlStatus status;
    bool dispatcher_started = false;

    if (runtime == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    sl_platform_mutex_lock(runtime->mutex);
    if (runtime->shutdown) {
        sl_platform_mutex_unlock(runtime->mutex);
        return sl_status_ok();
    }
    runtime->accepting = false;
    runtime->stop_requested = true;
    runtime->shutdown = true;
    dispatcher_started = runtime->dispatcher_started;
    sl_platform_cond_broadcast(runtime->cond);
    sl_platform_mutex_unlock(runtime->mutex);

    if (dispatcher_started) {
        sl_platform_thread_join(runtime->thread);
        sl_platform_mutex_lock(runtime->mutex);
        runtime->dispatcher_started = false;
        sl_platform_cond_broadcast(runtime->cond);
        sl_platform_mutex_unlock(runtime->mutex);
    }

    status = sl_log_runtime_flush(runtime);
    sl_platform_mutex_lock(runtime->sink_mutex);
    for (index = 0U; index < runtime->sink_count; index += 1U) {
        sl_log_close_sink(runtime->sinks[index]);
    }
    sl_platform_mutex_unlock(runtime->sink_mutex);
    return status;
}

SlLogRuntimeSnapshot sl_log_runtime_snapshot(const SlLogRuntime* runtime)
{
    SlLogRuntimeSnapshot snapshot = {0};

    if (runtime == NULL) {
        return snapshot;
    }
    sl_platform_mutex_lock(runtime->mutex);
    snapshot.minimum_level = runtime->config.minimum_level;
    snapshot.queued_events = sl_ring_queue_count(&runtime->queue);
    snapshot.in_flight_events = runtime->in_flight_events;
    snapshot.sink_count = runtime->sink_count;
    snapshot.submitted_events = runtime->submitted_events;
    snapshot.dispatched_events = runtime->dispatched_events;
    snapshot.dropped_new_events = runtime->dropped_new_events;
    snapshot.dropped_oldest_events = runtime->dropped_oldest_events;
    snapshot.dropped_events = runtime->dropped_new_events + runtime->dropped_oldest_events;
    snapshot.sink_failures = runtime->sink_failures;
    snapshot.accepting = runtime->accepting;
    snapshot.dispatcher_started = runtime->dispatcher_started;
    snapshot.shutdown = runtime->shutdown;
    sl_platform_mutex_unlock(runtime->mutex);
    return snapshot;
}

static SlStatus sl_log_memory_sink_write(SlLogSink* sink, const SlLogEvent* event)
{
    SlLogMemorySinkState* state = sink == NULL ? NULL : (SlLogMemorySinkState*)sink->state;

    if (state == NULL || event == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (sl_ring_queue_is_full(&state->events)) {
        (void)sl_ring_queue_discard_front(&state->events);
        state->overwritten_events += 1U;
    }
    return sl_ring_queue_push(&state->events, event);
}

SlStatus sl_log_memory_sink_create(SlArena* arena, size_t capacity, SlLogSink** out_sink)
{
    SlLogSink* sink = NULL;
    SlLogMemorySinkState* state = NULL;
    SlSlice event_storage = {0};
    void* sink_memory = NULL;
    void* state_memory = NULL;
    SlStatus status;

    if (arena == NULL || out_sink == NULL || capacity == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_sink = NULL;
    status = sl_arena_alloc(arena, sizeof(SlLogSink), _Alignof(SlLogSink), &sink_memory);
    if (sl_status_is_ok(status)) {
        status = sl_arena_alloc(arena, sizeof(SlLogMemorySinkState), _Alignof(SlLogMemorySinkState),
                                &state_memory);
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (sink_memory == NULL || state_memory == NULL) {
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }
    sink = (SlLogSink*)sink_memory;
    state = (SlLogMemorySinkState*)state_memory;
    *sink = (SlLogSink){0};
    *state = (SlLogMemorySinkState){0};

    status = sl_platform_mutex_create(arena, &sink->mutex);
    if (sl_status_is_ok(status)) {
        status = sl_arena_array_alloc(arena, capacity, sizeof(SlLogEvent), _Alignof(SlLogEvent),
                                      &event_storage);
    }
    if (sl_status_is_ok(status)) {
        status =
            sl_ring_queue_init(&state->events, event_storage.ptr, sizeof(SlLogEvent), capacity);
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }
    sink->kind = SL_LOG_SINK_MEMORY;
    sink->state = state;
    sink->write = sl_log_memory_sink_write;
    *out_sink = sink;
    return sl_status_ok();
}

SlStatus sl_log_memory_sink_snapshot(const SlLogSink* sink, SlArena* arena,
                                     SlLogMemorySnapshot* out_snapshot)
{
    const SlLogMemorySinkState* state =
        sink == NULL ? NULL : (const SlLogMemorySinkState*)sink->state;
    SlSlice storage = {0};
    SlLogEvent* events = NULL;
    size_t index = 0U;
    SlStatus status;

    if (sink == NULL || sink->kind != SL_LOG_SINK_MEMORY || sink->mutex == NULL || state == NULL ||
        arena == NULL || out_snapshot == NULL)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_snapshot = (SlLogMemorySnapshot){0};
    sl_platform_mutex_lock(sink->mutex);
    if (state->events.count == 0U) {
        out_snapshot->overwritten_events = state->overwritten_events;
        sl_platform_mutex_unlock(sink->mutex);
        return sl_status_ok();
    }
    status = sl_arena_array_alloc(arena, state->events.count, sizeof(SlLogEvent),
                                  _Alignof(SlLogEvent), &storage);
    if (!sl_status_is_ok(status)) {
        sl_platform_mutex_unlock(sink->mutex);
        return status;
    }
    if (storage.ptr == NULL) {
        sl_platform_mutex_unlock(sink->mutex);
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }
    events = (SlLogEvent*)storage.ptr;
    for (index = 0U; index < state->events.count; index += 1U) {
        size_t queue_index = (state->events.head + index) % state->events.capacity;
        events[index] = ((const SlLogEvent*)state->events.items)[queue_index];
    }
    out_snapshot->events = events;
    out_snapshot->count = state->events.count;
    out_snapshot->overwritten_events = state->overwritten_events;
    sl_platform_mutex_unlock(sink->mutex);
    return sl_status_ok();
}

static SlStatus sl_log_console_format_pretty(const SlLogEvent* event, char* buffer, size_t capacity,
                                             SlBytes* out)
{
    SlStringBuilder builder = {0};
    SlStatus status = sl_string_builder_init_fixed(&builder, buffer, capacity);
    size_t index = 0U;
    SlStr view;

    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_append_char(&builder, '[');
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_str(&builder, sl_log_level_name(event->level));
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_cstr(&builder, "] ");
    }
    if (sl_status_is_ok(status) && event->category.length != 0U) {
        status = sl_string_builder_append_str(&builder, sl_log_event_category(event));
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(&builder, ": ");
        }
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_str(&builder, sl_log_event_message(event));
    }
    for (index = 0U; sl_status_is_ok(status) && index < event->field_count; index += 1U) {
        const SlLogField* field = &event->fields[index];
        status = sl_string_builder_append_char(&builder, ' ');
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_str(&builder, sl_log_field_key(field));
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_char(&builder, '=');
        }
        if (sl_status_is_ok(status)) {
            if (field->redacted || field->kind == SL_LOG_FIELD_STRING ||
                field->kind == SL_LOG_FIELD_JSON)
            {
                status = sl_string_builder_append_str(&builder, sl_log_field_text_value(field));
            }
            else if (field->kind == SL_LOG_FIELD_BOOL) {
                status =
                    sl_string_builder_append_cstr(&builder, field->bool_value ? "true" : "false");
            }
            else if (field->kind == SL_LOG_FIELD_I64) {
                status = sl_string_builder_append_i64(&builder, field->i64_value);
            }
            else if (field->kind == SL_LOG_FIELD_F64) {
                status = sl_string_builder_append_f64(&builder, field->f64_value);
            }
            else {
                status = sl_string_builder_append_cstr(&builder, "null");
            }
        }
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_char(&builder, '\n');
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }
    view = sl_string_builder_view(&builder);
    *out = sl_bytes_from_parts((const unsigned char*)view.ptr, view.length);
    return sl_status_ok();
}

static SlStatus sl_log_console_sink_write(SlLogSink* sink, const SlLogEvent* event)
{
    SlLogConsoleSinkState* state = sink == NULL ? NULL : (SlLogConsoleSinkState*)sink->state;
    char buffer[SL_LOG_MAX_JSONL_BYTES];
    SlBytes bytes = {0};
    SlStatus status;

    if (state == NULL || event == NULL || state->writer == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = state->format == SL_LOG_CONSOLE_FORMAT_JSONL
                 ? sl_log_event_serialize_jsonl(event, buffer, sizeof(buffer), &bytes)
                 : sl_log_console_format_pretty(event, buffer, sizeof(buffer), &bytes);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return state->writer(bytes, state->writer_user);
}

SlStatus sl_log_console_sink_create(SlArena* arena, SlLogConsoleFormat format,
                                    SlLogBytesWriteFn writer, void* writer_user,
                                    SlLogSink** out_sink)
{
    SlLogSink* sink = NULL;
    SlLogConsoleSinkState* state = NULL;
    void* sink_memory = NULL;
    void* state_memory = NULL;
    SlStatus status;

    if (arena == NULL || writer == NULL || out_sink == NULL ||
        (format != SL_LOG_CONSOLE_FORMAT_PRETTY && format != SL_LOG_CONSOLE_FORMAT_JSONL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_sink = NULL;
    status = sl_arena_alloc(arena, sizeof(SlLogSink), _Alignof(SlLogSink), &sink_memory);
    if (sl_status_is_ok(status)) {
        status = sl_arena_alloc(arena, sizeof(SlLogConsoleSinkState),
                                _Alignof(SlLogConsoleSinkState), &state_memory);
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (sink_memory == NULL || state_memory == NULL) {
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }
    sink = (SlLogSink*)sink_memory;
    state = (SlLogConsoleSinkState*)state_memory;
    *sink = (SlLogSink){0};
    *state = (SlLogConsoleSinkState){0};
    status = sl_platform_mutex_create(arena, &sink->mutex);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    state->format = format;
    state->writer = writer;
    state->writer_user = writer_user;
    sink->kind = SL_LOG_SINK_CONSOLE;
    sink->state = state;
    sink->write = sl_log_console_sink_write;
    *out_sink = sink;
    return sl_status_ok();
}

static SlStatus sl_log_file_sink_flush(SlLogSink* sink)
{
    SlLogFileSinkState* state = sink == NULL ? NULL : (SlLogFileSinkState*)sink->state;
    SlStatus status;

    if (state == NULL || state->file == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (state->length != 0U) {
        status =
            sl_fs_file_write(state->file, sl_bytes_from_parts(state->buffer, state->length), NULL);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        state->length = 0U;
    }
    return sl_fs_file_flush(state->file, NULL);
}

static SlStatus sl_log_file_sink_append(SlLogFileSinkState* state, SlBytes bytes)
{
    SlStatus status;
    SlLogSink fake_sink = {0};
    size_t index = 0U;

    if (state == NULL || bytes.length == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    fake_sink.state = state;
    if (bytes.length > state->capacity) {
        status = sl_log_file_sink_flush(&fake_sink);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    if (bytes.length > state->capacity) {
        return sl_fs_file_write(state->file, bytes, NULL);
    }
    if (bytes.length > state->capacity - state->length) {
        status = sl_log_file_sink_flush(&fake_sink);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    for (index = 0U; index < bytes.length; index += 1U) {
        state->buffer[state->length + index] = bytes.ptr[index];
    }
    state->length += bytes.length;
    return sl_status_ok();
}

static SlStatus sl_log_file_sink_write(SlLogSink* sink, const SlLogEvent* event)
{
    SlLogFileSinkState* state = sink == NULL ? NULL : (SlLogFileSinkState*)sink->state;
    char serialized[SL_LOG_MAX_JSONL_BYTES];
    SlBytes bytes = {0};
    SlStatus status;

    if (state == NULL || event == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_log_event_serialize_jsonl(event, serialized, sizeof(serialized), &bytes);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_log_file_sink_append(state, bytes);
}

static void sl_log_file_sink_close(SlLogSink* sink)
{
    SlLogFileSinkState* state = sink == NULL ? NULL : (SlLogFileSinkState*)sink->state;

    if (state == NULL || state->file == NULL) {
        return;
    }
    (void)sl_log_file_sink_flush(sink);
    (void)sl_fs_file_close(state->file, NULL);
    state->file = NULL;
}

SlStatus sl_log_file_sink_create(SlArena* arena, SlStr path, size_t buffer_bytes,
                                 SlLogSink** out_sink)
{
    SlLogSink* sink = NULL;
    SlLogFileSinkState* state = NULL;
    SlSlice buffer_storage = {0};
    void* sink_memory = NULL;
    void* state_memory = NULL;
    SlStatus status;

    if (arena == NULL || out_sink == NULL || !sl_log_text_valid(path) || path.length == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_sink = NULL;
    if (buffer_bytes == 0U) {
        buffer_bytes = SL_LOG_DEFAULT_FILE_BUFFER_BYTES;
    }
    status = sl_arena_alloc(arena, sizeof(SlLogSink), _Alignof(SlLogSink), &sink_memory);
    if (sl_status_is_ok(status)) {
        status = sl_arena_alloc(arena, sizeof(SlLogFileSinkState), _Alignof(SlLogFileSinkState),
                                &state_memory);
    }
    if (sl_status_is_ok(status)) {
        status = sl_arena_array_alloc(arena, buffer_bytes, sizeof(unsigned char),
                                      _Alignof(unsigned char), &buffer_storage);
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (sink_memory == NULL || state_memory == NULL || buffer_storage.ptr == NULL) {
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }
    sink = (SlLogSink*)sink_memory;
    state = (SlLogFileSinkState*)state_memory;
    *sink = (SlLogSink){0};
    *state = (SlLogFileSinkState){0};
    state->buffer = (unsigned char*)buffer_storage.ptr;
    state->capacity = buffer_bytes;
    status = sl_platform_mutex_create(arena, &sink->mutex);
    if (sl_status_is_ok(status)) {
        status = sl_fs_open_file(arena, path, SL_FS_FILE_ACCESS_APPEND, true, &state->file, NULL);
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }
    sink->kind = SL_LOG_SINK_FILE;
    sink->state = state;
    sink->write = sl_log_file_sink_write;
    sink->flush = sl_log_file_sink_flush;
    sink->close = sl_log_file_sink_close;
    *out_sink = sink;
    return sl_status_ok();
}

static SlStatus sl_log_custom_sink_write(SlLogSink* sink, const SlLogEvent* event)
{
    if (sink == NULL || sink->custom_write == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sink->custom_write(sink->state, event);
}

static SlStatus sl_log_custom_sink_flush(SlLogSink* sink)
{
    if (sink == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sink->custom_flush == NULL ? sl_status_ok() : sink->custom_flush(sink->state);
}

static void sl_log_custom_sink_close(SlLogSink* sink)
{
    if (sink != NULL && sink->custom_close != NULL) {
        sink->custom_close(sink->state);
    }
}

SlStatus sl_log_custom_sink_create(SlArena* arena, SlLogCustomSinkWriteFn write,
                                   SlLogCustomSinkFlushFn flush, SlLogCustomSinkCloseFn close,
                                   void* state, SlLogSink** out_sink)
{
    SlLogSink* sink = NULL;
    void* sink_memory = NULL;
    SlStatus status;

    if (arena == NULL || out_sink == NULL || write == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_sink = NULL;
    status = sl_arena_alloc(arena, sizeof(SlLogSink), _Alignof(SlLogSink), &sink_memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    sink = (SlLogSink*)sink_memory;
    *sink = (SlLogSink){0};
    status = sl_platform_mutex_create(arena, &sink->mutex);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    sink->kind = SL_LOG_SINK_CUSTOM;
    sink->state = state;
    sink->write = sl_log_custom_sink_write;
    sink->flush = sl_log_custom_sink_flush;
    sink->close = sl_log_custom_sink_close;
    sink->custom_write = write;
    sink->custom_flush = flush;
    sink->custom_close = close;
    *out_sink = sink;
    return sl_status_ok();
}

SlLogSinkSnapshot sl_log_sink_snapshot(const SlLogSink* sink)
{
    SlLogSinkSnapshot snapshot = {0};

    if (sink == NULL) {
        return snapshot;
    }
    if (sink->mutex == NULL) {
        return snapshot;
    }
    sl_platform_mutex_lock(sink->mutex);
    snapshot.kind = sink->kind;
    snapshot.write_count = sink->write_count;
    snapshot.failure_count = sink->failure_count;
    snapshot.closed = sink->closed;
    sl_platform_mutex_unlock(sink->mutex);
    return snapshot;
}
