/*
 * src/core/capability.c
 *
 * Builds a startup-time capability registry from validated Sloppy Plan metadata and
 * provides explicit check hooks for provider bridge boundaries. The registry is immutable,
 * caller-owned, and borrows parsed plan storage; it does not open providers, inspect the
 * filesystem, perform network I/O, or create an OS sandbox.
 *
 * Safety invariants:
 * - no global mutable state;
 * - all metadata strings are borrowed from the parsed Plan lifetime;
 * - denied checks emit deterministic diagnostics without secret-bearing config values;
 * - callers must run checks before invoking provider work.
 *
 * Tests: tests/unit/core/test_capability.c.
 */
#include "sloppy/capability.h"

#include "sloppy/builder.h"
#include "sloppy/checked_math.h"

static SlStr sl_capability_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static bool sl_capability_kind_is(const SlPlanCapability* capability, const char* kind)
{
    return capability != NULL && sl_str_equal(capability->kind, sl_str_from_cstr(kind));
}

static SlCapabilityAccess sl_capability_access_from_plan(SlStr access)
{
    if (sl_str_equal(access, sl_str_from_cstr("read"))) {
        return SL_CAPABILITY_ACCESS_READ;
    }
    if (sl_str_equal(access, sl_str_from_cstr("write"))) {
        return SL_CAPABILITY_ACCESS_WRITE;
    }
    if (sl_str_equal(access, sl_str_from_cstr("readwrite"))) {
        return SL_CAPABILITY_ACCESS_READWRITE;
    }
    if (sl_str_equal(access, sl_str_from_cstr("connect"))) {
        return SL_CAPABILITY_ACCESS_CONNECT;
    }
    if (sl_str_equal(access, sl_str_from_cstr("listen"))) {
        return SL_CAPABILITY_ACCESS_LISTEN;
    }
    if (sl_str_equal(access, sl_str_from_cstr("connect-listen"))) {
        return SL_CAPABILITY_ACCESS_CONNECT_LISTEN;
    }
    return SL_CAPABILITY_ACCESS_UNKNOWN;
}

