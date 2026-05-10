#include "sloppy/logging.h"
#include "sloppy/platform_thread.h"

#include <stdbool.h>
#include <string.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static bool bytes_contains(SlBytes bytes, const char* needle)
{
    size_t needle_len = strlen(needle);
    size_t index = 0U;

    if (needle_len == 0U || bytes.ptr == NULL || bytes.length < needle_len) {
        return false;
    }
    for (index = 0U; index <= bytes.length - needle_len; index += 1U) {
        if (memcmp(bytes.ptr + index, needle, needle_len) == 0) {
            return true;
        }
    }
    return false;
}

static bool str_contains(SlStr haystack, const char* needle)
{
    return bytes_contains(sl_bytes_from_parts((const unsigned char*)haystack.ptr, haystack.length),
                          needle);
}

static bool json_parseable(SlBytes bytes)
{
    SlLogEventBuilder validator = {0};

    if (bytes.ptr == NULL || bytes.length == 0U) {
        return false;
    }
    return sl_status_is_ok(sl_log_event_builder_init(&validator, SL_LOG_LEVEL_INFO,
                                                     sl_str_from_cstr("validator"))) &&
           sl_status_is_ok(sl_log_event_builder_add_json(
               &validator, sl_str_from_cstr("line"),
               sl_str_from_parts((const char*)bytes.ptr, bytes.length)));
}

static SlStatus build_login_event(SlLogLevel level, SlLogEvent* out_event)
{
    SlLogEventBuilder builder = {0};
    SlStatus status =
        sl_log_event_builder_init(&builder, level, sl_str_from_cstr("user login completed"));

    if (sl_status_is_ok(status)) {
        status = sl_log_event_builder_set_category(&builder, sl_str_from_cstr("auth"));
    }
    if (sl_status_is_ok(status)) {
        status = sl_log_event_builder_set_request_id(&builder, sl_str_from_cstr("req-123"));
    }
    if (sl_status_is_ok(status)) {
        status = sl_log_event_builder_set_route(&builder, sl_str_from_cstr("login"),
                                                sl_str_from_cstr("/login"));
    }
    if (sl_status_is_ok(status)) {
        status = sl_log_event_builder_add_string(&builder, sl_str_from_cstr("userId"),
                                                 sl_str_from_cstr("u-42"));
    }
    if (sl_status_is_ok(status)) {
        status = sl_log_event_builder_add_i64(&builder, sl_str_from_cstr("attempt"), 7);
    }
    if (sl_status_is_ok(status)) {
        status = sl_log_event_builder_add_bool(&builder, sl_str_from_cstr("success"), true);
    }
    if (sl_status_is_ok(status)) {
        status = sl_log_event_builder_add_null(&builder, sl_str_from_cstr("tenant"));
    }
    if (sl_status_is_ok(status)) {
        status = sl_log_event_builder_add_f64(&builder, sl_str_from_cstr("elapsedMs"), 4.5);
    }
    if (sl_status_is_ok(status)) {
        status = sl_log_event_builder_add_string(&builder, sl_str_from_cstr("apiKey"),
                                                 sl_str_from_cstr("secret-api-key"));
    }
    if (sl_status_is_ok(status)) {
        status = sl_log_event_builder_finish(&builder, out_event);
    }
    return status;
}

