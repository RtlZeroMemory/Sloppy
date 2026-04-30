#ifndef SLOPPY_ENGINE_H
#define SLOPPY_ENGINE_H

#include "sloppy/arena.h"
#include "sloppy/capability.h"
#include "sloppy/diagnostics.h"
#include "sloppy/http_context.h"
#include "sloppy/http_response.h"
#include "sloppy/plan.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SlEngine SlEngine;

typedef enum SlEngineKind
{
    SL_ENGINE_KIND_NONE = 0,
    SL_ENGINE_KIND_V8 = 1
} SlEngineKind;

typedef enum SlEngineResultKind
{
    SL_ENGINE_RESULT_NONE = 0,
    SL_ENGINE_RESULT_TEXT = 1,
    SL_ENGINE_RESULT_JSON = 2,
    SL_ENGINE_RESULT_ERROR = 3
} SlEngineResultKind;

/*
 * Engine-neutral creation options.
 *
 * All SlStr fields are borrowed views. The current noop implementation copies no option
 * strings and stores only the selected kind. Future V8 bridge creation must keep V8 and C++
 * types behind src/engine/v8/ and may copy only the fields it needs into bridge-owned
 * storage.
 */
typedef struct SlEngineOptions
{
    SlEngineKind kind;
    SlStr runtime_name;
    SlStr runtime_version;
    SlStr target_platform;
    SlStr target_engine;
    /*
     * Optional borrowed app metadata used by provider bridges. When present, these pointers
     * must outlive the engine. The V8 SQLite bridge uses them only as hook inputs for
     * Plan-backed provider resolution and capability checks before provider work begins.
     */
    const SlPlan* plan;
    const SlCapabilityRegistry* capabilities;
} SlEngineOptions;

/*
 * Borrowed engine metadata.
 *
 * Returned strings are engine-owned or static borrowed views. They remain valid until the
 * engine is destroyed or, for the current arena-backed noop engine, until the arena backing
 * the engine is reset.
 */
typedef struct SlEngineInfo
{
    SlEngineKind kind;
    SlStr name;
    SlStr version;
} SlEngineInfo;

typedef struct SlEngineHandlerCall
{
    SlHandlerId handler_id;
} SlEngineHandlerCall;

typedef struct SlEngineResult
{
    SlEngineResultKind kind;
    SlStr text;
    SlHttpResponse response;
} SlEngineResult;

/*
 * Creates an opaque engine instance for the requested kind.
 *
 * `options`, `arena`, and `out_engine` are required. The returned engine is allocated from
 * `arena`; callers must call sl_engine_destroy before resetting that arena. The current
 * ABI calls are not generally thread-safe. V8 engines have one owning isolate/context
 * thread: create, eval, call, validate, and destroy on that owner thread. Worker threads
 * must not enter the engine directly.
 *
 * SL_ENGINE_KIND_NONE creates the deterministic noop engine. SL_ENGINE_KIND_V8 creates the
 * V8 bridge only in builds configured with SLOPPY_ENABLE_V8=ON and a valid SDK; otherwise
 * it returns SL_STATUS_UNSUPPORTED.
 */
SlStatus sl_engine_create(const SlEngineOptions* options, SlArena* arena, SlEngine** out_engine);

/*
 * Destroys an engine handle. Passing NULL is allowed. Double destroy is allowed and is a
 * no-op after the first destroy.
 *
 * Destroy releases bridge-owned resources and marks the arena-owned handle inactive. The
 * handle storage remains arena-owned until the caller resets the arena, but all subsequent
 * operations on that handle return `SL_STATUS_INVALID_STATE`. A wrong-thread V8 destroy
 * invalidates the handle without entering V8; tests and owners may call destroy again on
 * the owner thread to release bridge resources.
 */
void sl_engine_destroy(SlEngine* engine);

/*
 * Returns borrowed metadata for an active engine.
 *
 * `engine` and `out_info` are required. No V8 or C++ type is exposed through this metadata.
 */
SlStatus sl_engine_info(const SlEngine* engine, SlEngineInfo* out_info);