static bool sl_capability_token_syntax_valid(SlStr token)
{
    size_t index = 0U;

    if (sl_str_is_empty(token)) {
        return false;
    }

    for (index = 0U; index < token.length; index += 1U) {
        char ch = token.ptr[index];
        bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                  (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

static bool sl_capability_shape_valid(const SlPlanCapability* capability)
{
    if (capability == NULL || !sl_capability_token_syntax_valid(capability->token) ||
        sl_str_is_empty(capability->kind) || sl_str_is_empty(capability->access) ||
        !sl_plan_capability_kind_supported(capability->kind) ||
        !sl_plan_capability_access_supported(capability->kind, capability->access))
    {
        return false;
    }
    if (sl_capability_kind_is(capability, "database")) {
        return !sl_str_is_empty(capability->provider);
    }
    return sl_str_is_empty(capability->provider);
}

static bool sl_capability_provider_shape_valid(const SlPlanDataProvider* provider)
{
    if (provider == NULL || !sl_capability_token_syntax_valid(provider->token) ||
        sl_str_is_empty(provider->provider) || !sl_plan_provider_supported(provider->provider))
    {
        return false;
    }
    if (!sl_str_is_empty(provider->service) && !sl_capability_token_syntax_valid(provider->service))
    {
        return false;
    }
    if (!sl_str_is_empty(provider->capability) &&
        !sl_capability_token_syntax_valid(provider->capability))
    {
        return false;
    }
    return true;
}

static const SlPlanDataProvider* sl_capability_find_provider(const SlCapabilityRegistry* registry,
                                                             SlStr token)
{
    size_t index = 0U;

    if (registry == NULL || sl_str_is_empty(token) ||
        (registry->data_provider_count > 0U && registry->data_providers == NULL))
    {
        return NULL;
    }
    for (index = 0U; index < registry->data_provider_count; index += 1U) {
        if (sl_str_equal(registry->data_providers[index].token, token)) {
            return &registry->data_providers[index];
        }
    }
    return NULL;
}

static bool sl_capability_plan_has_provider(const SlPlan* plan, SlStr token)
{
    size_t index = 0U;

    if (plan == NULL || sl_str_is_empty(token) ||
        (plan->data_provider_count > 0U && plan->data_providers == NULL))
    {
        return false;
    }
    for (index = 0U; index < plan->data_provider_count; index += 1U) {
        if (sl_str_equal(plan->data_providers[index].token, token)) {
            return true;
        }
    }
    return false;
}

static bool sl_capability_plan_has_capability(const SlPlan* plan, SlStr token)
{
    size_t index = 0U;

    if (plan == NULL || sl_str_is_empty(token) ||
        (plan->capability_count > 0U && plan->capabilities == NULL))
    {
        return false;
    }
    for (index = 0U; index < plan->capability_count; index += 1U) {
        if (sl_str_equal(plan->capabilities[index].token, token)) {
            return true;
        }
    }
    return false;
}

static SlStr sl_capability_operation_name(SlCapabilityOperation operation)
{
    switch (operation) {
    case SL_CAPABILITY_OPERATION_READ:
        return sl_capability_literal("read", sizeof("read") - 1U);
    case SL_CAPABILITY_OPERATION_WRITE:
        return sl_capability_literal("write", sizeof("write") - 1U);
    case SL_CAPABILITY_OPERATION_CONNECT:
        return sl_capability_literal("connect", sizeof("connect") - 1U);
    case SL_CAPABILITY_OPERATION_LISTEN:
        return sl_capability_literal("listen", sizeof("listen") - 1U);
    case SL_CAPABILITY_OPERATION_READWRITE:
        return sl_capability_literal("readwrite", sizeof("readwrite") - 1U);
    default:
        return sl_capability_literal("unsupported", sizeof("unsupported") - 1U);
    }
}

static bool sl_capability_access_allows(SlCapabilityAccess actual, SlCapabilityOperation operation)
{
    if (operation == SL_CAPABILITY_OPERATION_READ) {
        return actual == SL_CAPABILITY_ACCESS_READ || actual == SL_CAPABILITY_ACCESS_READWRITE;
    }
    if (operation == SL_CAPABILITY_OPERATION_WRITE) {
        return actual == SL_CAPABILITY_ACCESS_WRITE || actual == SL_CAPABILITY_ACCESS_READWRITE;
    }
    if (operation == SL_CAPABILITY_OPERATION_READWRITE) {
        return actual == SL_CAPABILITY_ACCESS_READWRITE;
    }
    if (operation == SL_CAPABILITY_OPERATION_CONNECT) {
        return actual == SL_CAPABILITY_ACCESS_CONNECT ||
               actual == SL_CAPABILITY_ACCESS_CONNECT_LISTEN;
    }
    if (operation == SL_CAPABILITY_OPERATION_LISTEN) {
        return actual == SL_CAPABILITY_ACCESS_LISTEN ||
               actual == SL_CAPABILITY_ACCESS_CONNECT_LISTEN;
    }
    return false;
}

static SlStatus sl_capability_add_hint_pair(SlDiagBuilder* builder, SlStr prefix, SlStr value)
{
    SlStringBuilder hint_builder = {0};
    SlStr hint = {0};
    size_t hint_length = 0U;
    SlStatus status;

    if (builder == NULL || builder->arena == NULL || prefix.ptr == NULL ||
        (value.length > 0U && value.ptr == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_checked_add_size(prefix.length, value.length, &hint_length);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_string_builder_init_arena(&hint_builder, builder->arena, hint_length, hint_length);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_append_str(&hint_builder, prefix);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_append_str(&hint_builder, value);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    hint = sl_string_builder_view(&hint_builder);
    return sl_diag_builder_add_hint_owned(builder, hint);
}

static SlStatus sl_capability_denied(SlArena* arena, SlDiag* out_diag, SlStr token, SlStr kind,
                                     SlCapabilityOperation operation, SlStr actual_access,
                                     SlStr provider, SlStr message)
{
    SlDiagBuilder builder;
    SlStatus status;

    if (out_diag == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    status = sl_diag_builder_init(&builder, arena, SL_DIAG_SEVERITY_ERROR,
                                  SL_DIAG_PERMISSION_DENIED, message);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!sl_str_is_empty(token)) {
        status = sl_capability_add_hint_pair(
            &builder, sl_capability_literal("token: ", sizeof("token: ") - 1U), token);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    if (!sl_str_is_empty(kind) && (sl_str_is_empty(provider) || sl_str_is_empty(actual_access))) {
        status = sl_capability_add_hint_pair(
            &builder, sl_capability_literal("kind: ", sizeof("kind: ") - 1U), kind);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    status = sl_capability_add_hint_pair(
        &builder, sl_capability_literal("operation: ", sizeof("operation: ") - 1U),
        sl_capability_operation_name(operation));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!sl_str_is_empty(actual_access)) {
        status = sl_capability_add_hint_pair(
            &builder, sl_capability_literal("actual access: ", sizeof("actual access: ") - 1U),
            actual_access);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    if (!sl_str_is_empty(provider)) {
        status = sl_capability_add_hint_pair(
            &builder, sl_capability_literal("provider: ", sizeof("provider: ") - 1U), provider);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    status = sl_diag_builder_finish(&builder, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_status_from_code(SL_STATUS_INVALID_STATE);
}

static SlStatus sl_capability_check_common(const SlCapabilityRegistry* registry,
                                           SlArena* diag_arena, SlStr token, const char* kind,
                                           SlCapabilityOperation operation,
                                           const SlPlanCapability** out_capability,
                                           SlDiag* out_diag)
{
    const SlPlanCapability* capability = NULL;
    SlStatus status;

    if (out_capability == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_capability = NULL;

    if (diag_arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }

    status = sl_capability_registry_find(registry, token, &capability);
    if (sl_status_code(status) == SL_STATUS_OUT_OF_RANGE) {
        return sl_capability_denied(
            diag_arena, out_diag, token, sl_str_from_cstr(kind), operation, sl_str_empty(),
            sl_str_empty(),
            sl_capability_literal("capability access denied: missing capability",
                                  sizeof("capability access denied: missing capability") - 1U));
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (capability == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    if (!sl_capability_kind_is(capability, kind)) {
        return sl_capability_denied(
            diag_arena, out_diag, token, capability->kind, operation, capability->access,
            capability->provider,
            sl_capability_literal("capability access denied: wrong capability kind",
                                  sizeof("capability access denied: wrong capability kind") - 1U));
    }
    if (!sl_capability_access_allows(sl_capability_access_from_plan(capability->access), operation))
    {
        return sl_capability_denied(
            diag_arena, out_diag, token, capability->kind, operation, capability->access,
            capability->provider,
            sl_capability_literal("capability access denied: insufficient access",
                                  sizeof("capability access denied: insufficient access") - 1U));
    }

    *out_capability = capability;
    return sl_status_ok();
}

SlStatus sl_capability_registry_init_from_plan(const SlPlan* plan, SlCapabilityRegistry* out)
{
    size_t index = 0U;

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlCapabilityRegistry){0};
    if (plan == NULL || (plan->data_provider_count > 0U && plan->data_providers == NULL) ||
        (plan->capability_count > 0U && plan->capabilities == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (sl_plan_has_duplicate_data_provider_tokens(plan)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (sl_plan_has_duplicate_capability_tokens(plan)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    for (index = 0U; index < plan->data_provider_count; index += 1U) {
        if (!sl_capability_provider_shape_valid(&plan->data_providers[index])) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        if (!sl_str_is_empty(plan->data_providers[index].capability) &&
            !sl_capability_plan_has_capability(plan, plan->data_providers[index].capability))
        {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
    }
    for (index = 0U; index < plan->capability_count; index += 1U) {
        if (!sl_capability_shape_valid(&plan->capabilities[index])) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        if (sl_capability_kind_is(&plan->capabilities[index], "database") &&
            !sl_capability_plan_has_provider(plan, plan->capabilities[index].provider))
        {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
    }
    out->data_providers = plan->data_providers;
    out->data_provider_count = plan->data_provider_count;
    out->capabilities = plan->capabilities;
    out->capability_count = plan->capability_count;
    return sl_status_ok();
}

SlStatus sl_capability_registry_find(const SlCapabilityRegistry* registry, SlStr token,
                                     const SlPlanCapability** out)
{
    size_t index = 0U;

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = NULL;
    if (registry == NULL || sl_str_is_empty(token) ||
        (registry->capability_count > 0U && registry->capabilities == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    for (index = 0U; index < registry->capability_count; index += 1U) {
        if (sl_str_equal(registry->capabilities[index].token, token)) {
            *out = &registry->capabilities[index];
            return sl_status_ok();
        }
    }
    return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
}

SlStatus sl_capability_check_database(const SlCapabilityRegistry* registry, SlArena* diag_arena,
                                      SlStr token, SlCapabilityOperation operation, SlStr provider,
                                      SlDiag* out_diag)
{
    const SlPlanCapability* capability = NULL;
    SlStatus status = sl_capability_check_common(registry, diag_arena, token, "database", operation,
                                                 &capability, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (capability == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    if (!sl_str_is_empty(capability->provider) && !sl_str_equal(capability->provider, provider)) {
        return sl_capability_denied(
            diag_arena, out_diag, token, capability->kind, operation, capability->access, provider,
            sl_capability_literal("capability access denied: provider mismatch",
                                  sizeof("capability access denied: provider mismatch") - 1U));
    }
    return sl_status_ok();
}

SlStatus sl_capability_check_database_provider(const SlCapabilityRegistry* registry,
                                               SlArena* diag_arena, SlStr token,
                                               SlCapabilityOperation operation, SlStr provider_kind,
                                               SlDiag* out_diag)
{
    const SlPlanCapability* capability = NULL;
    const SlPlanDataProvider* provider = NULL;
    SlStatus status = sl_capability_check_common(registry, diag_arena, token, "database", operation,
                                                 &capability, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (capability == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    provider = sl_capability_find_provider(registry, capability->provider);
    if (provider == NULL || !sl_str_equal(provider->provider, provider_kind)) {
        return sl_capability_denied(
            diag_arena, out_diag, token, capability->kind, operation, capability->access,
            provider_kind,
            sl_capability_literal("capability access denied: provider mismatch",
                                  sizeof("capability access denied: provider mismatch") - 1U));
    }
    return sl_status_ok();
}

SlStatus sl_capability_check_filesystem(const SlCapabilityRegistry* registry, SlArena* diag_arena,
                                        SlStr token, SlCapabilityOperation operation,
                                        SlDiag* out_diag)
{
    const SlPlanCapability* capability = NULL;
    return sl_capability_check_common(registry, diag_arena, token, "filesystem", operation,
                                      &capability, out_diag);
}

SlStatus sl_capability_check_network(const SlCapabilityRegistry* registry, SlArena* diag_arena,
                                     SlStr token, SlCapabilityOperation operation, SlDiag* out_diag)
{
    const SlPlanCapability* capability = NULL;
    return sl_capability_check_common(registry, diag_arena, token, "network", operation,
                                      &capability, out_diag);
}
