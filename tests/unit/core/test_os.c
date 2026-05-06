#include "sloppy/os.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static bool str_contains(SlStr haystack, const char* needle)
{
    size_t needle_len = strlen(needle);

    if (needle_len == 0U) {
        return true;
    }
    if (haystack.ptr == NULL || haystack.length < needle_len) {
        return false;
    }
    for (size_t offset = 0U; offset <= haystack.length - needle_len; offset += 1U) {
        if (memcmp(haystack.ptr + offset, needle, needle_len) == 0) {
            return true;
        }
    }
    return false;
}

static int set_test_env(const char* key, const char* value)
{
#ifdef _WIN32
    return _putenv_s(key, value);
#else
    return setenv(key, value, 1);
#endif
}

static void sleep_ms(unsigned int ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec delay = {.tv_sec = (time_t)(ms / 1000U),
                             .tv_nsec = (long)((ms % 1000U) * 1000000U)};
    (void)nanosleep(&delay, NULL);
#endif
}

static int process_child_main(int argc, char** argv)
{
    if (argc >= 3 && strcmp(argv[2], "echo") == 0) {
        printf("child-output\n");
        fputs("child-error\n", stderr);
        return 7;
    }
    if (argc >= 4 && strcmp(argv[2], "argv") == 0) {
        printf("%s\n", argv[3]);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[2], "long") == 0) {
        for (int index = 0; index < 128; index += 1) {
            putchar('x');
        }
        return 0;
    }
    if (argc >= 3 && strcmp(argv[2], "sleep") == 0) {
        sleep_ms(250U);
        return 0;
    }
    if (argc >= 4 && strcmp(argv[2], "env") == 0) {
#ifdef _WIN32
        char* value = NULL;
        size_t value_length = 0U;
        if (_dupenv_s(&value, &value_length, argv[3]) == 0 && value != NULL) {
            printf("%s\n", value);
            free(value);
            return 0;
        }
        free(value);
        return 4;
#else
        const char* value = getenv(argv[3]);
        if (value != NULL) {
            printf("%s\n", value);
        }
        return value == NULL ? 4 : 0;
#endif
    }
    return 3;
}

static SlStr self_process_path(char* buffer, size_t capacity, const char* fallback)
{
#ifdef _WIN32
    DWORD length = GetModuleFileNameA(NULL, buffer, (DWORD)capacity);
    if (length != 0U && length < capacity) {
        return sl_str_from_parts(buffer, (size_t)length);
    }
#else
    (void)buffer;
    (void)capacity;
#endif
    return sl_str_from_cstr(fallback);
}

static int test_system_info_is_normalized(void)
{
    unsigned char storage[4096];
    SlArena arena = {0};
    SlOsPolicy policy = sl_os_development_policy();
    SlOsSystemInfo info = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_os_system_info(&arena, &policy, &info, NULL), SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (info.platform.length == 0U || info.arch.length == 0U || info.cpu_count == 0U ||
        info.temp_directory.length == 0U || info.end_of_line.length == 0U)
    {
        return 2;
    }
    return 0;
}

static int test_strict_system_info_denied(void)
{
    unsigned char storage[1024];
    SlArena arena = {0};
    SlOsPolicy policy = sl_os_strict_policy(NULL, 0U, false, sl_str_empty());
    SlOsSystemInfo info = {0};
    SlDiag diag = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_os_system_info(&arena, &policy, &info, &diag), SL_STATUS_INVALID_STATE) !=
            0)
    {
        return 10;
    }
    if (diag.code != SL_DIAG_OS_FEATURE_UNAVAILABLE) {
        return 11;
    }
    return 0;
}

