#ifndef SLOPPY_EXECUTION_DOMAIN_H
#define SLOPPY_EXECUTION_DOMAIN_H

#include "sloppy/string.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SlExecutionDomain
{
    SL_EXECUTION_DOMAIN_NONE = 0,
    SL_EXECUTION_DOMAIN_V8_OWNER_THREAD = 1,
    SL_EXECUTION_DOMAIN_LIBUV_EVENT_LOOP = 2,
    SL_EXECUTION_DOMAIN_HTTP_RUNTIME_CALLBACK = 3,
    SL_EXECUTION_DOMAIN_ASYNC_COMPLETION_QUEUE = 4,
    SL_EXECUTION_DOMAIN_PROVIDER_EXECUTOR_WORKER = 5,
    SL_EXECUTION_DOMAIN_BLOCKING_OFFLOAD_WORKER = 6,
    SL_EXECUTION_DOMAIN_APP_REQUEST_LIFECYCLE = 7
} SlExecutionDomain;

/*
 * Fixed ENGINE-26 execution-domain policy.
 *
 * This is a small source-of-truth table for tests, docs, and future runtime boundary
 * checks. It is not a dynamic feature registry and does not carry per-request state.
 */
SlStr sl_execution_domain_name(SlExecutionDomain domain);
bool sl_execution_domain_is_supported(SlExecutionDomain domain);
bool sl_execution_domain_may_enter_v8(SlExecutionDomain domain);
bool sl_execution_domain_requires_owned_cross_thread_data(SlExecutionDomain domain);
bool sl_execution_domain_may_run_blocking_work(SlExecutionDomain domain);
bool sl_execution_domain_requires_owner_thread_dispatch_for_js(SlExecutionDomain domain);

#ifdef __cplusplus
}
#endif

#endif
