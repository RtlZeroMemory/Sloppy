/*
 * src/core/stream.c
 *
 * Implements Sloppy's platform-neutral stream primitive. Streams are caller-owned,
 * bounded, and synchronous at this layer; transports/schedulers decide when to retry
 * after BACKPRESSURE.
 */
#include "sloppy/stream.h"

#include "sloppy/checked_math.h"

#include <string.h>

static bool sl_stream_bytes_valid(SlBytes bytes)
{
    return bytes.length == 0U || bytes.ptr != NULL;
}

static bool sl_stream_str_valid(SlStr text)
{
    return text.length == 0U || text.ptr != NULL;
}

static SlStreamOptions sl_stream_normalize_options(const SlStreamOptions* options)
{
    SlStreamOptions normalized = options == NULL ? sl_stream_default_options() : *options;

    if (normalized.max_chunk_bytes == 0U) {
        normalized.max_chunk_bytes = SL_STREAM_DEFAULT_MAX_CHUNK_BYTES;
    }
    if (normalized.max_buffered_bytes == 0U) {
        normalized.max_buffered_bytes = SL_STREAM_DEFAULT_MAX_BUFFERED_BYTES;
    }
    if (normalized.max_chunks == 0U) {
        normalized.max_chunks = SL_STREAM_DEFAULT_MAX_CHUNKS;
    }
    return normalized;
}

static SlStreamStatus sl_stream_status_from_cancellation(const SlCancellationToken* cancellation)
{
    SlCancellationReason reason;

    if (!sl_cancellation_token_is_cancelled(cancellation)) {
        return SL_STREAM_STATUS_OK;
    }

    reason = sl_cancellation_token_reason(cancellation);
    if (reason == SL_CANCELLATION_REASON_DEADLINE_EXCEEDED) {
        return SL_STREAM_STATUS_DEADLINE_EXCEEDED;
    }
    if (reason == SL_CANCELLATION_REASON_BACKPRESSURE) {
        return SL_STREAM_STATUS_BACKPRESSURE;
    }
    return SL_STREAM_STATUS_CANCELLED;
}

static void sl_readable_stream_set_status(SlReadableStream* stream, SlStreamStatus status)
{
    if (stream == NULL) {
        return;
    }
    stream->stats.last_status = status;
    if (status == SL_STREAM_STATUS_EOF) {
        stream->state = SL_STREAM_STATE_ENDED;
    }
    else if (status == SL_STREAM_STATUS_CANCELLED ||
             status == SL_STREAM_STATUS_DEADLINE_EXCEEDED)
    {
        stream->state = SL_STREAM_STATE_CANCELLED;
    }
    stream->stats.state = stream->state;
}

static void sl_writable_stream_set_status(SlWritableStream* stream, SlStreamStatus status)
{
    if (stream == NULL) {
        return;
    }
    stream->stats.last_status = status;
    if (status == SL_STREAM_STATUS_CANCELLED ||
        status == SL_STREAM_STATUS_DEADLINE_EXCEEDED)
    {
        stream->state = SL_STREAM_STATE_CANCELLED;
    }
    stream->stats.state = stream->state;
}

static SlStreamStatus sl_stream_state_write_status(SlStreamState state)
{
    switch (state) {
    case SL_STREAM_STATE_OPEN:
        return SL_STREAM_STATUS_OK;
    case SL_STREAM_STATE_ENDED:
    case SL_STREAM_STATE_CLOSED:
        return SL_STREAM_STATUS_CLOSED;
    case SL_STREAM_STATE_FAILED:
        return SL_STREAM_STATUS_FAILED;
    case SL_STREAM_STATE_ABORTED:
        return SL_STREAM_STATUS_ABORTED;
    case SL_STREAM_STATE_CANCELLED:
        return SL_STREAM_STATUS_CANCELLED;
    case SL_STREAM_STATE_NONE:
    default:
        return SL_STREAM_STATUS_INVALID_STATE;
    }
}

static SlStreamStatus sl_stream_state_read_status(SlStreamState state)
{
    switch (state) {
    case SL_STREAM_STATE_OPEN:
        return SL_STREAM_STATUS_OK;
    case SL_STREAM_STATE_ENDED:
        return SL_STREAM_STATUS_EOF;
    case SL_STREAM_STATE_CLOSED:
        return SL_STREAM_STATUS_CLOSED;
    case SL_STREAM_STATE_FAILED:
        return SL_STREAM_STATUS_FAILED;
    case SL_STREAM_STATE_ABORTED:
        return SL_STREAM_STATUS_ABORTED;
    case SL_STREAM_STATE_CANCELLED:
        return SL_STREAM_STATUS_CANCELLED;
    case SL_STREAM_STATE_NONE:
    default:
        return SL_STREAM_STATUS_INVALID_STATE;
    }
}

