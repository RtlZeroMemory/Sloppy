#include "sloppy/execution_domain.h"

#include <stddef.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_str(SlStr actual, SlStr expected)
{
    return expect_true(sl_str_equal(actual, expected));
}

static SlExecutionDomain unsupported_domain_for_test(void)
{
    /* Intentionally exercises lookup behavior for future/invalid enum values. */
    /* sloppy-analysis-suppress: #805 invalid enum test; remove when helper lands */
    /* NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) */
    return (SlExecutionDomain)999;
}

static int test_domain_names_are_stable(void)
{
    if (expect_str(sl_execution_domain_name(SL_EXECUTION_DOMAIN_V8_OWNER_THREAD),
                   sl_str_from_cstr("v8-owner-thread")) != 0 ||
        expect_str(sl_execution_domain_name(SL_EXECUTION_DOMAIN_LIBUV_EVENT_LOOP),
                   sl_str_from_cstr("libuv-event-loop")) != 0 ||
        expect_str(sl_execution_domain_name(SL_EXECUTION_DOMAIN_HTTP_RUNTIME_CALLBACK),
                   sl_str_from_cstr("http-runtime-callback")) != 0 ||
        expect_str(sl_execution_domain_name(SL_EXECUTION_DOMAIN_ASYNC_COMPLETION_QUEUE),
                   sl_str_from_cstr("async-completion-queue")) != 0 ||
        expect_str(sl_execution_domain_name(SL_EXECUTION_DOMAIN_PROVIDER_EXECUTOR_WORKER),
                   sl_str_from_cstr("provider-executor-worker")) != 0 ||
        expect_str(sl_execution_domain_name(SL_EXECUTION_DOMAIN_BLOCKING_OFFLOAD_WORKER),
                   sl_str_from_cstr("blocking-offload-worker")) != 0 ||
        expect_str(sl_execution_domain_name(SL_EXECUTION_DOMAIN_APP_REQUEST_LIFECYCLE),
                   sl_str_from_cstr("app-request-lifecycle")) != 0 ||
        expect_str(sl_execution_domain_name(SL_EXECUTION_DOMAIN_NONE), sl_str_from_cstr("none")) !=
            0 ||
        expect_str(sl_execution_domain_name(unsupported_domain_for_test()),
                   sl_str_from_cstr("unknown")) != 0)
    {
        return 1;
    }

    return 0;
}

static int test_only_v8_owner_domain_may_enter_v8(void)
{
    SlExecutionDomain domains[] = {
        SL_EXECUTION_DOMAIN_V8_OWNER_THREAD,          SL_EXECUTION_DOMAIN_LIBUV_EVENT_LOOP,
        SL_EXECUTION_DOMAIN_HTTP_RUNTIME_CALLBACK,    SL_EXECUTION_DOMAIN_ASYNC_COMPLETION_QUEUE,
        SL_EXECUTION_DOMAIN_PROVIDER_EXECUTOR_WORKER, SL_EXECUTION_DOMAIN_BLOCKING_OFFLOAD_WORKER,
        SL_EXECUTION_DOMAIN_APP_REQUEST_LIFECYCLE,
    };
    size_t index = 0U;

    for (index = 0U; index < sizeof(domains) / sizeof(domains[0]); index += 1U) {
        bool expected = domains[index] == SL_EXECUTION_DOMAIN_V8_OWNER_THREAD;
        if (sl_execution_domain_may_enter_v8(domains[index]) != expected) {
            return 10 + (int)index;
        }
    }

    return 0;
}

static int test_cross_thread_domains_require_owned_data(void)
{
    if (sl_execution_domain_requires_owned_cross_thread_data(SL_EXECUTION_DOMAIN_V8_OWNER_THREAD) ||
        sl_execution_domain_requires_owned_cross_thread_data(
            SL_EXECUTION_DOMAIN_APP_REQUEST_LIFECYCLE) ||
        !sl_execution_domain_requires_owned_cross_thread_data(
            SL_EXECUTION_DOMAIN_LIBUV_EVENT_LOOP) ||
        !sl_execution_domain_requires_owned_cross_thread_data(
            SL_EXECUTION_DOMAIN_HTTP_RUNTIME_CALLBACK) ||
        !sl_execution_domain_requires_owned_cross_thread_data(
            SL_EXECUTION_DOMAIN_ASYNC_COMPLETION_QUEUE) ||
        !sl_execution_domain_requires_owned_cross_thread_data(
            SL_EXECUTION_DOMAIN_PROVIDER_EXECUTOR_WORKER) ||
        !sl_execution_domain_requires_owned_cross_thread_data(
            SL_EXECUTION_DOMAIN_BLOCKING_OFFLOAD_WORKER))
    {
        return 20;
    }

    return 0;
}