static int test_environment_get_has_and_missing(void)
{
    unsigned char storage[4096];
    char long_value[2048];
    SlArena arena = {0};
    SlOsPolicy policy = sl_os_development_policy();
    SlOwnedStr value = {0};
    bool found = false;
    size_t index = 0U;

    for (index = 0U; index < sizeof(long_value) - 1U; index += 1U) {
        long_value[index] = 'x';
    }
    long_value[sizeof(long_value) - 1U] = '\0';
    if (set_test_env("SLOPPY_OS_TEST_VALUE", "visible-test-value") != 0) {
        return 20;
    }
    if (set_test_env("SLOPPY_OS_TEST_LONG_VALUE", long_value) != 0) {
        return 24;
    }
    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_os_environment_get(&arena, &policy,
                                            sl_str_from_cstr("SLOPPY_OS_TEST_VALUE"), &value,
                                            &found, NULL),
                      SL_STATUS_OK) != 0 ||
        !found ||
        !sl_str_equal(sl_owned_str_as_view(value), sl_str_from_cstr("visible-test-value")))
    {
        return 21;
    }
    found = true;
    if (expect_status(sl_os_environment_get(&arena, &policy,
                                            sl_str_from_cstr("SLOPPY_OS_TEST_MISSING"), &value,
                                            &found, NULL),
                      SL_STATUS_OK) != 0 ||
        found)
    {
        return 22;
    }
    if (expect_status(
            sl_os_environment_has(&policy, sl_str_from_cstr("SLOPPY_OS_TEST_VALUE"), &found, NULL),
            SL_STATUS_OK) != 0 ||
        !found)
    {
        return 23;
    }
    found = false;
    if (expect_status(sl_os_environment_has(&policy, sl_str_from_cstr("SLOPPY_OS_TEST_LONG_VALUE"),
                                            &found, NULL),
                      SL_STATUS_OK) != 0 ||
        !found)
    {
        return 25;
    }
    return 0;
}

static int test_strict_environment_denies_ungranted_key(void)
{
    unsigned char storage[4096];
    SlArena arena = {0};
    SlOsEnvironmentGrant grants[1] = {{sl_str_from_cstr("SLOPPY_OS_ALLOWED")}};
    SlOsPolicy policy = sl_os_strict_policy(grants, 1U, false, sl_str_empty());
    SlOwnedStr value = {0};
    bool found = false;
    SlDiag diag = {0};

    if (set_test_env("SLOPPY_OS_ALLOWED", "ok") != 0 ||
        set_test_env("SLOPPY_OS_DENIED", "must-not-appear") != 0)
    {
        return 30;
    }
    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_os_environment_get(&arena, &policy, sl_str_from_cstr("SLOPPY_OS_ALLOWED"),
                                            &value, &found, &diag),
                      SL_STATUS_OK) != 0 ||
        !found || !sl_str_equal(sl_owned_str_as_view(value), sl_str_from_cstr("ok")))
    {
        return 31;
    }
    if (expect_status(sl_os_environment_get(&arena, &policy, sl_str_from_cstr("SLOPPY_OS_DENIED"),
                                            &value, &found, &diag),
                      SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_OS_ENV_ACCESS_DENIED)
    {
        return 32;
    }
    if (str_contains(diag.message, "must-not-appear")) {
        return 33;
    }
    return 0;
}

static int test_environment_list_names_only_and_prefix_scoped(void)
{
    unsigned char storage[8192];
    SlArena arena = {0};
    SlOsPolicy policy = sl_os_development_policy();
    SlOsEnvironmentList list = {0};
    bool saw_value = false;
    bool saw_other = false;
    size_t index = 0U;

    if (set_test_env("SLOPPY_OS_LIST_ALPHA", "alpha-secret-value") != 0 ||
        set_test_env("SLOPPY_OS_OTHER", "other") != 0)
    {
        return 40;
    }
    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_os_environment_list(&arena, &policy, sl_str_from_cstr("SLOPPY_OS_LIST_"),
                                             &list, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 41;
    }
    for (index = 0U; index < list.count; index += 1U) {
        if (list.entries[index].key.length == 0U) {
            return 43;
        }
        if (sl_str_equal(sl_owned_str_as_view(list.entries[index].key),
                         sl_str_from_cstr("SLOPPY_OS_LIST_ALPHA")))
        {
            saw_value = true;
        }
        if (sl_str_equal(sl_owned_str_as_view(list.entries[index].key),
                         sl_str_from_cstr("SLOPPY_OS_OTHER")))
        {
            saw_other = true;
        }
    }
    return saw_value && !saw_other ? 0 : 42;
}

