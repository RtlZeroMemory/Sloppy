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

    return expect_true(actual.length == length &&
                       (length == 0U ||
                        memcmp(actual.ptr, (const unsigned char*)expected, length) == 0));
}

static int read_expected(SlReadableStream* stream, const char* expected)
{
    SlStreamReadResult result = {0};

    if (expect_stream_status(sl_readable_stream_read(stream, &result),
                             SL_STREAM_STATUS_OK) != 0)
    {
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
    if (expect_stream_status(sl_readable_stream_read(&stream, &result), SL_STREAM_STATUS_EOF) !=
        0)
    {
        return 3;
    }
    stats = sl_readable_stream_stats(&stream);
    if (stats.bytes_read != 10U || stats.chunks_read != 4U ||
        stats.state != SL_STREAM_STATE_ENDED)
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
    if (expect_stream_status(sl_writable_stream_write(
                                 &stream, (SlStreamChunk){bytes_from_cstr("abc")}, &result),
                             SL_STREAM_STATUS_OK) != 0 ||
        result.bytes_written != 3U || expect_bytes_equal(sl_memory_writable_stream_view(&adapter),
                                                         "abc") != 0)
    {
        return 2;
    }
    if (expect_stream_status(sl_writable_stream_write(
                                 &stream, (SlStreamChunk){bytes_from_cstr("def")}, &result),
                             SL_STREAM_STATUS_BACKPRESSURE) != 0)
    {
        return 3;
    }
    backpressure = sl_writable_stream_backpressure(&stream);
    if (backpressure.backpressure_hits != 1U || backpressure.available_bytes != 2U ||
        !backpressure.writable)
    {
        return 4;
    }
    if (expect_stream_status(sl_writable_stream_drain(&stream, 2U), SL_STREAM_STATUS_OK) != 0 ||
        expect_bytes_equal(sl_memory_writable_stream_view(&adapter), "c") != 0)
    {
        return 5;
    }
    if (expect_stream_status(sl_writable_stream_write(
                                 &stream, (SlStreamChunk){bytes_from_cstr("de")}, &result),
                             SL_STREAM_STATUS_OK) != 0 ||
        expect_bytes_equal(sl_memory_writable_stream_view(&adapter), "cde") != 0)
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
    return expect_stream_status(sl_writable_stream_write(
                                    &stream, (SlStreamChunk){bytes_from_cstr("x")}, &result),
                                SL_STREAM_STATUS_CLOSED);
}

static int test_pump_keeps_pending_chunk_across_backpressure(void)
{
    unsigned char buffer[3];
    SlStreamChunk chunks[2] = {{bytes_from_cstr("abc")}, {bytes_from_cstr("de")}};
    SlMemoryReadableStream readable_adapter = {0};
    SlMemoryWritableStream writable_adapter = {0};
    SlReadableStream readable = {0};
    SlWritableStream writable = {0};
    SlStreamPump pump = {0};
    SlStreamPumpResult result = {0};

    if (expect_status(sl_memory_readable_stream_init(&readable_adapter, chunks, 2U, NULL,
                                                     &readable),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_memory_writable_stream_init(&writable_adapter, buffer, sizeof(buffer),
                                                     NULL, &writable),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_stream_pump_init(&pump, &readable, &writable, NULL), SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (expect_stream_status(sl_stream_pump_step(&pump, &result), SL_STREAM_STATUS_OK) != 0 ||
        expect_bytes_equal(sl_memory_writable_stream_view(&writable_adapter), "abc") != 0)
    {
        return 2;
    }
    if (expect_stream_status(sl_stream_pump_step(&pump, &result), SL_STREAM_STATUS_BACKPRESSURE) !=
            0 ||
        !pump.has_pending_chunk || pump.bytes_transferred != 3U)
    {
        return 3;
    }
    if (expect_stream_status(sl_writable_stream_drain(&writable, 3U), SL_STREAM_STATUS_OK) != 0 ||
        expect_stream_status(sl_stream_pump_step(&pump, &result), SL_STREAM_STATUS_OK) != 0 ||
        pump.has_pending_chunk || pump.bytes_transferred != 5U || pump.chunks_transferred != 2U)
    {
        return 4;
    }
    return expect_stream_status(sl_stream_pump_step(&pump, &result), SL_STREAM_STATUS_EOF);
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

    if (expect_status(sl_chunk_list_writable_stream_init(&writable_adapter, storage, 2U, NULL,
                                                         &writable),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (expect_stream_status(sl_writable_stream_write(
                                 &writable,
                                 (SlStreamChunk){sl_bytes_from_parts(first, sizeof(first))},
                                 &write),
                             SL_STREAM_STATUS_OK) != 0 ||
        expect_stream_status(sl_writable_stream_write(
                                 &writable,
                                 (SlStreamChunk){sl_bytes_from_parts(second, sizeof(second))},
                                 &write),
                             SL_STREAM_STATUS_OK) != 0 ||
        expect_stream_status(sl_writable_stream_write(
                                 &writable,
                                 (SlStreamChunk){sl_bytes_from_parts(third, sizeof(third))},
                                 &write),
                             SL_STREAM_STATUS_BACKPRESSURE) != 0)
    {
        return 2;
    }
    if (writable_adapter.count != 2U || writable_adapter.total_bytes != 4U ||
        storage[0].bytes.ptr != first || storage[1].bytes.ptr != second)
    {
        return 3;
    }
    if (expect_status(sl_chunk_list_writable_stream_as_readable(
                          &writable_adapter, &readable_adapter, NULL, &readable),
                      SL_STATUS_OK) != 0)
    {
        return 4;
    }
    if (read_expected(&readable, "ab") != 0 || read_expected(&readable, "cd") != 0)
    {
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
    if (expect_status(sl_memory_readable_stream_init(&readable_adapter, chunks, 1U, &options,
                                                     &readable),
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
        expect_stream_status(sl_writable_stream_write(
                                 &writable, (SlStreamChunk){bytes_from_cstr("abc")}, &write),
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
    return expect_stream_status(sl_writable_stream_write(
                                    &writable, (SlStreamChunk){bytes_from_cstr("a")}, &write),
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

    if (expect_status(sl_readable_stream_init(&readable, &readable_vtable, &readable_calls,
                                              NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_writable_stream_init(&writable, &writable_vtable, &writable_calls,
                                              NULL),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (expect_stream_status(sl_readable_stream_fail(&readable, SL_STREAM_STATUS_FAILED,
                                                     sl_str_from_cstr("fail")),
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
    if (expect_stream_status(sl_writable_stream_fail(&writable, SL_STREAM_STATUS_FAILED,
                                                     sl_str_from_cstr("fail")),
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
