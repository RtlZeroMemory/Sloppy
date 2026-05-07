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
    uint64_t checksum;
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
} SlBenchDefinition;

const SlBenchDefinition* sl_bench_route_definitions(size_t* out_count);
const SlBenchDefinition* sl_bench_handler_dispatch_definitions(size_t* out_count);
const SlBenchDefinition* sl_bench_memory_definitions(size_t* out_count);

#endif
