#ifndef SLOPPY_HTTP_H
#define SLOPPY_HTTP_H

#include "sloppy/arena.h"
#include "sloppy/bytes.h"
#include "sloppy/diagnostics.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_HTTP_DEFAULT_MAX_HEADERS 32U

typedef enum SlHttpMethod
{
    SL_HTTP_METHOD_UNKNOWN = 0,
    SL_HTTP_METHOD_GET = 1,
    SL_HTTP_METHOD_POST = 2,
    SL_HTTP_METHOD_PUT = 3,
    SL_HTTP_METHOD_DELETE = 4,
    SL_HTTP_METHOD_PATCH = 5,
    SL_HTTP_METHOD_OPTIONS = 6,
    SL_HTTP_METHOD_HEAD = 7
} SlHttpMethod;

typedef struct SlHttpHeader
{
    SlStr name;
    SlStr value;
} SlHttpHeader;

/*
 * Parsed HTTP request head.
 *
 * sl_http_parse_request_head copies `raw_target`, `path`, header names, and header values
 * into the supplied arena. Returned views remain valid until that arena is reset or its
 * caller-owned backing storage ends. No body bytes, socket state, route dispatch, or query
 * parsing is represented in this skeleton.
 */
typedef struct SlHttpRequestHead
{
    SlHttpMethod method;
    SlStr path;
    SlStr raw_target;
    SlHttpHeader* headers;
    size_t header_count;
} SlHttpRequestHead;

typedef struct SlHttpParseOptions
{
    size_t max_headers;
} SlHttpParseOptions;

/*
 * Parses one complete in-memory HTTP/1 request head with llhttp.
 *
 * `arena`, `bytes`, and `out_request` are required. `bytes` must contain a complete request
 * line and headers ending in CRLF CRLF. Parsed strings and the header array are arena-owned.
 * `options` may be NULL, in which case SL_HTTP_DEFAULT_MAX_HEADERS is used. A zero
 * max_headers value permits no headers. Header overflow, malformed input, incomplete input,
 * empty request targets, and unsupported methods fail with SlStatus and, when supplied,
 * an arena-owned diagnostic.
 *
 * The parser stores `raw_target` exactly as llhttp reports it. `path` is the portion before
 * `?`; query parsing and percent decoding are intentionally deferred.
 */
SlStatus sl_http_parse_request_head(SlArena* arena, SlBytes bytes,
                                    const SlHttpParseOptions* options,
                                    SlHttpRequestHead* out_request, SlDiag* out_diag);

/*
 * Minimal libuv dependency smoke.
 *
 * Initializes and closes a local uv_loop_t to prove the dependency links. This does not
 * create sockets, timers, a backend bridge, or SlLoop integration.
 */
SlStatus sl_http_libuv_smoke(void);

#ifdef __cplusplus
}
#endif

#endif
