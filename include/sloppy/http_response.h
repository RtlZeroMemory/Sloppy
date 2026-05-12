#ifndef SLOPPY_HTTP_RESPONSE_H
#define SLOPPY_HTTP_RESPONSE_H

#include "sloppy/bytes.h"
#include "sloppy/http.h"
#include "sloppy/status.h"
#include "sloppy/stream.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SlHttpResponseKind
{
    SL_HTTP_RESPONSE_TEXT = 0,
    SL_HTTP_RESPONSE_JSON = 1,
    SL_HTTP_RESPONSE_EMPTY = 2,
    SL_HTTP_RESPONSE_PROBLEM = 3,
    SL_HTTP_RESPONSE_STREAM = 4,
    SL_HTTP_RESPONSE_BYTES = 5
} SlHttpResponseKind;

typedef enum SlHttpResponseConnectionPolicy
{
    SL_HTTP_RESPONSE_CONNECTION_CLOSE = 0,
    SL_HTTP_RESPONSE_CONNECTION_KEEP_ALIVE = 1
} SlHttpResponseConnectionPolicy;

typedef struct SlHttpResponseWriteOptions
{
    /* Defaults to Connection: close when options is NULL or this field is zero. */
    SlHttpResponseConnectionPolicy connection;
    /*
     * Writes response metadata including the original Content-Length, but omits the
     * body bytes on the wire. Used for HEAD responses after normal GET dispatch.
     * Status 204 and 304 always omit body bytes regardless of this flag.
     */
    bool suppress_body;
} SlHttpResponseWriteOptions;

/*
 * Native HTTP response descriptor for the bounded development response writer.
 *
 * The descriptor borrows `content_type`, `headers`, and `body` bytes from caller-owned or
 * arena-owned storage. The writer copies those bytes into the caller-provided output buffer
 * and never retains the descriptor. Content-Type and Content-Length remain runtime-managed;
 * custom headers must not try to override them.
 */
typedef struct SlHttpResponse
{
    uint16_t status;
    SlHttpResponseKind kind;
    SlStr content_type;
    const SlHttpHeader* headers;
    size_t header_count;
    SlBytes body;
    const SlStreamChunk* stream_chunks;
    size_t stream_chunk_count;
} SlHttpResponse;

typedef struct SlHttpResponseStreamReadable
{
    const SlStreamChunk* chunks;
    size_t chunk_count;
    size_t chunk_index;
    size_t chunk_offset;
} SlHttpResponseStreamReadable;

SlHttpResponse sl_http_response_text(uint16_t status, SlStr body);
SlHttpResponse sl_http_response_json(uint16_t status, SlBytes body);
SlHttpResponse sl_http_response_bytes(uint16_t status, SlStr content_type, SlBytes body);
SlHttpResponse sl_http_response_empty(uint16_t status);
SlHttpResponse sl_http_response_problem(uint16_t status, SlBytes body);
/*
 * Internal/native streaming descriptor. Transport dispatch callbacks must store `chunks`
 * and every non-empty chunk byte view in the dispatch arena passed to the callback.
 */
SlHttpResponse sl_http_response_stream(uint16_t status, SlStr content_type,
                                       const SlStreamChunk* chunks, size_t chunk_count);
SlStatus sl_http_response_stream_readable_init(SlHttpResponseStreamReadable* adapter,
                                               const SlHttpResponse* response,
                                               const SlStreamOptions* options,
                                               SlReadableStream* out_stream);

/*
 * Writes deterministic HTTP/1.1 response bytes into `buffer`.
 *
 * `response`, `buffer`, and `out_bytes` are required. Returned bytes borrow `buffer` and
 * remain valid until the caller mutates that storage. Supported statuses are 200, 201, 202,
 * 204, 304, 400, 401, 404, 405, 408, 413, 415, 417, 500, 501, and 503. Unknown statuses, invalid
 * custom header names, managed custom headers, and CR/LF in Content-Type or header values are
 * rejected. Bounded stream descriptors are validated and serialized with computed
 * Content-Length. Status 204 and 304 always write no Content-Type, no Content-Length, and no body;
 * non-empty stream bodies for those statuses are rejected.
 */
SlStatus sl_http_response_write(const SlHttpResponse* response, unsigned char* buffer,
                                size_t capacity, SlBytes* out_bytes);
/*
 * Variant of `sl_http_response_write` with optional write controls. `options` may be NULL,
 * which is equivalent to Connection: close and `suppress_body == false`. The caller retains
 * ownership of `response`, `options`, and `buffer`; `out_bytes` borrows `buffer`.
 */
SlStatus sl_http_response_write_with_options(const SlHttpResponse* response,
                                             const SlHttpResponseWriteOptions* options,
                                             unsigned char* buffer, size_t capacity,
                                             SlBytes* out_bytes);

#ifdef __cplusplus
}
#endif

#endif
