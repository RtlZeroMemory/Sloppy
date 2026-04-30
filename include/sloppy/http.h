#ifndef SLOPPY_HTTP_H
#define SLOPPY_HTTP_H

#include "sloppy/arena.h"
#include "sloppy/bytes.h"
#include "sloppy/diagnostics.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_HTTP_DEFAULT_MAX_HEADERS 32U
#define SL_HTTP_DEFAULT_MAX_TARGET_LENGTH 2048U
#define SL_HTTP_DEFAULT_MAX_HEADER_NAME_LENGTH 256U
#define SL_HTTP_DEFAULT_MAX_HEADER_VALUE_LENGTH 8192U
#define SL_HTTP_DEFAULT_MAX_TOTAL_HEADER_BYTES 16384U
#define SL_HTTP_DEFAULT_MAX_BODY_LENGTH 65536U

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

/*
 * Shared ENGINE-04 framework method set. These helpers intentionally return false for
 * HEAD/OPTIONS even though the parser recognizes those tokens: the current runtime only
 * dispatches GET, POST, PUT, PATCH, and DELETE route metadata.
 */
bool sl_http_method_supported(SlHttpMethod method);
SlStatus sl_http_method_from_str(SlStr method, SlHttpMethod* out_method);

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
 * caller-owned backing storage ends. Body bytes are complete-buffer, arena-owned request
 * input; there is still no streaming parser state, socket state, route dispatch, or query
 * parsing represented by this struct.
 */
typedef struct SlHttpRequestHead
{
    SlHttpMethod method;
    SlStr path;
    SlStr raw_target;
    SlHttpHeader* headers;
    size_t header_count;
    SlBytes body;
} SlHttpRequestHead;

typedef struct SlHttpParseOptions
{
    size_t max_headers;
    size_t max_target_length;
    size_t max_header_name_length;
    size_t max_header_value_length;
    size_t max_total_header_bytes;
    size_t max_body_length;
} SlHttpParseOptions;

/*
 * Parses one complete in-memory HTTP/1 request head with llhttp.
 *
 * `arena`, `bytes`, and `out_request` are required. `bytes` must contain a complete
 * request message for the declared Content-Length. Parsed strings, the header array, and
 * body bytes are arena-owned. `options` may be NULL, in which case default limits are used.
 * A zero max_headers value permits no headers. Zero target, header-name, header-value,
 * total-header-byte, and body limits use the documented defaults. Header count/name/value/
 * total-byte overflow, target overflow, body overflow, malformed input, incomplete input,
 * empty/non-path request targets, and unsupported methods fail with SlStatus and, when
 * supplied, an arena-owned diagnostic.
 *
 * The parser stores `raw_target` exactly as llhttp reports it. `path` is the portion before
 * `?`; EPIC-23 query parsing and percent decoding live in `http_context.h` so the request
 * head parser stays focused on HTTP syntax.
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
