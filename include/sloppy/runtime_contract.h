#ifndef SLOPPY_RUNTIME_CONTRACT_H
#define SLOPPY_RUNTIME_CONTRACT_H

#include "sloppy/arena.h"
#include "sloppy/diagnostics.h"
#include "sloppy/engine.h"
#include "sloppy/plan.h"
#include "sloppy/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Executes the minimal milestone runtime contract:
 *
 *   app.plan handler ID -> handler export name -> engine global function call.
 *
 * This is not the app host, HTTP runtime, compiler output loader, request context, route
 * matcher, or public JavaScript API. It is the smallest native boundary needed to prove
 * that a parsed handwritten plan can dispatch into a handwritten JavaScript bundle through
 * the engine bridge.
 *
 * `engine`, `arena`, `plan`, and `out_result` are required. `out_diag` is optional. The
 * helper borrows `plan` and its handler table for the duration of the call only; it does
 * not copy, retain, or free plan storage. The caller owns `engine` and must still destroy
 * it with `sl_engine_destroy`; this helper never closes or resets the engine.
 *
 * On success, result data is owned according to the engine call contract. For the current
 * V8 string-result path, `out_result->text` is copied into `arena` and remains valid until
 * that arena is reset or its backing storage ends. On failure, `out_result` is cleared to
 * `SL_ENGINE_RESULT_NONE`.
 *
 * Diagnostics produced directly by this helper are copied into `arena` and remain valid
 * until that arena is reset or its backing storage ends. Diagnostics produced by the engine
 * retain the engine API's lifetime rules; the current V8 bridge copies diagnostic text into
 * the engine arena.
 */
SlStatus sl_runtime_contract_call_handler(SlEngine* engine, SlArena* arena, const SlPlan* plan,
                                          SlHandlerId handler_id, SlEngineResult* out_result,
                                          SlDiag* out_diag);

/*
 * Executes the same plan handler lookup with one borrowed request context argument.
 *
 * `request_context` is required, is borrowed only for this call, and is not retained by the
 * runtime contract or engine bridge. Its nested request, route parameter, and query
 * parameter views must stay valid for the duration of the call.
 */
SlStatus sl_runtime_contract_call_handler_with_context(SlEngine* engine, SlArena* arena,
                                                       const SlPlan* plan, SlHandlerId handler_id,
                                                       const SlHttpRequestContext* request_context,
                                                       SlEngineResult* out_result,
                                                       SlDiag* out_diag);

#ifdef __cplusplus
}
#endif

#endif
