/*
 * src/engine/v8/engine_v8_internal.h
 *
 * Private C++ contract shared only by files inside src/engine/v8/.
 * Provider-specific V8 bridges belong in separate intrinsic modules, and framework-specific
 * bridges such as HTTP context/result conversion belong in separate sibling modules, not in
 * engine_v8.cc. This header intentionally keeps V8 types inside the V8 module boundary while
 * allowing those modules to use engine-owned state.
 */
#ifndef SLOPPY_ENGINE_V8_INTERNAL_H
#define SLOPPY_ENGINE_V8_INTERNAL_H

#include "../engine_internal.h"

#include "sloppy/features.h"
#include "sloppy/async_backend.h"
#include "sloppy/logging.h"
#include "sloppy/provider_executor.h"
#include "sloppy/resource.h"

#include <v8.h>

#include <array>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

struct SlV8TimeRequest;
struct SlV8CryptoPasswordRequest;
struct SlV8NetRequest;
struct SlV8OsRequest;
struct SlV8WorkerRequest;
struct SlV8JsWorker;

typedef enum SlV8HttpStringKey
{
    SL_V8_HTTP_STRING_ABORTED = 0,
    SL_V8_HTTP_STRING_BODY,
    SL_V8_HTTP_STRING_BODY_RESULT,
    SL_V8_HTTP_STRING_BYTES,
    SL_V8_HTTP_STRING_CONNECTION,
    SL_V8_HTTP_STRING_CONSUMED,
    SL_V8_HTTP_STRING_CONTENT_LENGTH,
    SL_V8_HTTP_STRING_CONTENT_TYPE,
    SL_V8_HTTP_STRING_DEADLINE,
    SL_V8_HTTP_STRING_ENTRIES,
    SL_V8_HTTP_STRING_EXPIRED,
    SL_V8_HTTP_STRING_GET,
    SL_V8_HTTP_STRING_HEADERS,
    SL_V8_HTTP_STRING_ID,
    SL_V8_HTTP_STRING_JSON,
    SL_V8_HTTP_STRING_KIND,
    SL_V8_HTTP_STRING_LOCATION,
    SL_V8_HTTP_STRING_LOG,
    SL_V8_HTTP_STRING_METHOD,
    SL_V8_HTTP_STRING_PATH,
    SL_V8_HTTP_STRING_PROTOCOL,
    SL_V8_HTTP_STRING_QUERY,
    SL_V8_HTTP_STRING_QUERY_STRING,
    SL_V8_HTTP_STRING_RAW_TARGET,
    SL_V8_HTTP_STRING_REASON,
    SL_V8_HTTP_STRING_REQUEST,
    SL_V8_HTTP_STRING_REQUEST_ID,
    SL_V8_HTTP_STRING_ROUTE,
    SL_V8_HTTP_STRING_ROUTE_NAME,
    SL_V8_HTTP_STRING_ROUTE_PATTERN,
    SL_V8_HTTP_STRING_SCHEME,
    SL_V8_HTTP_STRING_SECURE,
    SL_V8_HTTP_STRING_SIGNAL,
    SL_V8_HTTP_STRING_SLOPPY_RESULT,
    SL_V8_HTTP_STRING_STATUS,
    SL_V8_HTTP_STRING_TEXT,
    SL_V8_HTTP_STRING_THROW_IF_ABORTED,
    SL_V8_HTTP_STRING_TRACE,
    SL_V8_HTTP_STRING_DEBUG,
    SL_V8_HTTP_STRING_INFO,
    SL_V8_HTTP_STRING_WARN,
    SL_V8_HTTP_STRING_ERROR,
    SL_V8_HTTP_STRING_IS_ENABLED,
    SL_V8_HTTP_STRING_FOR_CATEGORY,
    SL_V8_HTTP_STRING_COUNT
} SlV8HttpStringKey;

