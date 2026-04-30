#ifndef SLOPPY_HTTP_DISPATCH_H
#define SLOPPY_HTTP_DISPATCH_H

#include "sloppy/arena.h"
#include "sloppy/diagnostics.h"
#include "sloppy/engine.h"
#include "sloppy/http.h"
#include "sloppy/plan.h"
#include "sloppy/route.h"
#include "sloppy/status.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Manual route binding for the bounded Plan-backed HTTP dispatch slice.
 *
 * The binding borrows a parsed route pattern. The caller must keep the pattern arena alive
 * for the duration of dispatch. GET, POST, PUT, PATCH, and DELETE bindings are runnable
 * when they are present in Sloppy Plan metadata.
 */
typedef struct SlHttpRouteBinding
{
    SlHttpMethod method;
    const SlRoutePattern* pattern;
    SlHandlerId handler_id;
} SlHttpRouteBinding;

/*
 * Borrowed route binding table.
 *
 * `routes` must point to `route_count` entries when `route_count` is nonzero. The table
 * does not own route patterns or plan handlers, and it is intentionally separate from
 * SlPlan until a future plan routes section is implemented.
 */
typedef struct SlHttpDispatchTable
{
    const SlHttpRouteBinding* routes;
    size_t route_count;
} SlHttpDispatchTable;

/*
 * Arena-owned route table materialized from Plan v1 alpha route metadata.
 *
 * `sl_http_route_table_build` parses supported route patterns, verifies handler references,
 * and fills a dispatch table ordered by Sloppy's alpha precedence policy: literal patterns
 * before parameter patterns, then stable source order for equal precedence. The returned
 * table borrows arena-owned patterns and bindings and remains valid until the arena is reset
 * or its backing storage ends.
 */
typedef struct SlHttpRouteTable
{
    SlHttpDispatchTable dispatch;
    size_t route_count;
} SlHttpRouteTable;

SlStatus sl_http_route_table_build(SlArena* arena, const SlPlan* plan, SlHttpRouteTable* out_table,
                                   SlDiag* out_diag);

/*
 * Dispatches one parsed in-memory HTTP request head to a Sloppy Plan handler ID.
 *
 * This is synthetic dispatch only: no sockets, no middleware, no streaming body state, and
 * no production hardening. `arena`, `engine`, `plan`, `dispatch_table`, `request`, and
 * `out_result` are required. On success, result ownership follows the engine/runtime-contract
 * result rules; for the current V8 path, response bodies and copied header strings are
 * stored in `arena`.
 *
 * Failure behavior:
 * - methods outside GET/POST/PUT/PATCH/DELETE fail with SL_STATUS_UNSUPPORTED;
 * - matching path with the wrong method fails with SL_STATUS_UNSUPPORTED;
 * - transfer-encoded bodies fail with SL_STATUS_UNSUPPORTED;
 * - unsupported body content types fail with SL_STATUS_UNSUPPORTED;
 * - bodies over SL_HTTP_DEFAULT_MAX_BODY_LENGTH fail with SL_STATUS_CAPACITY_EXCEEDED;
 * - malformed JSON bodies fail with SL_STATUS_INVALID_ARGUMENT;
 * - no matching route path fails with SL_STATUS_OUT_OF_RANGE;
 * - a matched route with a missing plan handler fails before entering the engine;
 * - missing/non-callable/throwing JavaScript handlers fail through the existing engine
 *   diagnostic path.
 *
 * Route parameters, query parameters, headers, and JSON/text body policy are materialized
 * into the request context passed to JavaScript. Route parameter values are strings;
 * `{id:int}` validates matching but does not coerce the JS value to a number.
 */
SlStatus sl_http_dispatch_request_head(SlArena* arena, SlEngine* engine, const SlPlan* plan,
                                       const SlHttpDispatchTable* dispatch_table,
                                       const SlHttpRequestHead* request, SlEngineResult* out_result,
                                       SlDiag* out_diag);

#ifdef __cplusplus
}
#endif

#endif
