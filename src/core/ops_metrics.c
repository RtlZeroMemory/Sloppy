#include "sloppy/ops_metrics.h"

#include "sloppy/checked_math.h"

#include <stdbool.h>

#define SL_OPS_METRICS_DEFAULT_MAX_METRICS 128U
#define SL_OPS_METRICS_DEFAULT_MAX_SERIES 128U

typedef struct SlOpsMetricSeries
{
    SlOpsMetricLabel labels[SL_OPS_METRIC_MAX_LABELS];
    size_t label_count;
    double value;
    double sum;
    uint64_t count;
    uint64_t buckets[SL_OPS_METRIC_MAX_BUCKETS];
} SlOpsMetricSeries;

typedef struct SlOpsMetric
{
    SlOpsMetricKind kind;
    SlStr name;
    SlStr description;
    double buckets[SL_OPS_METRIC_MAX_BUCKETS];
    size_t bucket_count;
    SlOpsMetricSeries* series;
    size_t series_count;
    size_t series_capacity;
} SlOpsMetric;

struct SlOpsMetricsRegistry
{
    SlArena* arena;
    SlPlatformMutex* mutex;
    SlOpsMetric* metrics;
    size_t metric_count;
    size_t max_metrics;
    size_t max_series_per_metric;
    uint64_t cardinality_drops;
};

static void sl_ops_metric_zero_series(SlOpsMetricSeries* series, size_t count)
{
    size_t index = 0U;

    for (index = 0U; index < count; index += 1U) {
        series[index] = (SlOpsMetricSeries){0};
    }
}

static void sl_ops_metric_copy_series(SlOpsMetricSeries* target, const SlOpsMetricSeries* source,
                                      size_t count)
{
    size_t index = 0U;

    for (index = 0U; index < count; index += 1U) {
        target[index] = source[index];
    }
}

static bool sl_ops_metric_name_valid(SlStr name)
{
    size_t index = 0U;

    if (sl_str_is_empty(name) || name.ptr == NULL) {
        return false;
    }
    for (index = 0U; index < name.length; index += 1U) {
        char ch = name.ptr[index];
        bool alpha = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
        bool digit = ch >= '0' && ch <= '9';
        bool punctuation = ch == '_' || ch == ':' || ch == '.' || ch == '-';

        if (!(alpha || digit || punctuation)) {
            return false;
        }
        if (index == 0U && digit) {
            return false;
        }
    }
    return true;
}

static int sl_ops_metric_label_compare(const SlOpsMetricLabel* left, const SlOpsMetricLabel* right)
{
    int name = sl_str_compare(left->name, right->name);

    if (name != 0) {
        return name;
    }
    return sl_str_compare(left->value, right->value);
}

