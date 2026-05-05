#include "sloppy/execution_domain.h"

SlStr sl_execution_domain_name(SlExecutionDomain domain)
{
    switch (domain) {
    case SL_EXECUTION_DOMAIN_V8_OWNER_THREAD:
        return sl_str_from_cstr("v8-owner-thread");
    case SL_EXECUTION_DOMAIN_LIBUV_EVENT_LOOP:
        return sl_str_from_cstr("libuv-event-loop");
    case SL_EXECUTION_DOMAIN_HTTP_RUNTIME_CALLBACK:
        return sl_str_from_cstr("http-runtime-callback");
    case SL_EXECUTION_DOMAIN_ASYNC_COMPLETION_QUEUE:
        return sl_str_from_cstr("async-completion-queue");
    case SL_EXECUTION_DOMAIN_PROVIDER_EXECUTOR_WORKER:
        return sl_str_from_cstr("provider-executor-worker");
    case SL_EXECUTION_DOMAIN_BLOCKING_OFFLOAD_WORKER:
        return sl_str_from_cstr("blocking-offload-worker");
    case SL_EXECUTION_DOMAIN_APP_REQUEST_LIFECYCLE:
        return sl_str_from_cstr("app-request-lifecycle");
    default:
        return sl_str_from_cstr("unknown");
    }
}

bool sl_execution_domain_is_supported(SlExecutionDomain domain)
{
    return domain == SL_EXECUTION_DOMAIN_V8_OWNER_THREAD ||
           domain == SL_EXECUTION_DOMAIN_LIBUV_EVENT_LOOP ||
           domain == SL_EXECUTION_DOMAIN_HTTP_RUNTIME_CALLBACK ||
           domain == SL_EXECUTION_DOMAIN_ASYNC_COMPLETION_QUEUE ||
           domain == SL_EXECUTION_DOMAIN_PROVIDER_EXECUTOR_WORKER ||
           domain == SL_EXECUTION_DOMAIN_BLOCKING_OFFLOAD_WORKER ||
           domain == SL_EXECUTION_DOMAIN_APP_REQUEST_LIFECYCLE;
}

bool sl_execution_domain_may_enter_v8(SlExecutionDomain domain)
{
    return domain == SL_EXECUTION_DOMAIN_V8_OWNER_THREAD;
}

bool sl_execution_domain_requires_owned_cross_thread_data(SlExecutionDomain domain)
{
    return domain == SL_EXECUTION_DOMAIN_LIBUV_EVENT_LOOP ||
           domain == SL_EXECUTION_DOMAIN_HTTP_RUNTIME_CALLBACK ||
           domain == SL_EXECUTION_DOMAIN_ASYNC_COMPLETION_QUEUE ||
           domain == SL_EXECUTION_DOMAIN_PROVIDER_EXECUTOR_WORKER ||
           domain == SL_EXECUTION_DOMAIN_BLOCKING_OFFLOAD_WORKER;
}

bool sl_execution_domain_may_run_blocking_work(SlExecutionDomain domain)
{
    return domain == SL_EXECUTION_DOMAIN_PROVIDER_EXECUTOR_WORKER ||
           domain == SL_EXECUTION_DOMAIN_BLOCKING_OFFLOAD_WORKER;
}

bool sl_execution_domain_requires_owner_thread_dispatch_for_js(SlExecutionDomain domain)
{
    return domain == SL_EXECUTION_DOMAIN_LIBUV_EVENT_LOOP ||
           domain == SL_EXECUTION_DOMAIN_HTTP_RUNTIME_CALLBACK ||
           domain == SL_EXECUTION_DOMAIN_ASYNC_COMPLETION_QUEUE ||
           domain == SL_EXECUTION_DOMAIN_PROVIDER_EXECUTOR_WORKER ||
           domain == SL_EXECUTION_DOMAIN_BLOCKING_OFFLOAD_WORKER;
}
