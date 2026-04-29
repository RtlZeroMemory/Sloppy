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
 * Manual route binding for the first synthetic HTTP dispatch slice.
 *
 * The binding borrows a parsed route pattern. The caller must keep the pattern arena alive
 * for the duration of dispatch. Only GET bindings are executed in TASK 10.C; other methods
 * are ignored so this remains a tiny table, not a method router.
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
 * Dispatches one parsed in-memory HTTP request head to a Sloppy Plan handler ID.
 *
 * This is synthetic dispatch only: no sockets, no response writer, no body parsing, no
 * request context, no middleware, and no public TypeScript API. `arena`, `engine`, `plan`,
 * `dispatch_table`, `request`, and `out_result` are required. On success, result ownership
 * follows the engine/runtime-contract result rules; for the current V8 path, text is copied
 * into `arena`.
 *
 * Failure behavior:
 * - non-GET requests fail with SL_STATUS_UNSUPPORTED;
 * - no matching GET route fails with SL_STATUS_OUT_OF_RANGE;
 * - a matched route with a missing plan handler fails before entering the engine;
 * - missing/non-callable/throwing JavaScript handlers fail through the existing engine
 *   diagnostic path.
 *
 * Route parameters and query parameters are materialized into the EPIC-23 request context
 * passed to JavaScript. Route parameter values are strings; `{id:int}` validates matching
 * but does not coerce the JS value to a number.
 */
SlStatus sl_http_dispatch_request_head(SlArena* arena, SlEngine* engine, const SlPlan* plan,
                                       const SlHttpDispatchTable* dispatch_table,
                                       const SlHttpRequestHead* request, SlEngineResult* out_result,
                                       SlDiag* out_diag);

#ifdef __cplusplus
}
#endif

#endif
