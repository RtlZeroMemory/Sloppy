#include "bench_internal.h"

#include "sloppy/alloc.h"
#include "sloppy/builder.h"
#include "sloppy/json_profile.h"
#include "sloppy/json_writer.h"
#include "sloppy/platform.h"
#include "sloppy/platform_time.h"
#include "sloppy/status.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef enum SlBenchFormat
{
    SL_BENCH_FORMAT_TEXT = 0,
    SL_BENCH_FORMAT_JSON = 1
} SlBenchFormat;

typedef struct SlBenchOptions
{
    SlBenchFormat format;
    bool list;
    bool smoke;
    bool include_v8;
    const char* only_name;
} SlBenchOptions;

static bool sl_bench_streq(const char* left, const char* right)
{
    return strcmp(left, right) == 0;
}

static void sl_bench_print_usage(void)
{
    printf("Usage: sloppy_bench [--list] [--smoke] [--format text|json] [--bench NAME] "
           "[--include-v8]\n");
}

static void sl_bench_print_error_text(const char* text)
{
    fputs(text, stderr);
}

static void sl_bench_print_json_string(const char* text)
{
    unsigned char stack_storage[512];
    unsigned char* storage = stack_storage;
    size_t escaped_length = 0U;
    SlHeapBuffer heap_storage = {0};
    SlByteBuilder builder = {0};
    SlStr value = text == NULL ? sl_str_empty() : sl_str_from_cstr(text);
    SlStatus status = sl_json_writer_escaped_string_length(value, &escaped_length);

    if (!sl_status_is_ok(status)) {
        fputs("\"<json-escape-error>\"", stdout);
        return;
    }
    if (escaped_length > sizeof(stack_storage)) {
        status = sl_heap_buffer_alloc(&heap_storage, escaped_length);
        if (!sl_status_is_ok(status)) {
            fputs("\"<out-of-memory>\"", stdout);
            return;
        }
        storage = heap_storage.ptr;
    }

    status = sl_byte_builder_init_fixed(&builder, storage, escaped_length);
    if (sl_status_is_ok(status)) {
        status = sl_json_writer_append_escaped_string_bytes(&builder, value);
    }
    if (sl_status_is_ok(status)) {
        SlBytes escaped = sl_byte_builder_view(&builder);
        fwrite(escaped.ptr, 1U, escaped.length, stdout);
    }
    else {
        fputs("\"<json-escape-error>\"", stdout);
    }
    sl_heap_buffer_dispose(&heap_storage);
}

static int sl_bench_parse_options(int argc, char** argv, SlBenchOptions* out_options)
{
    int index;

    *out_options = (SlBenchOptions){0};
    out_options->format = SL_BENCH_FORMAT_TEXT;

    for (index = 1; index < argc; index += 1) {
        if (sl_bench_streq(argv[index], "--help") || sl_bench_streq(argv[index], "-h")) {
            sl_bench_print_usage();
            return 1;
        }
        if (sl_bench_streq(argv[index], "--list")) {
            out_options->list = true;
            continue;
        }
        if (sl_bench_streq(argv[index], "--smoke")) {
            out_options->smoke = true;
            continue;
        }
        if (sl_bench_streq(argv[index], "--include-v8")) {
            out_options->include_v8 = true;
            continue;
        }
        if (sl_bench_streq(argv[index], "--format")) {
            if (index + 1 >= argc) {
                return -1;
            }
            index += 1;
            if (sl_bench_streq(argv[index], "json")) {
                out_options->format = SL_BENCH_FORMAT_JSON;
            }
            else if (sl_bench_streq(argv[index], "text")) {
                out_options->format = SL_BENCH_FORMAT_TEXT;
            }
            else {
                return -1;
            }
            continue;
        }
        if (sl_bench_streq(argv[index], "--bench")) {
            if (index + 1 >= argc) {
                return -1;
            }
            index += 1;
            out_options->only_name = argv[index];
            continue;
        }
        return -1;
    }

    return 0;
}