static int test_secret_redaction_helper_never_returns_value(void)
{
    unsigned char storage[1024];
    SlArena arena = {0};
    SlOwnedStr redacted = {0};
    SlDiag diag = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_os_environment_redacted_value(
                          &arena, sl_str_from_cstr("SLOPPY_OS_SECRET_TOKEN"), &redacted, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 50;
    }
    if (!sl_os_environment_key_is_secret(sl_str_from_cstr("SLOPPY_OS_SECRET_TOKEN")) ||
        !sl_str_equal(sl_owned_str_as_view(redacted), sl_str_from_cstr("[REDACTED]")) ||
        diag.code != SL_DIAG_OS_ENV_SECRET_REDACTED ||
        !sl_str_equal(diag.related[0].message, sl_str_from_cstr("SLOPPY_OS_SECRET_TOKEN")))
    {
        return 51;
    }
    return 0;
}

static int test_process_run_captures_explicit_argv(const char* self_path)
{
    unsigned char storage[524288];
    SlArena arena = {0};
    SlOsPolicy policy = sl_os_development_policy();
    char self_storage[4096];
    SlStr self = self_process_path(self_storage, sizeof(self_storage), self_path);
    SlStr args[2] = {sl_str_from_cstr("--sloppy-os-child"), sl_str_from_cstr("echo")};
    SlStr argv_args[3] = {sl_str_from_cstr("--sloppy-os-child"), sl_str_from_cstr("argv"),
                          sl_str_from_cstr("quote\"and\\")};
    SlOsProcessRunOptions options = {.capture = SL_OS_PROCESS_CAPTURE_TEXT};
    SlOsProcessRunResult result = {0};
    SlDiag diag = {0};
    SlStatus status;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0) {
        return 60;
    }
    status = sl_os_process_run(&arena, &policy, self, args, 2U, &options, &result, &diag);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 60;
    }
    if (result.exit_code != 7 ||
        !str_contains(sl_owned_str_as_view(result.stdout_text), "child-output") ||
        !str_contains(sl_owned_str_as_view(result.stderr_text), "child-error"))
    {
        return 61;
    }
    result = (SlOsProcessRunResult){0};
    status = sl_os_process_run(&arena, &policy, self, argv_args, 3U, &options, &result, &diag);
    if (expect_status(status, SL_STATUS_OK) != 0 ||
        !str_contains(sl_owned_str_as_view(result.stdout_text), "quote\"and\\"))
    {
        return 62;
    }
    return 0;
}

static int test_process_run_timeout_is_distinct(const char* self_path)
{
    unsigned char storage[524288];
    SlArena arena = {0};
    SlOsPolicy policy = sl_os_development_policy();
    char self_storage[4096];
    SlStr self = self_process_path(self_storage, sizeof(self_storage), self_path);
    SlStr args[2] = {sl_str_from_cstr("--sloppy-os-child"), sl_str_from_cstr("sleep")};
    SlOsProcessRunOptions options = {.capture = SL_OS_PROCESS_CAPTURE_TEXT, .timeout_ms = 20U};
    SlOsProcessRunResult result = {0};
    SlDiag diag = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_os_process_run(&arena, &policy, self, args, 2U, &options, &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        return 70;
    }
    if (!result.timed_out || diag.code != SL_DIAG_OS_PROCESS_TIMEOUT) {
        return 71;
    }
    return 0;
}