static SlStreamBackpressureState sl_stream_empty_backpressure(void)
{
    SlStreamBackpressureState state = {0};

    state.writable = true;
    return state;
}

static SlStreamStatus sl_stream_control_noop(void* user, SlStr message)
{
    (void)user;
    (void)message;
    return SL_STREAM_STATUS_OK;
}

SlStreamOptions sl_stream_default_options(void)
{
    SlStreamOptions options = {0};

    options.max_chunk_bytes = SL_STREAM_DEFAULT_MAX_CHUNK_BYTES;
    options.max_buffered_bytes = SL_STREAM_DEFAULT_MAX_BUFFERED_BYTES;
    options.max_chunks = SL_STREAM_DEFAULT_MAX_CHUNKS;
    options.cancellation = NULL;
    return options;
}

SlStr sl_stream_state_name(SlStreamState state)
{
    switch (state) {
    case SL_STREAM_STATE_OPEN:
        return sl_str_from_cstr("open");
    case SL_STREAM_STATE_ENDED:
        return sl_str_from_cstr("ended");
    case SL_STREAM_STATE_CLOSED:
        return sl_str_from_cstr("closed");
    case SL_STREAM_STATE_FAILED:
        return sl_str_from_cstr("failed");
    case SL_STREAM_STATE_ABORTED:
        return sl_str_from_cstr("aborted");
    case SL_STREAM_STATE_CANCELLED:
        return sl_str_from_cstr("cancelled");
    case SL_STREAM_STATE_NONE:
    default:
        return sl_str_from_cstr("none");
    }
}

SlStr sl_stream_status_name(SlStreamStatus status)
{
    switch (status) {
    case SL_STREAM_STATUS_OK:
        return sl_str_from_cstr("ok");
    case SL_STREAM_STATUS_EOF:
        return sl_str_from_cstr("eof");
    case SL_STREAM_STATUS_WOULD_BLOCK:
        return sl_str_from_cstr("would_block");
    case SL_STREAM_STATUS_BACKPRESSURE:
        return sl_str_from_cstr("backpressure");
    case SL_STREAM_STATUS_CAPACITY_EXCEEDED:
        return sl_str_from_cstr("capacity_exceeded");
    case SL_STREAM_STATUS_CLOSED:
        return sl_str_from_cstr("closed");
    case SL_STREAM_STATUS_FAILED:
        return sl_str_from_cstr("failed");
    case SL_STREAM_STATUS_ABORTED:
        return sl_str_from_cstr("aborted");
    case SL_STREAM_STATUS_CANCELLED:
        return sl_str_from_cstr("cancelled");
    case SL_STREAM_STATUS_DEADLINE_EXCEEDED:
        return sl_str_from_cstr("deadline_exceeded");
    case SL_STREAM_STATUS_INVALID_ARGUMENT:
        return sl_str_from_cstr("invalid_argument");
    case SL_STREAM_STATUS_INVALID_STATE:
        return sl_str_from_cstr("invalid_state");
    case SL_STREAM_STATUS_UNSUPPORTED:
        return sl_str_from_cstr("unsupported");
    default:
        return sl_str_from_cstr("unknown");
    }
}

