#include "bench_internal.h"

#include "sloppy/http_backend.h"
#include "sloppy/http_response.h"
#include "sloppy/stream.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STREAM_BENCH_SMALL_BYTES ((size_t)1024U)
#define STREAM_BENCH_LARGE_BYTES ((size_t)16384U)
#define STREAM_BENCH_TINY_CHUNKS ((size_t)256U)
#define STREAM_BENCH_TINY_BYTES ((size_t)16U)
#define STREAM_BENCH_SSE_BYTES                                                                     \
    ((sizeof("event: ready\ndata: {\"ok\":true}\n\n") - 1U) + (sizeof(": heartbeat\n\n") - 1U))

static void bench_stream_fill(unsigned char* buffer, size_t length, unsigned seed)
{
    size_t index = 0U;

    for (index = 0U; index < length; index += 1U) {
        buffer[index] = (unsigned char)((index * 17U + seed) & 0xffU);
    }
}

static uint64_t bench_stream_checksum(SlBytes bytes)
{
    uint64_t checksum = (uint64_t)bytes.length;

    if (bytes.length != 0U) {
        checksum += bytes.ptr[0];
        checksum += bytes.ptr[bytes.length - 1U];
    }
    return checksum;
}

static SlStatus bench_stream_pump_once(const SlStreamChunk* chunks, size_t chunk_count,
                                       unsigned char* output, size_t output_capacity,
                                       uint64_t* out_checksum)
{
    SlMemoryReadableStream readable_adapter = {0};
    SlMemoryWritableStream writable_adapter = {0};
    SlReadableStream readable = {0};
    SlWritableStream writable = {0};
    SlStreamPump pump = {0};
    SlStreamPumpResult result = {0};
    SlStatus status;

    status =
        sl_memory_readable_stream_init(&readable_adapter, chunks, chunk_count, NULL, &readable);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status =
        sl_memory_writable_stream_init(&writable_adapter, output, output_capacity, NULL, &writable);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_stream_pump_init(&pump, &readable, &writable, NULL);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (sl_stream_pump_run(&pump, chunk_count + 1U, &result) != SL_STREAM_STATUS_EOF) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    *out_checksum += bench_stream_checksum(sl_memory_writable_stream_view(&writable_adapter));
    return sl_status_ok();
}

