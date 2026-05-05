#include "sloppy/execution_domain.h"

typedef struct SlExecutionDomainPolicy
{
    const char* name;
    size_t name_length;
    SlExecutionDomain domain;
    bool may_enter_v8;
    bool requires_owned_cross_thread_data;
    bool may_run_blocking_work;
    bool requires_owner_thread_dispatch_for_js;
} SlExecutionDomainPolicy;

#define SL_EXECUTION_DOMAIN_POLICY_NAME(value) value, sizeof(value) - 1U

static const SlExecutionDomainPolicy SL_EXECUTION_DOMAIN_POLICIES[] = {
    {SL_EXECUTION_DOMAIN_POLICY_NAME("v8-owner-thread"), SL_EXECUTION_DOMAIN_V8_OWNER_THREAD, true,
     false, false, false},
    {SL_EXECUTION_DOMAIN_POLICY_NAME("libuv-event-loop"), SL_EXECUTION_DOMAIN_LIBUV_EVENT_LOOP,
     false, true, false, true},
    {SL_EXECUTION_DOMAIN_POLICY_NAME("http-runtime-callback"),
     SL_EXECUTION_DOMAIN_HTTP_RUNTIME_CALLBACK, false, true, false, true},
    {SL_EXECUTION_DOMAIN_POLICY_NAME("async-completion-queue"),
     SL_EXECUTION_DOMAIN_ASYNC_COMPLETION_QUEUE, false, true, false, true},
    {SL_EXECUTION_DOMAIN_POLICY_NAME("provider-executor-worker"),
     SL_EXECUTION_DOMAIN_PROVIDER_EXECUTOR_WORKER, false, true, true, true},
    {SL_EXECUTION_DOMAIN_POLICY_NAME("blocking-offload-worker"),
     SL_EXECUTION_DOMAIN_BLOCKING_OFFLOAD_WORKER, false, true, true, true},
    {SL_EXECUTION_DOMAIN_POLICY_NAME("app-request-lifecycle"),
     SL_EXECUTION_DOMAIN_APP_REQUEST_LIFECYCLE, false, false, false, false},
};

_Static_assert(sizeof(SL_EXECUTION_DOMAIN_POLICIES) / sizeof(SL_EXECUTION_DOMAIN_POLICIES[0]) ==
                   (size_t)SL_EXECUTION_DOMAIN_APP_REQUEST_LIFECYCLE,
               "Update SL_EXECUTION_DOMAIN_POLICIES when SlExecutionDomain changes");

static const SlExecutionDomainPolicy* sl_execution_domain_policy_find(SlExecutionDomain domain)
{
    size_t index = 0U;

    for (index = 0U;
         index < sizeof(SL_EXECUTION_DOMAIN_POLICIES) / sizeof(SL_EXECUTION_DOMAIN_POLICIES[0]);
         index += 1U)
    {
        if (SL_EXECUTION_DOMAIN_POLICIES[index].domain == domain) {
            return &SL_EXECUTION_DOMAIN_POLICIES[index];
        }
    }

    return NULL;
}

SlStr sl_execution_domain_name(SlExecutionDomain domain)
{
    if (domain == SL_EXECUTION_DOMAIN_NONE) {
        return sl_str_from_cstr("none");
    }

    const SlExecutionDomainPolicy* policy = sl_execution_domain_policy_find(domain);

    if (policy == NULL) {
        return sl_str_from_cstr("unknown");
    }

    return sl_str_from_parts(policy->name, policy->name_length);
}

bool sl_execution_domain_is_supported(SlExecutionDomain domain)
{
    return sl_execution_domain_policy_find(domain) != NULL;
}

bool sl_execution_domain_may_enter_v8(SlExecutionDomain domain)
{
    const SlExecutionDomainPolicy* policy = sl_execution_domain_policy_find(domain);

    return policy != NULL && policy->may_enter_v8;
}

bool sl_execution_domain_requires_owned_cross_thread_data(SlExecutionDomain domain)
{
    const SlExecutionDomainPolicy* policy = sl_execution_domain_policy_find(domain);

    return policy != NULL && policy->requires_owned_cross_thread_data;
}

bool sl_execution_domain_may_run_blocking_work(SlExecutionDomain domain)
{
    const SlExecutionDomainPolicy* policy = sl_execution_domain_policy_find(domain);

    return policy != NULL && policy->may_run_blocking_work;
}

bool sl_execution_domain_requires_owner_thread_dispatch_for_js(SlExecutionDomain domain)
{
    const SlExecutionDomainPolicy* policy = sl_execution_domain_policy_find(domain);

    return policy != NULL && policy->requires_owner_thread_dispatch_for_js;
}
