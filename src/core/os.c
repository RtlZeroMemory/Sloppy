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
                         .allow_process_run = true,
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
                         .allow_process_run = false,
                         .environment_list_prefix = environment_list_prefix,
                         .environment_grants = grants,
                         .environment_grant_count = grant_count};
    return policy;
}

void sl_os_policy_set_process_run(SlOsPolicy* policy, bool allowed)
{
    if (policy != NULL) {
        policy->allow_process_run = allowed;
    }
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
        key.length == 0U || !sl_status_is_ok(sl_str_validate_no_nul(key)))
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
    if (out_found == NULL || key.ptr == NULL || key.length == 0U ||
        !sl_status_is_ok(sl_str_validate_no_nul(key)))
    {
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

static bool sl_os_process_env_override_valid(const SlOsEnvironmentOverride* entry)
{
    size_t index = 0U;

    if (entry == NULL || entry->key.ptr == NULL || entry->key.length == 0U ||
        entry->value.ptr == NULL)
    {
        return false;
    }
    if (!sl_status_is_ok(sl_str_validate_no_nul(entry->key)) ||
        !sl_status_is_ok(sl_str_validate_no_nul(entry->value)))
    {
        return false;
    }
    for (index = 0U; index < entry->key.length; index += 1U) {
        if (entry->key.ptr[index] == '=') {
            return false;
        }
    }
    return true;
}

SlStatus sl_os_process_run(SlArena* arena, const SlOsPolicy* policy, SlStr command,
                           const SlStr* args, size_t arg_count,
                           const SlOsProcessRunOptions* options, SlOsProcessRunResult* out,
                           SlDiag* out_diag)
{
    size_t index = 0U;
    SlOsProcessRunOptions defaults = {0};

    if (arena == NULL || out == NULL || command.ptr == NULL || command.length == 0U ||
        !sl_status_is_ok(sl_str_validate_no_nul(command)) || (arg_count != 0U && args == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOsProcessRunResult){0};
    if (policy != NULL && policy->mode == SL_OS_POLICY_STRICT && !policy->allow_process_run) {
        return sl_os_denied(
            SL_DIAG_OS_PROCESS_EXECUTION_DENIED, out_diag,
            sl_str_from_cstr("process execution was denied"),
            sl_str_from_cstr("Grant process.run before admitting native process work."));
    }
    for (index = 0U; index < arg_count; index += 1U) {
        if (args[index].ptr == NULL || !sl_status_is_ok(sl_str_validate_no_nul(args[index]))) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
    }
    if (options == NULL) {
        options = &defaults;
    }
    if (!sl_str_is_empty(options->cwd) &&
        (options->cwd.ptr == NULL || !sl_status_is_ok(sl_str_validate_no_nul(options->cwd))))
    {
        return sl_os_denied(SL_DIAG_OS_INVALID_CWD, out_diag,
                            sl_str_from_cstr("process working directory is invalid"),
                            sl_str_from_cstr("Validate cwd before process admission."));
    }
    if (options->environment_override_count != 0U && options->environment_overrides == NULL) {
        return sl_os_denied(SL_DIAG_OS_INVALID_ENV_OVERRIDE, out_diag,
                            sl_str_from_cstr("process environment override is invalid"),
                            sl_str_from_cstr("Environment overrides require key/value entries."));
    }
    for (index = 0U; index < options->environment_override_count; index += 1U) {
        if (!sl_os_process_env_override_valid(&options->environment_overrides[index])) {
            return sl_os_denied(
                SL_DIAG_OS_INVALID_ENV_OVERRIDE, out_diag,
                sl_str_from_cstr("process environment override is invalid"),
                sl_str_from_cstr("Environment override diagnostics must not print values."));
        }
    }
    return sl_os_platform_process_run(arena, command, args, arg_count, options, out, out_diag);
}

static SlStatus sl_os_process_common_validate(const SlOsPolicy* policy, SlStr command,
                                              const SlStr* args, size_t arg_count, SlDiag* out_diag)
{
    size_t index = 0U;

    if (command.ptr == NULL || command.length == 0U ||
        !sl_status_is_ok(sl_str_validate_no_nul(command)) || (arg_count != 0U && args == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (policy != NULL && policy->mode == SL_OS_POLICY_STRICT && !policy->allow_process_run) {
        return sl_os_denied(
            SL_DIAG_OS_PROCESS_EXECUTION_DENIED, out_diag,
            sl_str_from_cstr("process execution was denied"),
            sl_str_from_cstr("Grant process.run before admitting native process work."));
    }
    for (index = 0U; index < arg_count; index += 1U) {
        if (args[index].ptr == NULL || !sl_status_is_ok(sl_str_validate_no_nul(args[index]))) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
    }
    return sl_status_ok();
}

static SlStatus sl_os_process_start_options_validate(const SlOsProcessStartOptions* options,
                                                     SlDiag* out_diag)
{
    size_t index = 0U;

    if (options == NULL) {
        return sl_status_ok();
    }
    if (!sl_str_is_empty(options->cwd) &&
        (options->cwd.ptr == NULL || !sl_status_is_ok(sl_str_validate_no_nul(options->cwd))))
    {
        return sl_os_denied(SL_DIAG_OS_INVALID_CWD, out_diag,
                            sl_str_from_cstr("process working directory is invalid"),
                            sl_str_from_cstr("Validate cwd before process admission."));
    }
    if (options->environment_override_count != 0U && options->environment_overrides == NULL) {
        return sl_os_denied(SL_DIAG_OS_INVALID_ENV_OVERRIDE, out_diag,
                            sl_str_from_cstr("process environment override is invalid"),
                            sl_str_from_cstr("Environment overrides require key/value entries."));
    }
    for (index = 0U; index < options->environment_override_count; index += 1U) {
        if (!sl_os_process_env_override_valid(&options->environment_overrides[index])) {
            return sl_os_denied(
                SL_DIAG_OS_INVALID_ENV_OVERRIDE, out_diag,
                sl_str_from_cstr("process environment override is invalid"),
                sl_str_from_cstr("Environment override diagnostics must not print values."));
        }
    }
    if (options->stdin_mode > SL_OS_PROCESS_PIPE_PIPE ||
        options->stdout_mode > SL_OS_PROCESS_PIPE_PIPE ||
        options->stderr_mode > SL_OS_PROCESS_PIPE_PIPE)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_status_ok();
}

SlStatus sl_os_process_start(SlArena* arena, const SlOsPolicy* policy, SlStr command,
                             const SlStr* args, size_t arg_count,
                             const SlOsProcessStartOptions* options, SlOsProcessHandle** out,
                             SlDiag* out_diag)
{
    SlOsProcessStartOptions defaults = {0};
    SlStatus status;

    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = NULL;
    status = sl_os_process_common_validate(policy, command, args, arg_count, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (options == NULL) {
        options = &defaults;
    }
    status = sl_os_process_start_options_validate(options, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_os_platform_process_start(arena, command, args, arg_count, options, out, out_diag);
}

SlStatus sl_os_process_wait(SlOsProcessHandle* handle, const SlOsProcessWaitOptions* options,
                            SlOsProcessExit* out, SlDiag* out_diag)
{
    if (handle == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOsProcessExit){0};
    return sl_os_platform_process_wait(handle, options, out, out_diag);
}

SlStatus sl_os_process_stdout_read(SlArena* arena, SlOsProcessHandle* handle, size_t max_bytes,
                                   SlOsProcessPipeRead* out, SlDiag* out_diag)
{
    if (arena == NULL || handle == NULL || out == NULL || max_bytes == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOsProcessPipeRead){0};
    return sl_os_platform_process_stdout_read(arena, handle, max_bytes, out, out_diag);
}

SlStatus sl_os_process_stderr_read(SlArena* arena, SlOsProcessHandle* handle, size_t max_bytes,
                                   SlOsProcessPipeRead* out, SlDiag* out_diag)
{
    if (arena == NULL || handle == NULL || out == NULL || max_bytes == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOsProcessPipeRead){0};
    return sl_os_platform_process_stderr_read(arena, handle, max_bytes, out, out_diag);
}

SlStatus sl_os_process_stdin_write(SlOsProcessHandle* handle, SlStr bytes, size_t* out_written,
                                   SlDiag* out_diag)
{
    if (handle == NULL || bytes.ptr == NULL || out_written == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_written = 0U;
    return sl_os_platform_process_stdin_write(handle, bytes, out_written, out_diag);
}

SlStatus sl_os_process_stdin_close(SlOsProcessHandle* handle, SlDiag* out_diag)
{
    if (handle == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_os_platform_process_stdin_close(handle, out_diag);
}

SlStatus sl_os_process_terminate(SlOsProcessHandle* handle, SlDiag* out_diag)
{
    if (handle == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_os_platform_process_terminate(handle, out_diag);
}

SlStatus sl_os_process_kill(SlOsProcessHandle* handle, SlDiag* out_diag)
{
    if (handle == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_os_platform_process_kill(handle, out_diag);
}

SlStatus sl_os_process_cancel(SlOsProcessHandle* handle, SlDiag* out_diag)
{
    if (handle == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_os_platform_process_cancel(handle, out_diag);
}

void sl_os_process_dispose(SlOsProcessHandle* handle)
{
    if (handle != NULL) {
        sl_os_platform_process_dispose(handle);
    }
}
