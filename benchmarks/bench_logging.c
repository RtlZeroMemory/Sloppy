#include "bench_internal.h"

#include "sloppy/logging.h"

static SlStatus logging_build_event(SlLogEvent* out_event)
{
    SlLogEventBuilder builder = {0};
    SlStatus status =
        sl_log_event_builder_init(&builder, SL_LOG_LEVEL_INFO, sl_str_from_cstr("benchmark event"));

    if (sl_status_is_ok(status)) {
        status = sl_log_event_builder_set_category(&builder, sl_str_from_cstr("bench.logging"));
    }
    if (sl_status_is_ok(status)) {
        status = sl_log_event_builder_add_i64(&builder, sl_str_from_cstr("count"), 42);
    }
    if (sl_status_is_ok(status)) {
        status = sl_log_event_builder_add_bool(&builder, sl_str_from_cstr("enabled"), true);
    }
    if (sl_status_is_ok(status)) {
        status = sl_log_event_builder_add_string(&builder, sl_str_from_cstr("user"),
                                                 sl_str_from_cstr("alice"));
    }
    if (sl_status_is_ok(status)) {
        status = sl_log_event_builder_add_string(&builder, sl_str_from_cstr("token"),
                                                 sl_str_from_cstr("secret-value"));
    }
    if (sl_status_is_ok(status)) {
        status = sl_log_event_builder_add_f64(&builder, sl_str_from_cstr("ratio"), 1.25);
    }
    if (sl_status_is_ok(status)) {
        status = sl_log_event_builder_finish(&builder, out_event);
    }
    return status;
}

static SlStatus logging_bench_disabled(const SlBenchContext* context, uint64_t iterations,
                                       uint64_t* out_checksum)
{
    uint64_t index = 0U;
    (void)context;

    for (index = 0U; index < iterations; index += 1U) {
        if (sl_log_level_enabled(SL_LOG_LEVEL_WARN, SL_LOG_LEVEL_DEBUG)) {
            *out_checksum += 1U;
        }
    }
    return sl_status_ok();
}

static SlStatus logging_bench_enabled_build(const SlBenchContext* context, uint64_t iterations,
                                            uint64_t* out_checksum)
{
    uint64_t index = 0U;
    (void)context;

    for (index = 0U; index < iterations; index += 1U) {
        SlLogEvent event = {0};
        SlStatus status = logging_build_event(&event);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        *out_checksum += event.field_count + event.message.length;
    }
    return sl_status_ok();
}

static SlStatus logging_bench_jsonl(const SlBenchContext* context, uint64_t iterations,
                                    uint64_t* out_checksum)
{
    SlLogEvent event = {0};
    uint64_t index = 0U;
    SlStatus status = logging_build_event(&event);
    (void)context;

    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_log_event_apply_redaction(&event, NULL, 0U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (index = 0U; index < iterations; index += 1U) {
        char buffer[SL_LOG_MAX_JSONL_BYTES];
        SlBytes bytes = {0};
        event.sequence = index + 1U;
        status = sl_log_event_serialize_jsonl(&event, buffer, sizeof(buffer), &bytes);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        *out_checksum += bytes.length;
    }
    return sl_status_ok();
}

const SlBenchDefinition* sl_bench_logging_definitions(size_t* out_count)
{
    static const SlBenchDefinition definitions[] = {
        {"logging.disabled.level_check", "logging", "disabled log level branch", 10000U, 1000000U,
         logging_bench_disabled, "disabled path does not build events", false, 0U, 0U},
        {"logging.enabled.five_fields", "logging", "build structured event with five fields", 1000U,
         100000U, logging_bench_enabled_build, "event builder only", false, 0U, 0U},
        {"logging.jsonl.serialize", "logging", "serialize redacted structured event to JSONL",
         1000U, 100000U, logging_bench_jsonl, "formatting benchmark, not correctness proof", false, 0U, 0U},
    };

    if (out_count != NULL) {
        *out_count = sizeof(definitions) / sizeof(definitions[0]);
    }
    return definitions;
}
