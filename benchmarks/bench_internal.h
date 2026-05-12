#ifndef SLOPPY_BENCH_INTERNAL_H
#define SLOPPY_BENCH_INTERNAL_H

#include "sloppy/status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct SlBenchContext
{
    bool smoke;
    bool include_v8;
} SlBenchContext;

typedef struct SlBenchResult
{
    const char* name;
    const char* category;
    uint64_t iterations;
    uint64_t warmup_iterations;
    uint64_t elapsed_ns;
    double ns_per_op;
    uint64_t bytes_processed;
    uint64_t chunks_processed;
    uint64_t rows_processed;
    double bytes_per_second;
    double chunks_per_second;
    double rows_per_second;
    uint64_t checksum;
    uint64_t backpressure_count;
    uint64_t native_hits;
    uint64_t generic_fallback_count;
    uint64_t materialization_count;
    uint64_t reject_count;
    const char* note;
} SlBenchResult;

typedef SlStatus (*SlBenchRunFn)(const SlBenchContext* context, uint64_t iterations,
                                 uint64_t* out_checksum);

typedef struct SlBenchDefinition
{
    const char* name;
    const char* category;
    const char* description;
    uint64_t warmup_iterations;
    uint64_t measured_iterations;
    SlBenchRunFn run;
    const char* note;
    bool requires_v8;
    uint64_t bytes_per_iteration;
    uint64_t chunks_per_iteration;
    uint64_t backpressure_per_iteration;
} SlBenchDefinition;

const SlBenchDefinition* sl_bench_route_definitions(size_t* out_count);
const SlBenchDefinition* sl_bench_handler_dispatch_definitions(size_t* out_count);
const SlBenchDefinition* sl_bench_json_dispatch_definitions(size_t* out_count);
const SlBenchDefinition* sl_bench_logging_definitions(size_t* out_count);
const SlBenchDefinition* sl_bench_memory_definitions(size_t* out_count);
const SlBenchDefinition* sl_bench_stream_definitions(size_t* out_count);
const SlBenchDefinition* sl_bench_v8_bridge_definitions(size_t* out_count);

#endif
