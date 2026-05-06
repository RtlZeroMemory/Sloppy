#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "../../core/os_platform.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char** environ;

static SlStatus sl_os_posix_copy_cstr(SlArena* arena, const char* value, SlOwnedStr* out)
{
    return sl_str_copy_to_arena(arena, sl_str_from_cstr(value == NULL ? "" : value), out);
}

static SlStr sl_os_posix_platform(void)
{
#if defined(__APPLE__)
    return sl_str_from_cstr("darwin");
#elif defined(__linux__)
    return sl_str_from_cstr("linux");
#elif defined(__FreeBSD__)
    return sl_str_from_cstr("freebsd");
#else
    return sl_str_from_cstr("unknown");
#endif
}

static SlStr sl_os_posix_arch(void)
{
#if defined(__x86_64__) || defined(_M_X64)
    return sl_str_from_cstr("x64");
#elif defined(__aarch64__) || defined(_M_ARM64)
    return sl_str_from_cstr("arm64");
#elif defined(__i386__) || defined(_M_IX86)
    return sl_str_from_cstr("ia32");
#else
    return sl_str_from_cstr("unknown");
#endif
}

static uint32_t sl_os_posix_cpu_count(void)
{
#ifdef _SC_NPROCESSORS_ONLN
    long value = sysconf(_SC_NPROCESSORS_ONLN);
    if (value > 0L && value <= (long)UINT32_MAX) {
        return (uint32_t)value;
    }
#endif
    return 1U;
}

SlStatus sl_os_platform_system_info(SlArena* arena, SlOsSystemInfo* out, SlDiag* out_diag)
{
    char host[256];
    const char* temp = getenv("TMPDIR");

    (void)out_diag;
    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOsSystemInfo){0};
    if (gethostname(host, sizeof(host)) != 0) {
        host[0] = '\0';
    }
    host[sizeof(host) - 1U] = '\0';
    if (temp == NULL || temp[0] == '\0') {
        temp = "/tmp";
    }
    SlStatus status = sl_str_copy_to_arena(arena, sl_os_posix_platform(), &out->platform);
    if (sl_status_is_ok(status)) {
        status = sl_str_copy_to_arena(arena, sl_os_posix_arch(), &out->arch);
    }
    if (sl_status_is_ok(status)) {
        status = sl_os_posix_copy_cstr(arena, temp, &out->temp_directory);
    }
    if (sl_status_is_ok(status)) {
        status = sl_os_posix_copy_cstr(arena, host, &out->hostname);
    }
    if (sl_status_is_ok(status)) {
        status = sl_str_copy_to_arena(arena, sl_str_from_cstr("\n"), &out->end_of_line);
    }
    out->cpu_count = sl_os_posix_cpu_count();
    return status;
}