static const SlBenchDefinition* sl_bench_definition_group(size_t group_index, size_t* out_count)
{
    if (group_index == 0U) {
        return sl_bench_route_definitions(out_count);
    }

    if (group_index == 1U) {
        return sl_bench_handler_dispatch_definitions(out_count);
    }

    if (group_index == 2U) {
        return sl_bench_json_dispatch_definitions(out_count);
    }

    if (group_index == 3U) {
        return sl_bench_diagnostics_definitions(out_count);
    }

    if (group_index == 4U) {
        return sl_bench_logging_definitions(out_count);
    }

    if (group_index == 5U) {
        return sl_bench_ops_definitions(out_count);
    }

    if (group_index == 6U) {
        return sl_bench_memory_definitions(out_count);
    }

    if (group_index == 7U) {
        return sl_bench_stream_definitions(out_count);
    }

    if (group_index == 8U) {
        return sl_bench_v8_bridge_definitions(out_count);
    }

    *out_count = 0U;
    return NULL;
}

static bool sl_bench_should_run(const SlBenchOptions* options, const SlBenchDefinition* definition)
{
    if (definition->requires_v8 && !options->include_v8) {
        return false;
    }

    if (options->only_name != NULL && !sl_bench_streq(options->only_name, definition->name)) {
        return false;
    }

    return true;
}

static void sl_bench_list(const SlBenchOptions* options)
{
    size_t group;

    for (group = 0U; group < 9U; group += 1U) {
        size_t count = 0U;
        size_t index;
        const SlBenchDefinition* definitions = sl_bench_definition_group(group, &count);

        for (index = 0U; index < count; index += 1U) {
            const SlBenchDefinition* definition = &definitions[index];
            if (!sl_bench_should_run(options, definition)) {
                continue;
            }
            printf("%s\t%s\t%s\n", definition->name, definition->category, definition->description);
        }
    }
}

static uint64_t sl_bench_iterations(uint64_t configured, bool smoke)
{
    if (!smoke) {
        return configured;
    }

    if (configured == 0U) {
        return 0U;
    }

    return configured < 10U ? configured : 10U;
}

static void sl_bench_apply_json_counters(const SlBenchDefinition* definition, SlBenchResult* result)
{
    if (definition == NULL || result == NULL || !sl_bench_streq(definition->category, "json")) {
        return;
    }

    result->rows_processed = result->iterations;
    if (strstr(definition->name, ".native") != NULL) {
        result->native_hits = result->iterations;
    }
    if (strstr(definition->name, ".generic") != NULL ||
        strstr(definition->name, "fallback") != NULL)
    {
        result->generic_fallback_count = result->iterations;
    }
    if (strstr(definition->name, "materialize") != NULL) {
        result->materialization_count = result->iterations;
    }
    if (strstr(definition->name, "native_schema.serialize") != NULL ||
        strstr(definition->name, "native_schema.http_response_write") != NULL)
    {
        result->schema_response_native_hits = result->iterations;
    }
    if (strstr(definition->name, "materialize_once") != NULL) {
        result->duplicate_validation_skipped_count = result->iterations;
    }
    if (strstr(definition->name, "reject") != NULL || strstr(definition->name, "malformed") != NULL)
    {
        result->reject_count = result->iterations;
    }
}

