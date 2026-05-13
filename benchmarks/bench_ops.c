#include "bench_internal.h"

#include "sloppy/arena.h"
#include "sloppy/ops_metrics.h"

#include <stdio.h>
#include <string.h>

#define SL_BENCH_OPS_ARENA_SIZE 262144U

static SlStatus bench_ops_registry(SlArena* arena, SlOpsMetricsRegistry** out_registry)
{
    SlOpsMetricsOptions options = {.max_metrics = 64U, .max_series_per_metric = 256U};

    return sl_ops_metrics_registry_init(arena, &options, out_registry);
}

static SlStatus bench_ops_counter_inc(const SlBenchContext* context, uint64_t iterations,
                                      uint64_t* out_checksum)
{
    unsigned char storage[SL_BENCH_OPS_ARENA_SIZE];
    SlArena arena = {0};
    SlOpsMetricsRegistry* registry = NULL;
    SlOpsMetricLabel labels[1] = {
        {sl_str_from_cstr("route"), sl_str_from_cstr("/orders/{id}")},
    };
    uint64_t index = 0U;
    SlStatus status;

    (void)context;
    if (out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_arena_init(&arena, storage, sizeof(storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = bench_ops_registry(&arena, &registry);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (index = 0U; index < iterations; index += 1U) {
        status = sl_ops_metrics_counter_inc(registry, sl_str_from_cstr("http.requests.total"),
                                            labels, 1U, 1.0);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    *out_checksum = iterations;
    return sl_status_ok();
}

static SlStatus bench_ops_histogram_observe(const SlBenchContext* context, uint64_t iterations,
                                            uint64_t* out_checksum)
{
    unsigned char storage[SL_BENCH_OPS_ARENA_SIZE];
    static const double buckets[] = {1.0, 5.0, 10.0, 25.0, 50.0, 100.0, 250.0, 500.0};
    SlArena arena = {0};
    SlOpsMetricsRegistry* registry = NULL;
    SlOpsMetricLabel labels[1] = {
        {sl_str_from_cstr("route"), sl_str_from_cstr("/orders/{id}")},
    };
    uint64_t index = 0U;
    SlStatus status;

    (void)context;
    if (out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_arena_init(&arena, storage, sizeof(storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = bench_ops_registry(&arena, &registry);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (index = 0U; index < iterations; index += 1U) {
        status = sl_ops_metrics_histogram_observe(
            registry, sl_str_from_cstr("http.request.duration.ms"), labels, 1U, buckets,
            sizeof(buckets) / sizeof(buckets[0]), (double)(index % 200U));
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    *out_checksum = iterations;
    return sl_status_ok();
}

static SlStatus bench_ops_render_prometheus(const SlBenchContext* context, uint64_t iterations,
                                            uint64_t* out_checksum)
{
    unsigned char storage[SL_BENCH_OPS_ARENA_SIZE];
    SlArena arena = {0};
    SlOpsMetricsRegistry* registry = NULL;
    uint64_t index = 0U;
    uint64_t checksum = 0U;
    SlStatus status;

    (void)context;
    if (out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_arena_init(&arena, storage, sizeof(storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = bench_ops_registry(&arena, &registry);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (index = 0U; index < 64U; index += 1U) {
        char route[32];
        SlOpsMetricLabel labels[1];
        int written =
            snprintf(route, sizeof(route), "/items/%02llu/{id}", (unsigned long long)index);

        if (written < 0 || (size_t)written >= sizeof(route)) {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        labels[0] = (SlOpsMetricLabel){sl_str_from_cstr("route"), sl_str_from_cstr(route)};
        status = sl_ops_metrics_counter_inc(registry, sl_str_from_cstr("http.requests.total"),
                                            labels, 1U, 1.0);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    for (index = 0U; index < iterations; index += 1U) {
        SlStringBuilder builder = {0};
        char output[65536];

        status = sl_string_builder_init_fixed(&builder, output, sizeof(output));
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_ops_metrics_render_prometheus(registry, &builder);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        checksum += (uint64_t)sl_string_builder_length(&builder);
    }
    *out_checksum = checksum;
    return sl_status_ok();
}

static const SlBenchDefinition ops_benchmarks[] = {
    {"ops.metrics.counter.inc", "ops", "increment a native metrics counter with one route label",
     1000U, 100000U, bench_ops_counter_inc,
     "registry is pre-created; one stable route-pattern labelset; no rendering or sockets", false,
     0U, 1U, 0U},
    {"ops.metrics.histogram.observe", "ops",
     "observe a native metrics histogram with default-like buckets", 1000U, 100000U,
     bench_ops_histogram_observe,
     "registry is pre-created; one stable route-pattern labelset; no rendering or sockets", false,
     0U, 1U, 0U},
    {"ops.metrics.prometheus.render_64", "ops",
     "render Prometheus text for 64 native counter series", 10U, 100U, bench_ops_render_prometheus,
     "renders a fixed registry snapshot; labels are route patterns, not raw paths", false, 0U, 64U,
     0U},
};

const SlBenchDefinition* sl_bench_ops_definitions(size_t* out_count)
{
    if (out_count != NULL) {
        *out_count = sizeof(ops_benchmarks) / sizeof(ops_benchmarks[0]);
    }
    return ops_benchmarks;
}
