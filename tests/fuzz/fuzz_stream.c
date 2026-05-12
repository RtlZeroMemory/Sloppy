#include "sloppy/stream.h"

#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>

#define FUZZ_STREAM_MAX_CHUNKS 8U
#define FUZZ_STREAM_OUTPUT_BYTES 64U

static size_t fuzz_bounded_size(uint8_t value, size_t max)
{
    size_t bounded = (size_t)value;

    return bounded > max ? max : bounded;
}

static int fuzz_status_ok(SlStatus status)
{
    return sl_status_is_ok(status) ? 1 : 0;
}

static int exercise_memory_pump(const uint8_t* data, size_t size)
{
    SlStreamChunk chunks[FUZZ_STREAM_MAX_CHUNKS];
    unsigned char output[FUZZ_STREAM_OUTPUT_BYTES];
    SlMemoryReadableStream readable_adapter;
    SlMemoryWritableStream writable_adapter;
    SlReadableStream readable;
    SlWritableStream writable;
    SlStreamPump pump;
    SlStreamPumpResult result;
    SlStreamOptions options = sl_stream_default_options();
    size_t chunk_count = 0U;
    size_t cursor = size > 3U ? 3U : size;
    size_t index = 0U;
    size_t output_capacity = size > 1U ? fuzz_bounded_size(data[1], sizeof(output)) : 8U;

    options.max_chunk_bytes = size > 0U ? fuzz_bounded_size(data[0], 16U) + 1U : 4U;
    options.max_buffered_bytes = output_capacity == 0U ? 1U : output_capacity;
    for (index = 0U; index < FUZZ_STREAM_MAX_CHUNKS && cursor < size; index += 1U) {
        size_t remaining = size - cursor;
        size_t length = fuzz_bounded_size(data[cursor], 16U);

        cursor += 1U;
        if (length > remaining) {
            length = remaining;
        }
        chunks[chunk_count].bytes = sl_bytes_from_parts(data + cursor, length);
        chunk_count += 1U;
        cursor += length;
    }

    if (!fuzz_status_ok(sl_memory_readable_stream_init(&readable_adapter, chunks, chunk_count,
                                                       &options, &readable)) ||
        !fuzz_status_ok(sl_memory_writable_stream_init(&writable_adapter, output,
                                                       output_capacity, &options, &writable)) ||
        !fuzz_status_ok(sl_stream_pump_init(&pump, &readable, &writable, NULL)))
    {
        return 1;
    }

    for (index = 0U; index < 64U; index += 1U) {
        SlStreamStatus status = sl_stream_pump_step(&pump, &result);

        if (writable_adapter.length > writable_adapter.capacity ||
            sl_memory_writable_stream_view(&writable_adapter).length > writable_adapter.capacity)
        {
            return 1;
        }
        if (status == SL_STREAM_STATUS_BACKPRESSURE) {
            size_t drain = writable_adapter.length == 0U ? 0U : 1U;
            if (sl_writable_stream_drain(&writable, drain) != SL_STREAM_STATUS_OK) {
                return 1;
            }
            continue;
        }
        if (status == SL_STREAM_STATUS_EOF) {
            break;
        }
        if (status != SL_STREAM_STATUS_OK && status != SL_STREAM_STATUS_WOULD_BLOCK) {
            break;
        }
    }

    return writable_adapter.length <= writable_adapter.capacity ? 0 : 1;
}

static int exercise_chunk_list(const uint8_t* data, size_t size)
{
    SlStreamChunk chunk_storage[FUZZ_STREAM_MAX_CHUNKS];
    SlChunkListWritableStream writable_adapter;
    SlChunkListReadableStream readable_adapter;
    SlWritableStream writable;
    SlReadableStream readable;
    SlStreamWriteResult write;
    SlStreamReadResult read;
    SlStreamOptions options = sl_stream_default_options();
    size_t cursor = 0U;
    size_t index = 0U;

    options.max_chunks = FUZZ_STREAM_MAX_CHUNKS;
    options.max_buffered_bytes = size > 0U ? fuzz_bounded_size(data[0], 64U) : 16U;
    options.max_chunk_bytes = 16U;

    if (!fuzz_status_ok(sl_chunk_list_writable_stream_init(
            &writable_adapter, chunk_storage, FUZZ_STREAM_MAX_CHUNKS, &options, &writable)))
    {
        return 1;
    }

    while (cursor < size && index < 64U) {
        size_t remaining = size - cursor;
        size_t length = fuzz_bounded_size(data[cursor], 16U);
        SlStreamStatus status;

        cursor += 1U;
        if (length > remaining) {
            length = remaining;
        }
        status = sl_writable_stream_write(
            &writable, (SlStreamChunk){sl_bytes_from_parts(data + cursor, length)}, &write);
        if (writable_adapter.count > writable_adapter.capacity ||
            writable_adapter.total_bytes > writable_adapter.max_total_bytes)
        {
            return 1;
        }
        cursor += length;
        index += 1U;
        if (status == SL_STREAM_STATUS_BACKPRESSURE ||
            status == SL_STREAM_STATUS_CAPACITY_EXCEEDED)
        {
            break;
        }
        if (status != SL_STREAM_STATUS_OK) {
            break;
        }
    }

    if (!fuzz_status_ok(sl_chunk_list_writable_stream_as_readable(
            &writable_adapter, &readable_adapter, &options, &readable)))
    {
        return 1;
    }

    for (index = 0U; index < FUZZ_STREAM_MAX_CHUNKS + 1U; index += 1U) {
        SlStreamStatus status = sl_readable_stream_read(&readable, &read);

        if (status == SL_STREAM_STATUS_EOF) {
            break;
        }
        if (status != SL_STREAM_STATUS_OK || read.chunk.bytes.length > options.max_chunk_bytes) {
            return 1;
        }
    }

    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (data == NULL) {
        return 0;
    }
    if (exercise_memory_pump(data, size) != 0 || exercise_chunk_list(data, size) != 0) {
        return 1;
    }
    return 0;
}
