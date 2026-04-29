#ifndef SLOPPY_ENGINE_INTERNAL_H
#define SLOPPY_ENGINE_INTERNAL_H

/*
 * src/engine/engine_internal.h
 *
 * Internal engine dispatch state shared by the C ABI wrapper and optional backend
 * implementations. This header intentionally stays engine-neutral: it exposes no V8,
 * platform, or C++ library types. Backend-specific state is stored behind `void*` and is
 * owned/released by that backend through the dispatch functions declared below.
 */

#include "sloppy/engine.h"

#ifdef __cplusplus
extern "C" {
#endif

struct SlEngine
{
    SlEngineKind kind;
    SlArena* arena;
    bool active;
    void* backend;
};

#if defined(SLOPPY_ENABLE_V8_BRIDGE)
SlStatus sl_engine_v8_create(const SlEngineOptions* options, SlArena* arena, SlEngine** out_engine);
void sl_engine_v8_destroy(SlEngine* engine);
SlStatus sl_engine_v8_info(const SlEngine* engine, SlEngineInfo* out_info);
SlStatus sl_engine_v8_eval_source(SlEngine* engine, SlStr source_name, SlStr source,
                                  SlDiag* out_diag);
SlStatus sl_engine_v8_call_function0(SlEngine* engine, SlArena* arena, SlStr function_name,
                                     SlEngineResult* out_result, SlDiag* out_diag);
SlStatus sl_engine_v8_call_function_with_context(SlEngine* engine, SlArena* arena,
                                                 SlStr function_name,
                                                 const SlHttpRequestContext* request_context,
                                                 SlEngineResult* out_result, SlDiag* out_diag);
#endif

#ifdef __cplusplus
}
#endif

#endif
