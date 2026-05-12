#include "sloppy/stream.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static SlBytes bytes_from_cstr(const char* text)
{
    return sl_bytes_from_parts((const unsigned char*)text, strlen(text));
}

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int expect_stream_status(SlStreamStatus status, SlStreamStatus expected)
{
    return expect_true(status == expected);
}

static int expect_bytes_equal(SlBytes actual, const char* expected)
{
    size_t length = strlen(expected);

    return expect_true(
        actual.length == length &&
        (length == 0U || memcmp(actual.ptr, (const unsigned char*)expected, length) == 0));
}

static int read_expected(SlReadableStream* stream, const char* expected)
{
    SlStreamReadResult result = {0};

    if (expect_stream_status(sl_readable_stream_read(stream, &result), SL_STREAM_STATUS_OK) != 0) {
        return 1;
    }
    return expect_bytes_equal(result.chunk.bytes, expected);
}

static SlStreamStatus test_empty_read(SlReadableStream* stream, SlStreamReadResult* out)
{
    (void)stream;
    (void)out;
    return SL_STREAM_STATUS_EOF;
}

static SlStreamStatus test_accept_write(SlWritableStream* stream, SlStreamChunk chunk,
                                        SlStreamWriteResult* out)
{
    (void)stream;
    out->bytes_written = chunk.bytes.length;
    return SL_STREAM_STATUS_OK;
}

typedef struct TestPartialWritable
{
    unsigned char* buffer;
    size_t capacity;
    size_t length;
    size_t max_per_write;
    SlStreamStatus partial_status;
} TestPartialWritable;

static SlStreamBackpressureState test_partial_backpressure(const SlWritableStream* stream)
{
    const TestPartialWritable* adapter = (const TestPartialWritable*)stream->user;
    SlStreamBackpressureState state = {0};

    if (adapter == NULL) {
        return state;
    }
    state.buffered_bytes = adapter->length;
    state.high_water_mark = adapter->length;
    state.available_bytes =
        adapter->capacity > adapter->length ? adapter->capacity - adapter->length : 0U;
    state.writable = state.available_bytes != 0U && stream->state == SL_STREAM_STATE_OPEN;
    return state;
}

static SlStreamStatus test_partial_write(SlWritableStream* stream, SlStreamChunk chunk,
                                         SlStreamWriteResult* out)
{
    TestPartialWritable* adapter = (TestPartialWritable*)stream->user;
    size_t available = 0U;
    size_t write_length = 0U;

    if (adapter == NULL) {
        return SL_STREAM_STATUS_INVALID_ARGUMENT;
    }
    if (chunk.bytes.length == 0U) {
        out->bytes_written = 0U;
        return SL_STREAM_STATUS_OK;
    }
    available = adapter->capacity > adapter->length ? adapter->capacity - adapter->length : 0U;
    if (available == 0U) {
        return SL_STREAM_STATUS_BACKPRESSURE;
    }
    write_length = chunk.bytes.length;
    if (write_length > available) {
        write_length = available;
    }
    if (write_length > adapter->max_per_write) {
        write_length = adapter->max_per_write;
    }
    memmove(adapter->buffer + adapter->length, chunk.bytes.ptr, write_length);
    adapter->length += write_length;
    out->bytes_written = write_length;
    return write_length == chunk.bytes.length ? SL_STREAM_STATUS_OK : adapter->partial_status;
}

static const SlWritableStreamVTable test_partial_writable_vtable = {
    test_partial_write, NULL, test_partial_backpressure, NULL, NULL, NULL, "partial-writable"};

typedef struct TestInvalidWritable
{
    SlStreamStatus status;
    size_t bytes_written;
} TestInvalidWritable;

static SlStreamStatus test_invalid_write(SlWritableStream* stream, SlStreamChunk chunk,
                                         SlStreamWriteResult* out)
{
    TestInvalidWritable* adapter = (TestInvalidWritable*)stream->user;

    (void)chunk;
    if (adapter == NULL) {
        return SL_STREAM_STATUS_INVALID_ARGUMENT;
    }
    out->bytes_written = adapter->bytes_written;
    return adapter->status;
}

static const SlWritableStreamVTable test_invalid_writable_vtable = {
    test_invalid_write, NULL, NULL, NULL, NULL, NULL, "invalid-writable"};