typedef enum SlV8HttpPrivateKey
{
    SL_V8_HTTP_PRIVATE_ABORTED = 0,
    SL_V8_HTTP_PRIVATE_BODY,
    SL_V8_HTTP_PRIVATE_BODY_BYTES,
    SL_V8_HTTP_PRIVATE_BODY_CONSUMED,
    SL_V8_HTTP_PRIVATE_BODY_KIND,
    SL_V8_HTTP_PRIVATE_HEADER_SNAPSHOT,
    SL_V8_HTTP_PRIVATE_LOG_CATEGORY,
    SL_V8_HTTP_PRIVATE_LOG_REQUEST_ID,
    SL_V8_HTTP_PRIVATE_LOG_ROUTE_NAME,
    SL_V8_HTTP_PRIVATE_LOG_ROUTE_PATTERN,
    SL_V8_HTTP_PRIVATE_REASON,
    SL_V8_HTTP_PRIVATE_COUNT
} SlV8HttpPrivateKey;

typedef enum SlV8HttpFunctionKey
{
    SL_V8_HTTP_FUNCTION_BODY_BYTES = 0,
    SL_V8_HTTP_FUNCTION_BODY_JSON,
    SL_V8_HTTP_FUNCTION_BODY_TEXT,
    SL_V8_HTTP_FUNCTION_HEADERS_ENTRIES,
    SL_V8_HTTP_FUNCTION_HEADERS_GET,
    SL_V8_HTTP_FUNCTION_REQUEST_BYTES,
    SL_V8_HTTP_FUNCTION_REQUEST_JSON,
    SL_V8_HTTP_FUNCTION_REQUEST_TEXT,
    SL_V8_HTTP_FUNCTION_SIGNAL_THROW_IF_ABORTED,
    SL_V8_HTTP_FUNCTION_LOG_TRACE,
    SL_V8_HTTP_FUNCTION_LOG_DEBUG,
    SL_V8_HTTP_FUNCTION_LOG_INFO,
    SL_V8_HTTP_FUNCTION_LOG_WARN,
    SL_V8_HTTP_FUNCTION_LOG_ERROR,
    SL_V8_HTTP_FUNCTION_LOG_IS_ENABLED,
    SL_V8_HTTP_FUNCTION_LOG_FOR_CATEGORY,
    SL_V8_HTTP_FUNCTION_COUNT
} SlV8HttpFunctionKey;

typedef enum SlV8HttpPrototypeKey
{
    SL_V8_HTTP_PROTOTYPE_BODY = 0,
    SL_V8_HTTP_PROTOTYPE_HEADERS,
    SL_V8_HTTP_PROTOTYPE_REQUEST,
    SL_V8_HTTP_PROTOTYPE_SIGNAL,
    SL_V8_HTTP_PROTOTYPE_LOGGER,
    SL_V8_HTTP_PROTOTYPE_COUNT
} SlV8HttpPrototypeKey;

