#include "bench_internal.h"

#include "sloppy/arena.h"
#include "sloppy/builder.h"
#include "sloppy/bytes.h"
#include "sloppy/checked_math.h"
#include "sloppy/string.h"

#include <stddef.h>
#include <stdint.h>

#define SL_BENCH_MEMORY_BUFFER_SIZE 4096U

static void sl_bench_fill_bytes(unsigned char* buffer, size_t length)
{
    size_t index = 0U;

    for (index = 0U; index < length; index += 1U) {
        buffer[index] = (unsigned char)((index * 31U + 17U) & 0xffU);
    }
    if (length > 64U) {
        buffer[63U] = (unsigned char)',';
    }
    if (length > 1024U) {
        buffer[1023U] = (unsigned char)'\n';
    }
}

static SlStatus bench_memory_byte_find_any(const SlBenchContext* context, uint64_t iterations,
                                           uint64_t* out_checksum)
{
    unsigned char buffer[SL_BENCH_MEMORY_BUFFER_SIZE];
    const unsigned char needles[] = {(unsigned char)',', (unsigned char)'\n', 0U};
    SlBytes bytes;
    SlBytes needle_bytes = sl_bytes_from_parts(needles, sizeof(needles));
    uint64_t checksum = 0U;
    uint64_t iteration = 0U;
    SlBytesFindResult result = {0};

    (void)context;
    sl_bench_fill_bytes(buffer, sizeof(buffer));
    bytes = sl_bytes_from_parts(buffer, sizeof(buffer));

    for (iteration = 0U; iteration < iterations; iteration += 1U) {
        SlStatus status = sl_bytes_find_any(bytes, needle_bytes, &result);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        checksum += result.found ? (uint64_t)(result.index + result.value) : 1U;
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_memory_string_ci_equal(const SlBenchContext* context, uint64_t iterations,
                                             uint64_t* out_checksum)
{
    static const char left[] = "Content-Type";
    static const char right[] = "content-type";
    SlStr left_str = sl_str_from_parts(left, sizeof(left) - 1U);
    SlStr right_str = sl_str_from_parts(right, sizeof(right) - 1U);
    uint64_t checksum = 0U;
    uint64_t iteration = 0U;

    (void)context;

    for (iteration = 0U; iteration < iterations; iteration += 1U) {
        checksum += sl_str_equal_ci_ascii(left_str, right_str) ? 1U : 0U;
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_memory_builder_append_growth(const SlBenchContext* context,
                                                   uint64_t iterations, uint64_t* out_checksum)
{
    unsigned char arena_storage[SL_BENCH_MEMORY_BUFFER_SIZE];
    unsigned char payload[128];
    SlArena arena;
    uint64_t checksum = 0U;
    uint64_t iteration = 0U;

    (void)context;
    sl_bench_fill_bytes(payload, sizeof(payload));

    for (iteration = 0U; iteration < iterations; iteration += 1U) {
        SlByteBuilder builder;
        SlByteBuilderStats stats;
        SlStatus status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_byte_builder_init_arena(&builder, &arena, 0U, sizeof(arena_storage));
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status =
            sl_byte_builder_append_bytes(&builder, sl_bytes_from_parts(payload, sizeof(payload)));
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status =
            sl_byte_builder_append_bytes(&builder, sl_bytes_from_parts(payload, sizeof(payload)));
        if (!sl_status_is_ok(status)) {
            return status;
        }
        stats = sl_byte_builder_stats(&builder);
        checksum += (uint64_t)(stats.length + stats.grow_count + stats.copied_bytes);
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_memory_checked_array_size(const SlBenchContext* context, uint64_t iterations,
                                                uint64_t* out_checksum)
{
    uint64_t checksum = 0U;
    uint64_t iteration = 0U;

    (void)context;

    for (iteration = 0U; iteration < iterations; iteration += 1U) {
        size_t bytes = 0U;
        SlStatus status =
            sl_checked_array_size((size_t)((iteration % 1024U) + 1U), sizeof(uint64_t), &bytes);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        checksum += (uint64_t)bytes;
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static const SlBenchDefinition memory_benchmarks[] = {
    {"memory.bytes.find_any", "memory", "scalar byte find-any over a fixed binary buffer", 10000U,
     1000000U, bench_memory_byte_find_any, "microbenchmark only; not a parser throughput claim",
     false},
    {"memory.string.equal_ci_ascii", "memory", "ASCII case-insensitive string comparison", 10000U,
     1000000U, bench_memory_string_ci_equal, "microbenchmark only; not a protocol claim", false},
    {"memory.builder.append_growth", "memory", "arena builder append and growth counters", 1000U,
     100000U, bench_memory_builder_append_growth,
     "reports counter checksum only; not an allocation-rate claim", false},
    {"memory.checked.array_size", "memory", "checked size calculation for array allocation", 10000U,
     1000000U, bench_memory_checked_array_size,
     "microbenchmark only; not an allocator throughput claim", false},
};

const SlBenchDefinition* sl_bench_memory_definitions(size_t* out_count)
{
    *out_count = sizeof(memory_benchmarks) / sizeof(memory_benchmarks[0]);
    return memory_benchmarks;
}