SlStatus sl_stream_status_to_status(SlStreamStatus status)
{
    switch (status) {
    case SL_STREAM_STATUS_OK:
    case SL_STREAM_STATUS_EOF:
        return sl_status_ok();
    case SL_STREAM_STATUS_BACKPRESSURE:
    case SL_STREAM_STATUS_CAPACITY_EXCEEDED:
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    case SL_STREAM_STATUS_CANCELLED:
        return sl_status_from_code(SL_STATUS_CANCELLED);
    case SL_STREAM_STATUS_DEADLINE_EXCEEDED:
        return sl_status_from_code(SL_STATUS_DEADLINE_EXCEEDED);
    case SL_STREAM_STATUS_INVALID_ARGUMENT:
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    case SL_STREAM_STATUS_UNSUPPORTED:
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    case SL_STREAM_STATUS_CLOSED:
    case SL_STREAM_STATUS_FAILED:
    case SL_STREAM_STATUS_ABORTED:
    case SL_STREAM_STATUS_INVALID_STATE:
    case SL_STREAM_STATUS_WOULD_BLOCK:
    default:
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
}

SlStatus sl_readable_stream_init(SlReadableStream* stream,
                                 const SlReadableStreamVTable* vtable, void* user,
                                 const SlStreamOptions* options)
{
    SlStreamOptions normalized;

    if (stream == NULL || vtable == NULL || vtable->read == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    normalized = sl_stream_normalize_options(options);
    *stream = (SlReadableStream){0};
    stream->vtable = vtable;
    stream->user = user;
    stream->cancellation = normalized.cancellation;
    stream->state = SL_STREAM_STATE_OPEN;
    stream->max_chunk_bytes = normalized.max_chunk_bytes;
    stream->stats.state = SL_STREAM_STATE_OPEN;
    stream->stats.last_status = SL_STREAM_STATUS_OK;
    stream->error.status = sl_status_ok();
    return sl_status_ok();
}

SlStreamStatus sl_readable_stream_read(SlReadableStream* stream, SlStreamReadResult* out)
{
    SlStreamStatus status;

    if (out != NULL) {
        *out = (SlStreamReadResult){0};
    }
    if (stream == NULL || out == NULL || stream->vtable == NULL || stream->vtable->read == NULL) {
        return SL_STREAM_STATUS_INVALID_ARGUMENT;
    }

    status = sl_stream_state_read_status(stream->state);
    if (status != SL_STREAM_STATUS_OK) {
        out->status = status;
        sl_readable_stream_set_status(stream, status);
        return status;
    }

    status = sl_stream_status_from_cancellation(stream->cancellation);
    if (status != SL_STREAM_STATUS_OK) {
        out->status = status;
        sl_readable_stream_set_status(stream, status);
        return status;
    }

    status = stream->vtable->read(stream, out);
    out->status = status;
    if (status == SL_STREAM_STATUS_OK) {
        if (!sl_stream_bytes_valid(out->chunk.bytes) ||
            out->chunk.bytes.length > stream->max_chunk_bytes)
        {
            status = SL_STREAM_STATUS_INVALID_STATE;
            out->status = status;
        }
        else if (out->chunk.bytes.length != 0U) {
            stream->stats.bytes_read += out->chunk.bytes.length;
            stream->stats.chunks_read += 1U;
        }
    }

    sl_readable_stream_set_status(stream, status);
    return status;
}

SlStreamStatus sl_readable_stream_close(SlReadableStream* stream, SlStr message)
{
    SlStreamStatus status = SL_STREAM_STATUS_OK;

    if (stream == NULL || !sl_stream_str_valid(message)) {
        return SL_STREAM_STATUS_INVALID_ARGUMENT;
    }
    if (stream->state == SL_STREAM_STATE_CLOSED || stream->state == SL_STREAM_STATE_ENDED) {
        return SL_STREAM_STATUS_OK;
    }
    if (stream->state != SL_STREAM_STATE_OPEN) {
        return SL_STREAM_STATUS_INVALID_STATE;
    }
    if (stream->vtable != NULL && stream->vtable->close != NULL) {
        status = stream->vtable->close(stream->user, message);
    }
    if (status == SL_STREAM_STATUS_OK) {
        stream->state = SL_STREAM_STATE_CLOSED;
    }
    sl_readable_stream_set_status(stream, status);
    return status;
}

SlStreamStatus sl_readable_stream_fail(SlReadableStream* stream, SlStreamStatus code, SlStr message)
{
    SlStreamStatus status = SL_STREAM_STATUS_OK;

    if (stream == NULL || code == SL_STREAM_STATUS_OK || code == SL_STREAM_STATUS_EOF ||
        !sl_stream_str_valid(message))
    {
        return SL_STREAM_STATUS_INVALID_ARGUMENT;
    }
    if (stream->state != SL_STREAM_STATE_OPEN) {
        return SL_STREAM_STATUS_INVALID_STATE;
    }
    if (stream->vtable != NULL && stream->vtable->fail != NULL) {
        status = stream->vtable->fail(stream->user, message);
    }
    if (status == SL_STREAM_STATUS_OK) {
        stream->state = SL_STREAM_STATE_FAILED;
        stream->error.code = code;
        stream->error.status = sl_stream_status_to_status(code);
        stream->error.message = message;
        sl_readable_stream_set_status(stream, code);
        return code;
    }
    sl_readable_stream_set_status(stream, status);
    return status;
}

SlStreamStatus sl_readable_stream_abort(SlReadableStream* stream, SlStr message)
{
    SlStreamStatus status = SL_STREAM_STATUS_OK;

    if (stream == NULL || !sl_stream_str_valid(message)) {
        return SL_STREAM_STATUS_INVALID_ARGUMENT;
    }
    if (stream->state == SL_STREAM_STATE_ABORTED) {
        return SL_STREAM_STATUS_OK;
    }
    if (stream->state != SL_STREAM_STATE_OPEN) {
        return SL_STREAM_STATUS_INVALID_STATE;
    }
    if (stream->vtable != NULL && stream->vtable->abort != NULL) {
        status = stream->vtable->abort(stream->user, message);
    }
    if (status == SL_STREAM_STATUS_OK) {
        stream->state = SL_STREAM_STATE_ABORTED;
        sl_readable_stream_set_status(stream, SL_STREAM_STATUS_ABORTED);
        return SL_STREAM_STATUS_ABORTED;
    }
    sl_readable_stream_set_status(stream, status);
    return status;
}

SlStreamStats sl_readable_stream_stats(const SlReadableStream* stream)
{
    if (stream == NULL) {
        return (SlStreamStats){0};
    }
    return stream->stats;
}

SlStatus sl_writable_stream_init(SlWritableStream* stream,
                                 const SlWritableStreamVTable* vtable, void* user,
                                 const SlStreamOptions* options)
{
    SlStreamOptions normalized;

    if (stream == NULL || vtable == NULL || vtable->write == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    normalized = sl_stream_normalize_options(options);
    *stream = (SlWritableStream){0};
    stream->vtable = vtable;
    stream->user = user;
    stream->cancellation = normalized.cancellation;
    stream->state = SL_STREAM_STATE_OPEN;
    stream->max_chunk_bytes = normalized.max_chunk_bytes;
    stream->max_buffered_bytes = normalized.max_buffered_bytes;
    stream->stats.state = SL_STREAM_STATE_OPEN;
    stream->stats.last_status = SL_STREAM_STATUS_OK;
    stream->error.status = sl_status_ok();
    return sl_status_ok();
}

SlStreamStatus sl_writable_stream_write(SlWritableStream* stream, SlStreamChunk chunk,
                                        SlStreamWriteResult* out)
{
    SlStreamStatus status;

    if (out != NULL) {
        *out = (SlStreamWriteResult){0};
    }
    if (stream == NULL || out == NULL || stream->vtable == NULL ||
        stream->vtable->write == NULL)
    {
        return SL_STREAM_STATUS_INVALID_ARGUMENT;
    }
    if (!sl_stream_bytes_valid(chunk.bytes)) {
        out->status = SL_STREAM_STATUS_INVALID_ARGUMENT;
        return SL_STREAM_STATUS_INVALID_ARGUMENT;
    }
    if (chunk.bytes.length > stream->max_chunk_bytes) {
        out->status = SL_STREAM_STATUS_CAPACITY_EXCEEDED;
        return SL_STREAM_STATUS_CAPACITY_EXCEEDED;
    }

    status = sl_stream_state_write_status(stream->state);
    if (status != SL_STREAM_STATUS_OK) {
        out->status = status;
        sl_writable_stream_set_status(stream, status);
        return status;
    }

    status = sl_stream_status_from_cancellation(stream->cancellation);
    if (status != SL_STREAM_STATUS_OK) {
        out->status = status;
        sl_writable_stream_set_status(stream, status);
        return status;
    }

    status = stream->vtable->write(stream, chunk, out);
    out->status = status;
    if (status == SL_STREAM_STATUS_OK) {
        stream->stats.bytes_written += out->bytes_written;
        if (out->bytes_written != 0U) {
            stream->stats.chunks_written += 1U;
        }
    }
    else if (status == SL_STREAM_STATUS_BACKPRESSURE ||
             status == SL_STREAM_STATUS_CAPACITY_EXCEEDED)
    {
        stream->stats.backpressure_hits += 1U;
    }
    out->backpressure = sl_writable_stream_backpressure(stream);
    sl_writable_stream_set_status(stream, status);
    return status;
}

SlStreamStatus sl_writable_stream_drain(SlWritableStream* stream, size_t bytes)
{
    SlStreamStatus status;

    if (stream == NULL) {
        return SL_STREAM_STATUS_INVALID_ARGUMENT;
    }
    if (stream->vtable == NULL || stream->vtable->drain == NULL) {
        return bytes == 0U ? SL_STREAM_STATUS_OK : SL_STREAM_STATUS_UNSUPPORTED;
    }
    status = stream->vtable->drain(stream, bytes);
    if (status == SL_STREAM_STATUS_OK) {
        stream->stats.drain_count += 1U;
        stream->stats.buffered_bytes = sl_writable_stream_backpressure(stream).buffered_bytes;
    }
    sl_writable_stream_set_status(stream, status);
    return status;
}

SlStreamStatus sl_writable_stream_close(SlWritableStream* stream, SlStr message)
{
    SlStreamStatus status = SL_STREAM_STATUS_OK;

    if (stream == NULL || !sl_stream_str_valid(message)) {
        return SL_STREAM_STATUS_INVALID_ARGUMENT;
    }
    if (stream->state == SL_STREAM_STATE_CLOSED) {
        return SL_STREAM_STATUS_OK;
    }
    if (stream->state != SL_STREAM_STATE_OPEN) {
        return SL_STREAM_STATUS_INVALID_STATE;
    }
    if (stream->vtable != NULL && stream->vtable->close != NULL) {
        status = stream->vtable->close(stream->user, message);
    }
    if (status == SL_STREAM_STATUS_OK) {
        stream->state = SL_STREAM_STATE_CLOSED;
    }
    sl_writable_stream_set_status(stream, status);
    return status;
}

SlStreamStatus sl_writable_stream_fail(SlWritableStream* stream, SlStreamStatus code,
                                       SlStr message)
{
    SlStreamStatus status = SL_STREAM_STATUS_OK;

    if (stream == NULL || code == SL_STREAM_STATUS_OK || code == SL_STREAM_STATUS_EOF ||
        !sl_stream_str_valid(message))
    {
        return SL_STREAM_STATUS_INVALID_ARGUMENT;
    }
    if (stream->state != SL_STREAM_STATE_OPEN) {
        return SL_STREAM_STATUS_INVALID_STATE;
    }
    if (stream->vtable != NULL && stream->vtable->fail != NULL) {
        status = stream->vtable->fail(stream->user, message);
    }
    if (status == SL_STREAM_STATUS_OK) {
        stream->state = SL_STREAM_STATE_FAILED;
        stream->error.code = code;
        stream->error.status = sl_stream_status_to_status(code);
        stream->error.message = message;
        sl_writable_stream_set_status(stream, code);
        return code;
    }
    sl_writable_stream_set_status(stream, status);
    return status;
}

SlStreamStatus sl_writable_stream_abort(SlWritableStream* stream, SlStr message)
{
    SlStreamStatus status = SL_STREAM_STATUS_OK;

    if (stream == NULL || !sl_stream_str_valid(message)) {
        return SL_STREAM_STATUS_INVALID_ARGUMENT;
    }
    if (stream->state == SL_STREAM_STATE_ABORTED) {
        return SL_STREAM_STATUS_OK;
    }
    if (stream->state != SL_STREAM_STATE_OPEN) {
        return SL_STREAM_STATUS_INVALID_STATE;
    }
    if (stream->vtable != NULL && stream->vtable->abort != NULL) {
        status = stream->vtable->abort(stream->user, message);
    }
    if (status == SL_STREAM_STATUS_OK) {
        stream->state = SL_STREAM_STATE_ABORTED;
        sl_writable_stream_set_status(stream, SL_STREAM_STATUS_ABORTED);
        return SL_STREAM_STATUS_ABORTED;
    }
    sl_writable_stream_set_status(stream, status);
    return status;
}

SlStreamBackpressureState sl_writable_stream_backpressure(const SlWritableStream* stream)
{
    SlStreamBackpressureState state;

    if (stream == NULL) {
        return (SlStreamBackpressureState){0};
    }
    if (stream->vtable != NULL && stream->vtable->backpressure != NULL) {
        state = stream->vtable->backpressure(stream);
    }
    else {
        state = sl_stream_empty_backpressure();
    }
    state.backpressure_hits = stream->stats.backpressure_hits;
    return state;
}

SlStreamStats sl_writable_stream_stats(const SlWritableStream* stream)
{
    SlStreamStats stats;

    if (stream == NULL) {
        return (SlStreamStats){0};
    }
    stats = stream->stats;
    stats.buffered_bytes = sl_writable_stream_backpressure(stream).buffered_bytes;
    return stats;
}

SlStatus sl_stream_pump_init(SlStreamPump* pump, SlReadableStream* readable,
                             SlWritableStream* writable, const SlCancellationToken* cancellation)
{
    if (pump == NULL || readable == NULL || writable == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *pump = (SlStreamPump){0};
    pump->readable = readable;
    pump->writable = writable;
    pump->cancellation = cancellation;
    pump->last_status = SL_STREAM_STATUS_OK;
    return sl_status_ok();
}

static void sl_stream_pump_snapshot(const SlStreamPump* pump, SlStreamStatus status,
                                    SlStreamPumpResult* out)
{
    if (out == NULL) {
        return;
    }
    *out = (SlStreamPumpResult){0};
    out->status = status;
    if (pump != NULL) {
        out->bytes_transferred = pump->bytes_transferred;
        out->chunks_transferred = pump->chunks_transferred;
        out->backpressure = sl_writable_stream_backpressure(pump->writable);
    }
}

SlStreamStatus sl_stream_pump_step(SlStreamPump* pump, SlStreamPumpResult* out)
{
    SlStreamReadResult read = {0};
    SlStreamWriteResult write = {0};
    SlStreamChunk chunk = {0};
    SlStreamStatus status;

    if (pump == NULL || pump->readable == NULL || pump->writable == NULL || out == NULL) {
        return SL_STREAM_STATUS_INVALID_ARGUMENT;
    }

    status = sl_stream_status_from_cancellation(pump->cancellation);
    if (status != SL_STREAM_STATUS_OK) {
        pump->last_status = status;
        sl_stream_pump_snapshot(pump, status, out);
        return status;
    }

    if (pump->has_pending_chunk) {
        chunk = pump->pending_chunk;
    }
    else {
        status = sl_readable_stream_read(pump->readable, &read);
        if (status != SL_STREAM_STATUS_OK) {
            pump->last_status = status;
            sl_stream_pump_snapshot(pump, status, out);
            return status;
        }
        if (read.chunk.bytes.length == 0U) {
            pump->last_status = SL_STREAM_STATUS_WOULD_BLOCK;
            sl_stream_pump_snapshot(pump, SL_STREAM_STATUS_WOULD_BLOCK, out);
            return SL_STREAM_STATUS_WOULD_BLOCK;
        }
        chunk = read.chunk;
        pump->pending_chunk = chunk;
        pump->has_pending_chunk = true;
    }

    status = sl_writable_stream_write(pump->writable, chunk, &write);
    if (status != SL_STREAM_STATUS_OK) {
        pump->last_status = status;
        sl_stream_pump_snapshot(pump, status, out);
        return status;
    }
    pump->has_pending_chunk = false;
    pump->pending_chunk = (SlStreamChunk){0};
    pump->bytes_transferred += write.bytes_written;
    if (write.bytes_written != 0U) {
        pump->chunks_transferred += 1U;
    }
    pump->last_status = SL_STREAM_STATUS_OK;
    sl_stream_pump_snapshot(pump, SL_STREAM_STATUS_OK, out);
    return SL_STREAM_STATUS_OK;
}

SlStreamStatus sl_stream_pump_run(SlStreamPump* pump, size_t max_steps, SlStreamPumpResult* out)
{
    SlStreamStatus status = SL_STREAM_STATUS_OK;
    size_t step = 0U;

    if (pump == NULL || out == NULL || max_steps == 0U) {
        return SL_STREAM_STATUS_INVALID_ARGUMENT;
    }
    for (step = 0U; step < max_steps; step += 1U) {
        status = sl_stream_pump_step(pump, out);
        if (status != SL_STREAM_STATUS_OK) {
            return status;
        }
    }
    sl_stream_pump_snapshot(pump, SL_STREAM_STATUS_OK, out);
    return SL_STREAM_STATUS_OK;
}

static SlStreamStatus sl_chunk_read(const SlStreamChunk* chunks, size_t chunk_count,
                                    size_t* chunk_index, size_t* chunk_offset,
                                    size_t max_chunk_bytes, SlStreamReadResult* out)
{
    while (*chunk_index < chunk_count) {
        const SlStreamChunk* source = &chunks[*chunk_index];
        size_t remaining;
        size_t length;

        if (!sl_stream_bytes_valid(source->bytes)) {
            return SL_STREAM_STATUS_INVALID_STATE;
        }
        if (*chunk_offset >= source->bytes.length) {
            *chunk_index += 1U;
            *chunk_offset = 0U;
            continue;
        }
        remaining = source->bytes.length - *chunk_offset;
        length = remaining > max_chunk_bytes ? max_chunk_bytes : remaining;
        out->chunk.bytes =
            sl_bytes_from_parts(source->bytes.ptr + *chunk_offset, length);
        *chunk_offset += length;
        if (*chunk_offset >= source->bytes.length) {
            *chunk_index += 1U;
            *chunk_offset = 0U;
        }
        return SL_STREAM_STATUS_OK;
    }
    return SL_STREAM_STATUS_EOF;
}

static SlStreamStatus sl_memory_readable_read(SlReadableStream* stream, SlStreamReadResult* out)
{
    SlMemoryReadableStream* adapter = (SlMemoryReadableStream*)stream->user;

    if (adapter == NULL) {
        return SL_STREAM_STATUS_INVALID_ARGUMENT;
    }
    return sl_chunk_read(adapter->chunks, adapter->chunk_count, &adapter->chunk_index,
                         &adapter->chunk_offset, stream->max_chunk_bytes, out);
}

static const SlReadableStreamVTable sl_memory_readable_vtable = {
    sl_memory_readable_read, sl_stream_control_noop, sl_stream_control_noop,
    sl_stream_control_noop, "memory-readable"};

SlStatus sl_memory_readable_stream_init(SlMemoryReadableStream* adapter,
                                        const SlStreamChunk* chunks, size_t chunk_count,
                                        const SlStreamOptions* options,
                                        SlReadableStream* out_stream)
{
    if (adapter == NULL || out_stream == NULL || (chunks == NULL && chunk_count != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *adapter = (SlMemoryReadableStream){0};
    adapter->chunks = chunks;
    adapter->chunk_count = chunk_count;
    return sl_readable_stream_init(out_stream, &sl_memory_readable_vtable, adapter, options);
}

static SlStreamBackpressureState sl_memory_writable_backpressure(const SlWritableStream* stream)
{
    const SlMemoryWritableStream* adapter = (const SlMemoryWritableStream*)stream->user;
    SlStreamBackpressureState state = {0};

    if (adapter == NULL) {
        return state;
    }
    state.buffered_bytes = adapter->length;
    state.high_water_mark = adapter->high_water_mark;
    state.available_bytes = adapter->capacity > adapter->length ? adapter->capacity - adapter->length
                                                                : 0U;
    state.writable = state.available_bytes != 0U && stream->state == SL_STREAM_STATE_OPEN;
    return state;
}

static SlStreamStatus sl_memory_writable_write(SlWritableStream* stream, SlStreamChunk chunk,
                                               SlStreamWriteResult* out)
{
    SlMemoryWritableStream* adapter = (SlMemoryWritableStream*)stream->user;
    size_t next_length = 0U;
    SlStatus status;

    if (adapter == NULL) {
        return SL_STREAM_STATUS_INVALID_ARGUMENT;
    }
    if (chunk.bytes.length == 0U) {
        out->bytes_written = 0U;
        return SL_STREAM_STATUS_OK;
    }
    if (chunk.bytes.length > stream->max_buffered_bytes ||
        adapter->length > stream->max_buffered_bytes)
    {
        return SL_STREAM_STATUS_CAPACITY_EXCEEDED;
    }
    status = sl_checked_add_size(adapter->length, chunk.bytes.length, &next_length);
    if (!sl_status_is_ok(status)) {
        return SL_STREAM_STATUS_CAPACITY_EXCEEDED;
    }
    if (next_length > adapter->capacity || next_length > stream->max_buffered_bytes) {
        return SL_STREAM_STATUS_BACKPRESSURE;
    }

    memmove(adapter->buffer + adapter->length, chunk.bytes.ptr, chunk.bytes.length);
    adapter->length = next_length;
    if (adapter->length > adapter->high_water_mark) {
        adapter->high_water_mark = adapter->length;
    }
    stream->stats.buffered_bytes = adapter->length;
    if (adapter->high_water_mark > stream->stats.high_water_mark) {
        stream->stats.high_water_mark = adapter->high_water_mark;
    }
    out->bytes_written = chunk.bytes.length;
    return SL_STREAM_STATUS_OK;
}

static SlStreamStatus sl_memory_writable_drain(SlWritableStream* stream, size_t bytes)
{
    SlMemoryWritableStream* adapter = (SlMemoryWritableStream*)stream->user;

    if (adapter == NULL) {
        return SL_STREAM_STATUS_INVALID_ARGUMENT;
    }
    if (bytes > adapter->length) {
        return SL_STREAM_STATUS_INVALID_ARGUMENT;
    }
    if (bytes != 0U) {
        memmove(adapter->buffer, adapter->buffer + bytes, adapter->length - bytes);
        adapter->length -= bytes;
    }
    stream->stats.buffered_bytes = adapter->length;
    return SL_STREAM_STATUS_OK;
}

static const SlWritableStreamVTable sl_memory_writable_vtable = {
    sl_memory_writable_write,
    sl_memory_writable_drain,
    sl_memory_writable_backpressure,
    sl_stream_control_noop,
    sl_stream_control_noop,
    sl_stream_control_noop,
    "memory-writable"};

SlStatus sl_memory_writable_stream_init(SlMemoryWritableStream* adapter, unsigned char* buffer,
                                        size_t capacity, const SlStreamOptions* options,
                                        SlWritableStream* out_stream)
{
    SlStatus status;

    if (adapter == NULL || out_stream == NULL || (buffer == NULL && capacity != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *adapter = (SlMemoryWritableStream){0};
    adapter->buffer = buffer;
    adapter->capacity = capacity;
    status = sl_writable_stream_init(out_stream, &sl_memory_writable_vtable, adapter, options);
    if (!sl_status_is_ok(status)) {
        *adapter = (SlMemoryWritableStream){0};
    }
    return status;
}

SlBytes sl_memory_writable_stream_view(const SlMemoryWritableStream* adapter)
{
    if (adapter == NULL || adapter->length == 0U) {
        return sl_bytes_empty();
    }
    return sl_bytes_from_parts(adapter->buffer, adapter->length);
}

void sl_memory_writable_stream_reset(SlMemoryWritableStream* adapter)
{
    if (adapter == NULL) {
        return;
    }
    adapter->length = 0U;
}

static SlStreamStatus sl_chunk_list_readable_read(SlReadableStream* stream,
                                                  SlStreamReadResult* out)
{
    SlChunkListReadableStream* adapter = (SlChunkListReadableStream*)stream->user;

    if (adapter == NULL) {
        return SL_STREAM_STATUS_INVALID_ARGUMENT;
    }
    return sl_chunk_read(adapter->chunks, adapter->chunk_count, &adapter->chunk_index,
                         &adapter->chunk_offset, stream->max_chunk_bytes, out);
}

static const SlReadableStreamVTable sl_chunk_list_readable_vtable = {
    sl_chunk_list_readable_read, sl_stream_control_noop, sl_stream_control_noop,
    sl_stream_control_noop, "chunk-list-readable"};

SlStatus sl_chunk_list_readable_stream_init(SlChunkListReadableStream* adapter,
                                            const SlStreamChunk* chunks, size_t chunk_count,
                                            const SlStreamOptions* options,
                                            SlReadableStream* out_stream)
{
    if (adapter == NULL || out_stream == NULL || (chunks == NULL && chunk_count != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *adapter = (SlChunkListReadableStream){0};
    adapter->chunks = chunks;
    adapter->chunk_count = chunk_count;
    return sl_readable_stream_init(out_stream, &sl_chunk_list_readable_vtable, adapter, options);
}

static SlStreamBackpressureState sl_chunk_list_writable_backpressure(const SlWritableStream* stream)
{
    const SlChunkListWritableStream* adapter = (const SlChunkListWritableStream*)stream->user;
    SlStreamBackpressureState state = {0};

    if (adapter == NULL) {
        return state;
    }
    state.buffered_bytes = adapter->total_bytes;
    state.high_water_mark = adapter->total_bytes;
    state.available_bytes =
        adapter->max_total_bytes > adapter->total_bytes ? adapter->max_total_bytes - adapter->total_bytes
                                                        : 0U;
    state.writable = adapter->count < adapter->capacity && state.available_bytes != 0U &&
                     stream->state == SL_STREAM_STATE_OPEN;
    return state;
}

static SlStreamStatus sl_chunk_list_writable_write(SlWritableStream* stream,
                                                   SlStreamChunk chunk,
                                                   SlStreamWriteResult* out)
{
    SlChunkListWritableStream* adapter = (SlChunkListWritableStream*)stream->user;
    size_t next_total = 0U;
    SlStatus status;

    if (adapter == NULL) {
        return SL_STREAM_STATUS_INVALID_ARGUMENT;
    }
    if (chunk.bytes.length == 0U) {
        out->bytes_written = 0U;
        return SL_STREAM_STATUS_OK;
    }
    if (adapter->count >= adapter->capacity) {
        return SL_STREAM_STATUS_BACKPRESSURE;
    }
    status = sl_checked_add_size(adapter->total_bytes, chunk.bytes.length, &next_total);
    if (!sl_status_is_ok(status) || next_total > adapter->max_total_bytes) {
        return SL_STREAM_STATUS_BACKPRESSURE;
    }
    adapter->chunks[adapter->count] = chunk;
    adapter->count += 1U;
    adapter->total_bytes = next_total;
    stream->stats.buffered_bytes = adapter->total_bytes;
    if (adapter->total_bytes > stream->stats.high_water_mark) {
        stream->stats.high_water_mark = adapter->total_bytes;
    }
    out->bytes_written = chunk.bytes.length;
    return SL_STREAM_STATUS_OK;
}

static const SlWritableStreamVTable sl_chunk_list_writable_vtable = {
    sl_chunk_list_writable_write,
    NULL,
    sl_chunk_list_writable_backpressure,
    sl_stream_control_noop,
    sl_stream_control_noop,
    sl_stream_control_noop,
    "chunk-list-writable"};

SlStatus sl_chunk_list_writable_stream_init(SlChunkListWritableStream* adapter,
                                            SlStreamChunk* chunks, size_t capacity,
                                            const SlStreamOptions* options,
                                            SlWritableStream* out_stream)
{
    SlStreamOptions normalized;
    SlStatus status;

    if (adapter == NULL || out_stream == NULL || (chunks == NULL && capacity != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    normalized = sl_stream_normalize_options(options);
    if (capacity > normalized.max_chunks) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }
    *adapter = (SlChunkListWritableStream){0};
    adapter->chunks = chunks;
    adapter->capacity = capacity;
    adapter->max_total_bytes = normalized.max_buffered_bytes;
    status = sl_writable_stream_init(out_stream, &sl_chunk_list_writable_vtable, adapter, &normalized);
    if (!sl_status_is_ok(status)) {
        *adapter = (SlChunkListWritableStream){0};
    }
    return status;
}

SlStatus sl_chunk_list_writable_stream_as_readable(const SlChunkListWritableStream* writable,
                                                   SlChunkListReadableStream* adapter,
                                                   const SlStreamOptions* options,
                                                   SlReadableStream* out_stream)
{
    if (writable == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_chunk_list_readable_stream_init(adapter, writable->chunks, writable->count, options,
                                             out_stream);
}
