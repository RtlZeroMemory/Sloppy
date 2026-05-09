#ifndef SLOPPY_LOGGING_H
#define SLOPPY_LOGGING_H

#include "sloppy/arena.h"
#include "sloppy/bytes.h"
#include "sloppy/fs.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_LOG_MAX_FIELDS 8U
#define SL_LOG_MAX_CATEGORY_BYTES 96U
#define SL_LOG_MAX_MESSAGE_BYTES 256U
#define SL_LOG_MAX_REQUEST_ID_BYTES 96U
#define SL_LOG_MAX_ROUTE_NAME_BYTES 96U
#define SL_LOG_MAX_ROUTE_PATTERN_BYTES 128U
#define SL_LOG_MAX_FIELD_KEY_BYTES 64U
#define SL_LOG_MAX_FIELD_VALUE_BYTES 160U
#define SL_LOG_MAX_JSONL_BYTES 4096U
#define SL_LOG_DEFAULT_QUEUE_CAPACITY 16U
#define SL_LOG_DEFAULT_SINK_CAPACITY 8U
#define SL_LOG_DEFAULT_MEMORY_CAPACITY 32U
#define SL_LOG_DEFAULT_FILE_BUFFER_BYTES 4096U

typedef enum SlLogLevel
{
    SL_LOG_LEVEL_TRACE = 0,
    SL_LOG_LEVEL_DEBUG = 1,
    SL_LOG_LEVEL_INFO = 2,
    SL_LOG_LEVEL_WARN = 3,
    SL_LOG_LEVEL_ERROR = 4,
    SL_LOG_LEVEL_OFF = 5
} SlLogLevel;

typedef enum SlLogFieldKind
{
    SL_LOG_FIELD_NULL = 0,
    SL_LOG_FIELD_BOOL = 1,
    SL_LOG_FIELD_I64 = 2,
    SL_LOG_FIELD_F64 = 3,
    SL_LOG_FIELD_STRING = 4,
    SL_LOG_FIELD_JSON = 5
} SlLogFieldKind;

typedef enum SlLogBackpressurePolicy
{
    SL_LOG_BACKPRESSURE_DROP_NEW = 0,
    SL_LOG_BACKPRESSURE_DROP_OLDEST = 1
} SlLogBackpressurePolicy;

typedef enum SlLogSinkKind
{
    SL_LOG_SINK_MEMORY = 0,
    SL_LOG_SINK_CONSOLE = 1,
    SL_LOG_SINK_FILE = 2,
    SL_LOG_SINK_CUSTOM = 3
} SlLogSinkKind;

typedef enum SlLogConsoleFormat
{
    SL_LOG_CONSOLE_FORMAT_PRETTY = 0,
    SL_LOG_CONSOLE_FORMAT_JSONL = 1
} SlLogConsoleFormat;

typedef struct SlLogText
{
    char bytes[SL_LOG_MAX_FIELD_VALUE_BYTES];
    size_t length;
} SlLogText;

typedef struct SlLogShortText
{
    char bytes[SL_LOG_MAX_FIELD_KEY_BYTES];
    size_t length;
} SlLogShortText;

typedef struct SlLogMessageText
{
    char bytes[SL_LOG_MAX_MESSAGE_BYTES];
    size_t length;
} SlLogMessageText;

typedef struct SlLogCategoryText
{
    char bytes[SL_LOG_MAX_CATEGORY_BYTES];
    size_t length;
} SlLogCategoryText;

typedef struct SlLogRequestIdText
{
    char bytes[SL_LOG_MAX_REQUEST_ID_BYTES];
    size_t length;
} SlLogRequestIdText;

typedef struct SlLogRouteNameText
{
    char bytes[SL_LOG_MAX_ROUTE_NAME_BYTES];
    size_t length;
} SlLogRouteNameText;

typedef struct SlLogRoutePatternText
{
    char bytes[SL_LOG_MAX_ROUTE_PATTERN_BYTES];
    size_t length;
} SlLogRoutePatternText;

typedef struct SlLogField
{
    SlLogShortText key;
    SlLogText value;
    SlLogFieldKind kind;
    bool redacted;
    bool bool_value;
    int64_t i64_value;
    double f64_value;
} SlLogField;

typedef struct SlLogEvent
{
    uint64_t sequence;
    uint64_t timestamp_ns;
    SlLogLevel level;
    SlLogCategoryText category;
    SlLogMessageText message;
    SlLogRequestIdText request_id;
    SlLogRouteNameText route_name;
    SlLogRoutePatternText route_pattern;
    SlLogField fields[SL_LOG_MAX_FIELDS];
    size_t field_count;
    size_t redacted_count;
} SlLogEvent;

typedef struct SlLogEventBuilder
{
    SlLogEvent event;
    bool finished;
} SlLogEventBuilder;

typedef struct SlLogRuntime SlLogRuntime;
typedef struct SlLogSink SlLogSink;

typedef SlStatus (*SlLogBytesWriteFn)(SlBytes bytes, void* user);
typedef SlStatus (*SlLogCustomSinkWriteFn)(SlLogSink* sink, const SlLogEvent* event);
typedef SlStatus (*SlLogCustomSinkFlushFn)(SlLogSink* sink);
typedef void (*SlLogCustomSinkCloseFn)(SlLogSink* sink);

typedef struct SlLogRuntimeConfig
{
    SlLogLevel minimum_level;
    size_t queue_capacity;
    size_t sink_capacity;
    SlLogBackpressurePolicy backpressure_policy;
    const SlStr* redaction_keys;
    size_t redaction_key_count;
} SlLogRuntimeConfig;

