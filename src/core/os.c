/*
 * src/core/os.c
 *
 * Runtime-owned OS policy and Environment admission logic. Platform calls stay behind
 * platform backends; this file owns Sloppy policy, redaction, and diagnostic stability.
 */
#include "sloppy/os.h"

#include "os_platform.h"

#include <string.h>

static SlDiag sl_os_diag(SlDiagCode code, SlStr message, SlStr hint)
{
    SlDiag diag = {0};

    diag.severity = SL_DIAG_SEVERITY_ERROR;
    diag.code = code;
    diag.message = message;
    diag.primary_span = sl_source_span_unknown();
    if (!sl_str_is_empty(hint)) {
        diag.hints[0] = hint;
        diag.hint_count = 1U;
    }
    return diag;
}

static SlStatus sl_os_denied(SlDiagCode code, SlDiag* out_diag, SlStr message, SlStr hint)
{
    if (out_diag != NULL) {
        *out_diag = sl_os_diag(code, message, hint);
    }
    return sl_status_from_code(SL_STATUS_INVALID_STATE);
}

static bool sl_os_policy_env_allowed(const SlOsPolicy* policy, SlStr key)
{
    size_t index = 0U;

    if (policy == NULL || policy->mode == SL_OS_POLICY_DEVELOPMENT) {
        return true;
    }
    if (key.length == 0U || key.ptr == NULL) {
        return false;
    }
    for (index = 0U; index < policy->environment_grant_count; index += 1U) {
        if (sl_str_equal(policy->environment_grants[index].key, key)) {
            return true;
        }
    }
    return false;
}

SlOsPolicy sl_os_development_policy(void)
{
    SlOsPolicy policy = {.mode = SL_OS_POLICY_DEVELOPMENT,
                         .allow_system_info = true,
                         .allow_environment_list = true,
                         .environment_list_prefix = sl_str_empty(),
                         .environment_grants = NULL,
                         .environment_grant_count = 0U};
    return policy;
}

SlOsPolicy sl_os_strict_policy(const SlOsEnvironmentGrant* grants, size_t grant_count,
                               bool allow_environment_list, SlStr environment_list_prefix)
{
    SlOsPolicy policy = {.mode = SL_OS_POLICY_STRICT,
                         .allow_system_info = false,
                         .allow_environment_list = allow_environment_list,
                         .environment_list_prefix = environment_list_prefix,
                         .environment_grants = grants,
                         .environment_grant_count = grant_count};
    return policy;
}

bool sl_os_environment_key_is_secret(SlStr key)
{
    static const char* needles[] = {"SECRET", "TOKEN", "PASSWORD", "PASS", "KEY", "PWD"};
    size_t index = 0U;
    size_t offset = 0U;

    if (key.ptr == NULL || key.length == 0U) {
        return false;
    }
    for (index = 0U; index < sizeof(needles) / sizeof(needles[0]); index += 1U) {
        SlStr needle = sl_str_from_cstr(needles[index]);
        if (key.length < needle.length) {
            continue;
        }
        for (offset = 0U; offset <= key.length - needle.length; offset += 1U) {
            size_t inner = 0U;
            for (inner = 0U; inner < needle.length; inner += 1U) {
                char ch = key.ptr[offset + inner];
                if (ch >= 'a' && ch <= 'z') {
                    ch = (char)(ch - ('a' - 'A'));
                }
                if (ch != needle.ptr[inner]) {
                    break;
                }
            }
            if (inner == needle.length) {
                return true;
            }
        }
    }
    return false;
}

SlStatus sl_os_environment_redacted_value(SlArena* arena, SlStr key, SlOwnedStr* out,
                                          SlDiag* out_diag)
{
    SlStatus status;

    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_str_copy_to_arena(arena, sl_str_from_cstr("[REDACTED]"), out);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (sl_os_environment_key_is_secret(key) && out_diag != NULL) {
        *out_diag = sl_os_diag(
            SL_DIAG_OS_ENV_SECRET_REDACTED, sl_str_from_cstr("environment secret redacted"),
            sl_str_from_cstr(
                "Diagnostics may name the environment key but must not print values."));
        out_diag->related[0].message = key;
        out_diag->related[0].span = sl_source_span_unknown();
        out_diag->related_count = 1U;
    }
    return sl_status_ok();
}

SlStatus sl_os_system_info(SlArena* arena, const SlOsPolicy* policy, SlOsSystemInfo* out,
                           SlDiag* out_diag)
{
    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOsSystemInfo){0};
    if (policy != NULL && policy->mode == SL_OS_POLICY_STRICT && !policy->allow_system_info) {
        return sl_os_denied(SL_DIAG_OS_FEATURE_UNAVAILABLE, out_diag,
                            sl_str_from_cstr("OS feature is unavailable"),
                            sl_str_from_cstr("Grant os.info before reading System metadata."));
    }
    return sl_os_platform_system_info(arena, out, out_diag);
}

SlStatus sl_os_environment_get(SlArena* arena, const SlOsPolicy* policy, SlStr key,
                               SlOwnedStr* out_value, bool* out_found, SlDiag* out_diag)
{
    if (arena == NULL || out_value == NULL || out_found == NULL || key.ptr == NULL ||
        key.length == 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_value = (SlOwnedStr){0};
    *out_found = false;
    if (!sl_os_policy_env_allowed(policy, key)) {
        return sl_os_denied(SL_DIAG_OS_ENV_ACCESS_DENIED, out_diag,
                            sl_str_from_cstr("environment variable access denied"),
                            sl_str_from_cstr("Grant env.read for the requested key."));
    }
    return sl_os_platform_environment_get(arena, key, out_value, out_found, out_diag);
}

SlStatus sl_os_environment_has(const SlOsPolicy* policy, SlStr key, bool* out_found,
                               SlDiag* out_diag)
{
    if (out_found == NULL || key.ptr == NULL || key.length == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_found = false;
    if (!sl_os_policy_env_allowed(policy, key)) {
        return sl_os_denied(SL_DIAG_OS_ENV_ACCESS_DENIED, out_diag,
                            sl_str_from_cstr("environment variable access denied"),
                            sl_str_from_cstr("Grant env.read for the requested key."));
    }
    return sl_os_platform_environment_has(key, out_found, out_diag);
}

SlStatus sl_os_environment_list(SlArena* arena, const SlOsPolicy* policy, SlStr prefix,
                                SlOsEnvironmentList* out, SlDiag* out_diag)
{
    SlStr effective_prefix = prefix;

    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOsEnvironmentList){0};
    if (policy != NULL && policy->mode == SL_OS_POLICY_STRICT) {
        if (!policy->allow_environment_list) {
            return sl_os_denied(
                SL_DIAG_OS_ENV_ACCESS_DENIED, out_diag,
                sl_str_from_cstr("environment variable access denied"),
                sl_str_from_cstr("Grant env.list before listing environment keys."));
        }
        if (!sl_str_is_empty(policy->environment_list_prefix)) {
            effective_prefix = policy->environment_list_prefix;
        }
    }
    return sl_os_platform_environment_list(arena, effective_prefix, out, out_diag);
}