static SlStatus
sl_ops_metric_copy_sorted_labels(SlArena* arena, const SlOpsMetricLabel* labels, size_t label_count,
                                 SlOpsMetricLabel out_labels[SL_OPS_METRIC_MAX_LABELS])
{
    size_t index = 0U;

    if (label_count > SL_OPS_METRIC_MAX_LABELS) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }
    if (label_count != 0U && labels == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    for (index = 0U; index < label_count; index += 1U) {
        size_t inner = 0U;

        if (!sl_ops_metric_name_valid(labels[index].name) ||
            (labels[index].value.length != 0U && labels[index].value.ptr == NULL))
        {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        out_labels[index] = labels[index];
        for (inner = index; inner > 0U && sl_ops_metric_label_compare(&out_labels[inner],
                                                                      &out_labels[inner - 1U]) < 0;
             inner -= 1U)
        {
            SlOpsMetricLabel tmp = out_labels[inner - 1U];
            out_labels[inner - 1U] = out_labels[inner];
            out_labels[inner] = tmp;
        }
    }
    for (index = 0U; index < label_count; index += 1U) {
        SlStr copied = sl_str_empty();
        SlStatus status = sl_str_copy_view_to_arena(arena, out_labels[index].name, &copied);

        if (!sl_status_is_ok(status)) {
            return status;
        }
        out_labels[index].name = copied;
        status = sl_str_copy_view_to_arena(arena, out_labels[index].value, &copied);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        out_labels[index].value = copied;
    }
    return sl_status_ok();
}

static bool sl_ops_metric_labels_equal(const SlOpsMetricSeries* series,
                                       const SlOpsMetricLabel* labels, size_t label_count)
{
    size_t index = 0U;

    if (series->label_count != label_count) {
        return false;
    }
    for (index = 0U; index < label_count; index += 1U) {
        if (!sl_str_equal(series->labels[index].name, labels[index].name) ||
            !sl_str_equal(series->labels[index].value, labels[index].value))
        {
            return false;
        }
    }
    return true;
}

static SlStatus sl_ops_metric_find_or_create(SlOpsMetricsRegistry* registry, SlStr name,
                                             SlOpsMetricKind kind, SlOpsMetric** out_metric)
{
    size_t index = 0U;
    SlOpsMetric* metric = NULL;
    SlStatus status;

    if (registry == NULL || out_metric == NULL || !sl_ops_metric_name_valid(name)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_metric = NULL;
    for (index = 0U; index < registry->metric_count; index += 1U) {
        if (sl_str_equal(registry->metrics[index].name, name)) {
            if (registry->metrics[index].kind != kind) {
                return sl_status_from_code(SL_STATUS_INVALID_STATE);
            }
            *out_metric = &registry->metrics[index];
            return sl_status_ok();
        }
    }
    if (registry->metric_count >= registry->max_metrics) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }
    metric = &registry->metrics[registry->metric_count];
    *metric = (SlOpsMetric){0};
    metric->kind = kind;
    status = sl_str_copy_view_to_arena(registry->arena, name, &metric->name);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    registry->metric_count += 1U;
    *out_metric = metric;
    return sl_status_ok();
}

static SlStatus sl_ops_metric_series_find_or_create(SlOpsMetricsRegistry* registry,
                                                    SlOpsMetric* metric,
                                                    const SlOpsMetricLabel* labels,
                                                    size_t label_count,
                                                    SlOpsMetricSeries** out_series)
{
    SlOpsMetricLabel sorted[SL_OPS_METRIC_MAX_LABELS] = {{0}};
    size_t index = 0U;
    SlStatus status;

    if (registry == NULL || metric == NULL || out_series == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_series = NULL;
    status = sl_ops_metric_copy_sorted_labels(registry->arena, labels, label_count, sorted);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (index = 0U; index < metric->series_count; index += 1U) {
        if (sl_ops_metric_labels_equal(&metric->series[index], sorted, label_count)) {
            *out_series = &metric->series[index];
            return sl_status_ok();
        }
    }
    if (metric->series_count >= registry->max_series_per_metric) {
        registry->cardinality_drops += 1U;
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }
    if (metric->series_count >= metric->series_capacity) {
        void* memory = NULL;
        SlOpsMetricSeries* grown = NULL;
        size_t next_capacity = metric->series_capacity == 0U ? 4U : metric->series_capacity * 2U;
        size_t bytes = 0U;

        if (next_capacity > registry->max_series_per_metric) {
            next_capacity = registry->max_series_per_metric;
        }
        status = sl_checked_mul_size(next_capacity, sizeof(SlOpsMetricSeries), &bytes);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_arena_alloc(registry->arena, bytes, _Alignof(SlOpsMetricSeries), &memory);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        grown = (SlOpsMetricSeries*)memory;
        sl_ops_metric_zero_series(grown, next_capacity);
        if (metric->series != NULL && metric->series_count != 0U) {
            sl_ops_metric_copy_series(grown, metric->series, metric->series_count);
        }
        metric->series = grown;
        metric->series_capacity = next_capacity;
    }
    *out_series = &metric->series[metric->series_count];
    **out_series = (SlOpsMetricSeries){0};
    (*out_series)->label_count = label_count;
    for (index = 0U; index < label_count; index += 1U) {
        (*out_series)->labels[index] = sorted[index];
    }
    metric->series_count += 1U;
    return sl_status_ok();
}

static SlStatus sl_ops_metric_prepare_histogram(SlOpsMetric* metric, const double* buckets,
                                                size_t bucket_count)
{
    size_t index = 0U;

    if (metric == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (bucket_count > SL_OPS_METRIC_MAX_BUCKETS || (bucket_count != 0U && buckets == NULL)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (metric->bucket_count != 0U) {
        if (metric->bucket_count != bucket_count) {
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        for (index = 0U; index < bucket_count; index += 1U) {
            if (metric->buckets[index] != buckets[index]) {
                return sl_status_from_code(SL_STATUS_INVALID_STATE);
            }
        }
        return sl_status_ok();
    }
    for (index = 0U; index < bucket_count; index += 1U) {
        if (index != 0U && buckets[index] <= buckets[index - 1U]) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        metric->buckets[index] = buckets[index];
    }
    metric->bucket_count = bucket_count;
    return sl_status_ok();
}

SlStatus sl_ops_metrics_registry_init(SlArena* arena, const SlOpsMetricsOptions* options,
                                      SlOpsMetricsRegistry** out_registry)
{
    SlOpsMetricsRegistry* registry = NULL;
    size_t max_metrics = SL_OPS_METRICS_DEFAULT_MAX_METRICS;
    size_t max_series = SL_OPS_METRICS_DEFAULT_MAX_SERIES;
    size_t bytes = 0U;
    void* memory = NULL;
    SlStatus status;

    if (arena == NULL || out_registry == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_registry = NULL;
    if (options != NULL) {
        if (options->max_metrics != 0U) {
            max_metrics = options->max_metrics;
        }
        if (options->max_series_per_metric != 0U) {
            max_series = options->max_series_per_metric;
        }
    }
    status = sl_arena_alloc(arena, sizeof(SlOpsMetricsRegistry), _Alignof(SlOpsMetricsRegistry),
                            &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    registry = (SlOpsMetricsRegistry*)memory;
    *registry = (SlOpsMetricsRegistry){0};
    registry->arena = arena;
    registry->max_metrics = max_metrics;
    registry->max_series_per_metric = max_series;
    status = sl_checked_mul_size(max_metrics, sizeof(SlOpsMetric), &bytes);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, bytes, _Alignof(SlOpsMetric), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    registry->metrics = (SlOpsMetric*)memory;
    for (registry->metric_count = 0U; registry->metric_count < max_metrics;
         registry->metric_count += 1U)
    {
        registry->metrics[registry->metric_count] = (SlOpsMetric){0};
    }
    registry->metric_count = 0U;
    status = sl_platform_mutex_create(arena, &registry->mutex);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out_registry = registry;
    return sl_status_ok();
}

SlStatus sl_ops_metrics_counter_inc(SlOpsMetricsRegistry* registry, SlStr name,
                                    const SlOpsMetricLabel* labels, size_t label_count,
                                    double amount)
{
    SlOpsMetric* metric = NULL;
    SlOpsMetricSeries* series = NULL;
    SlStatus status;

    if (registry == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (amount < 0.0) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    sl_platform_mutex_lock(registry->mutex);
    status = sl_ops_metric_find_or_create(registry, name, SL_OPS_METRIC_COUNTER, &metric);
    if (sl_status_is_ok(status)) {
        status =
            sl_ops_metric_series_find_or_create(registry, metric, labels, label_count, &series);
    }
    if (sl_status_is_ok(status) && series != NULL) {
        series->value += amount;
    }
    sl_platform_mutex_unlock(registry->mutex);
    return status;
}

SlStatus sl_ops_metrics_counter_set(SlOpsMetricsRegistry* registry, SlStr name,
                                    const SlOpsMetricLabel* labels, size_t label_count,
                                    double value)
{
    SlOpsMetric* metric = NULL;
    SlOpsMetricSeries* series = NULL;
    SlStatus status;

    if (registry == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (value < 0.0) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    sl_platform_mutex_lock(registry->mutex);
    status = sl_ops_metric_find_or_create(registry, name, SL_OPS_METRIC_COUNTER, &metric);
    if (sl_status_is_ok(status)) {
        status =
            sl_ops_metric_series_find_or_create(registry, metric, labels, label_count, &series);
    }
    if (sl_status_is_ok(status) && series != NULL) {
        if (value < series->value) {
            status = sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        else {
            series->value = value;
        }
    }
    sl_platform_mutex_unlock(registry->mutex);
    return status;
}

SlStatus sl_ops_metrics_gauge_set(SlOpsMetricsRegistry* registry, SlStr name,
                                  const SlOpsMetricLabel* labels, size_t label_count, double value)
{
    SlOpsMetric* metric = NULL;
    SlOpsMetricSeries* series = NULL;
    SlStatus status;

    if (registry == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    sl_platform_mutex_lock(registry->mutex);
    status = sl_ops_metric_find_or_create(registry, name, SL_OPS_METRIC_GAUGE, &metric);
    if (sl_status_is_ok(status)) {
        status =
            sl_ops_metric_series_find_or_create(registry, metric, labels, label_count, &series);
    }
    if (sl_status_is_ok(status) && series != NULL) {
        series->value = value;
    }
    sl_platform_mutex_unlock(registry->mutex);
    return status;
}

SlStatus sl_ops_metrics_gauge_add(SlOpsMetricsRegistry* registry, SlStr name,
                                  const SlOpsMetricLabel* labels, size_t label_count, double amount)
{
    SlOpsMetric* metric = NULL;
    SlOpsMetricSeries* series = NULL;
    SlStatus status;

    if (registry == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    sl_platform_mutex_lock(registry->mutex);
    status = sl_ops_metric_find_or_create(registry, name, SL_OPS_METRIC_GAUGE, &metric);
    if (sl_status_is_ok(status)) {
        status =
            sl_ops_metric_series_find_or_create(registry, metric, labels, label_count, &series);
    }
    if (sl_status_is_ok(status) && series != NULL) {
        series->value += amount;
    }
    sl_platform_mutex_unlock(registry->mutex);
    return status;
}

SlStatus sl_ops_metrics_histogram_observe(SlOpsMetricsRegistry* registry, SlStr name,
                                          const SlOpsMetricLabel* labels, size_t label_count,
                                          const double* buckets, size_t bucket_count, double value)
{
    SlOpsMetric* metric = NULL;
    SlOpsMetricSeries* series = NULL;
    SlStatus status;
    size_t index = 0U;

    if (registry == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    sl_platform_mutex_lock(registry->mutex);
    status = sl_ops_metric_find_or_create(registry, name, SL_OPS_METRIC_HISTOGRAM, &metric);
    if (sl_status_is_ok(status)) {
        status = sl_ops_metric_prepare_histogram(metric, buckets, bucket_count);
    }
    if (sl_status_is_ok(status)) {
        status =
            sl_ops_metric_series_find_or_create(registry, metric, labels, label_count, &series);
    }
    if (sl_status_is_ok(status) && series != NULL && metric != NULL) {
        series->count += 1U;
        series->sum += value;
        for (index = 0U; index < metric->bucket_count; index += 1U) {
            if (value <= metric->buckets[index]) {
                series->buckets[index] += 1U;
            }
        }
    }
    sl_platform_mutex_unlock(registry->mutex);
    return status;
}

static SlStatus sl_ops_metrics_append_json_string(SlStringBuilder* builder, SlStr value)
{
    static const char hex[] = "0123456789abcdef";
    size_t index = 0U;
    SlStatus status = sl_string_builder_append_char(builder, '"');

    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (index = 0U; index < value.length; index += 1U) {
        char ch = value.ptr[index];

        if (ch == '"' || ch == '\\') {
            status = sl_string_builder_append_char(builder, '\\');
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
        if ((unsigned char)ch < 0x20U) {
            unsigned char code = (unsigned char)ch;
            char escaped[7] = {'\\', 'u', '0', '0', hex[(code >> 4U) & 0x0FU], hex[code & 0x0FU],
                               '\0'};

            status = sl_string_builder_append_cstr(builder, escaped);
        }
        else {
            status = sl_string_builder_append_char(builder, ch);
        }
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_string_builder_append_char(builder, '"');
}

static SlStatus sl_ops_metrics_append_f64(SlStringBuilder* builder, double value)
{
    return sl_string_builder_append_f64(builder, value);
}

static SlStatus sl_ops_metrics_append_label_json(SlStringBuilder* builder,
                                                 const SlOpsMetricSeries* series)
{
    size_t index = 0U;
    SlStatus status = sl_string_builder_append_cstr(builder, "\"labels\":{");

    for (index = 0U; sl_status_is_ok(status) && index < series->label_count; index += 1U) {
        if (index != 0U) {
            status = sl_string_builder_append_char(builder, ',');
        }
        if (sl_status_is_ok(status)) {
            status = sl_ops_metrics_append_json_string(builder, series->labels[index].name);
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_char(builder, ':');
        }
        if (sl_status_is_ok(status)) {
            status = sl_ops_metrics_append_json_string(builder, series->labels[index].value);
        }
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_char(builder, '}');
    }
    return status;
}

SlStatus sl_ops_metrics_render_json(SlOpsMetricsRegistry* registry, SlStringBuilder* builder)
{
    size_t metric_index = 0U;
    SlStatus status;

    if (registry == NULL || builder == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    sl_platform_mutex_lock(registry->mutex);
    status = sl_string_builder_append_cstr(builder, "{\"metrics\":[");
    for (metric_index = 0U; sl_status_is_ok(status) && metric_index < registry->metric_count;
         metric_index += 1U)
    {
        SlOpsMetric* metric = &registry->metrics[metric_index];
        size_t series_index = 0U;

        if (metric_index != 0U) {
            status = sl_string_builder_append_char(builder, ',');
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(builder, "{\"name\":");
        }
        if (sl_status_is_ok(status)) {
            status = sl_ops_metrics_append_json_string(builder, metric->name);
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(builder, ",\"kind\":");
        }
        if (sl_status_is_ok(status)) {
            status = sl_ops_metrics_append_json_string(builder,
                                                       metric->kind == SL_OPS_METRIC_COUNTER
                                                           ? sl_str_from_cstr("counter")
                                                           : (metric->kind == SL_OPS_METRIC_GAUGE
                                                                  ? sl_str_from_cstr("gauge")
                                                                  : sl_str_from_cstr("histogram")));
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(builder, ",\"series\":[");
        }
        for (series_index = 0U; sl_status_is_ok(status) && series_index < metric->series_count;
             series_index += 1U)
        {
            SlOpsMetricSeries* series = &metric->series[series_index];
            size_t bucket_index = 0U;

            if (series_index != 0U) {
                status = sl_string_builder_append_char(builder, ',');
            }
            if (sl_status_is_ok(status)) {
                status = sl_string_builder_append_char(builder, '{');
            }
            if (sl_status_is_ok(status)) {
                status = sl_ops_metrics_append_label_json(builder, series);
            }
            if (metric->kind == SL_OPS_METRIC_HISTOGRAM) {
                if (sl_status_is_ok(status)) {
                    status = sl_string_builder_append_cstr(builder, ",\"sum\":");
                }
                if (sl_status_is_ok(status)) {
                    status = sl_ops_metrics_append_f64(builder, series->sum);
                }
                if (sl_status_is_ok(status)) {
                    status = sl_string_builder_append_cstr(builder, ",\"count\":");
                }
                if (sl_status_is_ok(status)) {
                    status = sl_string_builder_append_u64(builder, series->count);
                }
                if (sl_status_is_ok(status)) {
                    status = sl_string_builder_append_cstr(builder, ",\"buckets\":[");
                }
                for (bucket_index = 0U;
                     sl_status_is_ok(status) && bucket_index < metric->bucket_count;
                     bucket_index += 1U)
                {
                    if (bucket_index != 0U) {
                        status = sl_string_builder_append_char(builder, ',');
                    }
                    if (sl_status_is_ok(status)) {
                        status = sl_string_builder_append_cstr(builder, "{\"le\":");
                    }
                    if (sl_status_is_ok(status)) {
                        status = sl_ops_metrics_append_f64(builder, metric->buckets[bucket_index]);
                    }
                    if (sl_status_is_ok(status)) {
                        status = sl_string_builder_append_cstr(builder, ",\"count\":");
                    }
                    if (sl_status_is_ok(status)) {
                        status =
                            sl_string_builder_append_u64(builder, series->buckets[bucket_index]);
                    }
                    if (sl_status_is_ok(status)) {
                        status = sl_string_builder_append_char(builder, '}');
                    }
                }
                if (sl_status_is_ok(status)) {
                    status = sl_string_builder_append_char(builder, ']');
                }
            }
            else {
                if (sl_status_is_ok(status)) {
                    status = sl_string_builder_append_cstr(builder, ",\"value\":");
                }
                if (sl_status_is_ok(status)) {
                    status = sl_ops_metrics_append_f64(builder, series->value);
                }
            }
            if (sl_status_is_ok(status)) {
                status = sl_string_builder_append_char(builder, '}');
            }
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(builder, "]}");
        }
    }
    if (sl_status_is_ok(status)) {
        if (registry->cardinality_drops != 0U && registry->metric_count != 0U) {
            status = sl_string_builder_append_char(builder, ',');
        }
        if (sl_status_is_ok(status) && registry->cardinality_drops != 0U) {
            status = sl_string_builder_append_cstr(
                builder,
                "{\"name\":\"sloppy_metrics_cardinality_drops_total\",\"kind\":\"counter\","
                "\"series\":[{\"labels\":{},\"value\":");
        }
        if (sl_status_is_ok(status) && registry->cardinality_drops != 0U) {
            status = sl_string_builder_append_u64(builder, registry->cardinality_drops);
        }
        if (sl_status_is_ok(status) && registry->cardinality_drops != 0U) {
            status = sl_string_builder_append_cstr(builder, "}]}");
        }
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_cstr(builder, "]}");
    }
    sl_platform_mutex_unlock(registry->mutex);
    return status;
}

static SlStatus sl_ops_metrics_append_prom_name(SlStringBuilder* builder, SlStr name)
{
    size_t index = 0U;

    for (index = 0U; index < name.length; index += 1U) {
        char ch = name.ptr[index];
        bool keep = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                    (ch >= '0' && ch <= '9') || ch == '_' || ch == ':';
        char out = '_';
        SlStatus status;

        if (keep) {
            out = ch;
        }
        status = sl_string_builder_append_char(builder, out);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_status_ok();
}

static SlStatus sl_ops_metrics_append_prom_label_value(SlStringBuilder* builder, SlStr value)
{
    size_t index = 0U;
    SlStatus status = sl_status_ok();

    for (index = 0U; sl_status_is_ok(status) && index < value.length; index += 1U) {
        char ch = value.ptr[index];

        if (ch == '\\' || ch == '"') {
            status = sl_string_builder_append_char(builder, '\\');
            if (sl_status_is_ok(status)) {
                status = sl_string_builder_append_char(builder, ch);
            }
        }
        else if (ch == '\n') {
            status = sl_string_builder_append_cstr(builder, "\\n");
        }
        else {
            status = sl_string_builder_append_char(builder, ch);
        }
    }
    return status;
}

static SlStatus sl_ops_metrics_append_prom_labels(SlStringBuilder* builder,
                                                  const SlOpsMetricSeries* series,
                                                  const char* extra_name, SlStr extra_value)
{
    size_t index = 0U;
    bool any = series->label_count != 0U || extra_name != NULL;
    SlStatus status = sl_status_ok();

    if (!any) {
        return status;
    }
    status = sl_string_builder_append_char(builder, '{');
    for (index = 0U; sl_status_is_ok(status) && index < series->label_count; index += 1U) {
        if (index != 0U) {
            status = sl_string_builder_append_char(builder, ',');
        }
        if (sl_status_is_ok(status)) {
            status = sl_ops_metrics_append_prom_name(builder, series->labels[index].name);
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(builder, "=\"");
        }
        if (sl_status_is_ok(status)) {
            status = sl_ops_metrics_append_prom_label_value(builder, series->labels[index].value);
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_char(builder, '"');
        }
    }
    if (sl_status_is_ok(status) && extra_name != NULL) {
        if (series->label_count != 0U) {
            status = sl_string_builder_append_char(builder, ',');
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(builder, extra_name);
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(builder, "=\"");
        }
        if (sl_status_is_ok(status)) {
            status = sl_ops_metrics_append_prom_label_value(builder, extra_value);
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_char(builder, '"');
        }
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_char(builder, '}');
    }
    return status;
}

SlStatus sl_ops_metrics_render_prometheus(SlOpsMetricsRegistry* registry, SlStringBuilder* builder)
{
    size_t metric_index = 0U;
    SlStatus status;

    if (registry == NULL || builder == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    sl_platform_mutex_lock(registry->mutex);
    status = sl_status_ok();
    for (metric_index = 0U; sl_status_is_ok(status) && metric_index < registry->metric_count;
         metric_index += 1U)
    {
        SlOpsMetric* metric = &registry->metrics[metric_index];
        size_t series_index = 0U;

        status = sl_string_builder_append_cstr(builder, "# TYPE ");
        if (sl_status_is_ok(status)) {
            status = sl_ops_metrics_append_prom_name(builder, metric->name);
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_char(builder, ' ');
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(
                builder, metric->kind == SL_OPS_METRIC_COUNTER
                             ? "counter\n"
                             : (metric->kind == SL_OPS_METRIC_GAUGE ? "gauge\n" : "histogram\n"));
        }
        for (series_index = 0U; sl_status_is_ok(status) && series_index < metric->series_count;
             series_index += 1U)
        {
            SlOpsMetricSeries* series = &metric->series[series_index];
            size_t bucket_index = 0U;

            if (metric->kind == SL_OPS_METRIC_HISTOGRAM) {
                for (bucket_index = 0U;
                     sl_status_is_ok(status) && bucket_index < metric->bucket_count;
                     bucket_index += 1U)
                {
                    char le_buffer[SL_STRING_FORMAT_F64_CAPACITY];
                    SlStr le = sl_str_empty();

                    status = sl_string_format_f64(le_buffer, sizeof(le_buffer),
                                                  metric->buckets[bucket_index], &le);
                    if (sl_status_is_ok(status)) {
                        status = sl_ops_metrics_append_prom_name(builder, metric->name);
                    }
                    if (sl_status_is_ok(status)) {
                        status = sl_string_builder_append_cstr(builder, "_bucket");
                    }
                    if (sl_status_is_ok(status)) {
                        status = sl_ops_metrics_append_prom_labels(builder, series, "le", le);
                    }
                    if (sl_status_is_ok(status)) {
                        status = sl_string_builder_append_char(builder, ' ');
                    }
                    if (sl_status_is_ok(status)) {
                        status =
                            sl_string_builder_append_u64(builder, series->buckets[bucket_index]);
                    }
                    if (sl_status_is_ok(status)) {
                        status = sl_string_builder_append_char(builder, '\n');
                    }
                }
                if (sl_status_is_ok(status)) {
                    status = sl_ops_metrics_append_prom_name(builder, metric->name);
                }
                if (sl_status_is_ok(status)) {
                    status = sl_string_builder_append_cstr(builder, "_bucket");
                }
                if (sl_status_is_ok(status)) {
                    status = sl_ops_metrics_append_prom_labels(builder, series, "le",
                                                               sl_str_from_cstr("+Inf"));
                }
                if (sl_status_is_ok(status)) {
                    status = sl_string_builder_append_char(builder, ' ');
                }
                if (sl_status_is_ok(status)) {
                    status = sl_string_builder_append_u64(builder, series->count);
                }
                if (sl_status_is_ok(status)) {
                    status = sl_string_builder_append_char(builder, '\n');
                }
                if (sl_status_is_ok(status)) {
                    status = sl_ops_metrics_append_prom_name(builder, metric->name);
                }
                if (sl_status_is_ok(status)) {
                    status = sl_string_builder_append_cstr(builder, "_sum");
                }
                if (sl_status_is_ok(status)) {
                    status =
                        sl_ops_metrics_append_prom_labels(builder, series, NULL, sl_str_empty());
                }
                if (sl_status_is_ok(status)) {
                    status = sl_string_builder_append_char(builder, ' ');
                }
                if (sl_status_is_ok(status)) {
                    status = sl_ops_metrics_append_f64(builder, series->sum);
                }
                if (sl_status_is_ok(status)) {
                    status = sl_string_builder_append_char(builder, '\n');
                }
                if (sl_status_is_ok(status)) {
                    status = sl_ops_metrics_append_prom_name(builder, metric->name);
                }
                if (sl_status_is_ok(status)) {
                    status = sl_string_builder_append_cstr(builder, "_count");
                }
                if (sl_status_is_ok(status)) {
                    status =
                        sl_ops_metrics_append_prom_labels(builder, series, NULL, sl_str_empty());
                }
                if (sl_status_is_ok(status)) {
                    status = sl_string_builder_append_char(builder, ' ');
                }
                if (sl_status_is_ok(status)) {
                    status = sl_string_builder_append_u64(builder, series->count);
                }
            }
            else {
                status = sl_ops_metrics_append_prom_name(builder, metric->name);
                if (sl_status_is_ok(status)) {
                    status =
                        sl_ops_metrics_append_prom_labels(builder, series, NULL, sl_str_empty());
                }
                if (sl_status_is_ok(status)) {
                    status = sl_string_builder_append_char(builder, ' ');
                }
                if (sl_status_is_ok(status)) {
                    status = sl_ops_metrics_append_f64(builder, series->value);
                }
            }
            if (sl_status_is_ok(status)) {
                status = sl_string_builder_append_char(builder, '\n');
            }
        }
    }
    if (sl_status_is_ok(status) && registry->cardinality_drops != 0U) {
        status = sl_string_builder_append_cstr(
            builder, "# TYPE sloppy_metrics_cardinality_drops_total counter\n"
                     "sloppy_metrics_cardinality_drops_total ");
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_u64(builder, registry->cardinality_drops);
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_char(builder, '\n');
        }
    }
    sl_platform_mutex_unlock(registry->mutex);
    return status;
}

SlStatus sl_ops_metrics_reset(SlOpsMetricsRegistry* registry)
{
    size_t metric_index = 0U;

    if (registry == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    sl_platform_mutex_lock(registry->mutex);
    for (metric_index = 0U; metric_index < registry->metric_count; metric_index += 1U) {
        SlOpsMetric* metric = &registry->metrics[metric_index];
        size_t series_index = 0U;

        for (series_index = 0U; series_index < metric->series_count; series_index += 1U) {
            SlOpsMetricSeries* series = &metric->series[series_index];
            size_t bucket_index = 0U;

            series->value = 0.0;
            series->sum = 0.0;
            series->count = 0U;
            for (bucket_index = 0U; bucket_index < metric->bucket_count; bucket_index += 1U) {
                series->buckets[bucket_index] = 0U;
            }
        }
    }
    registry->cardinality_drops = 0U;
    sl_platform_mutex_unlock(registry->mutex);
    return sl_status_ok();
}

uint64_t sl_ops_metrics_cardinality_drops(const SlOpsMetricsRegistry* registry)
{
    return registry == NULL ? 0U : registry->cardinality_drops;
}
