#ifndef SLOPPY_OS_H
#define SLOPPY_OS_H

#include "sloppy/arena.h"
#include "sloppy/diagnostics.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SlOsPolicyMode
{
    SL_OS_POLICY_DEVELOPMENT = 0,
    SL_OS_POLICY_STRICT = 1
} SlOsPolicyMode;

typedef struct SlOsEnvironmentGrant
{
    SlStr key;
} SlOsEnvironmentGrant;

typedef struct SlOsPolicy
{
    SlOsPolicyMode mode;
    bool allow_system_info;
    bool allow_environment_list;
    bool allow_process_run;
    SlStr environment_list_prefix;
    const SlOsEnvironmentGrant* environment_grants;
    size_t environment_grant_count;
} SlOsPolicy;

typedef struct SlOsSystemInfo
{
    SlOwnedStr platform;
    SlOwnedStr arch;
    uint32_t cpu_count;
    SlOwnedStr temp_directory;
    SlOwnedStr hostname;
    SlOwnedStr end_of_line;
} SlOsSystemInfo;

typedef struct SlOsEnvironmentEntry
{
    SlOwnedStr key;
} SlOsEnvironmentEntry;

typedef struct SlOsEnvironmentList
{
    SlOsEnvironmentEntry* entries;
    size_t count;
} SlOsEnvironmentList;

typedef enum SlOsProcessCaptureMode
{
    SL_OS_PROCESS_CAPTURE_NONE = 0,
    SL_OS_PROCESS_CAPTURE_TEXT = 1,
    SL_OS_PROCESS_CAPTURE_BYTES = 2
} SlOsProcessCaptureMode;

typedef struct SlOsEnvironmentOverride
{
    SlStr key;
    SlStr value;
} SlOsEnvironmentOverride;

typedef struct SlOsProcessRunOptions
{
    SlStr cwd;
    const SlOsEnvironmentOverride* environment_overrides;
    size_t environment_override_count;
    SlOsProcessCaptureMode capture;
    size_t max_stdout_bytes;
    size_t max_stderr_bytes;
    uint64_t timeout_ms;
} SlOsProcessRunOptions;

typedef struct SlOsProcessRunResult
{
    int32_t exit_code;
    bool timed_out;
    bool stdout_truncated;
    bool stderr_truncated;
    SlOwnedStr stdout_text;
    SlOwnedStr stderr_text;
} SlOsProcessRunResult;

SlOsPolicy sl_os_development_policy(void);
SlOsPolicy sl_os_strict_policy(const SlOsEnvironmentGrant* grants, size_t grant_count,
                               bool allow_environment_list, SlStr environment_list_prefix);
void sl_os_policy_set_process_run(SlOsPolicy* policy, bool allowed);

bool sl_os_environment_key_is_secret(SlStr key);
SlStatus sl_os_environment_redacted_value(SlArena* arena, SlStr key, SlOwnedStr* out,
                                          SlDiag* out_diag);

SlStatus sl_os_system_info(SlArena* arena, const SlOsPolicy* policy, SlOsSystemInfo* out,
                           SlDiag* out_diag);
SlStatus sl_os_environment_get(SlArena* arena, const SlOsPolicy* policy, SlStr key,
                               SlOwnedStr* out_value, bool* out_found, SlDiag* out_diag);
SlStatus sl_os_environment_has(const SlOsPolicy* policy, SlStr key, bool* out_found,
                               SlDiag* out_diag);
SlStatus sl_os_environment_list(SlArena* arena, const SlOsPolicy* policy, SlStr prefix,
                                SlOsEnvironmentList* out, SlDiag* out_diag);
SlStatus sl_os_process_run(SlArena* arena, const SlOsPolicy* policy, SlStr command,
                           const SlStr* args, size_t arg_count,
                           const SlOsProcessRunOptions* options, SlOsProcessRunResult* out,
                           SlDiag* out_diag);

#ifdef __cplusplus
}
#endif

#endif