static SlStreamStatus test_reject_control(void* user, SlStr message)
{
    int* calls = (int*)user;

    (void)message;
    if (calls != NULL) {
        *calls += 1;
    }
    return SL_STREAM_STATUS_UNSUPPORTED;
}

static int test_memory_readable_splits_chunks_and_reports_eof(void)
{
    SlStreamChunk chunks[2] = {{bytes_from_cstr("hello")}, {bytes_from_cstr("world")}};
    SlMemoryReadableStream adapter = {0};
    SlReadableStream stream = {0};
    SlStreamReadResult result = {0};
    SlStreamOptions options = sl_stream_default_options();
    SlStreamStats stats = {0};

    options.max_chunk_bytes = 3U;
    if (expect_status(sl_memory_readable_stream_init(&adapter, chunks, 2U, &options, &stream),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (read_expected(&stream, "hel") != 0 || read_expected(&stream, "lo") != 0 ||
        read_expected(&stream, "wor") != 0 || read_expected(&stream, "ld") != 0)
    {
        return 2;
    }
    if (expect_stream_status(sl_readable_stream_read(&stream, &result), SL_STREAM_STATUS_EOF) != 0)
    {
        return 3;
    }
    stats = sl_readable_stream_stats(&stream);
    if (stats.bytes_read != 10U || stats.chunks_read != 4U || stats.state != SL_STREAM_STATE_ENDED)
    {
        return 4;
    }
    return expect_stream_status(sl_readable_stream_close(&stream, sl_str_empty()),
                                SL_STREAM_STATUS_OK);
}

static int test_memory_writable_backpressure_and_drain(void)
{
    unsigned char buffer[5];
    SlMemoryWritableStream adapter = {0};
    SlWritableStream stream = {0};
    SlStreamWriteResult result = {0};
    SlStreamBackpressureState backpressure = {0};

    if (expect_status(
            sl_memory_writable_stream_init(&adapter, buffer, sizeof(buffer), NULL, &stream),
            SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (expect_stream_status(
            sl_writable_stream_write(&stream, (SlStreamChunk){bytes_from_cstr("abc")}, &result),
            SL_STREAM_STATUS_OK) != 0 ||
        result.bytes_written != 3U ||
        expect_bytes_equal(sl_memory_writable_stream_view(&adapter), "abc") != 0)
    {
        return 2;
    }
    if (expect_stream_status(
            sl_writable_stream_write(&stream, (SlStreamChunk){bytes_from_cstr("def")}, &result),
            SL_STREAM_STATUS_BACKPRESSURE) != 0 ||
        result.bytes_written != 2U ||
        expect_bytes_equal(sl_memory_writable_stream_view(&adapter), "abcde") != 0)
    {
        return 3;
    }
    backpressure = sl_writable_stream_backpressure(&stream);
    if (backpressure.backpressure_hits != 1U || backpressure.available_bytes != 0U ||
        backpressure.writable)
    {
        return 4;
    }
    if (expect_stream_status(sl_writable_stream_drain(&stream, 2U), SL_STREAM_STATUS_OK) != 0 ||
        expect_bytes_equal(sl_memory_writable_stream_view(&adapter), "cde") != 0)
    {
        return 5;
    }
    if (expect_stream_status(
            sl_writable_stream_write(&stream, (SlStreamChunk){bytes_from_cstr("fg")}, &result),
            SL_STREAM_STATUS_OK) != 0 ||
        expect_bytes_equal(sl_memory_writable_stream_view(&adapter), "cdefg") != 0)
    {
        return 6;
    }
    if (expect_stream_status(sl_writable_stream_close(&stream, sl_str_empty()),
                             SL_STREAM_STATUS_OK) != 0 ||
        expect_stream_status(sl_writable_stream_close(&stream, sl_str_empty()),
                             SL_STREAM_STATUS_OK) != 0)
    {
        return 7;
    }
    return expect_stream_status(
        sl_writable_stream_write(&stream, (SlStreamChunk){bytes_from_cstr("x")}, &result),
        SL_STREAM_STATUS_CLOSED);
}

static int test_pump_keeps_pending_chunk_across_backpressure(void)
{
    unsigned char buffer[4];
    SlStreamChunk chunks[1] = {{bytes_from_cstr("abcdef")}};
    SlMemoryReadableStream readable_adapter = {0};
    SlMemoryWritableStream writable_adapter = {0};
    SlReadableStream readable = {0};
    SlWritableStream writable = {0};
    SlStreamPump pump = {0};
    SlStreamPumpResult result = {0};

    if (expect_status(
            sl_memory_readable_stream_init(&readable_adapter, chunks, 1U, NULL, &readable),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_memory_writable_stream_init(&writable_adapter, buffer, sizeof(buffer),
                                                     NULL, &writable),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_stream_pump_init(&pump, &readable, &writable, NULL), SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (expect_stream_status(sl_stream_pump_step(&pump, &result), SL_STREAM_STATUS_BACKPRESSURE) !=
            0 ||
        !pump.has_pending_chunk || pump.bytes_transferred != 4U || pump.chunks_transferred != 0U ||
        expect_bytes_equal(sl_memory_writable_stream_view(&writable_adapter), "abcd") != 0 ||
        expect_bytes_equal(pump.pending_chunk.bytes, "ef") != 0)
    {
        return 2;
    }
    if (expect_stream_status(sl_writable_stream_drain(&writable, 4U), SL_STREAM_STATUS_OK) != 0 ||
        expect_stream_status(sl_stream_pump_step(&pump, &result), SL_STREAM_STATUS_OK) != 0 ||
        pump.has_pending_chunk || pump.bytes_transferred != 6U || pump.chunks_transferred != 1U ||
        expect_bytes_equal(sl_memory_writable_stream_view(&writable_adapter), "ef") != 0)
    {
        return 3;
    }
    return expect_stream_status(sl_stream_pump_step(&pump, &result), SL_STREAM_STATUS_EOF);
}

static int test_pump_retries_partial_ok_writes(void)
{
    unsigned char buffer[8];
    SlStreamChunk chunks[1] = {{bytes_from_cstr("abcd")}};
    SlMemoryReadableStream readable_adapter = {0};
    TestPartialWritable writable_adapter = {buffer, sizeof(buffer), 0U, 2U, SL_STREAM_STATUS_OK};
    SlReadableStream readable = {0};
    SlWritableStream writable = {0};
    SlStreamPump pump = {0};
    SlStreamPumpResult result = {0};

    if (expect_status(
            sl_memory_readable_stream_init(&readable_adapter, chunks, 1U, NULL, &readable),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_writable_stream_init(&writable, &test_partial_writable_vtable,
                                              &writable_adapter, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_stream_pump_init(&pump, &readable, &writable, NULL), SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (expect_stream_status(sl_stream_pump_step(&pump, &result), SL_STREAM_STATUS_OK) != 0 ||
        !pump.has_pending_chunk || pump.bytes_transferred != 2U || pump.chunks_transferred != 0U ||
        expect_bytes_equal(sl_bytes_from_parts(buffer, writable_adapter.length), "ab") != 0 ||
        expect_bytes_equal(pump.pending_chunk.bytes, "cd") != 0)
    {
        return 2;
    }
    if (expect_stream_status(sl_stream_pump_step(&pump, &result), SL_STREAM_STATUS_OK) != 0 ||
        pump.has_pending_chunk || pump.bytes_transferred != 4U || pump.chunks_transferred != 1U ||
        expect_bytes_equal(sl_bytes_from_parts(buffer, writable_adapter.length), "abcd") != 0)
    {
        return 3;
    }
    return expect_stream_status(sl_stream_pump_step(&pump, &result), SL_STREAM_STATUS_EOF);
}

static int test_writable_rejects_invalid_partial_contracts(void)
{
    SlWritableStream writable = {0};
    SlStreamWriteResult result = {0};
    TestInvalidWritable adapter = {SL_STREAM_STATUS_OK, 0U};

    if (expect_status(
            sl_writable_stream_init(&writable, &test_invalid_writable_vtable, &adapter, NULL),
            SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (expect_stream_status(
            sl_writable_stream_write(&writable, (SlStreamChunk){bytes_from_cstr("x")}, &result),
            SL_STREAM_STATUS_INVALID_STATE) != 0 ||
        result.bytes_written != 0U)
    {
        return 2;
    }

    adapter.status = SL_STREAM_STATUS_BACKPRESSURE;
    adapter.bytes_written = 2U;
    if (expect_stream_status(
            sl_writable_stream_write(&writable, (SlStreamChunk){bytes_from_cstr("x")}, &result),
            SL_STREAM_STATUS_INVALID_STATE) != 0 ||
        result.bytes_written != 0U)
    {
        return 3;
    }
    return 0;
}

static int test_chunk_list_writer_borrows_and_reads_back(void)
{
    static const unsigned char first[] = {'a', 'b'};
    static const unsigned char second[] = {'c', 'd'};
    static const unsigned char third[] = {'e'};
    SlStreamChunk storage[2] = {0};
    SlChunkListWritableStream writable_adapter = {0};
    SlChunkListReadableStream readable_adapter = {0};
    SlWritableStream writable = {0};
    SlReadableStream readable = {0};
    SlStreamWriteResult write = {0};

    if (expect_status(
            sl_chunk_list_writable_stream_init(&writable_adapter, storage, 2U, NULL, &writable),
            SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (expect_stream_status(
            sl_writable_stream_write(
                &writable, (SlStreamChunk){sl_bytes_from_parts(first, sizeof(first))}, &write),
            SL_STREAM_STATUS_OK) != 0 ||
        expect_stream_status(
            sl_writable_stream_write(
                &writable, (SlStreamChunk){sl_bytes_from_parts(second, sizeof(second))}, &write),
            SL_STREAM_STATUS_OK) != 0 ||
        expect_stream_status(
            sl_writable_stream_write(
                &writable, (SlStreamChunk){sl_bytes_from_parts(third, sizeof(third))}, &write),
            SL_STREAM_STATUS_BACKPRESSURE) != 0)
    {
        return 2;
    }
    if (writable_adapter.count != 2U || writable_adapter.total_bytes != 4U ||
        storage[0].bytes.ptr != first || storage[1].bytes.ptr != second)
    {
        return 3;
    }
    if (expect_status(sl_chunk_list_writable_stream_as_readable(&writable_adapter,
                                                                &readable_adapter, NULL, &readable),
                      SL_STATUS_OK) != 0)
    {
        return 4;
    }
    if (read_expected(&readable, "ab") != 0 || read_expected(&readable, "cd") != 0) {
        return 5;
    }
    return expect_stream_status(sl_readable_stream_read(&readable, &(SlStreamReadResult){0}),
                                SL_STREAM_STATUS_EOF);
}

static int test_cancellation_failure_and_limits(void)
{
    SlCancellationToken cancellation = {0};
    SlStreamChunk chunks[1] = {{bytes_from_cstr("hello")}};
    SlMemoryReadableStream readable_adapter = {0};
    SlReadableStream readable = {0};
    SlStreamReadResult read = {0};
    SlMemoryWritableStream writable_adapter = {0};
    SlWritableStream writable = {0};
    SlStreamWriteResult write = {0};
    unsigned char buffer[8];
    SlStreamOptions options = sl_stream_default_options();

    sl_cancellation_token_init(&cancellation);
    if (expect_status(sl_cancellation_token_cancel(&cancellation,
                                                   SL_CANCELLATION_REASON_DEADLINE_EXCEEDED,
                                                   sl_str_from_cstr("deadline")),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }
    options.cancellation = &cancellation;
    if (expect_status(
            sl_memory_readable_stream_init(&readable_adapter, chunks, 1U, &options, &readable),
            SL_STATUS_OK) != 0 ||
        expect_stream_status(sl_readable_stream_read(&readable, &read),
                             SL_STREAM_STATUS_DEADLINE_EXCEEDED) != 0 ||
        sl_readable_stream_stats(&readable).state != SL_STREAM_STATE_CANCELLED)
    {
        return 2;
    }

    options = sl_stream_default_options();
    options.max_chunk_bytes = 2U;
    if (expect_status(sl_memory_writable_stream_init(&writable_adapter, buffer, sizeof(buffer),
                                                     &options, &writable),
                      SL_STATUS_OK) != 0 ||
        expect_stream_status(
            sl_writable_stream_write(&writable, (SlStreamChunk){bytes_from_cstr("abc")}, &write),
            SL_STREAM_STATUS_CAPACITY_EXCEEDED) != 0)
    {
        return 3;
    }

    if (expect_stream_status(sl_writable_stream_fail(&writable, SL_STREAM_STATUS_FAILED,
                                                     sl_str_from_cstr("writer failed")),
                             SL_STREAM_STATUS_FAILED) != 0 ||
        writable.state != SL_STREAM_STATE_FAILED)
    {
        return 4;
    }
    return expect_stream_status(
        sl_writable_stream_write(&writable, (SlStreamChunk){bytes_from_cstr("a")}, &write),
        SL_STREAM_STATUS_FAILED);
}

static int test_control_hook_rejection_keeps_original_state(void)
{
    static const SlReadableStreamVTable readable_vtable = {
        test_empty_read, NULL, test_reject_control, test_reject_control, "reject-readable"};
    static const SlWritableStreamVTable writable_vtable = {
        test_accept_write, NULL, NULL, NULL, test_reject_control, test_reject_control,
        "reject-writable"};
    SlReadableStream readable = {0};
    SlWritableStream writable = {0};
    int readable_calls = 0;
    int writable_calls = 0;

    if (expect_status(sl_readable_stream_init(&readable, &readable_vtable, &readable_calls, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_writable_stream_init(&writable, &writable_vtable, &writable_calls, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (expect_stream_status(
            sl_readable_stream_fail(&readable, SL_STREAM_STATUS_FAILED, sl_str_from_cstr("fail")),
            SL_STREAM_STATUS_UNSUPPORTED) != 0 ||
        readable.state != SL_STREAM_STATE_OPEN ||
        sl_readable_stream_stats(&readable).last_status != SL_STREAM_STATUS_UNSUPPORTED ||
        readable_calls != 1)
    {
        return 2;
    }
    if (expect_stream_status(sl_readable_stream_abort(&readable, sl_str_from_cstr("abort")),
                             SL_STREAM_STATUS_UNSUPPORTED) != 0 ||
        readable.state != SL_STREAM_STATE_OPEN ||
        sl_readable_stream_stats(&readable).last_status != SL_STREAM_STATUS_UNSUPPORTED ||
        readable_calls != 2)
    {
        return 3;
    }
    if (expect_stream_status(
            sl_writable_stream_fail(&writable, SL_STREAM_STATUS_FAILED, sl_str_from_cstr("fail")),
            SL_STREAM_STATUS_UNSUPPORTED) != 0 ||
        writable.state != SL_STREAM_STATE_OPEN ||
        sl_writable_stream_stats(&writable).last_status != SL_STREAM_STATUS_UNSUPPORTED ||
        writable_calls != 1)
    {
        return 4;
    }
    return expect_stream_status(sl_writable_stream_abort(&writable, sl_str_from_cstr("abort")),
                                SL_STREAM_STATUS_UNSUPPORTED) ||
           expect_true(writable.state == SL_STREAM_STATE_OPEN &&
                       sl_writable_stream_stats(&writable).last_status ==
                           SL_STREAM_STATUS_UNSUPPORTED &&
                       writable_calls == 2);
}

static int run_test(const char* name, int (*test)(void))
{
    int result = test();

    if (result != 0) {
#ifdef _MSC_VER
        fprintf_s(stderr, "FAIL: %s returned %d\n", name, result);
#else
        fprintf(stderr, "FAIL: %s returned %d\n", name, result);
#endif
    }

    return result;
}

int main(void)
{
    int result = run_test("test_memory_readable_splits_chunks_and_reports_eof",
                          test_memory_readable_splits_chunks_and_reports_eof);
    if (result != 0) {
        return result;
    }
    result = run_test("test_memory_writable_backpressure_and_drain",
                      test_memory_writable_backpressure_and_drain);
    if (result != 0) {
        return result;
    }
    result = run_test("test_pump_keeps_pending_chunk_across_backpressure",
                      test_pump_keeps_pending_chunk_across_backpressure);
    if (result != 0) {
        return result;
    }
    result = run_test("test_pump_retries_partial_ok_writes", test_pump_retries_partial_ok_writes);
    if (result != 0) {
        return result;
    }
    result = run_test("test_writable_rejects_invalid_partial_contracts",
                      test_writable_rejects_invalid_partial_contracts);
    if (result != 0) {
        return result;
    }
    result = run_test("test_chunk_list_writer_borrows_and_reads_back",
                      test_chunk_list_writer_borrows_and_reads_back);
    if (result != 0) {
        return result;
    }
    result = run_test("test_cancellation_failure_and_limits", test_cancellation_failure_and_limits);
    if (result != 0) {
        return result;
    }
    return run_test("test_control_hook_rejection_keeps_original_state",
                    test_control_hook_rejection_keeps_original_state);
}
