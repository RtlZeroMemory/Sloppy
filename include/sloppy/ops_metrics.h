#ifndef SLOPPY_OPS_METRICS_H
#define SLOPPY_OPS_METRICS_H

#include "sloppy/arena.h"
#include "sloppy/builder.h"
#include "sloppy/platform_thread.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_OPS_METRIC_MAX_LABELS 4U
#define SL_OPS_METRIC_MAX_BUCKETS 16U

typedef struct SlOpsMetricsRegistry SlOpsMetricsRegistry;

typedef enum SlOpsMetricKind
{
    SL_OPS_METRIC_COUNTER = 0,
    SL_OPS_METRIC_GAUGE = 1,
    SL_OPS_METRIC_HISTOGRAM = 2
} SlOpsMetricKind;

typedef struct SlOpsMetricLabel
{
    SlStr name;
    SlStr value;
} SlOpsMetricLabel;

typedef struct SlOpsMetricsOptions
{
    size_t max_metrics;
    size_t max_series_per_metric;
} SlOpsMetricsOptions;

SlStatus sl_ops_metrics_registry_init(SlArena* arena, const SlOpsMetricsOptions* options,
                                      SlOpsMetricsRegistry** out_registry);

SlStatus sl_ops_metrics_counter_inc(SlOpsMetricsRegistry* registry, SlStr name,
                                    const SlOpsMetricLabel* labels, size_t label_count,
                                    double amount);
SlStatus sl_ops_metrics_counter_set(SlOpsMetricsRegistry* registry, SlStr name,
                                    const SlOpsMetricLabel* labels, size_t label_count,
                                    double value);
SlStatus sl_ops_metrics_gauge_set(SlOpsMetricsRegistry* registry, SlStr name,
                                  const SlOpsMetricLabel* labels, size_t label_count, double value);
SlStatus sl_ops_metrics_gauge_add(SlOpsMetricsRegistry* registry, SlStr name,
                                  const SlOpsMetricLabel* labels, size_t label_count,
                                  double amount);
SlStatus sl_ops_metrics_histogram_observe(SlOpsMetricsRegistry* registry, SlStr name,
                                          const SlOpsMetricLabel* labels, size_t label_count,
                                          const double* buckets, size_t bucket_count, double value);

SlStatus sl_ops_metrics_render_json(SlOpsMetricsRegistry* registry, SlStringBuilder* builder);
SlStatus sl_ops_metrics_render_prometheus(SlOpsMetricsRegistry* registry, SlStringBuilder* builder);
SlStatus sl_ops_metrics_reset(SlOpsMetricsRegistry* registry);
uint64_t sl_ops_metrics_cardinality_drops(const SlOpsMetricsRegistry* registry);

#ifdef __cplusplus
}
#endif

#endif
