#ifndef SLOPPY_HTTP_CONTEXT_H
#define SLOPPY_HTTP_CONTEXT_H

#include "sloppy/arena.h"
#include "sloppy/cancellation.h"
#include "sloppy/http.h"
#include "sloppy/route.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_HTTP_DEFAULT_MAX_QUERY_PARAMS 64U

typedef enum SlHttpRequestBodyKind
{
    SL_HTTP_REQUEST_BODY_NONE = 0,
    SL_HTTP_REQUEST_BODY_JSON = 1,
    SL_HTTP_REQUEST_BODY_TEXT = 2,
    SL_HTTP_REQUEST_BODY_BYTES = 3
} SlHttpRequestBodyKind;

typedef struct SlHttpQueryParam
{
    SlStr name;
    SlStr value;
} SlHttpQueryParam;

typedef struct SlHttpQuery
{
    SlHttpQueryParam* params;
    size_t param_count;
} SlHttpQuery;

typedef struct SlHttpRequestContext
{
    const SlHttpRequestHead* request;
    uint64_t request_id;
    uint64_t connection_id;
    SlStr scheme;
    SlStr protocol;
    SlStr query_string;
    SlStr content_type;
    uint64_t content_length;
    bool has_content_length;
    const SlRouteParam* route_params;
    size_t route_param_count;
    SlStr route_name;
    SlStr route_pattern;
    const SlHttpQueryParam* query_params;
    size_t query_param_count;
    SlHttpRequestBodyKind body_kind;
    /*
     * Optional borrowed request cancellation token. A cancelled token means the handler
     * boundary must reject before entering JavaScript or before converting an async result.
     * A NULL token represents a live request with no configured deadline.
     */
    const SlCancellationToken* cancellation;
} SlHttpRequestContext;

/*
 * Parses the query component of an origin-form raw target.
 *
 * The parser supports `key=value` pairs split on `&`, empty values, repeated keys, `%XX`
 * percent decoding, and `+` as a space. Repeated keys use last-wins semantics by replacing
 * the earlier value for the same decoded key. Invalid percent escapes fail instead of being
 * silently preserved.
 */
SlStatus sl_http_query_parse(SlArena* arena, SlStr raw_target, SlHttpQuery* out_query);

#ifdef __cplusplus
}
#endif

#endif