static int test_process_run_negative_paths(const char* self_path)
{
    unsigned char storage[524288];
    SlArena arena = {0};
    SlOsPolicy strict = sl_os_strict_policy(NULL, 0U, false, sl_str_empty());
    SlOsPolicy policy = sl_os_development_policy();
    char self_storage[4096];
    SlStr self = self_process_path(self_storage, sizeof(self_storage), self_path);
    SlStr args[2] = {sl_str_from_cstr("--sloppy-os-child"), sl_str_from_cstr("echo")};
    SlOsProcessRunOptions options = {.capture = SL_OS_PROCESS_CAPTURE_TEXT};
    SlOsProcessRunResult result = {0};
    SlDiag diag = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0) {
        return 80;
    }
    if (expect_status(sl_os_process_run(&arena, &strict, self, args, 2U, &options, &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_OS_PROCESS_EXECUTION_DENIED)
    {
        return 81;
    }
    diag = (SlDiag){0};
    if (expect_status(sl_os_process_run(&arena, &policy,
                                        sl_str_from_cstr("sloppy-definitely-missing-command-xyz"),
                                        NULL, 0U, &options, &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_OS_COMMAND_NOT_FOUND)
    {
        return 82;
    }
    diag = (SlDiag){0};
    options.cwd = sl_str_from_cstr("Z:/sloppy/definitely/missing/cwd");
    SlStatus cwd_status =
        sl_os_process_run(&arena, &policy, self, args, 2U, &options, &result, &diag);
    if (expect_status(cwd_status, SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_OS_INVALID_CWD)
    {
        return 83;
    }
    return 0;
}

static int test_process_run_env_override_and_strict_grant(const char* self_path)
{
    unsigned char storage[524288];
    SlArena arena = {0};
    SlOsPolicy strict = sl_os_strict_policy(NULL, 0U, false, sl_str_empty());
    char self_storage[4096];
    SlStr self = self_process_path(self_storage, sizeof(self_storage), self_path);
    SlStr args[3] = {sl_str_from_cstr("--sloppy-os-child"), sl_str_from_cstr("env"),
                     sl_str_from_cstr("SLOPPY_PROCESS_RUN_TEST_VALUE")};
    SlOsEnvironmentOverride overrides[1] = {
        {.key = sl_str_from_cstr("SLOPPY_PROCESS_RUN_TEST_VALUE"),
         .value = sl_str_from_cstr("visible-override")}};
    SlOsProcessRunOptions options = {.environment_overrides = overrides,
                                     .environment_override_count = 1U,
                                     .capture = SL_OS_PROCESS_CAPTURE_TEXT};
    SlOsProcessRunResult result = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0) {
        return 90;
    }
    sl_os_policy_set_process_run(&strict, true);
    if (expect_status(sl_os_process_run(&arena, &strict, self, args, 3U, &options, &result, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 91;
    }
    if (result.exit_code != 0 ||
        !str_contains(sl_owned_str_as_view(result.stdout_text), "visible-override"))
    {
        return 92;
    }
    return 0;
}

static int test_process_run_capture_bound(const char* self_path)
{
    unsigned char storage[196608];
    SlArena arena = {0};
    SlOsPolicy policy = sl_os_development_policy();
    char self_storage[4096];
    SlStr self = self_process_path(self_storage, sizeof(self_storage), self_path);
    SlStr args[2] = {sl_str_from_cstr("--sloppy-os-child"), sl_str_from_cstr("long")};
    SlOsProcessRunOptions options = {
        .capture = SL_OS_PROCESS_CAPTURE_TEXT, .max_stdout_bytes = 12U, .max_stderr_bytes = 12U};
    SlOsProcessRunResult result = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_os_process_run(&arena, &policy, self, args, 2U, &options, &result, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 100;
    }
    if (!result.stdout_truncated || result.stdout_text.length != 12U) {
        return 101;
    }
    return 0;
}

int main(int argc, char** argv)
{
    if (argc >= 2 && strcmp(argv[1], "--sloppy-os-child") == 0) {
        return process_child_main(argc, argv);
    }
    int result = test_system_info_is_normalized();
    if (result != 0) {
        return result;
    }
    result = test_strict_system_info_denied();
    if (result != 0) {
        return result;
    }
    result = test_environment_get_has_and_missing();
    if (result != 0) {
        return result;
    }
    result = test_strict_environment_denies_ungranted_key();
    if (result != 0) {
        return result;
    }
    result = test_environment_list_names_only_and_prefix_scoped();
    if (result != 0) {
        return result;
    }
    result = test_secret_redaction_helper_never_returns_value();
    if (result != 0) {
        return result;
    }
    result = test_process_run_captures_explicit_argv(argv[0]);
    if (result != 0) {
        return result;
    }
    result = test_process_run_timeout_is_distinct(argv[0]);
    if (result != 0) {
        return result;
    }
    result = test_process_run_negative_paths(argv[0]);
    if (result != 0) {
        return result;
    }
    result = test_process_run_env_override_and_strict_grant(argv[0]);
    if (result != 0) {
        return result;
    }
    return test_process_run_capture_bound(argv[0]);
}
