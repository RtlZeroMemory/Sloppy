#ifndef SLOPPY_HTTP_RESPONSE_H
#define SLOPPY_HTTP_RESPONSE_H

#include "sloppy/bytes.h"
#include "sloppy/http.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

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
    SL_HTTP_RESPONSE_STREAM = 4
} SlHttpResponseKind;

typedef enum SlHttpResponseConnectionPolicy
{
    SL_HTTP_RESPONSE_CONNECTION_CLOSE = 0,
    SL_HTTP_RESPONSE_CONNECTION_KEEP_ALIVE = 1
} SlHttpResponseConnectionPolicy;

typedef struct SlHttpResponseWriteOptions
{
    SlHttpResponseConnectionPolicy connection;
} SlHttpResponseWriteOptions;

typedef struct SlHttpResponseStreamChunk
{
    SlBytes bytes;
} SlHttpResponseStreamChunk;

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
    const SlHttpResponseStreamChunk* stream_chunks;
    size_t stream_chunk_count;
} SlHttpResponse;

SlHttpResponse sl_http_response_text(uint16_t status, SlStr body);
SlHttpResponse sl_http_response_json(uint16_t status, SlBytes body);
SlHttpResponse sl_http_response_empty(uint16_t status);
SlHttpResponse sl_http_response_problem(uint16_t status, SlBytes body);
/*
 * Internal/native streaming descriptor. Transport dispatch callbacks must store `chunks`
 * and every non-empty chunk byte view in the dispatch arena passed to the callback.
 */
SlHttpResponse sl_http_response_stream(uint16_t status, SlStr content_type,
                                       const SlHttpResponseStreamChunk* chunks, size_t chunk_count);

/*
 * Writes deterministic HTTP/1.1 response bytes into `buffer`.
 *
 * `response`, `buffer`, and `out_bytes` are required. Returned bytes borrow `buffer` and
 * remain valid until the caller mutates that storage. Supported statuses are 200, 201, 202,
 * 204, 400, 404, 405, 413, 415, 500, and 501. Unknown statuses, invalid custom header
 * names, managed custom headers, and CR/LF in Content-Type or header values are rejected.
 * Status 204 always writes no Content-Type, no Content-Length, and no body.
 */
SlStatus sl_http_response_write(const SlHttpResponse* response, unsigned char* buffer,
                                size_t capacity, SlBytes* out_bytes);
SlStatus sl_http_response_write_with_options(const SlHttpResponse* response,
                                             const SlHttpResponseWriteOptions* options,
                                             unsigned char* buffer, size_t capacity,
                                             SlBytes* out_bytes);

#ifdef __cplusplus
}
#endif

#endif
