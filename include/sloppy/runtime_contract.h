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
 */
SlStatus sl_runtime_contract_call_handler(SlEngine* engine, SlArena* arena, const SlPlan* plan,
                                          SlHandlerId handler_id, SlEngineResult* out_result,
                                          SlDiag* out_diag);

#ifdef __cplusplus
}
#endif

#endif
