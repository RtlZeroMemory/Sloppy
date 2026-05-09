#include "sloppy/logging.h"

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

typedef struct ConsoleCapture
{
    char text[4096];
    size_t length;
} ConsoleCapture;

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
                            test_runtime_memory_queue_pressure,
                            test_console_sink_pretty_and_jsonl,
                            test_file_sink_append_flush_and_missing_parent,
                            test_threaded_dispatch_shutdown};
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
