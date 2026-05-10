#include "sloppy/builder.h"
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
    nanosleep(&delay, NULL);
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
        for (int index = 0; index < 8192; index += 1) {
            putchar('x');
        }
        return 0;
    }
    if (argc >= 3 && strcmp(argv[2], "sleep") == 0) {
        sleep_ms(250U);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[2], "stdin") == 0) {
        char buffer[128];
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            return 5;
        }
        printf("stdin:%s", buffer);
        return 0;
    }
    if (argc >= 4 && strcmp(argv[2], "env") == 0) {
#ifdef _WIN32
        char value[4096];
        DWORD value_length = GetEnvironmentVariableA(argv[3], value, (DWORD)sizeof(value));
        if (value_length != 0U && value_length < sizeof(value)) {
            printf("%s\n", value);
            return 0;
        }
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

#ifdef _WIN32
static int win32_copy_nul_from_str(char* buffer, size_t capacity, SlStr value)
{
    SlStringBuilder builder = {0};
    SlStr view = {0};

    if (expect_status(sl_string_builder_init_fixed(&builder, buffer, capacity), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_string_builder_append_str(&builder, value), SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_view_with_nul(&builder, &view), SL_STATUS_OK) != 0)
    {
        return 1;
    }
    return 0;
}

static int win32_build_probe_dir(char* probe_dir, size_t capacity)
{
    char temp_path[MAX_PATH + 1U];
    DWORD temp_length = GetTempPathA((DWORD)sizeof(temp_path), temp_path);
    SlStringBuilder builder = {0};
    SlStr view = {0};

    if (temp_length == 0U || temp_length >= sizeof(temp_path)) {
        return 66;
    }
    if (expect_status(sl_string_builder_init_fixed(&builder, probe_dir, capacity), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_string_builder_append_cstr(&builder, temp_path), SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_append_cstr(&builder, "SloppyOsPathProbe_"),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_append_u64(&builder, (uint64_t)GetCurrentProcessId()),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_view_with_nul(&builder, &view), SL_STATUS_OK) != 0)
    {
        return 67;
    }
    return 0;
}

static int win32_build_probe_path(char* probe_path, size_t capacity, const char* probe_dir,
                                  const char* probe_name)
{
    SlStringBuilder builder = {0};
    SlStr view = {0};

    if (expect_status(sl_string_builder_init_fixed(&builder, probe_path, capacity), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_string_builder_append_cstr(&builder, probe_dir), SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_append_char(&builder, '\\'), SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_append_cstr(&builder, probe_name), SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_view_with_nul(&builder, &view), SL_STATUS_OK) != 0)
    {
        return 69;
    }
    return 0;
}

static int win32_prepend_probe_dir_to_path(const char* probe_dir, char* old_path,
                                           size_t old_path_capacity, bool* had_old_path,
                                           char* new_path, size_t new_path_capacity)
{
    DWORD old_path_length = GetEnvironmentVariableA("PATH", old_path, (DWORD)old_path_capacity);
    SlStringBuilder builder = {0};
    SlStr view = {0};

    *had_old_path = false;
    if (old_path_length != 0U) {
        if (old_path_length >= old_path_capacity) {
            return 71;
        }
        *had_old_path = true;
    }
    if (expect_status(sl_string_builder_init_fixed(&builder, new_path, new_path_capacity),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_append_cstr(&builder, probe_dir), SL_STATUS_OK) != 0)
    {
        return 71;
    }
    if (*had_old_path &&
        (expect_status(sl_string_builder_append_char(&builder, ';'), SL_STATUS_OK) != 0 ||
         expect_status(sl_string_builder_append_cstr(&builder, old_path), SL_STATUS_OK) != 0))
    {
        return 71;
    }
    if (expect_status(sl_string_builder_view_with_nul(&builder, &view), SL_STATUS_OK) != 0) {
        return 71;
    }
    return _putenv_s("PATH", new_path) == 0 ? 0 : 72;
}

static int win32_prepare_path_probe(const char* self_path, const char* probe_name, char* probe_dir,
                                    size_t probe_dir_capacity, char* probe_path,
                                    size_t probe_path_capacity, char* old_path,
                                    size_t old_path_capacity, bool* had_old_path, char* new_path,
                                    size_t new_path_capacity)
{
    int outcome = win32_build_probe_dir(probe_dir, probe_dir_capacity);

    if (outcome != 0) {
        return outcome;
    }
    if (!CreateDirectoryA(probe_dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return 68;
    }
    outcome = win32_build_probe_path(probe_path, probe_path_capacity, probe_dir, probe_name);
    if (outcome != 0) {
        return outcome;
    }
    if (!CopyFileA(self_path, probe_path, FALSE)) {
        return 70;
    }
    return win32_prepend_probe_dir_to_path(probe_dir, old_path, old_path_capacity, had_old_path,
                                           new_path, new_path_capacity);
}

static int win32_run_path_lookup_probe(SlArena* arena, const SlOsPolicy* policy,
                                       const char* probe_name)
{
    SlStr args[2] = {sl_str_from_cstr("--sloppy-os-child"), sl_str_from_cstr("echo")};
    SlOsProcessRunOptions run_options = {.capture = SL_OS_PROCESS_CAPTURE_TEXT};
    SlOsProcessRunResult result = {0};
    SlDiag diag = {0};

    if (expect_status(sl_os_process_run(arena, policy, sl_str_from_cstr(probe_name), args, 2U,
                                        &run_options, &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.exit_code != 7 ||
        !str_contains(sl_owned_str_as_view(result.stdout_text), "child-output"))
    {
        return 73;
    }
    return 0;
}

static int win32_start_path_lookup_probe(SlArena* arena, const SlOsPolicy* policy,
                                         const char* probe_name)
{
    SlStr args[2] = {sl_str_from_cstr("--sloppy-os-child"), sl_str_from_cstr("echo")};
    SlOsProcessStartOptions start_options = {.stdout_mode = SL_OS_PROCESS_PIPE_PIPE};
    SlOsProcessHandle* handle = NULL;
    SlOsProcessPipeRead stdout_read = {0};
    SlOsProcessExit exit = {0};
    SlDiag diag = {0};
    int outcome = 0;

    if (expect_status(sl_os_process_start(arena, policy, sl_str_from_cstr(probe_name), args, 2U,
                                          &start_options, &handle, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_os_process_wait(handle, NULL, &exit, &diag), SL_STATUS_OK) != 0 ||
        expect_status(sl_os_process_stdout_read(arena, handle, 256U, &stdout_read, &diag),
                      SL_STATUS_OK) != 0 ||
        exit.exit_code != 7 ||
        !str_contains(sl_owned_str_as_view(stdout_read.bytes), "child-output"))
    {
        outcome = 74;
    }
    sl_os_process_dispose(handle);
    return outcome;
}
#endif

static int test_process_windows_path_lookup(const char* self_path)
{
#ifdef _WIN32
    const char* probe_name = "sloppy-os-path-probe.exe";
    unsigned char storage[524288];
    SlArena arena = {0};
    SlOsPolicy policy = sl_os_development_policy();
    char self_storage[4096];
    char self_nul[4096];
    char probe_dir[MAX_PATH + 1U] = {0};
    char probe_path[MAX_PATH + 1U] = {0};
    char new_path[32768];
    char old_path[32768];
    bool had_old_path = false;
    SlStr self = self_process_path(self_storage, sizeof(self_storage), self_path);
    int outcome = 0;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0) {
        return 64;
    }
    if (self.length == 0U || win32_copy_nul_from_str(self_nul, sizeof(self_nul), self) != 0) {
        return 65;
    }
    outcome = win32_prepare_path_probe(self_nul, probe_name, probe_dir, sizeof(probe_dir),
                                       probe_path, sizeof(probe_path), old_path, sizeof(old_path),
                                       &had_old_path, new_path, sizeof(new_path));
    if (outcome != 0) {
        goto cleanup;
    }
    outcome = win32_run_path_lookup_probe(&arena, &policy, probe_name);
    if (outcome != 0) {
        goto cleanup;
    }
    outcome = win32_start_path_lookup_probe(&arena, &policy, probe_name);

cleanup:
    if (had_old_path) {
        _putenv_s("PATH", old_path);
    }
    else {
        _putenv_s("PATH", "");
    }
    DeleteFileA(probe_path);
    RemoveDirectoryA(probe_dir);
    return outcome;
#else
    (void)self_path;
    return 0;
#endif
}

static int test_system_info_is_normalized(void)
{
    unsigned char storage[16384];
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
    unsigned char storage[16384];
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

static int test_embedded_nul_cstring_boundaries_reject(void)
{
    static const char bad_key[] = {'S', 'L', 'O', 'P',  'P', 'Y', '_', 'O', 'S', '_',
                                   'B', 'A', 'D', '\0', 'S', 'U', 'F', 'F', 'I', 'X'};
    static const char bad_command[] = {'s', 'l', 'o', 'p', 'p', 'y', '\0', 'x'};
    static const char bad_arg[] = {'e', 'c', 'h', 'o', '\0', 'x'};
    static const char bad_cwd[] = {'Z', ':', '/', 's', 'l', 'o', 'p', 'p', 'y', '\0', 'x'};
    unsigned char storage[4096];
    SlArena arena = {0};
    SlOsPolicy policy = sl_os_development_policy();
    SlOwnedStr value = {.ptr = (char*)bad_key, .length = sizeof(bad_key)};
    bool found = true;
    SlStr args[1] = {sl_str_from_parts(bad_arg, sizeof(bad_arg))};
    SlOsProcessRunOptions run_options = {.capture = SL_OS_PROCESS_CAPTURE_TEXT};
    SlOsProcessRunResult run_result = {.exit_code = 99};
    SlOsProcessStartOptions start_options = {0};
    SlOsProcessHandle* handle = (SlOsProcessHandle*)bad_key;
    SlDiag diag = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0) {
        return 26;
    }

    if (expect_status(sl_os_environment_get(&arena, &policy,
                                            sl_str_from_parts(bad_key, sizeof(bad_key)), &value,
                                            &found, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        value.ptr != bad_key || value.length != sizeof(bad_key) || !found)
    {
        return 27;
    }

    if (expect_status(sl_os_environment_has(&policy, sl_str_from_parts(bad_key, sizeof(bad_key)),
                                            &found, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        !found)
    {
        return 28;
    }

    if (expect_status(sl_os_process_run(&arena, &policy,
                                        sl_str_from_parts(bad_command, sizeof(bad_command)), NULL,
                                        0U, &run_options, &run_result, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        run_result.exit_code != 99)
    {
        return 29;
    }

    run_result.exit_code = 99;
    if (expect_status(sl_os_process_run(&arena, &policy, sl_str_from_cstr("echo"), args, 1U,
                                        &run_options, &run_result, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        run_result.exit_code != 0)
    {
        return 34;
    }

    run_options.cwd = sl_str_from_parts(bad_cwd, sizeof(bad_cwd));
    run_result.exit_code = 99;
    if (expect_status(sl_os_process_run(&arena, &policy, sl_str_from_cstr("echo"), NULL, 0U,
                                        &run_options, &run_result, &diag),
                      SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_OS_INVALID_CWD)
    {
        return 35;
    }

    diag = (SlDiag){0};
    if (expect_status(sl_os_process_start(&arena, &policy,
                                          sl_str_from_parts(bad_command, sizeof(bad_command)), NULL,
                                          0U, &start_options, &handle, &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        handle != NULL)
    {
        return 36;
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
    unsigned char storage[65536];
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
    unsigned char storage[4096];
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
    result = (SlOsProcessRunResult){0};
    options.max_stdout_bytes = 9000U;
    options.max_stderr_bytes = 12U;
    if (expect_status(sl_os_process_run(&arena, &policy, self, args, 2U, &options, &result, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 102;
    }
    if (result.stdout_truncated || result.stdout_text.length != 8192U) {
        return 103;
    }
    return 0;
}

static int test_process_start_streams_and_wait(const char* self_path)
{
    unsigned char storage[524288];
    SlArena arena = {0};
    SlOsPolicy policy = sl_os_development_policy();
    char self_storage[4096];
    SlStr self = self_process_path(self_storage, sizeof(self_storage), self_path);
    SlStr args[2] = {sl_str_from_cstr("--sloppy-os-child"), sl_str_from_cstr("echo")};
    SlOsProcessStartOptions options = {.stdout_mode = SL_OS_PROCESS_PIPE_PIPE,
                                       .stderr_mode = SL_OS_PROCESS_PIPE_PIPE};
    SlOsProcessHandle* handle = NULL;
    SlOsProcessPipeRead stdout_read = {0};
    SlOsProcessPipeRead stderr_read = {0};
    SlOsProcessExit exit = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_os_process_start(&arena, &policy, self, args, 2U, &options, &handle, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 110;
    }
    if (handle == NULL ||
        expect_status(sl_os_process_wait(handle, NULL, &exit, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_os_process_stdout_read(&arena, handle, 256U, &stdout_read, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_os_process_stderr_read(&arena, handle, 256U, &stderr_read, NULL),
                      SL_STATUS_OK) != 0)
    {
        sl_os_process_dispose(handle);
        return 111;
    }
    if (exit.exit_code != 7 ||
        !str_contains(sl_owned_str_as_view(stdout_read.bytes), "child-output") ||
        !str_contains(sl_owned_str_as_view(stderr_read.bytes), "child-error"))
    {
        sl_os_process_dispose(handle);
        return 112;
    }
    sl_os_process_dispose(handle);
    sl_os_process_dispose(handle);
    return 0;
}

static int test_process_start_stdin_pipe(const char* self_path)
{
    unsigned char storage[524288];
    SlArena arena = {0};
    SlOsPolicy policy = sl_os_development_policy();
    char self_storage[4096];
    SlStr self = self_process_path(self_storage, sizeof(self_storage), self_path);
    SlStr args[2] = {sl_str_from_cstr("--sloppy-os-child"), sl_str_from_cstr("stdin")};
    SlOsProcessStartOptions options = {.stdin_mode = SL_OS_PROCESS_PIPE_PIPE,
                                       .stdout_mode = SL_OS_PROCESS_PIPE_PIPE};
    SlOsProcessHandle* handle = NULL;
    SlOsProcessPipeRead stdout_read = {0};
    SlOsProcessExit exit = {0};
    size_t written = 0U;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_os_process_start(&arena, &policy, self, args, 2U, &options, &handle, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 120;
    }
    if (expect_status(
            sl_os_process_stdin_write(handle, sl_str_from_cstr("hello\n"), &written, NULL),
            SL_STATUS_OK) != 0 ||
        written != 6U ||
        expect_status(sl_os_process_stdin_close(handle, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_os_process_wait(handle, NULL, &exit, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_os_process_stdout_read(&arena, handle, 256U, &stdout_read, NULL),
                      SL_STATUS_OK) != 0)
    {
        sl_os_process_dispose(handle);
        return 121;
    }
    if (exit.exit_code != 0 ||
        !str_contains(sl_owned_str_as_view(stdout_read.bytes), "stdin:hello"))
    {
        sl_os_process_dispose(handle);
        return 122;
    }
    sl_os_process_dispose(handle);
    return 0;
}

static int test_process_start_timeout_kill_cancel_and_stale(const char* self_path)
{
    unsigned char storage[524288];
    SlArena arena = {0};
    SlOsPolicy policy = sl_os_development_policy();
    char self_storage[4096];
    SlStr self = self_process_path(self_storage, sizeof(self_storage), self_path);
    SlStr args[2] = {sl_str_from_cstr("--sloppy-os-child"), sl_str_from_cstr("sleep")};
    SlOsProcessStartOptions options = {.stdout_mode = SL_OS_PROCESS_PIPE_PIPE};
    SlOsProcessWaitOptions wait_options = {.timeout_ms = 10U};
    SlOsProcessHandle* handle = NULL;
    SlOsProcessExit exit = {0};
    SlOsProcessPipeRead pipe_read = {0};
    SlDiag diag = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_os_process_start(&arena, &policy, self, args, 2U, &options, &handle, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 130;
    }
    if (expect_status(sl_os_process_wait(handle, &wait_options, &exit, &diag),
                      SL_STATUS_INVALID_STATE) != 0 ||
        !exit.timed_out || diag.code != SL_DIAG_OS_PROCESS_TIMEOUT)
    {
        sl_os_process_dispose(handle);
        return 131;
    }
    if (expect_status(sl_os_process_kill(handle, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_os_process_wait(handle, NULL, &exit, NULL), SL_STATUS_OK) != 0 ||
        !exit.killed)
    {
        sl_os_process_dispose(handle);
        return 132;
    }
    sl_os_process_dispose(handle);
    diag = (SlDiag){0};
    if (expect_status(sl_os_process_stdout_read(&arena, handle, 16U, &pipe_read, &diag),
                      SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_OS_PIPE_CLOSED)
    {
        return 133;
    }
    handle = NULL;
    if (expect_status(sl_os_process_start(&arena, &policy, self, args, 2U, NULL, &handle, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_os_process_cancel(handle, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_os_process_wait(handle, NULL, &exit, NULL), SL_STATUS_OK) != 0 ||
        !exit.cancelled)
    {
        sl_os_process_dispose(handle);
        return 134;
    }
    sl_os_process_dispose(handle);
    return 0;
}

static int test_system_and_environment_suite(void)
{
    int result = test_system_info_is_normalized();
    if (result != 0) {
        fprintf(stderr, "test_system_info_is_normalized failed: %d\n", result);
        return result;
    }
    result = test_strict_system_info_denied();
    if (result != 0) {
        fprintf(stderr, "test_strict_system_info_denied failed: %d\n", result);
        return result;
    }
    result = test_environment_get_has_and_missing();
    if (result != 0) {
        fprintf(stderr, "test_environment_get_has_and_missing failed: %d\n", result);
        return result;
    }
    result = test_embedded_nul_cstring_boundaries_reject();
    if (result != 0) {
        fprintf(stderr, "test_embedded_nul_cstring_boundaries_reject failed: %d\n", result);
        return result;
    }
    result = test_strict_environment_denies_ungranted_key();
    if (result != 0) {
        fprintf(stderr, "test_strict_environment_denies_ungranted_key failed: %d\n", result);
        return result;
    }
    result = test_environment_list_names_only_and_prefix_scoped();
    if (result != 0) {
        fprintf(stderr, "test_environment_list_names_only_and_prefix_scoped failed: %d\n", result);
        return result;
    }
    result = test_secret_redaction_helper_never_returns_value();
    if (result != 0) {
        fprintf(stderr, "test_secret_redaction_helper_never_returns_value failed: %d\n", result);
        return result;
    }

    return 0;
}

static int test_process_run_suite(const char* self_path)
{
    int result = test_process_run_captures_explicit_argv(self_path);
    if (result != 0) {
        fprintf(stderr, "test_process_run_captures_explicit_argv failed: %d\n", result);
        return result;
    }
    result = test_process_windows_path_lookup(self_path);
    if (result != 0) {
        fprintf(stderr, "test_process_windows_path_lookup failed: %d\n", result);
        return result;
    }
    result = test_process_run_timeout_is_distinct(self_path);
    if (result != 0) {
        fprintf(stderr, "test_process_run_timeout_is_distinct failed: %d\n", result);
        return result;
    }
    result = test_process_run_negative_paths(self_path);
    if (result != 0) {
        fprintf(stderr, "test_process_run_negative_paths failed: %d\n", result);
        return result;
    }
    result = test_process_run_env_override_and_strict_grant(self_path);
    if (result != 0) {
        fprintf(stderr, "test_process_run_env_override_and_strict_grant failed: %d\n", result);
        return result;
    }
    result = test_process_run_capture_bound(self_path);
    if (result != 0) {
        fprintf(stderr, "test_process_run_capture_bound failed: %d\n", result);
    }
    return result;
}

static int test_process_start_suite(const char* self_path)
{
    int result = test_process_start_streams_and_wait(self_path);
    if (result != 0) {
        fprintf(stderr, "test_process_start_streams_and_wait failed: %d\n", result);
        return result;
    }
    result = test_process_start_stdin_pipe(self_path);
    if (result != 0) {
        fprintf(stderr, "test_process_start_stdin_pipe failed: %d\n", result);
        return result;
    }

    result = test_process_start_timeout_kill_cancel_and_stale(self_path);
    if (result != 0) {
        fprintf(stderr, "test_process_start_timeout_kill_cancel_and_stale failed: %d\n", result);
    }
    return result;
}

int main(int argc, char** argv)
{
    int result = 0;

    if (argc >= 2 && strcmp(argv[1], "--sloppy-os-child") == 0) {
        return process_child_main(argc, argv);
    }

    result = test_system_and_environment_suite();
    if (result != 0) {
        return result;
    }

    result = test_process_run_suite(argv[0]);
    if (result != 0) {
        return result;
    }

    return test_process_start_suite(argv[0]);
}
