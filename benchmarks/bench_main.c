#include "bench_internal.h"

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
    (void)printf("Usage: sloppy_bench [--list] [--smoke] [--format text|json] [--bench NAME] "
                 "[--include-v8]\n");
}

static void sl_bench_print_error_text(const char* text)
{
    (void)fputs(text, stderr);
}

static void sl_bench_print_json_string(const char* text)
{
    const unsigned char* cursor = (const unsigned char*)text;

    (void)fputc('"', stdout);
    while (*cursor != '\0') {
        unsigned char ch = *cursor;
        switch (ch) {
        case '"':
            (void)fputs("\\\"", stdout);
            break;
        case '\\':
            (void)fputs("\\\\", stdout);
            break;
        case '\b':
            (void)fputs("\\b", stdout);
            break;
        case '\f':
            (void)fputs("\\f", stdout);
            break;
        case '\n':
            (void)fputs("\\n", stdout);
            break;
        case '\r':
            (void)fputs("\\r", stdout);
            break;
        case '\t':
            (void)fputs("\\t", stdout);
            break;
        default:
            if (ch < 0x20U) {
                (void)printf("\\u%04x", (unsigned int)ch);
            }
            else {
                (void)fputc((int)ch, stdout);
            }
            break;
        }
        cursor += 1;
    }
    (void)fputc('"', stdout);
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
        return sl_bench_memory_definitions(out_count);
    }

    if (group_index == 3U) {
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

    for (group = 0U; group < 4U; group += 1U) {
        size_t count = 0U;
        size_t index;
        const SlBenchDefinition* definitions = sl_bench_definition_group(group, &count);

        for (index = 0U; index < count; index += 1U) {
            const SlBenchDefinition* definition = &definitions[index];
            if (!sl_bench_should_run(options, definition)) {
                continue;
            }
            (void)printf("%s\t%s\t%s\n", definition->name, definition->category,
                         definition->description);
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
    return sl_status_ok();
}

static void sl_bench_print_text_header(const SlBenchOptions* options)
{
    (void)printf("Sloppy benchmarks v1\n");
    (void)printf("mode: %s\n", options->smoke ? "smoke" : "measured");
    (void)printf("configuration: run Release builds for meaningful numbers\n");
}

static void sl_bench_print_text_result(const SlBenchResult* result)
{
    (void)printf("%-42s %12" PRIu64 " iters %12" PRIu64 " ns %10.2f ns/op checksum=%" PRIu64 "\n",
                 result->name, result->iterations, result->elapsed_ns, result->ns_per_op,
                 result->checksum);
}

static void sl_bench_print_json_header(const SlBenchOptions* options)
{
    (void)printf("{\n");
    (void)printf("  \"sloppyBenchmarkVersion\": 1,\n");
    (void)printf("  \"mode\": \"%s\",\n", options->smoke ? "smoke" : "measured");
    (void)printf("  \"build\": {\n");
    (void)printf("    \"configuration\": \"%s\",\n",
#ifdef NDEBUG
                 "Release"
#else
                 "Debug"
#endif
    );
    (void)printf("    \"platform\": \"");
#if SL_PLATFORM_WINDOWS
    (void)printf("windows-x64");
#elif SL_PLATFORM_LINUX
    (void)printf("linux");
#elif SL_PLATFORM_APPLE
    (void)printf("macos");
#else
    (void)printf("unknown");
#endif
    (void)printf("\",\n");
#ifdef SLOPPY_ENABLE_V8_BRIDGE
    (void)printf("    \"v8Enabled\": true\n");
#else
    (void)printf("    \"v8Enabled\": false\n");
#endif
    (void)printf("  },\n");
    (void)printf("  \"benchmarks\": [\n");
}

static void sl_bench_print_json_result(const SlBenchResult* result, bool first)
{
    (void)printf("%s", first ? "" : ",\n");
    (void)printf("    {\n");
    (void)printf("      \"name\": ");
    sl_bench_print_json_string(result->name);
    (void)printf(",\n");
    (void)printf("      \"category\": ");
    sl_bench_print_json_string(result->category);
    (void)printf(",\n");
    (void)printf("      \"warmupIterations\": %" PRIu64 ",\n", result->warmup_iterations);
    (void)printf("      \"iterations\": %" PRIu64 ",\n", result->iterations);
    (void)printf("      \"elapsedNs\": %" PRIu64 ",\n", result->elapsed_ns);
    (void)printf("      \"nsPerOp\": %.2f,\n", result->ns_per_op);
    (void)printf("      \"checksum\": %" PRIu64 ",\n", result->checksum);
    (void)printf("      \"note\": ");
    sl_bench_print_json_string(result->note);
    (void)printf("\n");
    (void)printf("    }");
}

static void sl_bench_print_json_footer(bool any_result)
{
    (void)printf("%s", any_result ? "\n" : "");
    (void)printf("  ]\n");
    (void)printf("}\n");
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

    for (group = 0U; group < 4U; group += 1U) {
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
