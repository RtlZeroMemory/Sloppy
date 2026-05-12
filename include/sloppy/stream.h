#ifndef SLOPPY_STREAM_H
#define SLOPPY_STREAM_H

#include "sloppy/bytes.h"
#include "sloppy/cancellation.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_STREAM_DEFAULT_MAX_CHUNK_BYTES 65536U
#define SL_STREAM_DEFAULT_MAX_BUFFERED_BYTES 131072U
#define SL_STREAM_DEFAULT_MAX_CHUNKS 1024U

typedef enum SlStreamState
{
    SL_STREAM_STATE_NONE = 0,
    SL_STREAM_STATE_OPEN = 1,
    SL_STREAM_STATE_ENDED = 2,
    SL_STREAM_STATE_CLOSED = 3,
    SL_STREAM_STATE_FAILED = 4,
    SL_STREAM_STATE_ABORTED = 5,
    SL_STREAM_STATE_CANCELLED = 6
} SlStreamState;

typedef enum SlStreamStatus
{
    SL_STREAM_STATUS_OK = 0,
    SL_STREAM_STATUS_EOF = 1,
    SL_STREAM_STATUS_WOULD_BLOCK = 2,
    SL_STREAM_STATUS_BACKPRESSURE = 3,
    SL_STREAM_STATUS_CAPACITY_EXCEEDED = 4,
    SL_STREAM_STATUS_CLOSED = 5,
    SL_STREAM_STATUS_FAILED = 6,
    SL_STREAM_STATUS_ABORTED = 7,
    SL_STREAM_STATUS_CANCELLED = 8,
    SL_STREAM_STATUS_DEADLINE_EXCEEDED = 9,
    SL_STREAM_STATUS_INVALID_ARGUMENT = 10,
    SL_STREAM_STATUS_INVALID_STATE = 11,
    SL_STREAM_STATUS_UNSUPPORTED = 12
} SlStreamStatus;

/*
 * Stream chunks are borrowed byte views. Producers own the bytes until the receiving
 * operation returns, unless a specific adapter documents a longer borrowed lifetime.
 */
typedef struct SlStreamChunk
{
    SlBytes bytes;
} SlStreamChunk;

typedef struct SlStreamOptions
{
    size_t max_chunk_bytes;
    size_t max_buffered_bytes;
    size_t max_chunks;
    const SlCancellationToken* cancellation;
} SlStreamOptions;

typedef struct SlStreamError
{
    SlStreamStatus code;
    SlStatus status;
    SlStr message;
} SlStreamError;

typedef struct SlStreamBackpressureState
{
    bool writable;
    size_t buffered_bytes;
    size_t available_bytes;
    size_t high_water_mark;
    uint64_t backpressure_hits;
} SlStreamBackpressureState;

typedef struct SlStreamStats
{
    uint64_t bytes_read;
    uint64_t bytes_written;
    uint64_t chunks_read;
    uint64_t chunks_written;
    uint64_t backpressure_hits;
    uint64_t drain_count;
    size_t buffered_bytes;
    size_t high_water_mark;
    SlStreamState state;
    SlStreamStatus last_status;
} SlStreamStats;

typedef struct SlStreamReadResult
{
    SlStreamStatus status;
    SlStreamChunk chunk;
} SlStreamReadResult;

typedef struct SlStreamWriteResult
{
    SlStreamStatus status;
    size_t bytes_written;
    SlStreamBackpressureState backpressure;
} SlStreamWriteResult;

typedef struct SlReadableStream SlReadableStream;
typedef struct SlWritableStream SlWritableStream;

typedef SlStreamStatus (*SlReadableStreamReadFn)(SlReadableStream* stream,
                                                 SlStreamReadResult* out);
typedef SlStreamStatus (*SlStreamControlFn)(void* user, SlStr message);

typedef struct SlReadableStreamVTable
{
    SlReadableStreamReadFn read;
    SlStreamControlFn close;
    SlStreamControlFn fail;
    SlStreamControlFn abort;
    const char* name;
} SlReadableStreamVTable;

typedef SlStreamStatus (*SlWritableStreamWriteFn)(SlWritableStream* stream, SlStreamChunk chunk,
                                                  SlStreamWriteResult* out);
typedef SlStreamStatus (*SlWritableStreamDrainFn)(SlWritableStream* stream, size_t bytes);
typedef SlStreamBackpressureState (*SlWritableStreamBackpressureFn)(const SlWritableStream* stream);

typedef struct SlWritableStreamVTable
{
    SlWritableStreamWriteFn write;
    SlWritableStreamDrainFn drain;
    SlWritableStreamBackpressureFn backpressure;
    SlStreamControlFn close;
    SlStreamControlFn fail;
    SlStreamControlFn abort;
    const char* name;
} SlWritableStreamVTable;

struct SlReadableStream
{
    const SlReadableStreamVTable* vtable;
    void* user;
    const SlCancellationToken* cancellation;
    SlStreamState state;
    SlStreamStats stats;
    SlStreamError error;
    size_t max_chunk_bytes;
};

struct SlWritableStream
{
    const SlWritableStreamVTable* vtable;
    void* user;
    const SlCancellationToken* cancellation;
    SlStreamState state;
    SlStreamStats stats;
    SlStreamError error;
    size_t max_chunk_bytes;
    size_t max_buffered_bytes;
};

