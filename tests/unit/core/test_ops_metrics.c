#include "sloppy/ops_metrics.h"

#include <stdbool.h>
#include <string.h>

#define TEST_ARENA_SIZE 65536U

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static bool str_contains(SlStr haystack, const char* needle)
{
    size_t needle_length = 0U;
    size_t index = 0U;

    if (needle == NULL) {
        return false;
    }
    needle_length = strlen(needle);
    if (haystack.ptr == NULL || needle_length == 0U || haystack.length < needle_length) {
        return false;
    }
    for (index = 0U; index <= haystack.length - needle_length; index += 1U) {
        if (memcmp(haystack.ptr + index, needle, needle_length) == 0) {
            return true;
        }
    }
    return false;
}

static int test_counter_gauge_histogram_and_renderers(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlOpsMetricsRegistry* registry = NULL;
    SlOpsMetricLabel labels[2] = {
        {sl_str_from_cstr("route"), sl_str_from_cstr("/orders/{id}")},
        {sl_str_from_cstr("status"), sl_str_from_cstr("200")},
    };
    double buckets[3] = {10.0, 50.0, 100.0};
    SlStringBuilder json = {0};
    SlStringBuilder prom = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_ops_metrics_registry_init(&arena, NULL, &registry), SL_STATUS_OK) != 0 ||
        expect_status(sl_ops_metrics_counter_inc(registry, sl_str_from_cstr("http.requests.total"),
                                                 labels, 2U, 1.0),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_ops_metrics_counter_inc(registry, sl_str_from_cstr("http.requests.total"),
                                                 labels, 2U, 2.0),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_ops_metrics_gauge_set(registry, sl_str_from_cstr("http.requests.active"),
                                               NULL, 0U, 4.0),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_ops_metrics_gauge_add(registry, sl_str_from_cstr("http.requests.active"),
                                               NULL, 0U, -1.0),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_ops_metrics_histogram_observe(registry,
                                                       sl_str_from_cstr("http.request.duration.ms"),
                                                       labels, 2U, buckets, 3U, 25.0),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_init_arena(&json, &arena, 1024U, 32768U), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_string_builder_init_arena(&prom, &arena, 1024U, 32768U), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_ops_metrics_render_json(registry, &json), SL_STATUS_OK) != 0 ||
        expect_status(sl_ops_metrics_render_prometheus(registry, &prom), SL_STATUS_OK) != 0)
    {
        return 1;
    }

    if (!str_contains(sl_string_builder_view(&json), "\"name\":\"http.requests.total\"") ||
        !str_contains(sl_string_builder_view(&json), "\"value\":3") ||
        !str_contains(sl_string_builder_view(&json), "\"sum\":25") ||
        !str_contains(sl_string_builder_view(&json), "\"count\":1"))
    {
        return 2;
    }
    if (!str_contains(sl_string_builder_view(&prom), "# TYPE http_requests_total counter") ||
        !str_contains(sl_string_builder_view(&prom),
                      "http_requests_total{route=\"/orders/{id}\"") ||
        !str_contains(sl_string_builder_view(&prom), "http_request_duration_ms_bucket") ||
        !str_contains(sl_string_builder_view(&prom), "le=\"50\"") ||
        !str_contains(sl_string_builder_view(&prom),
                      "http_request_duration_ms_sum{route=\"/orders/{id}\",status=\"200\"} 25") ||
        !str_contains(sl_string_builder_view(&prom),
                      "http_request_duration_ms_count{route=\"/orders/{id}\",status=\"200\"} 1") ||
        !str_contains(sl_string_builder_view(&prom), "http_requests_active 3"))
    {
        return 3;
    }
    return 0;
}

static int test_cardinality_guard_records_drops(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlOpsMetricsRegistry* registry = NULL;
    SlOpsMetricsOptions options = {.max_metrics = 4U, .max_series_per_metric = 1U};
    SlOpsMetricLabel first[1] = {
        {sl_str_from_cstr("route"), sl_str_from_cstr("/one")},
    };
    SlOpsMetricLabel second[1] = {
        {sl_str_from_cstr("route"), sl_str_from_cstr("/two")},
    };
    SlStringBuilder prom = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_ops_metrics_registry_init(&arena, &options, &registry), SL_STATUS_OK) !=
            0 ||
        expect_status(
            sl_ops_metrics_counter_inc(registry, sl_str_from_cstr("requests"), first, 1U, 1.0),
            SL_STATUS_OK) != 0 ||
        expect_status(
            sl_ops_metrics_counter_inc(registry, sl_str_from_cstr("requests"), second, 1U, 1.0),
            SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        sl_ops_metrics_cardinality_drops(registry) != 1U ||
        expect_status(sl_string_builder_init_arena(&prom, &arena, 1024U, 8192U), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_ops_metrics_render_prometheus(registry, &prom), SL_STATUS_OK) != 0)
    {
        return 10;
    }
    if (!str_contains(sl_string_builder_view(&prom), "sloppy_metrics_cardinality_drops_total 1")) {
        return 11;
    }
    return 0;
}