static SlStatus sl_bench_measure(const SlBenchContext* context, const SlBenchDefinition* definition,
                                 SlBenchResult* out_result)
{
    uint64_t start_ns = 0U;
    uint64_t end_ns = 0U;
    uint64_t checksum = 0U;
    uint64_t warmup_iterations = sl_bench_iterations(definition->warmup_iterations, context->smoke);
    uint64_t measured_iterations =
        sl_bench_iterations(definition->measured_iterations, context->smoke);
    SlStatus status;

    *out_result = (SlBenchResult){0};
    out_result->name = definition->name;
    out_result->category = definition->category;
    out_result->warmup_iterations = warmup_iterations;
    out_result->iterations = measured_iterations;
    out_result->note = definition->note;

    if (measured_iterations == 0U) {
        out_result->checksum = 0U;
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    }

    if (warmup_iterations > 0U) {
        status = definition->run(context, warmup_iterations, &checksum);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    checksum = 0U;
    if (sl_json_profile_enabled() && sl_bench_streq(definition->category, "json")) {
        sl_json_profile_reset(definition->name, measured_iterations);
    }
    status = sl_platform_monotonic_time_ns(&start_ns);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = definition->run(context, measured_iterations, &checksum);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_platform_monotonic_time_ns(&end_ns);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    out_result->elapsed_ns = end_ns >= start_ns ? end_ns - start_ns : 0U;
    out_result->checksum = checksum;
    out_result->ns_per_op = measured_iterations == 0U
                                ? 0.0
                                : (double)out_result->elapsed_ns / (double)measured_iterations;
    out_result->bytes_processed = definition->bytes_per_iteration * measured_iterations;
    out_result->chunks_processed = definition->chunks_per_iteration * measured_iterations;
    out_result->backpressure_count = definition->backpressure_per_iteration * measured_iterations;
    sl_bench_apply_json_counters(definition, out_result);
    if (sl_json_profile_enabled() && sl_bench_streq(definition->category, "json")) {
        sl_json_profile_snapshot(&out_result->json_profile);
    }
    if (out_result->elapsed_ns != 0U) {
        double seconds = (double)out_result->elapsed_ns / 1000000000.0;

        out_result->bytes_per_second =
            out_result->bytes_processed == 0U ? 0.0 : (double)out_result->bytes_processed / seconds;
        out_result->chunks_per_second = out_result->chunks_processed == 0U
                                            ? 0.0
                                            : (double)out_result->chunks_processed / seconds;
        out_result->rows_per_second =
            out_result->rows_processed == 0U ? 0.0 : (double)out_result->rows_processed / seconds;
    }
    return sl_status_ok();
}

static void sl_bench_print_text_header(const SlBenchOptions* options)
{
    printf("Sloppy benchmarks v1\n");
    printf("mode: %s\n", options->smoke ? "smoke" : "measured");
    printf("configuration: run Release builds for meaningful numbers\n");
}

static void sl_bench_print_text_result(const SlBenchResult* result)
{
    printf("%-42s %12" PRIu64 " iters %12" PRIu64 " ns %10.2f ns/op", result->name,
           result->iterations, result->elapsed_ns, result->ns_per_op);
    if (result->bytes_processed != 0U) {
        printf(" bytes/sec=%.2f", result->bytes_per_second);
    }
    if (result->chunks_processed != 0U) {
        printf(" chunks/sec=%.2f", result->chunks_per_second);
    }
    if (result->rows_processed != 0U) {
        printf(" rows/sec=%.2f", result->rows_per_second);
    }
    if (result->backpressure_count != 0U) {
        printf(" backpressure=%" PRIu64, result->backpressure_count);
    }
    if (result->native_hits != 0U) {
        printf(" native=%" PRIu64, result->native_hits);
    }
    if (result->generic_fallback_count != 0U) {
        printf(" fallback=%" PRIu64, result->generic_fallback_count);
    }
    if (result->materialization_count != 0U) {
        printf(" materialize=%" PRIu64, result->materialization_count);
    }
    if (result->reject_count != 0U) {
        printf(" rejects=%" PRIu64, result->reject_count);
    }
    if (result->schema_response_native_hits != 0U) {
        printf(" schema_response_native=%" PRIu64, result->schema_response_native_hits);
    }
    if (result->duplicate_validation_skipped_count != 0U) {
        printf(" duplicate_validation_skipped=%" PRIu64,
               result->duplicate_validation_skipped_count);
    }
    printf(" checksum=%" PRIu64 "\n", result->checksum);
}

static void sl_bench_print_json_header(const SlBenchOptions* options)
{
    printf("{\n");
    printf("  \"sloppyBenchmarkVersion\": 1,\n");
    printf("  \"mode\": \"%s\",\n", options->smoke ? "smoke" : "measured");
    printf("  \"build\": {\n");
    printf("    \"configuration\": \"%s\",\n",
#ifdef NDEBUG
           "Release"
#else
           "Debug"
#endif
    );
    printf("    \"platform\": \"");
#if SL_PLATFORM_WINDOWS
    printf("windows-x64");
#elif SL_PLATFORM_LINUX
    printf("linux");
#elif SL_PLATFORM_APPLE
    printf("macos");
#else
    printf("unknown");
#endif
    printf("\",\n");
#ifdef SLOPPY_ENABLE_V8_BRIDGE
    printf("    \"v8Enabled\": true\n");
#else
    printf("    \"v8Enabled\": false\n");
#endif
    printf("  },\n");
    printf("  \"benchmarks\": [\n");
}

static void sl_bench_print_json_result(const SlBenchResult* result, bool first)
{
    printf("%s", first ? "" : ",\n");
    printf("    {\n");
    printf("      \"name\": ");
    sl_bench_print_json_string(result->name);
    printf(",\n");
    printf("      \"category\": ");
    sl_bench_print_json_string(result->category);
    printf(",\n");
    printf("      \"warmupIterations\": %" PRIu64 ",\n", result->warmup_iterations);
    printf("      \"iterations\": %" PRIu64 ",\n", result->iterations);
    printf("      \"elapsedNs\": %" PRIu64 ",\n", result->elapsed_ns);
    printf("      \"nsPerOp\": %.2f,\n", result->ns_per_op);
    printf("      \"bytesProcessed\": %" PRIu64 ",\n", result->bytes_processed);
    printf("      \"chunksProcessed\": %" PRIu64 ",\n", result->chunks_processed);
    printf("      \"rowsProcessed\": %" PRIu64 ",\n", result->rows_processed);
    printf("      \"bytesPerSecond\": %.2f,\n", result->bytes_per_second);
    printf("      \"chunksPerSecond\": %.2f,\n", result->chunks_per_second);
    printf("      \"rowsPerSecond\": %.2f,\n", result->rows_per_second);
    printf("      \"checksum\": %" PRIu64 ",\n", result->checksum);
    printf("      \"backpressureCount\": %" PRIu64 ",\n", result->backpressure_count);
    printf("      \"nativeHits\": %" PRIu64 ",\n", result->native_hits);
    printf("      \"genericFallbackCount\": %" PRIu64 ",\n", result->generic_fallback_count);
    printf("      \"materializationCount\": %" PRIu64 ",\n", result->materialization_count);
    printf("      \"rejectCount\": %" PRIu64 ",\n", result->reject_count);
    printf("      \"schemaResponseNativeHits\": %" PRIu64 ",\n",
           result->schema_response_native_hits);
    printf("      \"duplicateValidationSkippedCount\": %" PRIu64 ",\n",
           result->duplicate_validation_skipped_count);
    if (sl_json_profile_snapshot_has_data(&result->json_profile)) {
        printf("      \"jsonProfile\": ");
        sl_json_profile_fprint_json(stdout, &result->json_profile, 6U);
        printf(",\n");
    }
    printf("      \"note\": ");
    sl_bench_print_json_string(result->note);
    printf("\n");
    printf("    }");
}

static void sl_bench_print_json_footer(bool any_result)
{
    printf("%s", any_result ? "\n" : "");
    printf("  ]\n");
    printf("}\n");
}

static int sl_bench_run(const SlBenchOptions* options)
{
    SlBenchContext context = {options->smoke, options->include_v8};
    bool any_selected = false;
    bool any_result = false;
    bool any_skipped = false;
    size_t group;

    if (options->format == SL_BENCH_FORMAT_TEXT) {
        sl_bench_print_text_header(options);
    }
    else {
        sl_bench_print_json_header(options);
    }

    for (group = 0U; group < 9U; group += 1U) {
        size_t count = 0U;
        size_t index;
        const SlBenchDefinition* definitions = sl_bench_definition_group(group, &count);

        for (index = 0U; index < count; index += 1U) {
            SlBenchResult result = {0};
            SlStatus status;
            const SlBenchDefinition* definition = &definitions[index];

            if (!sl_bench_should_run(options, definition)) {
                continue;
            }
            any_selected = true;

            status = sl_bench_measure(&context, definition, &result);
            if (!sl_status_is_ok(status)) {
                if (definition->requires_v8 && sl_status_code(status) == SL_STATUS_UNSUPPORTED) {
                    sl_bench_print_error_text("benchmark skipped/deferred: ");
                    sl_bench_print_error_text(definition->name);
                    sl_bench_print_error_text("\n");
                    any_skipped = true;
                    continue;
                }
                sl_bench_print_error_text("benchmark failed: ");
                sl_bench_print_error_text(definition->name);
                sl_bench_print_error_text("\n");
                return 1;
            }

            if (options->format == SL_BENCH_FORMAT_TEXT) {
                sl_bench_print_text_result(&result);
            }
            else {
                sl_bench_print_json_result(&result, !any_result);
            }
            any_result = true;
        }
    }

    if (options->format == SL_BENCH_FORMAT_JSON) {
        sl_bench_print_json_footer(any_result);
    }

    if (!any_selected) {
        sl_bench_print_error_text("no benchmarks selected\n");
        return 1;
    }

    if (!any_result && any_skipped) {
        return 0;
    }

    return 0;
}

int main(int argc, char** argv)
{
    SlBenchOptions options;
    int parse_result = sl_bench_parse_options(argc, argv, &options);

    if (parse_result > 0) {
        return 0;
    }

    if (parse_result < 0) {
        sl_bench_print_usage();
        return 2;
    }

    if (options.list) {
        sl_bench_list(&options);
        return 0;
    }

    return sl_bench_run(&options);
}