typedef struct SlDuplexStream
{
    SlReadableStream* readable;
    SlWritableStream* writable;
} SlDuplexStream;

typedef struct SlStreamPump
{
    SlReadableStream* readable;
    SlWritableStream* writable;
    const SlCancellationToken* cancellation;
    bool has_pending_chunk;
    SlStreamChunk pending_chunk;
    SlStreamStatus last_status;
    uint64_t bytes_transferred;
    uint64_t chunks_transferred;
} SlStreamPump;

typedef struct SlStreamPumpResult
{
    SlStreamStatus status;
    uint64_t bytes_transferred;
    uint64_t chunks_transferred;
    SlStreamBackpressureState backpressure;
} SlStreamPumpResult;

typedef struct SlMemoryReadableStream
{
    const SlStreamChunk* chunks;
    size_t chunk_count;
    size_t chunk_index;
    size_t chunk_offset;
} SlMemoryReadableStream;

typedef struct SlMemoryWritableStream
{
    unsigned char* buffer;
    size_t capacity;
    size_t length;
    size_t high_water_mark;
} SlMemoryWritableStream;

typedef struct SlChunkListReadableStream
{
    const SlStreamChunk* chunks;
    size_t chunk_count;
    size_t chunk_index;
    size_t chunk_offset;
} SlChunkListReadableStream;

typedef struct SlChunkListWritableStream
{
    SlStreamChunk* chunks;
    size_t capacity;
    size_t count;
    size_t total_bytes;
    size_t max_total_bytes;
} SlChunkListWritableStream;

SlStreamOptions sl_stream_default_options(void);
SlStr sl_stream_state_name(SlStreamState state);
SlStr sl_stream_status_name(SlStreamStatus status);
SlStatus sl_stream_status_to_status(SlStreamStatus status);

SlStatus sl_readable_stream_init(SlReadableStream* stream,
                                 const SlReadableStreamVTable* vtable, void* user,
                                 const SlStreamOptions* options);
SlStreamStatus sl_readable_stream_read(SlReadableStream* stream, SlStreamReadResult* out);
SlStreamStatus sl_readable_stream_close(SlReadableStream* stream, SlStr message);
SlStreamStatus sl_readable_stream_fail(SlReadableStream* stream, SlStreamStatus code,
                                       SlStr message);
SlStreamStatus sl_readable_stream_abort(SlReadableStream* stream, SlStr message);
SlStreamStats sl_readable_stream_stats(const SlReadableStream* stream);

SlStatus sl_writable_stream_init(SlWritableStream* stream,
                                 const SlWritableStreamVTable* vtable, void* user,
                                 const SlStreamOptions* options);
SlStreamStatus sl_writable_stream_write(SlWritableStream* stream, SlStreamChunk chunk,
                                        SlStreamWriteResult* out);
SlStreamStatus sl_writable_stream_drain(SlWritableStream* stream, size_t bytes);
SlStreamStatus sl_writable_stream_close(SlWritableStream* stream, SlStr message);
SlStreamStatus sl_writable_stream_fail(SlWritableStream* stream, SlStreamStatus code,
                                       SlStr message);
SlStreamStatus sl_writable_stream_abort(SlWritableStream* stream, SlStr message);
SlStreamBackpressureState sl_writable_stream_backpressure(const SlWritableStream* stream);
SlStreamStats sl_writable_stream_stats(const SlWritableStream* stream);

SlStatus sl_stream_pump_init(SlStreamPump* pump, SlReadableStream* readable,
                             SlWritableStream* writable, const SlCancellationToken* cancellation);
SlStreamStatus sl_stream_pump_step(SlStreamPump* pump, SlStreamPumpResult* out);
SlStreamStatus sl_stream_pump_run(SlStreamPump* pump, size_t max_steps,
                                  SlStreamPumpResult* out);

SlStatus sl_memory_readable_stream_init(SlMemoryReadableStream* adapter,
                                        const SlStreamChunk* chunks, size_t chunk_count,
                                        const SlStreamOptions* options,
                                        SlReadableStream* out_stream);
SlStatus sl_memory_writable_stream_init(SlMemoryWritableStream* adapter, unsigned char* buffer,
                                        size_t capacity, const SlStreamOptions* options,
                                        SlWritableStream* out_stream);
SlBytes sl_memory_writable_stream_view(const SlMemoryWritableStream* adapter);
void sl_memory_writable_stream_reset(SlMemoryWritableStream* adapter);

SlStatus sl_chunk_list_readable_stream_init(SlChunkListReadableStream* adapter,
                                            const SlStreamChunk* chunks, size_t chunk_count,
                                            const SlStreamOptions* options,
                                            SlReadableStream* out_stream);
SlStatus sl_chunk_list_writable_stream_init(SlChunkListWritableStream* adapter,
                                            SlStreamChunk* chunks, size_t capacity,
                                            const SlStreamOptions* options,
                                            SlWritableStream* out_stream);
SlStatus sl_chunk_list_writable_stream_as_readable(const SlChunkListWritableStream* writable,
                                                   SlChunkListReadableStream* adapter,
                                                   const SlStreamOptions* options,
                                                   SlReadableStream* out_stream);

#ifdef __cplusplus
}
#endif

#endif