struct SlV8Engine
{
    v8::ArrayBuffer::Allocator* allocator = nullptr;
    v8::Isolate* isolate = nullptr;
    SlArena* arena = nullptr;
    v8::Global<v8::Context> context;
    std::array<v8::Global<v8::String>, SL_V8_HTTP_STRING_COUNT> http_strings = {};
    std::array<v8::Global<v8::Private>, SL_V8_HTTP_PRIVATE_COUNT> http_private_keys = {};
    std::array<v8::Global<v8::Function>, SL_V8_HTTP_FUNCTION_COUNT> http_functions = {};
    v8::Global<v8::Function> http_log_noop_function;
    std::array<v8::Global<v8::Object>, SL_V8_HTTP_PROTOTYPE_COUNT> http_prototypes = {};
    std::unordered_map<uint32_t, v8::Global<v8::Function>> handlers;
    std::unordered_map<uint32_t, v8::Global<v8::Function>>* pending_handlers = nullptr;
    std::thread::id owner_thread;
    /* Non-owning app metadata; both referenced objects must outlive this engine. */
    const SlPlan* plan = nullptr;
    const SlCapabilityRegistry* capabilities = nullptr;
    const SlFsPolicy* filesystem_policy = nullptr;
    SlLogRuntime* logging = nullptr;
    SlBytes source_map = {};
    SlStr source_map_source_name = {};
    bool has_runtime_features = false;
    SlRuntimeFeatureSet runtime_features = {};
    std::array<SlResourceEntry, 64U> resource_entries = {};
    SlResourceTable resources = {};
    std::array<SlAsyncCompletion, 64U> async_completions = {};
    SlAsyncLoop* async_loop = nullptr;
    std::mutex time_mutex;
    std::condition_variable time_cv;
    std::vector<std::shared_ptr<SlV8TimeRequest>> time_requests;
    std::thread time_scheduler;
    bool time_scheduler_started = false;
    bool time_shutting_down = false;
    std::mutex crypto_mutex;
    std::vector<std::shared_ptr<SlV8CryptoPasswordRequest>> crypto_password_requests;
    bool crypto_shutting_down = false;
    std::mutex net_mutex;
    std::vector<std::shared_ptr<SlV8NetRequest>> net_requests;
    bool net_shutting_down = false;
    std::mutex os_mutex;
    std::vector<std::shared_ptr<SlV8OsRequest>> os_requests;
    bool os_shutting_down = false;
    std::mutex workers_mutex;
    std::vector<std::shared_ptr<SlV8WorkerRequest>> worker_requests;
    std::vector<std::shared_ptr<SlV8JsWorker>> js_workers;
    uint32_t next_worker_id = 1U;
    bool workers_shutting_down = false;
    SlProviderInstanceExecutor fs_executor = {};
    std::array<SlProviderExecutorSlot, 32U> fs_slots = {};
    bool fs_executor_initialized = false;
    SlProviderInstanceExecutor sqlite_executor = {};
    std::array<SlProviderExecutorSlot, 32U> sqlite_slots = {};
    bool sqlite_executor_initialized = false;
};

bool sl_v8_runtime_feature_active(const SlV8Engine* backend, SlRuntimeFeatureId id);

bool sl_v8_install_provider_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context,
                                       v8::Local<v8::Object> data);

bool sl_v8_install_fs_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context,
                                 v8::Local<v8::Object> sloppy);

bool sl_v8_install_time_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context,
                                   v8::Local<v8::Object> sloppy);
void sl_v8_time_dispose(SlV8Engine* backend);

bool sl_v8_install_crypto_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context,
                                     v8::Local<v8::Object> sloppy);
void sl_v8_crypto_dispose(SlV8Engine* backend);

bool sl_v8_install_net_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context,
                                  v8::Local<v8::Object> sloppy);
void sl_v8_net_dispose(SlV8Engine* backend);

bool sl_v8_install_os_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context,
                                 v8::Local<v8::Object> sloppy);
void sl_v8_os_dispose(SlV8Engine* backend);

bool sl_v8_install_codec_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context,
                                    v8::Local<v8::Object> sloppy);

bool sl_v8_install_workers_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context,
                                      v8::Local<v8::Object> sloppy);
void sl_v8_workers_dispose(SlV8Engine* backend);

bool sl_v8_install_sqlite_intrinsics(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                     v8::Local<v8::Object> data);
bool sl_v8_install_postgres_intrinsics(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                       v8::Local<v8::Object> data);
bool sl_v8_install_sqlserver_intrinsics(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                        v8::Local<v8::Object> data);

bool sl_v8_make_http_context_object(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                    const SlHttpRequestContext* request_context,
                                    v8::Local<v8::Object>* out);

SlStatus sl_v8_convert_http_handler_result(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                           SlEngine* engine, SlArena* arena,
                                           v8::Local<v8::Value> js_result,
                                           SlEngineResult* out_result, SlDiag* out_diag);

#endif