static int test_reset_preserves_series_and_clears_values(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlOpsMetricsRegistry* registry = NULL;
    SlStringBuilder json = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_ops_metrics_registry_init(&arena, NULL, &registry), SL_STATUS_OK) != 0 ||
        expect_status(
            sl_ops_metrics_counter_inc(registry, sl_str_from_cstr("requests"), NULL, 0U, 5.0),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_ops_metrics_reset(registry), SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_init_arena(&json, &arena, 1024U, 8192U), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_ops_metrics_render_json(registry, &json), SL_STATUS_OK) != 0)
    {
        return 20;
    }
    if (!str_contains(sl_string_builder_view(&json), "\"name\":\"requests\"") ||
        !str_contains(sl_string_builder_view(&json), "\"value\":0"))
    {
        return 21;
    }
    return 0;
}

static int test_counter_set_preserves_absolute_snapshots(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlOpsMetricsRegistry* registry = NULL;
    SlStringBuilder prom = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_ops_metrics_registry_init(&arena, NULL, &registry), SL_STATUS_OK) != 0 ||
        expect_status(
            sl_ops_metrics_counter_set(registry, sl_str_from_cstr("db.query.total"), NULL, 0U, 5.0),
            SL_STATUS_OK) != 0 ||
        expect_status(
            sl_ops_metrics_counter_set(registry, sl_str_from_cstr("db.query.total"), NULL, 0U, 5.0),
            SL_STATUS_OK) != 0 ||
        expect_status(
            sl_ops_metrics_counter_set(registry, sl_str_from_cstr("db.query.total"), NULL, 0U, 4.0),
            SL_STATUS_INVALID_STATE) != 0 ||
        expect_status(sl_string_builder_init_arena(&prom, &arena, 1024U, 8192U), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_ops_metrics_render_prometheus(registry, &prom), SL_STATUS_OK) != 0)
    {
        return 30;
    }
    if (!str_contains(sl_string_builder_view(&prom), "db_query_total 5")) {
        return 31;
    }
    return 0;
}

static int test_prometheus_label_values_are_escaped(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlOpsMetricsRegistry* registry = NULL;
    SlOpsMetricLabel labels[1] = {
        {sl_str_from_cstr("value"), sl_str_from_cstr("quote\"slash\\line\nnext")},
    };
    SlStringBuilder prom = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_ops_metrics_registry_init(&arena, NULL, &registry), SL_STATUS_OK) != 0 ||
        expect_status(
            sl_ops_metrics_counter_inc(registry, sl_str_from_cstr("requests"), labels, 1U, 1.0),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_init_arena(&prom, &arena, 1024U, 8192U), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_ops_metrics_render_prometheus(registry, &prom), SL_STATUS_OK) != 0)
    {
        return 40;
    }
    if (!str_contains(sl_string_builder_view(&prom), "value=\"quote\\\"slash\\\\line\\nnext\"")) {
        return 41;
    }
    return 0;
}

static int test_json_label_control_chars_are_escaped_by_codepoint(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlOpsMetricsRegistry* registry = NULL;
    SlOpsMetricLabel labels[1] = {
        {sl_str_from_cstr("value"), sl_str_from_parts("line\n\t\r", 7U)},
    };
    SlOpsMetricLabel nul_labels[1] = {
        {sl_str_from_cstr("value"), sl_str_from_parts("nul\0byte", 8U)},
    };
    SlStringBuilder json = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_ops_metrics_registry_init(&arena, NULL, &registry), SL_STATUS_OK) != 0 ||
        expect_status(
            sl_ops_metrics_counter_inc(registry, sl_str_from_cstr("requests"), labels, 1U, 1.0),
            SL_STATUS_OK) != 0 ||
        expect_status(
            sl_ops_metrics_counter_inc(registry, sl_str_from_cstr("requests"), nul_labels, 1U, 1.0),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_init_arena(&json, &arena, 1024U, 8192U), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_ops_metrics_render_json(registry, &json), SL_STATUS_OK) != 0)
    {
        return 50;
    }
    if (!str_contains(sl_string_builder_view(&json), "line\\u000a\\u0009\\u000d") ||
        !str_contains(sl_string_builder_view(&json), "nul\\u0000byte") ||
        str_contains(sl_string_builder_view(&json), "\\u001f"))
    {
        return 51;
    }
    return 0;
}

int main(void)
{
    int result = test_counter_gauge_histogram_and_renderers();

    if (result != 0) {
        return result;
    }
    result = test_cardinality_guard_records_drops();
    if (result != 0) {
        return result;
    }
    result = test_reset_preserves_series_and_clears_values();
    if (result != 0) {
        return result;
    }
    result = test_counter_set_preserves_absolute_snapshots();
    if (result != 0) {
        return result;
    }
    result = test_prometheus_label_values_are_escaped();
    if (result != 0) {
        return result;
    }
    return test_json_label_control_chars_are_escaped_by_codepoint();
}