typedef struct SlLogRuntimeSnapshot
{
    SlLogLevel minimum_level;
    size_t queued_events;
    size_t in_flight_events;
    size_t sink_count;
    uint64_t submitted_events;
    uint64_t dispatched_events;
    uint64_t dropped_events;
    uint64_t dropped_new_events;
    uint64_t dropped_oldest_events;
    uint64_t sink_failures;
    bool accepting;
    bool dispatcher_started;
    bool shutdown;
} SlLogRuntimeSnapshot;

typedef struct SlLogMemorySnapshot
{
    SlLogEvent* events;
    size_t count;
    uint64_t overwritten_events;
} SlLogMemorySnapshot;

typedef struct SlLogSinkSnapshot
{
    SlLogSinkKind kind;
    uint64_t write_count;
    uint64_t failure_count;
    bool closed;
} SlLogSinkSnapshot;

SlLogRuntimeConfig sl_log_runtime_config_default(void);
SlStatus sl_log_runtime_create(SlArena* arena, const SlLogRuntimeConfig* config,
                               SlLogRuntime** out_runtime);
SlStatus sl_log_runtime_add_sink(SlLogRuntime* runtime, SlLogSink* sink);
SlStatus sl_log_runtime_start(SlLogRuntime* runtime);
bool sl_log_runtime_is_enabled(const SlLogRuntime* runtime, SlLogLevel level);
SlStatus sl_log_runtime_submit(SlLogRuntime* runtime, const SlLogEvent* event);
SlStatus sl_log_runtime_drain(SlLogRuntime* runtime);
SlStatus sl_log_runtime_flush(SlLogRuntime* runtime);
SlStatus sl_log_runtime_shutdown(SlLogRuntime* runtime);
SlLogRuntimeSnapshot sl_log_runtime_snapshot(const SlLogRuntime* runtime);

SlStatus sl_log_memory_sink_create(SlArena* arena, size_t capacity, SlLogSink** out_sink);
SlStatus sl_log_memory_sink_snapshot(const SlLogSink* sink, SlArena* arena,
                                     SlLogMemorySnapshot* out_snapshot);
SlStatus sl_log_console_sink_create(SlArena* arena, SlLogConsoleFormat format,
                                    SlLogBytesWriteFn writer, void* writer_user,
                                    SlLogSink** out_sink);
SlStatus sl_log_file_sink_create(SlArena* arena, SlStr path, size_t buffer_bytes,
                                 SlLogSink** out_sink);
SlStatus sl_log_custom_sink_create(SlArena* arena, SlLogSinkKind kind, SlLogCustomSinkWriteFn write,
                                   SlLogCustomSinkFlushFn flush, SlLogCustomSinkCloseFn close,
                                   void* state, SlLogSink** out_sink);
SlLogSinkSnapshot sl_log_sink_snapshot(const SlLogSink* sink);

SlStatus sl_log_event_builder_init(SlLogEventBuilder* builder, SlLogLevel level, SlStr message);
SlStatus sl_log_event_builder_set_category(SlLogEventBuilder* builder, SlStr category);
SlStatus sl_log_event_builder_set_request_id(SlLogEventBuilder* builder, SlStr request_id);
SlStatus sl_log_event_builder_set_route(SlLogEventBuilder* builder, SlStr route_name,
                                        SlStr route_pattern);
SlStatus sl_log_event_builder_add_null(SlLogEventBuilder* builder, SlStr key);
SlStatus sl_log_event_builder_add_bool(SlLogEventBuilder* builder, SlStr key, bool value);
SlStatus sl_log_event_builder_add_i64(SlLogEventBuilder* builder, SlStr key, int64_t value);
SlStatus sl_log_event_builder_add_f64(SlLogEventBuilder* builder, SlStr key, double value);
SlStatus sl_log_event_builder_add_string(SlLogEventBuilder* builder, SlStr key, SlStr value);
SlStatus sl_log_event_builder_add_json(SlLogEventBuilder* builder, SlStr key, SlStr value);
SlStatus sl_log_event_builder_finish(SlLogEventBuilder* builder, SlLogEvent* out_event);

SlStr sl_log_text_view(const char* bytes, size_t length);
SlStr sl_log_event_category(const SlLogEvent* event);
SlStr sl_log_event_message(const SlLogEvent* event);
SlStr sl_log_event_request_id(const SlLogEvent* event);
SlStr sl_log_event_route_name(const SlLogEvent* event);
SlStr sl_log_event_route_pattern(const SlLogEvent* event);
SlStr sl_log_field_key(const SlLogField* field);
SlStr sl_log_field_text_value(const SlLogField* field);

SlStr sl_log_level_name(SlLogLevel level);
SlStatus sl_log_level_from_str(SlStr text, SlLogLevel* out_level);
bool sl_log_level_enabled(SlLogLevel minimum_level, SlLogLevel level);
bool sl_log_key_is_sensitive(SlStr key, const SlStr* extra_keys, size_t extra_key_count);
SlStatus sl_log_event_apply_redaction(SlLogEvent* event, const SlStr* extra_keys,
                                      size_t extra_key_count);
SlStatus sl_log_event_serialize_jsonl(const SlLogEvent* event, char* buffer, size_t capacity,
                                      SlBytes* out_bytes);

#ifdef __cplusplus
}
#endif

#endif