static int test_event_builder_fields_and_redaction(void)
{
    SlLogEvent event = {0};
    SlLogEventBuilder builder = {0};
    size_t index = 0U;

    if (expect_status(build_login_event(SL_LOG_LEVEL_INFO, &event), SL_STATUS_OK) != 0 ||
        event.field_count != 6U ||
        !sl_str_equal(sl_log_event_category(&event), sl_str_from_cstr("auth")) ||
        !sl_str_equal(sl_log_event_message(&event), sl_str_from_cstr("user login completed")) ||
        !sl_str_equal(sl_log_event_request_id(&event), sl_str_from_cstr("req-123")) ||
        !sl_str_equal(sl_log_event_route_pattern(&event), sl_str_from_cstr("/login")))
    {
        return 1;
    }

    if (expect_status(sl_log_event_apply_redaction(&event, NULL, 0U), SL_STATUS_OK) != 0 ||
        event.redacted_count != 1U || !event.fields[5].redacted ||
        !sl_str_equal(sl_log_field_text_value(&event.fields[5]), sl_str_from_cstr("[REDACTED]")))
    {
        return 2;
    }

    if (!sl_log_key_is_sensitive(sl_str_from_cstr("client_secret"), NULL, 0U) ||
        !sl_log_key_is_sensitive(sl_str_from_cstr("Authorization"), NULL, 0U) ||
        !sl_log_key_is_sensitive(sl_str_from_cstr("request.headers.authorization"), NULL, 0U) ||
        !sl_log_key_is_sensitive(sl_str_from_cstr("request.cookie"), NULL, 0U) ||
        !sl_log_key_is_sensitive(sl_str_from_cstr("response.setCookie"), NULL, 0U) ||
        !sl_log_key_is_sensitive(sl_str_from_cstr("db.connectionString"), NULL, 0U))
    {
        return 3;
    }

    if (expect_status(
            sl_log_event_builder_init(&builder, SL_LOG_LEVEL_DEBUG, sl_str_from_cstr("bounded")),
            SL_STATUS_OK) != 0)
    {
        return 4;
    }
    for (index = 0U; index < SL_LOG_MAX_FIELDS; index += 1U) {
        if (expect_status(
                sl_log_event_builder_add_i64(&builder, sl_str_from_cstr("field"), (int64_t)index),
                SL_STATUS_OK) != 0)
        {
            return 5;
        }
    }
    if (expect_status(sl_log_event_builder_add_i64(&builder, sl_str_from_cstr("overflow"), 1),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0)
    {
        return 6;
    }
    return 0;
}

static int test_jsonl_serialization(void)
{
    SlLogEvent event = {0};
    char buffer[SL_LOG_MAX_JSONL_BYTES];
    SlBytes bytes = {0};

    if (expect_status(build_login_event(SL_LOG_LEVEL_WARN, &event), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_event_apply_redaction(&event, NULL, 0U), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_event_serialize_jsonl(&event, buffer, sizeof(buffer), &bytes),
                      SL_STATUS_OK) != 0)
    {
        return 10;
    }

    if (!bytes_contains(bytes, "\"level\":\"warn\"") ||
        !bytes_contains(bytes, "\"category\":\"auth\"") ||
        !bytes_contains(bytes, "\"requestId\":\"req-123\"") ||
        !bytes_contains(bytes, "\"apiKey\":\"[REDACTED]\"") || bytes.ptr[bytes.length - 1U] != '\n')
    {
        return 11;
    }
    return 0;
}

static int test_json_field_validation_and_parseable_jsonl(void)
{
    SlLogEventBuilder builder = {0};
    SlLogEventBuilder redacted_builder = {0};
    SlLogEvent event = {0};
    SlLogEvent redacted_event = {0};
    char buffer[SL_LOG_MAX_JSONL_BYTES];
    char redacted_buffer[SL_LOG_MAX_JSONL_BYTES];
    SlBytes bytes = {0};
    SlBytes redacted_bytes = {0};

    if (expect_status(sl_log_event_builder_init(&builder, SL_LOG_LEVEL_INFO,
                                                sl_str_from_cstr("json payloads")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_event_builder_add_json(&builder, sl_str_from_cstr("object"),
                                                    sl_str_from_cstr("{\"ok\":true}")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_event_builder_add_json(&builder, sl_str_from_cstr("array"),
                                                    sl_str_from_cstr("[1,2,3]")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_event_builder_add_json(&builder, sl_str_from_cstr("scalar"),
                                                    sl_str_from_cstr("42")),
                      SL_STATUS_OK) != 0)
    {
        return 12;
    }

    if (builder.event.field_count != 3U) {
        return 13;
    }

    if (expect_status(sl_log_event_builder_add_json(&builder, sl_str_from_cstr("bad"),
                                                    sl_str_from_cstr("{bad")),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 14;
    }

    if (builder.event.field_count != 3U) {
        return 15;
    }

    if (expect_status(sl_log_event_builder_finish(&builder, &event), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_event_serialize_jsonl(&event, buffer, sizeof(buffer), &bytes),
                      SL_STATUS_OK) != 0)
    {
        return 16;
    }

    if (!bytes_contains(bytes, "\"object\":{\"ok\":true}") ||
        !bytes_contains(bytes, "\"array\":[1,2,3]") || !bytes_contains(bytes, "\"scalar\":42") ||
        !json_parseable(bytes))
    {
        return 17;
    }

    if (expect_status(sl_log_event_builder_init(&redacted_builder, SL_LOG_LEVEL_INFO,
                                                sl_str_from_cstr("redacted json")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_event_builder_add_json(&redacted_builder, sl_str_from_cstr("token"),
                                                    sl_str_from_cstr("{\"token\":\"abc\"}")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_event_builder_finish(&redacted_builder, &redacted_event),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_event_apply_redaction(&redacted_event, NULL, 0U), SL_STATUS_OK) != 0 ||
        redacted_event.redacted_count != 1U ||
        expect_status(sl_log_event_serialize_jsonl(&redacted_event, redacted_buffer,
                                                   sizeof(redacted_buffer), &redacted_bytes),
                      SL_STATUS_OK) != 0)
    {
        return 18;
    }
    if (!bytes_contains(redacted_bytes, "\"token\":\"[REDACTED]\"") ||
        !json_parseable(redacted_bytes))
    {
        return 19;
    }
    return 0;
}

static int test_runtime_memory_queue_pressure(void)
{
    unsigned char storage[131072];
    unsigned char snapshot_storage[32768];
    SlArena arena = {0};
    SlArena snapshot_arena = {0};
    SlLogRuntimeConfig config = sl_log_runtime_config_default();
    SlLogRuntime* runtime = NULL;
    SlLogSink* memory = NULL;
    SlLogMemorySnapshot memory_snapshot = {0};
    SlLogRuntimeSnapshot runtime_snapshot = {0};
    SlLogEvent event = {0};

    config.queue_capacity = 2U;
    config.sink_capacity = 1U;
    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&snapshot_arena, snapshot_storage, sizeof(snapshot_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_create(&arena, &config, &runtime), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_memory_sink_create(&arena, 4U, &memory), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_add_sink(runtime, memory), SL_STATUS_OK) != 0 ||
        expect_status(build_login_event(SL_LOG_LEVEL_INFO, &event), SL_STATUS_OK) != 0)
    {
        return 20;
    }

    if (expect_status(sl_log_runtime_submit(runtime, &event), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_submit(runtime, &event), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_submit(runtime, &event), SL_STATUS_OK) != 0)
    {
        return 21;
    }
    runtime_snapshot = sl_log_runtime_snapshot(runtime);
    if (runtime_snapshot.submitted_events != 2U || runtime_snapshot.dropped_new_events != 1U) {
        return 22;
    }
    if (expect_status(sl_log_runtime_flush(runtime), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_memory_sink_snapshot(memory, &snapshot_arena, &memory_snapshot),
                      SL_STATUS_OK) != 0 ||
        memory_snapshot.count != 2U || memory_snapshot.events[0].sequence != 1U ||
        memory_snapshot.events[1].sequence != 2U)
    {
        return 23;
    }
    if (expect_status(sl_log_runtime_shutdown(runtime), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_submit(runtime, &event), SL_STATUS_INVALID_STATE) != 0)
    {
        return 24;
    }
    return 0;
}

static int test_runtime_shutdown_public_apis_remain_safe(void)
{
    unsigned char storage[131072];
    unsigned char snapshot_storage[32768];
    SlArena arena = {0};
    SlArena snapshot_arena = {0};
    SlLogRuntimeConfig config = sl_log_runtime_config_default();
    SlLogRuntime* runtime = NULL;
    SlLogSink* memory = NULL;
    SlLogMemorySnapshot memory_snapshot = {0};
    SlLogRuntimeSnapshot runtime_snapshot = {0};
    SlLogEvent event = {0};

    config.queue_capacity = 4U;
    config.sink_capacity = 1U;
    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&snapshot_arena, snapshot_storage, sizeof(snapshot_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_create(&arena, &config, &runtime), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_memory_sink_create(&arena, 4U, &memory), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_add_sink(runtime, memory), SL_STATUS_OK) != 0 ||
        expect_status(build_login_event(SL_LOG_LEVEL_INFO, &event), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_submit(runtime, &event), SL_STATUS_OK) != 0)
    {
        return 25;
    }

    runtime_snapshot = sl_log_runtime_snapshot(runtime);
    if (runtime_snapshot.queued_events != 1U || runtime_snapshot.submitted_events != 1U ||
        runtime_snapshot.shutdown)
    {
        return 26;
    }

    if (expect_status(sl_log_runtime_flush(runtime), SL_STATUS_OK) != 0) {
        return 27;
    }
    runtime_snapshot = sl_log_runtime_snapshot(runtime);
    if (runtime_snapshot.queued_events != 0U || runtime_snapshot.dispatched_events != 1U ||
        runtime_snapshot.in_flight_events != 0U)
    {
        return 28;
    }

    if (expect_status(sl_log_runtime_shutdown(runtime), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_shutdown(runtime), SL_STATUS_OK) != 0)
    {
        return 29;
    }

    runtime_snapshot = sl_log_runtime_snapshot(runtime);
    if (!runtime_snapshot.shutdown || runtime_snapshot.accepting ||
        runtime_snapshot.dispatcher_started)
    {
        return 30;
    }

    if (expect_status(sl_log_runtime_submit(runtime, &event), SL_STATUS_INVALID_STATE) != 0 ||
        expect_status(sl_log_runtime_flush(runtime), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_drain(runtime), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_memory_sink_snapshot(memory, &snapshot_arena, &memory_snapshot),
                      SL_STATUS_OK) != 0 ||
        memory_snapshot.count != 1U)
    {
        return 31;
    }
    return 0;
}

static int test_runtime_sink_registration_lifecycle(void)
{
    unsigned char storage[131072];
    SlArena arena = {0};
    SlLogRuntimeConfig config = sl_log_runtime_config_default();
    SlLogRuntime* runtime = NULL;
    SlLogSink* first = NULL;
    SlLogSink* second = NULL;

    config.queue_capacity = 4U;
    config.sink_capacity = 2U;
    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_create(&arena, &config, &runtime), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_memory_sink_create(&arena, 4U, &first), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_memory_sink_create(&arena, 4U, &second), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_add_sink(runtime, first), SL_STATUS_OK) != 0)
    {
        return 32;
    }

    if (expect_status(sl_log_runtime_start(runtime), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_add_sink(runtime, second), SL_STATUS_INVALID_STATE) != 0)
    {
        return 33;
    }

    if (expect_status(sl_log_runtime_shutdown(runtime), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_add_sink(runtime, second), SL_STATUS_INVALID_STATE) != 0)
    {
        return 34;
    }
    return 0;
}

typedef struct ConsoleCapture
{
    char text[4096];
    size_t length;
} ConsoleCapture;

typedef struct CustomSinkState
{
    unsigned int writes;
    unsigned int flushes;
    unsigned int closes;
    const SlLogEvent* last_event;
    SlStatus write_status;
} CustomSinkState;

static SlStatus capture_writer(SlBytes bytes, void* user)
{
    ConsoleCapture* capture = (ConsoleCapture*)user;

    if (capture == NULL || bytes.ptr == NULL ||
        bytes.length > sizeof(capture->text) - capture->length)
    {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }
    memcpy(capture->text + capture->length, bytes.ptr, bytes.length);
    capture->length += bytes.length;
    return sl_status_ok();
}

static SlStatus custom_sink_write(void* state, const SlLogEvent* event)
{
    CustomSinkState* custom = (CustomSinkState*)state;

    if (custom == NULL || event == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    custom->writes += 1U;
    custom->last_event = event;
    return custom->write_status;
}

static SlStatus custom_sink_flush(void* state)
{
    CustomSinkState* custom = (CustomSinkState*)state;

    if (custom == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    custom->flushes += 1U;
    return sl_status_ok();
}

static void custom_sink_close(void* state)
{
    CustomSinkState* custom = (CustomSinkState*)state;

    if (custom != NULL) {
        custom->closes += 1U;
    }
}

static int test_custom_sink_state_and_failure_accounting(void)
{
    unsigned char storage[131072];
    unsigned char snapshot_storage[32768];
    SlArena arena = {0};
    SlArena snapshot_arena = {0};
    SlLogRuntimeConfig config = sl_log_runtime_config_default();
    SlLogRuntime* runtime = NULL;
    SlLogSink* custom = NULL;
    SlLogMemorySnapshot memory_snapshot = {0};
    SlLogSinkSnapshot sink_snapshot = {0};
    SlLogRuntimeSnapshot runtime_snapshot = {0};
    SlLogEvent event = {0};
    CustomSinkState state = {0};

    state.write_status = sl_status_from_code(SL_STATUS_INVALID_STATE);
    config.queue_capacity = 4U;
    config.sink_capacity = 1U;
    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&snapshot_arena, snapshot_storage, sizeof(snapshot_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_create(&arena, &config, &runtime), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_custom_sink_create(&arena, custom_sink_write, custom_sink_flush,
                                                custom_sink_close, &state, &custom),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_add_sink(runtime, custom), SL_STATUS_OK) != 0 ||
        expect_status(build_login_event(SL_LOG_LEVEL_INFO, &event), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_submit(runtime, &event), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_flush(runtime), SL_STATUS_OK) != 0)
    {
        return 35;
    }

    sink_snapshot = sl_log_sink_snapshot(custom);
    runtime_snapshot = sl_log_runtime_snapshot(runtime);
    if (state.writes != 1U || state.flushes != 1U || state.last_event == NULL ||
        sink_snapshot.kind != SL_LOG_SINK_CUSTOM || sink_snapshot.failure_count != 1U ||
        runtime_snapshot.sink_failures != 1U)
    {
        return 36;
    }

    if (expect_status(sl_log_memory_sink_snapshot(custom, &snapshot_arena, &memory_snapshot),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 37;
    }

    if (expect_status(sl_log_runtime_shutdown(runtime), SL_STATUS_OK) != 0 || state.closes != 1U ||
        expect_status(sl_log_runtime_shutdown(runtime), SL_STATUS_OK) != 0 || state.closes != 1U)
    {
        return 38;
    }
    return 0;
}

static int test_console_sink_pretty_and_jsonl(void)
{
    unsigned char storage[131072];
    SlArena arena = {0};
    SlLogRuntimeConfig config = sl_log_runtime_config_default();
    SlLogRuntime* runtime = NULL;
    SlLogSink* console = NULL;
    ConsoleCapture capture = {0};
    SlLogEvent event = {0};
    SlStr text = {0};

    config.queue_capacity = 4U;
    config.sink_capacity = 1U;
    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_create(&arena, &config, &runtime), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_console_sink_create(&arena, SL_LOG_CONSOLE_FORMAT_PRETTY,
                                                 capture_writer, &capture, &console),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_add_sink(runtime, console), SL_STATUS_OK) != 0 ||
        expect_status(build_login_event(SL_LOG_LEVEL_INFO, &event), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_submit(runtime, &event), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_flush(runtime), SL_STATUS_OK) != 0)
    {
        return 30;
    }
    text = sl_str_from_parts(capture.text, capture.length);
    if (!str_contains(text, "[info] auth: user login completed") ||
        !str_contains(text, "apiKey=[REDACTED]"))
    {
        return 31;
    }
    if (expect_status(sl_log_runtime_shutdown(runtime), SL_STATUS_OK) != 0) {
        return 32;
    }
    return 0;
}

static int test_file_sink_append_flush_and_missing_parent(void)
{
    unsigned char storage[131072];
    unsigned char read_storage[32768];
    SlArena arena = {0};
    SlArena read_arena = {0};
    SlLogRuntimeConfig config = sl_log_runtime_config_default();
    SlLogRuntime* runtime = NULL;
    SlLogSink* file = NULL;
    SlLogEvent event = {0};
    SlOwnedBytes bytes = {0};
    SlStr dir = sl_str_from_cstr("./sloppy-log-test");
    SlStr path = sl_str_from_cstr("./sloppy-log-test/app.jsonl");
    SlStr missing = sl_str_from_cstr("./sloppy-log-test-missing/app.jsonl");

    (void)sl_fs_delete_directory(dir, true, NULL);
    (void)sl_fs_delete_directory(sl_str_from_cstr("./sloppy-log-test-missing"), true, NULL);

    config.queue_capacity = 4U;
    config.sink_capacity = 1U;
    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&read_arena, read_storage, sizeof(read_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_fs_create_directory(dir, true, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_fs_write_file(path, sl_bytes_from_parts((const unsigned char*)"old\n", 4U),
                                       false, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_file_sink_create(&arena, missing, 128U, &file), SL_STATUS_OK) == 0)
    {
        return 40;
    }

    if (expect_status(sl_log_runtime_create(&arena, &config, &runtime), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_file_sink_create(&arena, path, 128U, &file), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_add_sink(runtime, file), SL_STATUS_OK) != 0 ||
        expect_status(build_login_event(SL_LOG_LEVEL_ERROR, &event), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_submit(runtime, &event), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_shutdown(runtime), SL_STATUS_OK) != 0 ||
        expect_status(sl_fs_read_file(&read_arena, path, &bytes, NULL), SL_STATUS_OK) != 0)
    {
        return 41;
    }

    if (!bytes_contains(sl_owned_bytes_as_view(bytes), "old\n") ||
        !bytes_contains(sl_owned_bytes_as_view(bytes), "\"level\":\"error\"") ||
        !bytes_contains(sl_owned_bytes_as_view(bytes), "\"apiKey\":\"[REDACTED]\""))
    {
        return 42;
    }

    (void)sl_fs_delete_directory(dir, true, NULL);
    return 0;
}

static int test_threaded_dispatch_shutdown(void)
{
    unsigned char storage[262144];
    unsigned char snapshot_storage[131072];
    SlArena arena = {0};
    SlArena snapshot_arena = {0};
    SlLogRuntimeConfig config = sl_log_runtime_config_default();
    SlLogRuntime* runtime = NULL;
    SlLogSink* memory = NULL;
    SlLogMemorySnapshot snapshot = {0};
    SlLogEvent event = {0};
    size_t index = 0U;

    config.queue_capacity = 32U;
    config.sink_capacity = 1U;
    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&snapshot_arena, snapshot_storage, sizeof(snapshot_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_create(&arena, &config, &runtime), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_memory_sink_create(&arena, 32U, &memory), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_add_sink(runtime, memory), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_start(runtime), SL_STATUS_OK) != 0 ||
        expect_status(build_login_event(SL_LOG_LEVEL_INFO, &event), SL_STATUS_OK) != 0)
    {
        return 50;
    }

    for (index = 0U; index < 20U; index += 1U) {
        if (expect_status(sl_log_runtime_submit(runtime, &event), SL_STATUS_OK) != 0) {
            return 51;
        }
    }
    if (expect_status(sl_log_runtime_flush(runtime), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_memory_sink_snapshot(memory, &snapshot_arena, &snapshot),
                      SL_STATUS_OK) != 0 ||
        snapshot.count != 20U || expect_status(sl_log_runtime_shutdown(runtime), SL_STATUS_OK) != 0)
    {
        return 52;
    }
    return 0;
}

typedef struct SubmitRaceState
{
    SlLogRuntime* runtime;
    SlLogEvent event;
    SlStatus last_status;
    size_t attempts;
    size_t max_attempts;
} SubmitRaceState;

typedef struct MultiProducerState
{
    SlLogRuntime* runtime;
    SlLogEvent event;
    SlStatus first_failure;
    size_t attempts;
} MultiProducerState;

typedef struct FlushFailureState
{
    unsigned int writes;
    unsigned int flushes;
    unsigned int closes;
    SlStatus flush_status;
} FlushFailureState;

typedef struct BlockingSinkState
{
    SlPlatformMutex* mutex;
    SlPlatformCond* cond;
    bool block_writes;
    bool entered;
    bool release;
    unsigned int writes;
    unsigned int flushes;
    unsigned int closes;
} BlockingSinkState;

typedef struct RuntimeCallState
{
    SlLogRuntime* runtime;
    SlStatus status;
} RuntimeCallState;

static void submit_race_main(void* user)
{
    SubmitRaceState* state = (SubmitRaceState*)user;

    if (state == NULL || state->runtime == NULL) {
        return;
    }
    size_t limit = state->max_attempts == 0U ? 4096U : state->max_attempts;

    for (state->attempts = 0U; state->attempts < limit; state->attempts += 1U) {
        state->last_status = sl_log_runtime_submit(state->runtime, &state->event);
        if (sl_status_code(state->last_status) == SL_STATUS_INVALID_STATE) {
            return;
        }
    }
}

static void multi_producer_main(void* user)
{
    MultiProducerState* state = (MultiProducerState*)user;
    size_t index = 0U;

    if (state == NULL || state->runtime == NULL) {
        return;
    }

    state->first_failure = sl_status_ok();
    for (index = 0U; index < state->attempts; index += 1U) {
        SlStatus status = sl_log_runtime_submit(state->runtime, &state->event);
        if (!sl_status_is_ok(status) && sl_status_is_ok(state->first_failure)) {
            state->first_failure = status;
        }
    }
}

static void runtime_flush_main(void* user)
{
    RuntimeCallState* state = (RuntimeCallState*)user;

    if (state != NULL && state->runtime != NULL) {
        state->status = sl_log_runtime_flush(state->runtime);
    }
}

static void runtime_shutdown_main(void* user)
{
    RuntimeCallState* state = (RuntimeCallState*)user;

    if (state != NULL && state->runtime != NULL) {
        state->status = sl_log_runtime_shutdown(state->runtime);
    }
}

static int test_submit_racing_shutdown_does_not_crash(void)
{
    static unsigned char storage[1048576];
    SlArena arena = {0};
    SlLogRuntimeConfig config = sl_log_runtime_config_default();
    SlLogRuntime* runtime = NULL;
    SlLogSink* memory = NULL;
    SlPlatformThread* thread = NULL;
    SubmitRaceState state = {0};

    config.queue_capacity = 64U;
    config.sink_capacity = 1U;
    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0) {
        return 53;
    }
    if (expect_status(sl_log_runtime_create(&arena, &config, &runtime), SL_STATUS_OK) != 0) {
        return 54;
    }
    if (expect_status(sl_log_memory_sink_create(&arena, 64U, &memory), SL_STATUS_OK) != 0) {
        return 55;
    }
    if (expect_status(sl_log_runtime_add_sink(runtime, memory), SL_STATUS_OK) != 0) {
        return 56;
    }
    if (expect_status(sl_log_runtime_start(runtime), SL_STATUS_OK) != 0) {
        return 57;
    }
    if (expect_status(build_login_event(SL_LOG_LEVEL_INFO, &state.event), SL_STATUS_OK) != 0) {
        return 58;
    }

    state.runtime = runtime;
    state.last_status = sl_status_ok();
    if (expect_status(sl_platform_thread_start(&arena, submit_race_main, &state, &thread),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_shutdown(runtime), SL_STATUS_OK) != 0)
    {
        return 54;
    }
    sl_platform_thread_join(thread);

    if (sl_status_code(state.last_status) != SL_STATUS_OK &&
        sl_status_code(state.last_status) != SL_STATUS_INVALID_STATE)
    {
        return 55;
    }
    if (expect_status(sl_log_runtime_submit(runtime, &state.event), SL_STATUS_INVALID_STATE) != 0) {
        return 56;
    }
    return 0;
}

static int test_memory_sink_snapshot_concurrent_dispatch_is_stable(void)
{
    static unsigned char storage[2097152];
    static unsigned char snapshot_storage[524288];
    SlArena arena = {0};
    SlArena snapshot_arena = {0};
    SlLogRuntimeConfig config = sl_log_runtime_config_default();
    SlLogRuntime* runtime = NULL;
    SlLogSink* memory = NULL;
    SlPlatformThread* thread = NULL;
    SubmitRaceState state = {0};
    size_t index = 0U;

    config.queue_capacity = 128U;
    config.sink_capacity = 1U;
    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&snapshot_arena, snapshot_storage, sizeof(snapshot_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_create(&arena, &config, &runtime), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_memory_sink_create(&arena, 64U, &memory), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_add_sink(runtime, memory), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_start(runtime), SL_STATUS_OK) != 0 ||
        expect_status(build_login_event(SL_LOG_LEVEL_INFO, &state.event), SL_STATUS_OK) != 0)
    {
        return 65;
    }

    state.runtime = runtime;
    state.last_status = sl_status_ok();
    state.max_attempts = 20000U;
    if (expect_status(sl_platform_thread_start(&arena, submit_race_main, &state, &thread),
                      SL_STATUS_OK) != 0)
    {
        return 66;
    }

    for (index = 0U; index < 256U; index += 1U) {
        SlArenaMark mark = sl_arena_mark(&snapshot_arena);
        SlLogMemorySnapshot snapshot = {0};
        if (expect_status(sl_log_memory_sink_snapshot(memory, &snapshot_arena, &snapshot),
                          SL_STATUS_OK) != 0)
        {
            return 67;
        }
        if (snapshot.count > 64U || (snapshot.count != 0U && snapshot.events == NULL)) {
            return 68;
        }
        if (expect_status(sl_arena_reset_to(&snapshot_arena, mark), SL_STATUS_OK) != 0) {
            return 69;
        }
    }

    sl_platform_thread_join(thread);
    if (sl_status_code(state.last_status) != SL_STATUS_OK) {
        return 70;
    }
    if (expect_status(sl_log_runtime_flush(runtime), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_shutdown(runtime), SL_STATUS_OK) != 0)
    {
        return 71;
    }
    return 0;
}

static int test_redaction_variants_and_json_escaping_contract(void)
{
    SlLogEventBuilder builder = {0};
    SlLogEvent event = {0};
    SlStr extra = sl_str_from_cstr("sessionTicket");
    char buffer[SL_LOG_MAX_JSONL_BYTES];
    SlBytes bytes = {0};

    if (expect_status(sl_log_event_builder_init(&builder, SL_LOG_LEVEL_INFO,
                                                sl_str_from_cstr("quote \" newline\n control")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_event_builder_add_string(&builder, sl_str_from_cstr("Password"),
                                                      sl_str_from_cstr("plain-password")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_event_builder_add_string(&builder, sl_str_from_cstr("auth.token"),
                                                      sl_str_from_cstr("plain-token")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_event_builder_add_string(&builder, sl_str_from_cstr("X-Api-Key"),
                                                      sl_str_from_cstr("plain-api-key")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_event_builder_add_string(&builder,
                                                      sl_str_from_cstr("db.connection-string"),
                                                      sl_str_from_cstr("Server=.;Password=secret")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_event_builder_add_string(&builder, sl_str_from_cstr("sessionTicket"),
                                                      sl_str_from_cstr("plain-ticket")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_event_builder_add_string(&builder, sl_str_from_cstr("safe"),
                                                      sl_str_from_cstr("line\n\"quoted\"")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_event_builder_finish(&builder, &event), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_event_apply_redaction(&event, &extra, 1U), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_event_serialize_jsonl(&event, buffer, sizeof(buffer), &bytes),
                      SL_STATUS_OK) != 0)
    {
        return 72;
    }

    if (event.redacted_count != 5U) {
        return 73;
    }
    if (bytes_contains(bytes, "plain-password") || bytes_contains(bytes, "plain-token") ||
        bytes_contains(bytes, "plain-api-key") || bytes_contains(bytes, "Password=secret") ||
        bytes_contains(bytes, "plain-ticket"))
    {
        return 74;
    }
    if (!bytes_contains(bytes, "quote \\\" newline\\n control") ||
        !bytes_contains(bytes, "line\\n\\\"quoted\\\""))
    {
        return 75;
    }
    if (bytes_contains(bytes, "quote \" newline\n control") ||
        bytes_contains(bytes, "line\n\"quoted\""))
    {
        return 76;
    }
    return 0;
}

static SlStatus flush_failure_write(void* state, const SlLogEvent* event)
{
    FlushFailureState* custom = (FlushFailureState*)state;

    if (custom == NULL || event == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    custom->writes += 1U;
    return sl_status_ok();
}

static SlStatus flush_failure_flush(void* state)
{
    FlushFailureState* custom = (FlushFailureState*)state;

    if (custom == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    custom->flushes += 1U;
    return custom->flush_status;
}

static void flush_failure_close(void* state)
{
    FlushFailureState* custom = (FlushFailureState*)state;

    if (custom != NULL) {
        custom->closes += 1U;
    }
}

static SlStatus blocking_sink_write(void* state, const SlLogEvent* event)
{
    BlockingSinkState* blocking = (BlockingSinkState*)state;

    if (blocking == NULL || blocking->mutex == NULL || blocking->cond == NULL || event == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    sl_platform_mutex_lock(blocking->mutex);
    blocking->writes += 1U;
    blocking->entered = true;
    sl_platform_cond_broadcast(blocking->cond);
    while (blocking->block_writes && !blocking->release) {
        sl_platform_cond_wait(blocking->cond, blocking->mutex);
    }
    sl_platform_mutex_unlock(blocking->mutex);
    return sl_status_ok();
}

static SlStatus blocking_sink_flush(void* state)
{
    BlockingSinkState* blocking = (BlockingSinkState*)state;

    if (blocking == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    blocking->flushes += 1U;
    return sl_status_ok();
}

static void blocking_sink_close(void* state)
{
    BlockingSinkState* blocking = (BlockingSinkState*)state;

    if (blocking != NULL) {
        blocking->closes += 1U;
    }
}

static void blocking_sink_prepare_for_next_write(BlockingSinkState* state)
{
    if (state == NULL || state->mutex == NULL) {
        return;
    }

    sl_platform_mutex_lock(state->mutex);
    state->entered = false;
    state->release = false;
    state->block_writes = true;
    sl_platform_mutex_unlock(state->mutex);
}

static int blocking_sink_wait_until_entered(BlockingSinkState* state)
{
    if (state == NULL || state->mutex == NULL || state->cond == NULL) {
        return 1;
    }

    sl_platform_mutex_lock(state->mutex);
    while (!state->entered) {
        sl_platform_cond_wait(state->cond, state->mutex);
    }
    sl_platform_mutex_unlock(state->mutex);
    return 0;
}

static void blocking_sink_release_writes(BlockingSinkState* state)
{
    if (state == NULL || state->mutex == NULL || state->cond == NULL) {
        return;
    }

    sl_platform_mutex_lock(state->mutex);
    state->release = true;
    sl_platform_cond_broadcast(state->cond);
    sl_platform_mutex_unlock(state->mutex);
}

static int test_flush_failure_is_reported_and_shutdown_closes_once(void)
{
    unsigned char storage[131072];
    SlArena arena = {0};
    SlLogRuntimeConfig config = sl_log_runtime_config_default();
    SlLogRuntime* runtime = NULL;
    SlLogSink* custom = NULL;
    SlLogSinkSnapshot sink_snapshot = {0};
    SlLogRuntimeSnapshot runtime_snapshot = {0};
    SlLogEvent event = {0};
    FlushFailureState state = {0};

    state.flush_status = sl_status_from_code(SL_STATUS_INTERNAL);
    config.queue_capacity = 4U;
    config.sink_capacity = 1U;
    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_create(&arena, &config, &runtime), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_custom_sink_create(&arena, flush_failure_write, flush_failure_flush,
                                                flush_failure_close, &state, &custom),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_add_sink(runtime, custom), SL_STATUS_OK) != 0 ||
        expect_status(build_login_event(SL_LOG_LEVEL_INFO, &event), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_submit(runtime, &event), SL_STATUS_OK) != 0)
    {
        return 77;
    }

    if (expect_status(sl_log_runtime_flush(runtime), SL_STATUS_INTERNAL) != 0) {
        return 78;
    }
    sink_snapshot = sl_log_sink_snapshot(custom);
    runtime_snapshot = sl_log_runtime_snapshot(runtime);
    if (state.writes != 1U || state.flushes != 1U || state.closes != 0U ||
        sink_snapshot.failure_count != 1U || runtime_snapshot.sink_failures != 1U)
    {
        return 79;
    }

    if (expect_status(sl_log_runtime_shutdown(runtime), SL_STATUS_INTERNAL) != 0 ||
        state.closes != 1U || expect_status(sl_log_runtime_shutdown(runtime), SL_STATUS_OK) != 0 ||
        state.closes != 1U)
    {
        return 80;
    }
    return 0;
}

static int test_blocked_sink_allows_snapshot_flush_and_shutdown_without_deadlock(void)
{
    static unsigned char storage[1048576];
    static unsigned char snapshot_storage[262144];
    SlArena arena = {0};
    SlArena snapshot_arena = {0};
    SlLogRuntimeConfig config = sl_log_runtime_config_default();
    SlLogRuntime* runtime = NULL;
    SlLogSink* memory = NULL;
    SlLogSink* blocked = NULL;
    SlLogMemorySnapshot memory_snapshot = {0};
    SlLogRuntimeSnapshot runtime_snapshot = {0};
    SlLogEvent event = {0};
    BlockingSinkState state = {0};
    RuntimeCallState flush_call = {0};
    RuntimeCallState shutdown_call = {0};
    SlPlatformThread* flush_thread = NULL;
    SlPlatformThread* shutdown_thread = NULL;

    config.queue_capacity = 8U;
    config.sink_capacity = 2U;
    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&snapshot_arena, snapshot_storage, sizeof(snapshot_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_platform_mutex_create(&arena, &state.mutex), SL_STATUS_OK) != 0 ||
        expect_status(sl_platform_cond_create(&arena, &state.cond), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_create(&arena, &config, &runtime), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_memory_sink_create(&arena, 8U, &memory), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_custom_sink_create(&arena, blocking_sink_write, blocking_sink_flush,
                                                blocking_sink_close, &state, &blocked),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_add_sink(runtime, memory), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_add_sink(runtime, blocked), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_start(runtime), SL_STATUS_OK) != 0 ||
        expect_status(build_login_event(SL_LOG_LEVEL_INFO, &event), SL_STATUS_OK) != 0)
    {
        return 81;
    }

    blocking_sink_prepare_for_next_write(&state);
    if (expect_status(sl_log_runtime_submit(runtime, &event), SL_STATUS_OK) != 0 ||
        blocking_sink_wait_until_entered(&state) != 0)
    {
        return 82;
    }

    flush_call.runtime = runtime;
    flush_call.status = sl_status_from_code(SL_STATUS_INTERNAL);
    if (expect_status(
            sl_platform_thread_start(&arena, runtime_flush_main, &flush_call, &flush_thread),
            SL_STATUS_OK) != 0)
    {
        return 83;
    }

    runtime_snapshot = sl_log_runtime_snapshot(runtime);
    if (runtime_snapshot.in_flight_events != 1U || runtime_snapshot.queued_events != 0U ||
        expect_status(sl_log_memory_sink_snapshot(memory, &snapshot_arena, &memory_snapshot),
                      SL_STATUS_OK) != 0 ||
        memory_snapshot.count != 1U || state.writes != 1U)
    {
        blocking_sink_release_writes(&state);
        sl_platform_thread_join(flush_thread);
        return 84;
    }

    blocking_sink_release_writes(&state);
    sl_platform_thread_join(flush_thread);
    if (expect_status(flush_call.status, SL_STATUS_OK) != 0 || state.flushes != 1U ||
        state.closes != 0U)
    {
        return 85;
    }

    sl_arena_reset(&snapshot_arena);

    blocking_sink_prepare_for_next_write(&state);
    if (expect_status(sl_log_runtime_submit(runtime, &event), SL_STATUS_OK) != 0 ||
        blocking_sink_wait_until_entered(&state) != 0)
    {
        return 87;
    }

    shutdown_call.runtime = runtime;
    shutdown_call.status = sl_status_from_code(SL_STATUS_INTERNAL);
    if (expect_status(sl_platform_thread_start(&arena, runtime_shutdown_main, &shutdown_call,
                                               &shutdown_thread),
                      SL_STATUS_OK) != 0)
    {
        return 88;
    }

    do {
        runtime_snapshot = sl_log_runtime_snapshot(runtime);
    } while (!runtime_snapshot.shutdown);
    if (!runtime_snapshot.shutdown || runtime_snapshot.in_flight_events != 1U ||
        expect_status(sl_log_memory_sink_snapshot(memory, &snapshot_arena, &memory_snapshot),
                      SL_STATUS_OK) != 0 ||
        memory_snapshot.count != 2U || state.writes != 2U)
    {
        blocking_sink_release_writes(&state);
        sl_platform_thread_join(shutdown_thread);
        return 89;
    }

    blocking_sink_release_writes(&state);
    sl_platform_thread_join(shutdown_thread);
    if (expect_status(shutdown_call.status, SL_STATUS_OK) != 0 || state.flushes != 2U ||
        state.closes != 1U ||
        expect_status(sl_log_runtime_submit(runtime, &event), SL_STATUS_INVALID_STATE) != 0)
    {
        return 90;
    }

    return 0;
}

static int test_multithreaded_drop_oldest_producers_keep_runtime_consistent(void)
{
    enum
    {
        producer_count = 4,
        attempts_per_producer = 128
    };
    static unsigned char storage[1048576];
    static unsigned char snapshot_storage[262144];
    SlArena arena = {0};
    SlArena snapshot_arena = {0};
    SlLogRuntimeConfig config = sl_log_runtime_config_default();
    SlLogRuntime* runtime = NULL;
    SlLogSink* memory = NULL;
    SlPlatformThread* threads[producer_count] = {0};
    MultiProducerState states[producer_count];
    SlLogRuntimeSnapshot runtime_snapshot = {0};
    SlLogMemorySnapshot memory_snapshot = {0};
    uint64_t expected_submitted_events = (uint64_t)producer_count * (uint64_t)attempts_per_producer;
    size_t index = 0U;

    config.queue_capacity = 32U;
    config.sink_capacity = 1U;
    config.backpressure_policy = SL_LOG_BACKPRESSURE_DROP_OLDEST;
    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&snapshot_arena, snapshot_storage, sizeof(snapshot_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_create(&arena, &config, &runtime), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_memory_sink_create(&arena, 64U, &memory), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_add_sink(runtime, memory), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_start(runtime), SL_STATUS_OK) != 0)
    {
        return 78;
    }

    for (index = 0U; index < producer_count; index += 1U) {
        states[index] = (MultiProducerState){0};
        states[index].runtime = runtime;
        states[index].attempts = attempts_per_producer;
        if (expect_status(build_login_event(SL_LOG_LEVEL_INFO, &states[index].event),
                          SL_STATUS_OK) != 0 ||
            expect_status(sl_platform_thread_start(&arena, multi_producer_main, &states[index],
                                                   &threads[index]),
                          SL_STATUS_OK) != 0)
        {
            return 79;
        }
    }

    for (index = 0U; index < producer_count; index += 1U) {
        sl_platform_thread_join(threads[index]);
        if (!sl_status_is_ok(states[index].first_failure)) {
            return 80;
        }
    }

    if (expect_status(sl_log_runtime_flush(runtime), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_memory_sink_snapshot(memory, &snapshot_arena, &memory_snapshot),
                      SL_STATUS_OK) != 0)
    {
        return 81;
    }
    runtime_snapshot = sl_log_runtime_snapshot(runtime);
    if (runtime_snapshot.submitted_events != expected_submitted_events ||
        runtime_snapshot.dropped_new_events != 0U || runtime_snapshot.queued_events != 0U ||
        runtime_snapshot.in_flight_events != 0U || memory_snapshot.count > 64U)
    {
        return 82;
    }
    for (index = 1U; index < memory_snapshot.count; index += 1U) {
        if (memory_snapshot.events[index - 1U].sequence >= memory_snapshot.events[index].sequence) {
            return 83;
        }
    }

    if (expect_status(sl_log_runtime_shutdown(runtime), SL_STATUS_OK) != 0) {
        return 84;
    }
    return 0;
}

static int test_stress_pressure_smoke(void)
{
    unsigned char storage[262144];
    SlArena arena = {0};
    SlLogRuntimeConfig config = sl_log_runtime_config_default();
    SlLogRuntime* runtime = NULL;
    SlLogSink* memory = NULL;
    SlLogEvent event = {0};
    size_t index = 0U;
    SlLogRuntimeSnapshot snapshot = {0};

    config.queue_capacity = 8U;
    config.sink_capacity = 1U;
    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_create(&arena, &config, &runtime), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_memory_sink_create(&arena, 8U, &memory), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_add_sink(runtime, memory), SL_STATUS_OK) != 0 ||
        expect_status(build_login_event(SL_LOG_LEVEL_INFO, &event), SL_STATUS_OK) != 0)
    {
        return 60;
    }
    for (index = 0U; index < 1000U; index += 1U) {
        if (expect_status(sl_log_runtime_submit(runtime, &event), SL_STATUS_OK) != 0) {
            return 61;
        }
    }
    snapshot = sl_log_runtime_snapshot(runtime);
    if (snapshot.submitted_events != 8U || snapshot.dropped_events != 992U) {
        return 62;
    }
    if (expect_status(sl_log_runtime_shutdown(runtime), SL_STATUS_OK) != 0) {
        return 63;
    }
    return 0;
}

int main(int argc, char** argv)
{
    int (*tests[])(void) = {test_event_builder_fields_and_redaction,
                            test_jsonl_serialization,
                            test_json_field_validation_and_parseable_jsonl,
                            test_runtime_memory_queue_pressure,
                            test_runtime_shutdown_public_apis_remain_safe,
                            test_runtime_sink_registration_lifecycle,
                            test_custom_sink_state_and_failure_accounting,
                            test_console_sink_pretty_and_jsonl,
                            test_file_sink_append_flush_and_missing_parent,
                            test_threaded_dispatch_shutdown,
                            test_submit_racing_shutdown_does_not_crash,
                            test_memory_sink_snapshot_concurrent_dispatch_is_stable,
                            test_redaction_variants_and_json_escaping_contract,
                            test_flush_failure_is_reported_and_shutdown_closes_once,
                            test_blocked_sink_allows_snapshot_flush_and_shutdown_without_deadlock,
                            test_multithreaded_drop_oldest_producers_keep_runtime_consistent};
    size_t index = 0U;

    if (argc > 1 && strcmp(argv[1], "--stress") == 0) {
        return test_stress_pressure_smoke();
    }

    for (index = 0U; index < sizeof(tests) / sizeof(tests[0]); index += 1U) {
        int result = tests[index]();
        if (result != 0) {
            return (int)(index + 1U) * 100 + result;
        }
    }
    return 0;
}
