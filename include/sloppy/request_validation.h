#ifndef SLOPPY_REQUEST_VALIDATION_H
#define SLOPPY_REQUEST_VALIDATION_H

#include "sloppy/arena.h"
#include "sloppy/diagnostics.h"
#include "sloppy/engine.h"
#include "sloppy/http_context.h"
#include "sloppy/plan.h"
#include "sloppy/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Validates Plan-backed framework request binding metadata before handler execution.
 *
 * This is a bounded runtime consumer for compiler-emitted route bindings and schemas. It
 * performs no TypeScript reflection, provider/DI execution, controller dispatch, OpenAPI
 * export, or HTTP transport work. On validation failure it returns SL_STATUS_OK with
 * `out_result` populated as a 400 application/problem+json response so dispatch can safely
 * answer the request without entering user JavaScript.
 */
SlStatus sl_request_validation_validate(SlArena* arena, const SlPlan* plan,
                                        const SlPlanRoute* route,
                                        const SlHttpRequestContext* request_context,
                                        SlEngineResult* out_result, SlDiag* out_diag);

#ifdef __cplusplus
}
#endif

#endif