static int test_blocking_and_js_dispatch_policy(void)
{
    if (sl_execution_domain_may_run_blocking_work(SL_EXECUTION_DOMAIN_V8_OWNER_THREAD) ||
        sl_execution_domain_may_run_blocking_work(SL_EXECUTION_DOMAIN_LIBUV_EVENT_LOOP) ||
        sl_execution_domain_may_run_blocking_work(SL_EXECUTION_DOMAIN_HTTP_RUNTIME_CALLBACK) ||
        sl_execution_domain_may_run_blocking_work(SL_EXECUTION_DOMAIN_ASYNC_COMPLETION_QUEUE) ||
        sl_execution_domain_may_run_blocking_work(SL_EXECUTION_DOMAIN_APP_REQUEST_LIFECYCLE) ||
        !sl_execution_domain_may_run_blocking_work(SL_EXECUTION_DOMAIN_PROVIDER_EXECUTOR_WORKER) ||
        !sl_execution_domain_may_run_blocking_work(SL_EXECUTION_DOMAIN_BLOCKING_OFFLOAD_WORKER))
    {
        return 30;
    }

    if (sl_execution_domain_requires_owner_thread_dispatch_for_js(
            SL_EXECUTION_DOMAIN_V8_OWNER_THREAD) ||
        sl_execution_domain_requires_owner_thread_dispatch_for_js(
            SL_EXECUTION_DOMAIN_APP_REQUEST_LIFECYCLE) ||
        !sl_execution_domain_requires_owner_thread_dispatch_for_js(
            SL_EXECUTION_DOMAIN_LIBUV_EVENT_LOOP) ||
        !sl_execution_domain_requires_owner_thread_dispatch_for_js(
            SL_EXECUTION_DOMAIN_HTTP_RUNTIME_CALLBACK) ||
        !sl_execution_domain_requires_owner_thread_dispatch_for_js(
            SL_EXECUTION_DOMAIN_ASYNC_COMPLETION_QUEUE) ||
        !sl_execution_domain_requires_owner_thread_dispatch_for_js(
            SL_EXECUTION_DOMAIN_PROVIDER_EXECUTOR_WORKER) ||
        !sl_execution_domain_requires_owner_thread_dispatch_for_js(
            SL_EXECUTION_DOMAIN_BLOCKING_OFFLOAD_WORKER))
    {
        return 31;
    }

    return 0;
}

static int test_supported_domain_classification(void)
{
    if (!sl_execution_domain_is_supported(SL_EXECUTION_DOMAIN_V8_OWNER_THREAD) ||
        !sl_execution_domain_is_supported(SL_EXECUTION_DOMAIN_LIBUV_EVENT_LOOP) ||
        !sl_execution_domain_is_supported(SL_EXECUTION_DOMAIN_HTTP_RUNTIME_CALLBACK) ||
        !sl_execution_domain_is_supported(SL_EXECUTION_DOMAIN_ASYNC_COMPLETION_QUEUE) ||
        !sl_execution_domain_is_supported(SL_EXECUTION_DOMAIN_PROVIDER_EXECUTOR_WORKER) ||
        !sl_execution_domain_is_supported(SL_EXECUTION_DOMAIN_BLOCKING_OFFLOAD_WORKER) ||
        !sl_execution_domain_is_supported(SL_EXECUTION_DOMAIN_APP_REQUEST_LIFECYCLE) ||
        sl_execution_domain_is_supported(SL_EXECUTION_DOMAIN_NONE) ||
        sl_execution_domain_is_supported(unsupported_domain_for_test()))
    {
        return 40;
    }

    return 0;
}

int main(void)
{
    int result = test_domain_names_are_stable();

    if (result != 0) {
        return result;
    }

    result = test_only_v8_owner_domain_may_enter_v8();
    if (result != 0) {
        return result;
    }

    result = test_cross_thread_domains_require_owned_data();
    if (result != 0) {
        return result;
    }

    result = test_blocking_and_js_dispatch_policy();
    if (result != 0) {
        return result;
    }

    return test_supported_domain_classification();
}