/*
 * Evaluates classic JavaScript source inside an active engine.
 *
 * `engine` is required and must be active. `source_name` is a borrowed diagnostic label
 * only; no file I/O is
 * performed. `source` is a borrowed JavaScript source string and is not TypeScript. The
 * current V8 implementation evaluates classic scripts in a single context so smoke tests
 * can define global functions. No ESM loader, module resolver, app.plan integration, or
 * public Sloppy JS API is provided by this call.
 *
 * `out_diag` is optional. When provided, diagnostic text and source paths are copied into
 * the engine arena, not into a caller result arena. Those diagnostic views remain valid
 * until the engine arena is reset or the engine is destroyed.
 */
SlStatus sl_engine_eval_source(SlEngine* engine, SlStr source_name, SlStr source, SlDiag* out_diag);

/*
 * Calls a zero-argument global JavaScript function by name.
 *
 * `engine`, `arena`, `function_name`, and `out_result` are required, and `engine` must be
 * active. On success, supported primitive result data is copied into `arena`; result
 * string views remain valid until that arena is reset or its caller-owned backing storage
 * ends. V8 handles and values never escape this API. The current bridge supports concrete
 * strings, supported result descriptors, and Promise values that settle during the
 * owner-thread microtask drain. Pending Promises that require timers or native completion
 * queues fail with `SL_STATUS_DEADLINE_EXCEEDED`.
 *
 * `out_diag` is optional. When provided, diagnostic text and source paths are copied into
 * the engine arena, not into the result arena passed to this call. Those diagnostic views
 * remain valid until the engine arena is reset or the engine is destroyed.
 */
SlStatus sl_engine_call_function0(SlEngine* engine, SlArena* arena, SlStr function_name,
                                  SlEngineResult* out_result, SlDiag* out_diag);

/*
 * Calls a global JavaScript handler with one request context argument.
 *
 * The request context is borrowed for the duration of the call. The bridge materializes a
 * plain JS object with `route`, `query`, `request`, `signal`, and `deadline` fields and
 * never exposes native pointers or handles. If `request_context->cancellation` is already
 * cancelled, the bridge fails before entering JavaScript. On success, supported
 * `Results.*` descriptors and the plain-string compatibility fallback are converted into
 * `out_result->response`.
 */
SlStatus sl_engine_call_function_with_context(SlEngine* engine, SlArena* arena, SlStr function_name,
                                              const SlHttpRequestContext* request_context,
                                              SlEngineResult* out_result, SlDiag* out_diag);

/*
 * Validates that every handler ID in `plan` was registered through the runtime-owned
 * handler intrinsic.
 *
 * This is the EPIC-24 bridge from compiler-emitted app modules to native dispatch. The
 * plan is borrowed for this call only. No V8 handles, JavaScript values, or native
 * pointers are exposed through the ABI. Non-V8 engines return SL_STATUS_UNSUPPORTED with a
 * diagnostic when requested.
 */
SlStatus sl_engine_validate_registered_handlers(SlEngine* engine, const SlPlan* plan,
                                                SlDiag* out_diag);

/*
 * Calls a runtime-registered handler by numeric ID with one borrowed request context.
 *
 * `request_context` is materialized as a plain JavaScript object for the duration of the
 * call. Handler function handles stay backend-owned; JS receives no native pointers.
 */
SlStatus sl_engine_call_registered_handler_with_context(SlEngine* engine, SlArena* arena,
                                                        SlHandlerId handler_id,
                                                        const SlHttpRequestContext* request_context,
                                                        SlEngineResult* out_result,
                                                        SlDiag* out_diag);

/*
 * Calls a handler by numeric Sloppy Plan handler ID.
 *
 * This defines the C-side shape needed by later V8 bridge work but does not execute
 * JavaScript today. The noop engine always returns SL_STATUS_UNSUPPORTED and clears
 * `out_result`. When `out_diag` is provided, unsupported calls receive a diagnostic.
 */
SlStatus sl_engine_call_handler(SlEngine* engine, const SlEngineHandlerCall* call,
                                SlEngineResult* out_result, SlDiag* out_diag);

#ifdef __cplusplus
}
#endif

#endif