static SlStatus bench_stream_memory_to_memory(const SlBenchContext* context, uint64_t iterations,
                                              uint64_t* out_checksum)
{
    static unsigned char input[STREAM_BENCH_SMALL_BYTES * 4U];
    static unsigned char output[STREAM_BENCH_SMALL_BYTES * 4U];
    SlStreamChunk chunks[4];
    uint64_t checksum = 0U;
    uint64_t iteration = 0U;

    (void)context;
    bench_stream_fill(input, sizeof(input), 11U);
    chunks[0].bytes = sl_bytes_from_parts(input, STREAM_BENCH_SMALL_BYTES);
    chunks[1].bytes =
        sl_bytes_from_parts(input + STREAM_BENCH_SMALL_BYTES, STREAM_BENCH_SMALL_BYTES);
    chunks[2].bytes =
        sl_bytes_from_parts(input + STREAM_BENCH_SMALL_BYTES * 2U, STREAM_BENCH_SMALL_BYTES);
    chunks[3].bytes =
        sl_bytes_from_parts(input + STREAM_BENCH_SMALL_BYTES * 3U, STREAM_BENCH_SMALL_BYTES);

    for (iteration = 0U; iteration < iterations; iteration += 1U) {
        SlStatus status = bench_stream_pump_once(chunks, 4U, output, sizeof(output), &checksum);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_stream_many_tiny_chunks(const SlBenchContext* context, uint64_t iterations,
                                              uint64_t* out_checksum)
{
    static unsigned char input[STREAM_BENCH_TINY_CHUNKS * STREAM_BENCH_TINY_BYTES];
    static unsigned char output[STREAM_BENCH_TINY_CHUNKS * STREAM_BENCH_TINY_BYTES];
    SlStreamChunk chunks[STREAM_BENCH_TINY_CHUNKS];
    uint64_t checksum = 0U;
    uint64_t iteration = 0U;
    size_t index = 0U;

    (void)context;
    bench_stream_fill(input, sizeof(input), 23U);
    for (index = 0U; index < STREAM_BENCH_TINY_CHUNKS; index += 1U) {
        chunks[index].bytes =
            sl_bytes_from_parts(input + (index * STREAM_BENCH_TINY_BYTES), STREAM_BENCH_TINY_BYTES);
    }
    for (iteration = 0U; iteration < iterations; iteration += 1U) {
        SlStatus status = bench_stream_pump_once(chunks, STREAM_BENCH_TINY_CHUNKS, output,
                                                 sizeof(output), &checksum);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_stream_few_large_chunks(const SlBenchContext* context, uint64_t iterations,
                                              uint64_t* out_checksum)
{
    static unsigned char input[STREAM_BENCH_LARGE_BYTES * 4U];
    static unsigned char output[STREAM_BENCH_LARGE_BYTES * 4U];
    SlStreamChunk chunks[4];
    uint64_t checksum = 0U;
    uint64_t iteration = 0U;

    (void)context;
    bench_stream_fill(input, sizeof(input), 37U);
    chunks[0].bytes = sl_bytes_from_parts(input, STREAM_BENCH_LARGE_BYTES);
    chunks[1].bytes =
        sl_bytes_from_parts(input + STREAM_BENCH_LARGE_BYTES, STREAM_BENCH_LARGE_BYTES);
    chunks[2].bytes =
        sl_bytes_from_parts(input + STREAM_BENCH_LARGE_BYTES * 2U, STREAM_BENCH_LARGE_BYTES);
    chunks[3].bytes =
        sl_bytes_from_parts(input + STREAM_BENCH_LARGE_BYTES * 3U, STREAM_BENCH_LARGE_BYTES);

    for (iteration = 0U; iteration < iterations; iteration += 1U) {
        SlStatus status = bench_stream_pump_once(chunks, 4U, output, sizeof(output), &checksum);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_stream_backpressure_resume(const SlBenchContext* context, uint64_t iterations,
                                                 uint64_t* out_checksum)
{
    static unsigned char input[STREAM_BENCH_SMALL_BYTES * 4U];
    unsigned char output[STREAM_BENCH_SMALL_BYTES];
    SlStreamChunk chunks[4];
    uint64_t checksum = 0U;
    uint64_t iteration = 0U;

    (void)context;
    bench_stream_fill(input, sizeof(input), 41U);
    chunks[0].bytes = sl_bytes_from_parts(input, STREAM_BENCH_SMALL_BYTES);
    chunks[1].bytes =
        sl_bytes_from_parts(input + STREAM_BENCH_SMALL_BYTES, STREAM_BENCH_SMALL_BYTES);
    chunks[2].bytes =
        sl_bytes_from_parts(input + STREAM_BENCH_SMALL_BYTES * 2U, STREAM_BENCH_SMALL_BYTES);
    chunks[3].bytes =
        sl_bytes_from_parts(input + STREAM_BENCH_SMALL_BYTES * 3U, STREAM_BENCH_SMALL_BYTES);

    for (iteration = 0U; iteration < iterations; iteration += 1U) {
        SlMemoryReadableStream readable_adapter = {0};
        SlMemoryWritableStream writable_adapter = {0};
        SlReadableStream readable = {0};
        SlWritableStream writable = {0};
        SlStreamPump pump = {0};
        SlStreamPumpResult result = {0};
        SlStatus status;
        size_t step = 0U;

        status = sl_memory_readable_stream_init(&readable_adapter, chunks, 4U, NULL, &readable);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_memory_writable_stream_init(&writable_adapter, output, sizeof(output), NULL,
                                                &writable);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_stream_pump_init(&pump, &readable, &writable, NULL);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        bool reached_eof = false;
        for (step = 0U; step < 16U; step += 1U) {
            SlStreamStatus stream_status = sl_stream_pump_step(&pump, &result);
            if (stream_status == SL_STREAM_STATUS_BACKPRESSURE) {
                checksum +=
                    bench_stream_checksum(sl_memory_writable_stream_view(&writable_adapter));
                status = sl_stream_status_to_status(sl_writable_stream_drain(
                    &writable, sl_memory_writable_stream_view(&writable_adapter).length));
                if (!sl_status_is_ok(status)) {
                    return status;
                }
                continue;
            }
            if (stream_status == SL_STREAM_STATUS_EOF) {
                reached_eof = true;
                break;
            }
            if (stream_status != SL_STREAM_STATUS_OK) {
                return sl_status_from_code(SL_STATUS_INTERNAL);
            }
        }
        if (!reached_eof) {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        checksum += pump.bytes_transferred;
    }
    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_stream_http_response_chunks(const SlBenchContext* context,
                                                  uint64_t iterations, uint64_t* out_checksum)
{
    static unsigned char input[STREAM_BENCH_SMALL_BYTES * 2U];
    static unsigned char output[STREAM_BENCH_SMALL_BYTES * 2U];
    SlStreamChunk chunks[2];
    SlHttpResponse response;
    uint64_t checksum = 0U;
    uint64_t iteration = 0U;

    (void)context;
    bench_stream_fill(input, sizeof(input), 53U);
    chunks[0].bytes = sl_bytes_from_parts(input, STREAM_BENCH_SMALL_BYTES);
    chunks[1].bytes =
        sl_bytes_from_parts(input + STREAM_BENCH_SMALL_BYTES, STREAM_BENCH_SMALL_BYTES);
    response =
        sl_http_response_stream(200U, sl_str_from_cstr("application/octet-stream"), chunks, 2U);

    for (iteration = 0U; iteration < iterations; iteration += 1U) {
        SlHttpResponseStreamReadable readable_adapter = {0};
        SlMemoryWritableStream writable_adapter = {0};
        SlReadableStream readable = {0};
        SlWritableStream writable = {0};
        SlStreamPump pump = {0};
        SlStreamPumpResult result = {0};
        SlStatus status;

        status =
            sl_http_response_stream_readable_init(&readable_adapter, &response, NULL, &readable);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_memory_writable_stream_init(&writable_adapter, output, sizeof(output), NULL,
                                                &writable);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_stream_pump_init(&pump, &readable, &writable, NULL);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (sl_stream_pump_run(&pump, 3U, &result) != SL_STREAM_STATUS_EOF) {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        checksum += bench_stream_checksum(sl_memory_writable_stream_view(&writable_adapter));
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_stream_http_response_serialization(const SlBenchContext* context,
                                                         uint64_t iterations,
                                                         uint64_t* out_checksum)
{
    static unsigned char input[STREAM_BENCH_SMALL_BYTES * 2U];
    unsigned char output[(STREAM_BENCH_SMALL_BYTES * 2U) + 256U];
    SlStreamChunk chunks[2];
    SlHttpResponse response;
    uint64_t checksum = 0U;
    uint64_t iteration = 0U;

    (void)context;
    bench_stream_fill(input, sizeof(input), 61U);
    chunks[0].bytes = sl_bytes_from_parts(input, STREAM_BENCH_SMALL_BYTES);
    chunks[1].bytes =
        sl_bytes_from_parts(input + STREAM_BENCH_SMALL_BYTES, STREAM_BENCH_SMALL_BYTES);
    response =
        sl_http_response_stream(200U, sl_str_from_cstr("application/octet-stream"), chunks, 2U);

    for (iteration = 0U; iteration < iterations; iteration += 1U) {
        SlBytes bytes = {0};
        SlStatus status = sl_http_response_write(&response, output, sizeof(output), &bytes);

        if (!sl_status_is_ok(status)) {
            return status;
        }
        checksum += bench_stream_checksum(bytes);
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_stream_sse_response_descriptor(const SlBenchContext* context,
                                                     uint64_t iterations, uint64_t* out_checksum)
{
    static const unsigned char first[] = "event: ready\ndata: {\"ok\":true}\n\n";
    static const unsigned char second[] = ": heartbeat\n\n";
    unsigned char output[512];
    SlStreamChunk chunks[2];
    SlHttpResponse response;
    uint64_t checksum = 0U;
    uint64_t iteration = 0U;

    (void)context;
    chunks[0].bytes = sl_bytes_from_parts(first, sizeof(first) - 1U);
    chunks[1].bytes = sl_bytes_from_parts(second, sizeof(second) - 1U);
    response = sl_http_response_stream(200U, sl_str_from_cstr("text/event-stream"), chunks, 2U);

    for (iteration = 0U; iteration < iterations; iteration += 1U) {
        SlBytes bytes = {0};
        SlStatus status = sl_http_response_write(&response, output, sizeof(output), &bytes);

        if (!sl_status_is_ok(status)) {
            return status;
        }
        checksum += bench_stream_checksum(bytes);
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_stream_request_body_readable(const SlBenchContext* context,
                                                   uint64_t iterations, uint64_t* out_checksum)
{
    static unsigned char input[STREAM_BENCH_SMALL_BYTES * 4U];
    static unsigned char output[STREAM_BENCH_SMALL_BYTES * 4U];
    SlHttpRequestLifecycle request = {0};
    uint64_t checksum = 0U;
    uint64_t iteration = 0U;

    (void)context;
    bench_stream_fill(input, sizeof(input), 71U);
    sl_cancellation_token_init(&request.cancellation);
    request.head.body = sl_bytes_from_parts(input, sizeof(input));

    for (iteration = 0U; iteration < iterations; iteration += 1U) {
        SlHttpRequestBodyReadable body_adapter = {0};
        SlMemoryWritableStream writable_adapter = {0};
        SlReadableStream readable = {0};
        SlWritableStream writable = {0};
        SlStreamPump pump = {0};
        SlStreamPumpResult result = {0};
        SlStatus status;

        status = sl_http_request_body_readable_init(&body_adapter, &request, NULL, &readable);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_memory_writable_stream_init(&writable_adapter, output, sizeof(output), NULL,
                                                &writable);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_stream_pump_init(&pump, &readable, &writable, NULL);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (sl_stream_pump_run(&pump, 5U, &result) != SL_STREAM_STATUS_EOF) {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        checksum += bench_stream_checksum(sl_memory_writable_stream_view(&writable_adapter));
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static const SlBenchDefinition stream_benchmarks[] = {
    {"stream.pump.memory_to_memory", "stream", "pump bounded memory chunks into memory sink", 1000U,
     100000U, bench_stream_memory_to_memory,
     "core stream pump only; transport scheduling is not included", false, 4096U, 4U, 0U},
    {"stream.pump.many_tiny_chunks", "stream", "pump many small chunks through the core stream",
     100U, 10000U, bench_stream_many_tiny_chunks,
     "core chunk overhead only; not a socket throughput claim", false, 4096U, 256U, 0U},
    {"stream.pump.few_large_chunks", "stream", "pump a few larger chunks through the core stream",
     100U, 10000U, bench_stream_few_large_chunks,
     "core memory copy only; not a network throughput claim", false, 65536U, 4U, 0U},
    {"stream.pump.backpressure_resume", "stream", "pump with bounded sink backpressure and resume",
     100U, 10000U, bench_stream_backpressure_resume,
     "deterministic partial-write drain simulation; no socket readiness is implied", false, 4096U,
     4U, 3U},
    {"stream.http.response_chunks", "stream",
     "copy bounded SlHttpResponse stream chunks through the core stream adapter", 1000U, 100000U,
     bench_stream_http_response_chunks,
     "adapter path for bounded response stream descriptors, not full HTTP serialization", false,
     2048U, 2U, 0U},
    {"stream.http.response_serialization", "stream",
     "serialize a bounded SlHttpResponse stream through the core response writer", 1000U, 100000U,
     bench_stream_http_response_serialization,
     "core HTTP/1.1 serialization path with computed Content-Length; not socket I/O", false, 2048U,
     2U, 0U},
    {"stream.sse.response_descriptor", "stream",
     "serialize bounded SSE frames through Results.stream-compatible response descriptors", 1000U,
     100000U, bench_stream_sse_response_descriptor,
     "SSE descriptor path only; live browser push/backpressure is not included", false,
     STREAM_BENCH_SSE_BYTES, 2U, 0U},
    {"stream.http.request_body_readable", "stream",
     "adapt a bounded HTTP request body to a Core readable stream and pump it", 1000U, 100000U,
     bench_stream_request_body_readable,
     "request body bytes are already bounded in request storage; no JSON parsing included", false,
     4096U, 1U, 0U},
};

const SlBenchDefinition* sl_bench_stream_definitions(size_t* out_count)
{
    *out_count = sizeof(stream_benchmarks) / sizeof(stream_benchmarks[0]);
    return stream_benchmarks;
}