SlStatus sl_os_platform_environment_get(SlArena* arena, SlStr key, SlOwnedStr* out_value,
                                        bool* out_found, SlDiag* out_diag)
{
    SlOwnedStr key_cstr = {0};
    const char* value = NULL;
    SlStatus status;

    (void)out_diag;
    if (arena == NULL || out_value == NULL || out_found == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_value = (SlOwnedStr){0};
    *out_found = false;
    status = sl_str_copy_to_arena_nul(arena, key, &key_cstr);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    value = getenv(key_cstr.ptr);
    if (value == NULL) {
        return sl_status_ok();
    }
    *out_found = true;
    return sl_str_copy_to_arena(arena, sl_str_from_cstr(value), out_value);
}

SlStatus sl_os_platform_environment_has(SlStr key, bool* out_found, SlDiag* out_diag)
{
    unsigned char storage[512];
    SlArena arena = {0};
    SlOwnedStr key_cstr = {0};
    SlStatus status;

    (void)out_diag;
    if (out_found == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_found = false;
    status = sl_arena_init(&arena, storage, sizeof(storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_str_copy_to_arena_nul(&arena, key, &key_cstr);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out_found = getenv(key_cstr.ptr) != NULL;
    return sl_status_ok();
}

SlStatus sl_os_platform_environment_list(SlArena* arena, SlStr prefix, SlOsEnvironmentList* out,
                                         SlDiag* out_diag)
{
    size_t count = 0U;
    size_t index = 0U;
    char** cursor = environ;
    void* memory = NULL;
    SlStatus status;

    (void)out_diag;
    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOsEnvironmentList){0};
    while (cursor != NULL && cursor[count] != NULL) {
        SlStr entry = sl_str_from_cstr(cursor[count]);
        const char* equals = strchr(cursor[count], '=');
        SlStr key = sl_str_from_parts(
            cursor[count], equals == NULL ? entry.length : (size_t)(equals - cursor[count]));
        if (sl_str_starts_with(key, prefix)) {
            out->count += 1U;
        }
        count += 1U;
    }
    if (out->count == 0U) {
        return sl_status_ok();
    }
    status = sl_arena_alloc(arena, out->count * sizeof(SlOsEnvironmentEntry),
                            _Alignof(SlOsEnvironmentEntry), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    out->entries = (SlOsEnvironmentEntry*)memory;
    for (size_t scan = 0U; scan < count; scan += 1U) {
        SlStr entry = sl_str_from_cstr(environ[scan]);
        const char* equals = strchr(environ[scan], '=');
        SlStr key = sl_str_from_parts(
            environ[scan], equals == NULL ? entry.length : (size_t)(equals - environ[scan]));
        if (sl_str_starts_with(key, prefix)) {
            status = sl_str_copy_to_arena(arena, key, &out->entries[index].key);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            index += 1U;
        }
    }
    return sl_status_ok();
}

static SlDiag sl_os_posix_process_diag(SlDiagCode code, SlStr message, SlStr hint)
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

static SlStatus sl_os_posix_process_fail(SlDiagCode code, SlDiag* out_diag, SlStr message,
                                         SlStr hint)
{
    if (out_diag != NULL) {
        *out_diag = sl_os_posix_process_diag(code, message, hint);
    }
    return sl_status_from_code(SL_STATUS_INVALID_STATE);
}

static uint64_t sl_os_posix_now_ms(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0U;
    }
    return ((uint64_t)now.tv_sec * 1000U) + ((uint64_t)now.tv_nsec / 1000000U);
}

static void sl_os_posix_sleep_poll(void)
{
    struct timespec delay = {.tv_sec = 0, .tv_nsec = 5000000L};
    (void)nanosleep(&delay, NULL);
}

static SlStatus sl_os_posix_copy_nul(SlArena* arena, SlStr value, char** out)
{
    SlOwnedStr owned = {0};
    SlStatus status = sl_str_copy_to_arena_nul(arena, value, &owned);

    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out = owned.ptr;
    return sl_status_ok();
}

static void sl_os_posix_set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags >= 0) {
        (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

static void sl_os_posix_capture_read(int fd, char* buffer, size_t capacity, size_t* inout_used,
                                     bool* out_truncated)
{
    char chunk[4096];
    ssize_t got;

    if (fd < 0) {
        return;
    }
    got = read(fd, chunk, sizeof(chunk));
    if (got > 0) {
        size_t available = capacity > *inout_used ? capacity - *inout_used : 0U;
        size_t copy = (size_t)got < available ? (size_t)got : available;
        if (copy != 0U) {
            for (size_t index = 0U; index < copy; index += 1U) {
                buffer[*inout_used + index] = chunk[index];
            }
            *inout_used += copy;
        }
        if (copy != (size_t)got) {
            *out_truncated = true;
        }
    }
}

static bool sl_os_posix_env_entry_overridden(const char* entry,
                                             const SlOsEnvironmentOverride* overrides,
                                             size_t override_count)
{
    const char* equals = strchr(entry, '=');
    SlStr key = {0};

    if (equals == NULL || equals == entry) {
        return false;
    }
    key = sl_str_from_parts(entry, (size_t)(equals - entry));
    for (size_t index = 0U; index < override_count; index += 1U) {
        if (sl_str_equal(key, overrides[index].key)) {
            return true;
        }
    }
    return false;
}

static SlStatus sl_os_posix_env_assignment(SlArena* arena, SlOsEnvironmentOverride entry,
                                           char** out)
{
    size_t length = entry.key.length + 1U + entry.value.length;
    size_t offset = 0U;
    void* memory = NULL;
    SlStatus status = sl_arena_alloc(arena, length + 1U, _Alignof(char), &memory);

    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out = (char*)memory;
    for (size_t index = 0U; index < entry.key.length; index += 1U) {
        (*out)[offset++] = entry.key.ptr[index];
    }
    (*out)[offset++] = '=';
    for (size_t index = 0U; index < entry.value.length; index += 1U) {
        (*out)[offset++] = entry.value.ptr[index];
    }
    (*out)[offset] = '\0';
    return sl_status_ok();
}

static SlStatus sl_os_posix_environment_block(SlArena* arena,
                                              const SlOsEnvironmentOverride* overrides,
                                              size_t override_count, char*** out)
{
    size_t inherited_count = 0U;
    size_t offset = 0U;
    void* memory = NULL;
    SlStatus status;

    for (char** cursor = environ; cursor != NULL && *cursor != NULL; cursor += 1) {
        if (!sl_os_posix_env_entry_overridden(*cursor, overrides, override_count)) {
            inherited_count += 1U;
        }
    }
    status = sl_arena_alloc(arena, (inherited_count + override_count + 1U) * sizeof(char*),
                            _Alignof(char*), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out = (char**)memory;
    for (char** cursor = environ; cursor != NULL && *cursor != NULL; cursor += 1) {
        if (!sl_os_posix_env_entry_overridden(*cursor, overrides, override_count)) {
            (*out)[offset++] = *cursor;
        }
    }
    for (size_t index = 0U; index < override_count; index += 1U) {
        status = sl_os_posix_env_assignment(arena, overrides[index], &(*out)[offset]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        offset += 1U;
    }
    (*out)[offset] = NULL;
    return sl_status_ok();
}

static bool sl_os_posix_has_slash(const char* value)
{
    for (size_t index = 0U; value[index] != '\0'; index += 1U) {
        if (value[index] == '/') {
            return true;
        }
    }
    return false;
}

static const char* sl_os_posix_env_value(char** envp, const char* key)
{
    size_t key_length = sl_str_from_cstr(key).length;

    for (char** cursor = envp; cursor != NULL && *cursor != NULL; cursor += 1) {
        if (strncmp(*cursor, key, key_length) == 0 && (*cursor)[key_length] == '=') {
            return *cursor + key_length + 1U;
        }
    }
    return NULL;
}

static SlStatus sl_os_posix_join_path(SlArena* arena, const char* directory, size_t directory_len,
                                      const char* command, char** out)
{
    size_t command_len = sl_str_from_cstr(command).length;
    size_t separator = directory_len == 0U ? 0U : 1U;
    size_t offset = 0U;
    void* memory = NULL;
    SlStatus status = sl_arena_alloc(arena, directory_len + separator + command_len + 1U,
                                     _Alignof(char), &memory);

    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out = (char*)memory;
    for (size_t index = 0U; index < directory_len; index += 1U) {
        (*out)[offset++] = directory[index];
    }
    if (separator != 0U) {
        (*out)[offset++] = '/';
    }
    for (size_t index = 0U; index < command_len; index += 1U) {
        (*out)[offset++] = command[index];
    }
    (*out)[offset] = '\0';
    return sl_status_ok();
}

static SlStatus sl_os_posix_resolve_command(SlArena* arena, const char* command, char** envp,
                                            char** out)
{
    const char* path = NULL;
    const char* segment = NULL;

    if (sl_os_posix_has_slash(command)) {
        *out = (char*)command;
        return sl_status_ok();
    }
    path = sl_os_posix_env_value(envp, "PATH");
    if (path == NULL || path[0] == '\0') {
        path = "/usr/bin:/bin";
    }
    segment = path;
    for (;;) {
        const char* end = segment;
        char* candidate = NULL;
        while (*end != '\0' && *end != ':') {
            end += 1;
        }
        if (end == segment) {
            SlStatus status = sl_os_posix_join_path(arena, ".", 1U, command, &candidate);
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
        else {
            SlStatus status =
                sl_os_posix_join_path(arena, segment, (size_t)(end - segment), command, &candidate);
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
        if (access(candidate, X_OK) == 0) {
            *out = candidate;
            return sl_status_ok();
        }
        if (*end == '\0') {
            break;
        }
        segment = end + 1;
    }
    *out = NULL;
    return sl_status_ok();
}

SlStatus sl_os_platform_process_run(SlArena* arena, SlStr command, const SlStr* args,
                                    size_t arg_count, const SlOsProcessRunOptions* options,
                                    SlOsProcessRunResult* out, SlDiag* out_diag)
{
    char** argv = NULL;
    char** envp = NULL;
    char* command_cstr = NULL;
    char* exec_path = NULL;
    char* cwd_cstr = NULL;
    char* stdout_buffer = NULL;
    char* stderr_buffer = NULL;
    size_t stdout_capacity = 0U;
    size_t stderr_capacity = 0U;
    size_t stdout_used = 0U;
    size_t stderr_used = 0U;
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    int error_pipe[2] = {-1, -1};
    pid_t child = -1;
    uint64_t start_ms = 0U;
    bool capture = options->capture != SL_OS_PROCESS_CAPTURE_NONE;
    SlStatus status;
    void* memory = NULL;

    (void)out_diag;
    *out = (SlOsProcessRunResult){0};
    status = sl_os_posix_copy_nul(arena, command, &command_cstr);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, (arg_count + 2U) * sizeof(char*), _Alignof(char*), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    argv = (char**)memory;
    argv[0] = command_cstr;
    for (size_t index = 0U; index < arg_count; index += 1U) {
        status = sl_os_posix_copy_nul(arena, args[index], &argv[index + 1U]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    argv[arg_count + 1U] = NULL;
    if (!sl_str_is_empty(options->cwd)) {
        struct stat cwd_stat;
        status = sl_os_posix_copy_nul(arena, options->cwd, &cwd_cstr);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (stat(cwd_cstr, &cwd_stat) != 0 || !S_ISDIR(cwd_stat.st_mode)) {
            return sl_os_posix_process_fail(
                SL_DIAG_OS_INVALID_CWD, out_diag,
                sl_str_from_cstr("process working directory is invalid"),
                sl_str_from_cstr("Validate cwd before process admission."));
        }
    }
    status = sl_os_posix_environment_block(arena, options->environment_overrides,
                                           options->environment_override_count, &envp);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_os_posix_resolve_command(arena, command_cstr, envp, &exec_path);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (exec_path == NULL) {
        return sl_os_posix_process_fail(
            SL_DIAG_OS_COMMAND_NOT_FOUND, out_diag, sl_str_from_cstr("command was not found"),
            sl_str_from_cstr("Process APIs execute explicit argv only."));
    }
    if (capture) {
        stdout_capacity = options->max_stdout_bytes == 0U ? 65536U : options->max_stdout_bytes;
        stderr_capacity = options->max_stderr_bytes == 0U ? 65536U : options->max_stderr_bytes;
        status = sl_arena_alloc(arena, stdout_capacity + 1U, _Alignof(char), &memory);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        stdout_buffer = (char*)memory;
        status = sl_arena_alloc(arena, stderr_capacity + 1U, _Alignof(char), &memory);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        stderr_buffer = (char*)memory;
        if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
    }
    if (pipe(error_pipe) != 0) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    (void)fcntl(error_pipe[1], F_SETFD, FD_CLOEXEC);
    child = fork();
    if (child < 0) {
        return sl_os_posix_process_fail(SL_DIAG_OS_PROCESS_START_FAILED, out_diag,
                                        sl_str_from_cstr("process start failed"),
                                        sl_str_from_cstr("fork failed before process admission."));
    }
    if (child == 0) {
        int devnull = -1;
        if (cwd_cstr != NULL && chdir(cwd_cstr) != 0) {
            int err = errno;
            (void)write(error_pipe[1], &err, sizeof(err));
            _exit(126);
        }
        if (capture) {
            (void)dup2(stdout_pipe[1], STDOUT_FILENO);
            (void)dup2(stderr_pipe[1], STDERR_FILENO);
        }
        else {
            devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                (void)dup2(devnull, STDOUT_FILENO);
                (void)dup2(devnull, STDERR_FILENO);
            }
        }
        execve(exec_path, argv, envp);
        {
            int err = errno;
            (void)write(error_pipe[1], &err, sizeof(err));
            _exit(err == ENOENT ? 127 : 126);
        }
    }
    if (capture) {
        (void)close(stdout_pipe[1]);
        (void)close(stderr_pipe[1]);
        sl_os_posix_set_nonblock(stdout_pipe[0]);
        sl_os_posix_set_nonblock(stderr_pipe[0]);
    }
    (void)close(error_pipe[1]);
    start_ms = sl_os_posix_now_ms();
    for (;;) {
        int child_status = 0;
        pid_t waited;
        if (capture) {
            sl_os_posix_capture_read(stdout_pipe[0], stdout_buffer, stdout_capacity, &stdout_used,
                                     &out->stdout_truncated);
            sl_os_posix_capture_read(stderr_pipe[0], stderr_buffer, stderr_capacity, &stderr_used,
                                     &out->stderr_truncated);
        }
        waited = waitpid(child, &child_status, WNOHANG);
        if (waited == child) {
            if (WIFEXITED(child_status)) {
                out->exit_code = (int32_t)WEXITSTATUS(child_status);
            }
            else if (WIFSIGNALED(child_status)) {
                out->exit_code = 128 + (int32_t)WTERMSIG(child_status);
            }
            break;
        }
        if (options->timeout_ms != 0U && start_ms != 0U &&
            sl_os_posix_now_ms() - start_ms >= options->timeout_ms)
        {
            (void)kill(child, SIGKILL);
            (void)waitpid(child, &child_status, 0);
            out->timed_out = true;
            out->exit_code = 128 + SIGKILL;
            break;
        }
        sl_os_posix_sleep_poll();
    }
    if (capture) {
        sl_os_posix_capture_read(stdout_pipe[0], stdout_buffer, stdout_capacity, &stdout_used,
                                 &out->stdout_truncated);
        sl_os_posix_capture_read(stderr_pipe[0], stderr_buffer, stderr_capacity, &stderr_used,
                                 &out->stderr_truncated);
        stdout_buffer[stdout_used] = '\0';
        stderr_buffer[stderr_used] = '\0';
        out->stdout_text = (SlOwnedStr){.ptr = stdout_buffer, .length = stdout_used};
        out->stderr_text = (SlOwnedStr){.ptr = stderr_buffer, .length = stderr_used};
        (void)close(stdout_pipe[0]);
        (void)close(stderr_pipe[0]);
    }
    {
        int exec_error = 0;
        ssize_t got = read(error_pipe[0], &exec_error, sizeof(exec_error));
        (void)close(error_pipe[0]);
        if (got == (ssize_t)sizeof(exec_error)) {
            if (exec_error == ENOENT) {
                return sl_os_posix_process_fail(
                    SL_DIAG_OS_COMMAND_NOT_FOUND, out_diag,
                    sl_str_from_cstr("command was not found"),
                    sl_str_from_cstr("Process APIs execute explicit argv only."));
            }
            if (exec_error == ENOTDIR) {
                return sl_os_posix_process_fail(
                    SL_DIAG_OS_INVALID_CWD, out_diag,
                    sl_str_from_cstr("process working directory is invalid"),
                    sl_str_from_cstr("Validate cwd before process admission."));
            }
            return sl_os_posix_process_fail(SL_DIAG_OS_PROCESS_START_FAILED, out_diag,
                                            sl_str_from_cstr("process start failed"),
                                            sl_str_from_cstr("native process admission failed."));
        }
    }
    if (out->timed_out) {
        return sl_os_posix_process_fail(
            SL_DIAG_OS_PROCESS_TIMEOUT, out_diag, sl_str_from_cstr("process timed out"),
            sl_str_from_cstr("Timeout and caller cancellation remain distinguishable."));
    }
    return sl_status_ok();
}
