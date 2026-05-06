#include "../../core/os_platform.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdint.h>
#include <string.h>

static SlStatus sl_os_win32_copy_cstr(SlArena* arena, const char* value, SlOwnedStr* out)
{
    return sl_str_copy_to_arena(arena, sl_str_from_cstr(value == NULL ? "" : value), out);
}

static SlStr sl_os_win32_arch(void)
{
#if defined(_M_X64) || defined(__x86_64__)
    return sl_str_from_cstr("x64");
#elif defined(_M_ARM64) || defined(__aarch64__)
    return sl_str_from_cstr("arm64");
#elif defined(_M_IX86) || defined(__i386__)
    return sl_str_from_cstr("ia32");
#else
    return sl_str_from_cstr("unknown");
#endif
}

SlStatus sl_os_platform_system_info(SlArena* arena, SlOsSystemInfo* out, SlDiag* out_diag)
{
    SYSTEM_INFO system_info;
    char temp[MAX_PATH + 1U];
    char host[MAX_COMPUTERNAME_LENGTH + 1U];
    DWORD host_len = MAX_COMPUTERNAME_LENGTH + 1U;
    DWORD temp_len = 0U;
    SlStatus status;

    (void)out_diag;
    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOsSystemInfo){0};
    GetSystemInfo(&system_info);
    temp_len = GetTempPathA((DWORD)sizeof(temp), temp);
    if (temp_len == 0U || temp_len >= sizeof(temp)) {
        temp[0] = '\0';
    }
    if (!GetComputerNameA(host, &host_len)) {
        host[0] = '\0';
    }
    status = sl_str_copy_to_arena(arena, sl_str_from_cstr("windows"), &out->platform);
    if (sl_status_is_ok(status)) {
        status = sl_str_copy_to_arena(arena, sl_os_win32_arch(), &out->arch);
    }
    if (sl_status_is_ok(status)) {
        status = sl_os_win32_copy_cstr(arena, temp, &out->temp_directory);
    }
    if (sl_status_is_ok(status)) {
        status = sl_os_win32_copy_cstr(arena, host, &out->hostname);
    }
    if (sl_status_is_ok(status)) {
        status = sl_str_copy_to_arena(arena, sl_str_from_cstr("\r\n"), &out->end_of_line);
    }
    out->cpu_count = system_info.dwNumberOfProcessors == 0U ? 1U : system_info.dwNumberOfProcessors;
    return status;
}

SlStatus sl_os_platform_environment_get(SlArena* arena, SlStr key, SlOwnedStr* out_value,
                                        bool* out_found, SlDiag* out_diag)
{
    SlOwnedStr key_cstr = {0};
    DWORD required = 0U;
    void* memory = NULL;
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
    required = GetEnvironmentVariableA(key_cstr.ptr, NULL, 0U);
    if (required == 0U) {
        return sl_status_ok();
    }
    status = sl_arena_alloc(arena, (size_t)required, _Alignof(char), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (GetEnvironmentVariableA(key_cstr.ptr, (char*)memory, required) >= required) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    out_value->ptr = (char*)memory;
    out_value->length = sl_str_from_cstr((const char*)memory).length;
    *out_found = true;
    return sl_status_ok();
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
    *out_found = GetEnvironmentVariableA(key_cstr.ptr, NULL, 0U) != 0U;
    return sl_status_ok();
}

SlStatus sl_os_platform_environment_list(SlArena* arena, SlStr prefix, SlOsEnvironmentList* out,
                                         SlDiag* out_diag)
{
    LPCH block = NULL;
    const char* cursor = NULL;
    size_t count = 0U;
    size_t index = 0U;
    void* memory = NULL;
    SlStatus status;

    (void)out_diag;
    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOsEnvironmentList){0};
    block = GetEnvironmentStringsA();
    if (block == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    for (cursor = block; *cursor != '\0';) {
        SlStr entry = sl_str_from_cstr(cursor);
        const char* equals = strchr(cursor, '=');
        SlStr key =
            sl_str_from_parts(cursor, equals == NULL ? entry.length : (size_t)(equals - cursor));
        if (key.length != 0U && sl_str_starts_with(key, prefix)) {
            count += 1U;
        }
        cursor += entry.length + 1U;
    }
    if (count != 0U) {
        status = sl_arena_alloc(arena, count * sizeof(SlOsEnvironmentEntry),
                                _Alignof(SlOsEnvironmentEntry), &memory);
        if (!sl_status_is_ok(status)) {
            FreeEnvironmentStringsA(block);
            return status;
        }
        out->entries = (SlOsEnvironmentEntry*)memory;
        out->count = count;
        for (cursor = block; *cursor != '\0';) {
            SlStr entry = sl_str_from_cstr(cursor);
            const char* equals = strchr(cursor, '=');
            SlStr key = sl_str_from_parts(cursor, equals == NULL ? entry.length
                                                                 : (size_t)(equals - cursor));
            if (key.length != 0U && sl_str_starts_with(key, prefix)) {
                status = sl_str_copy_to_arena(arena, key, &out->entries[index].key);
                if (!sl_status_is_ok(status)) {
                    FreeEnvironmentStringsA(block);
                    return status;
                }
                index += 1U;
            }
            cursor += entry.length + 1U;
        }
    }
    FreeEnvironmentStringsA(block);
    return sl_status_ok();
}

static SlDiag sl_os_win32_process_diag(SlDiagCode code, SlStr message, SlStr hint)
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

static SlStatus sl_os_win32_process_fail(SlDiagCode code, SlDiag* out_diag, SlStr message,
                                         SlStr hint)
{
    if (out_diag != NULL) {
        *out_diag = sl_os_win32_process_diag(code, message, hint);
    }
    return sl_status_from_code(SL_STATUS_INVALID_STATE);
}

static size_t sl_os_win32_quoted_length(SlStr value)
{
    size_t length = 2U;
    size_t backslashes = 0U;

    for (size_t index = 0U; index < value.length; index += 1U) {
        if (value.ptr[index] == '\\') {
            length += 1U;
            backslashes += 1U;
            continue;
        }
        if (value.ptr[index] == '"') {
            length += backslashes + 2U;
            backslashes = 0U;
            continue;
        }
        length += 1U;
        backslashes = 0U;
    }
    return length + backslashes;
}

static void sl_os_win32_append_quoted(char* target, size_t* inout_offset, SlStr value)
{
    size_t backslashes = 0U;

    target[(*inout_offset)++] = '"';
    for (size_t index = 0U; index < value.length; index += 1U) {
        if (value.ptr[index] == '\\') {
            target[(*inout_offset)++] = '\\';
            backslashes += 1U;
            continue;
        }
        if (value.ptr[index] == '"') {
            for (size_t slash = 0U; slash < backslashes; slash += 1U) {
                target[(*inout_offset)++] = '\\';
            }
            target[(*inout_offset)++] = '\\';
            target[(*inout_offset)++] = '"';
            backslashes = 0U;
            continue;
        }
        target[(*inout_offset)++] = value.ptr[index];
        backslashes = 0U;
    }
    for (size_t slash = 0U; slash < backslashes; slash += 1U) {
        target[(*inout_offset)++] = '\\';
    }
    target[(*inout_offset)++] = '"';
}

static SlStatus sl_os_win32_command_line(SlArena* arena, SlStr command, const SlStr* args,
                                         size_t arg_count, char** out)
{
    size_t length = sl_os_win32_quoted_length(command);
    size_t offset = 0U;
    void* memory = NULL;
    SlStatus status;

    for (size_t index = 0U; index < arg_count; index += 1U) {
        length += 1U + sl_os_win32_quoted_length(args[index]);
    }
    status = sl_arena_alloc(arena, length + 1U, _Alignof(char), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out = (char*)memory;
    sl_os_win32_append_quoted(*out, &offset, command);
    for (size_t index = 0U; index < arg_count; index += 1U) {
        (*out)[offset++] = ' ';
        sl_os_win32_append_quoted(*out, &offset, args[index]);
    }
    (*out)[offset] = '\0';
    return sl_status_ok();
}

static void sl_os_win32_capture_read(HANDLE pipe, char* buffer, size_t capacity, size_t* inout_used,
                                     bool* out_truncated)
{
    char chunk[4096];
    DWORD available = 0U;
    DWORD got = 0U;
    DWORD remaining = 0U;

    if (!PeekNamedPipe(pipe, NULL, 0U, NULL, &available, NULL) || available == 0U) {
        return;
    }
    if (!ReadFile(pipe, chunk, available < sizeof(chunk) ? available : (DWORD)sizeof(chunk), &got,
                  NULL) ||
        got == 0U)
    {
        return;
    }
    {
        size_t open = capacity > *inout_used ? capacity - *inout_used : 0U;
        size_t copy = (size_t)got < open ? (size_t)got : open;
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
    if (PeekNamedPipe(pipe, NULL, 0U, NULL, &remaining, NULL) && remaining != 0U &&
        *inout_used >= capacity)
    {
        *out_truncated = true;
    }
}

static void sl_os_win32_capture_drain(HANDLE pipe, char* buffer, size_t capacity,
                                      size_t* inout_used, bool* out_truncated)
{
    for (;;) {
        DWORD available = 0U;
        if (!PeekNamedPipe(pipe, NULL, 0U, NULL, &available, NULL) || available == 0U) {
            break;
        }
        sl_os_win32_capture_read(pipe, buffer, capacity, inout_used, out_truncated);
        if (*inout_used >= capacity) {
            *out_truncated = true;
            break;
        }
    }
}

static void sl_os_win32_close_handle(HANDLE* handle)
{
    if (*handle != NULL && *handle != INVALID_HANDLE_VALUE) {
        CloseHandle(*handle);
        *handle = NULL;
    }
}

static char sl_os_win32_ascii_lower(char value)
{
    if (value >= 'A' && value <= 'Z') {
        return (char)(value - 'A' + 'a');
    }
    return value;
}

static bool sl_os_win32_env_key_equal(SlStr left, SlStr right)
{
    if (left.length != right.length) {
        return false;
    }
    for (size_t index = 0U; index < left.length; index += 1U) {
        if (sl_os_win32_ascii_lower(left.ptr[index]) != sl_os_win32_ascii_lower(right.ptr[index])) {
            return false;
        }
    }
    return true;
}

static bool sl_os_win32_env_entry_overridden(const char* entry,
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
        if (sl_os_win32_env_key_equal(key, overrides[index].key)) {
            return true;
        }
    }
    return false;
}

static void sl_os_win32_copy_cstr_bytes(char* target, size_t* inout_offset, const char* source)
{
    for (size_t index = 0U; source[index] != '\0'; index += 1U) {
        target[(*inout_offset)++] = source[index];
    }
}

static void sl_os_win32_copy_str_bytes(char* target, size_t* inout_offset, SlStr source)
{
    for (size_t index = 0U; index < source.length; index += 1U) {
        target[(*inout_offset)++] = source.ptr[index];
    }
}

static SlStatus sl_os_win32_environment_block(SlArena* arena,
                                              const SlOsEnvironmentOverride* overrides,
                                              size_t override_count, char** out)
{
    LPCH inherited = NULL;
    const char* cursor = NULL;
    size_t total = 1U;
    size_t offset = 0U;
    void* memory = NULL;
    SlStatus status;

    *out = NULL;
    if (override_count == 0U) {
        return sl_status_ok();
    }
    inherited = GetEnvironmentStringsA();
    if (inherited == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    for (cursor = inherited; *cursor != '\0';) {
        size_t length = sl_str_from_cstr(cursor).length;
        if (!sl_os_win32_env_entry_overridden(cursor, overrides, override_count)) {
            total += length + 1U;
        }
        cursor += length + 1U;
    }
    for (size_t index = 0U; index < override_count; index += 1U) {
        total += overrides[index].key.length + 1U + overrides[index].value.length + 1U;
    }
    status = sl_arena_alloc(arena, total, _Alignof(char), &memory);
    if (!sl_status_is_ok(status)) {
        FreeEnvironmentStringsA(inherited);
        return status;
    }
    *out = (char*)memory;
    for (cursor = inherited; *cursor != '\0';) {
        size_t length = sl_str_from_cstr(cursor).length;
        if (!sl_os_win32_env_entry_overridden(cursor, overrides, override_count)) {
            sl_os_win32_copy_cstr_bytes(*out, &offset, cursor);
            (*out)[offset++] = '\0';
        }
        cursor += length + 1U;
    }
    for (size_t index = 0U; index < override_count; index += 1U) {
        sl_os_win32_copy_str_bytes(*out, &offset, overrides[index].key);
        (*out)[offset++] = '=';
        sl_os_win32_copy_str_bytes(*out, &offset, overrides[index].value);
        (*out)[offset++] = '\0';
    }
    (*out)[offset++] = '\0';
    FreeEnvironmentStringsA(inherited);
    return sl_status_ok();
}

SlStatus sl_os_platform_process_run(SlArena* arena, SlStr command, const SlStr* args,
                                    size_t arg_count, const SlOsProcessRunOptions* options,
                                    SlOsProcessRunResult* out, SlDiag* out_diag)
{
    SECURITY_ATTRIBUTES security = {
        .nLength = sizeof(security), .lpSecurityDescriptor = NULL, .bInheritHandle = TRUE};
    STARTUPINFOEXA startup = {0};
    PROCESS_INFORMATION process = {0};
    HANDLE stdout_read = NULL;
    HANDLE stdout_write = NULL;
    HANDLE stderr_read = NULL;
    HANDLE stderr_write = NULL;
    HANDLE stdin_read = INVALID_HANDLE_VALUE;
    HANDLE devnull = INVALID_HANDLE_VALUE;
    HANDLE inherited_handles[3] = {0};
    SIZE_T inherited_handle_count = 0U;
    SIZE_T attribute_size = 0U;
    SlOwnedStr application_name = {0};
    char* command_line = NULL;
    char* cwd = NULL;
    char* environment_block = NULL;
    char* stdout_buffer = NULL;
    char* stderr_buffer = NULL;
    size_t stdout_capacity = 0U;
    size_t stderr_capacity = 0U;
    size_t stdout_used = 0U;
    size_t stderr_used = 0U;
    bool capture = options->capture != SL_OS_PROCESS_CAPTURE_NONE;
    DWORD creation_flags = CREATE_NO_WINDOW;
    uint64_t elapsed_ms = 0U;
    void* memory = NULL;
    SlStatus status;

    *out = (SlOsProcessRunResult){0};
    status = sl_os_win32_command_line(arena, command, args, arg_count, &command_line);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_str_copy_to_arena_nul(arena, command, &application_name);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!sl_str_is_empty(options->cwd)) {
        SlOwnedStr owned = {0};
        DWORD attributes = 0U;
        status = sl_str_copy_to_arena_nul(arena, options->cwd, &owned);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        cwd = owned.ptr;
        attributes = GetFileAttributesA(cwd);
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U)
        {
            return sl_os_win32_process_fail(
                SL_DIAG_OS_INVALID_CWD, out_diag,
                sl_str_from_cstr("process working directory is invalid"),
                sl_str_from_cstr("Validate cwd before process admission."));
        }
    }
    status = sl_os_win32_environment_block(arena, options->environment_overrides,
                                           options->environment_override_count, &environment_block);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
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
        if (!CreatePipe(&stdout_read, &stdout_write, &security, 0U)) {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        if (!CreatePipe(&stderr_read, &stderr_write, &security, 0U)) {
            sl_os_win32_close_handle(&stdout_read);
            sl_os_win32_close_handle(&stdout_write);
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        (void)SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0U);
        (void)SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0U);
        startup.StartupInfo.hStdOutput = stdout_write;
        startup.StartupInfo.hStdError = stderr_write;
        inherited_handles[inherited_handle_count++] = stdout_write;
        inherited_handles[inherited_handle_count++] = stderr_write;
    }
    else {
        devnull = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (devnull == INVALID_HANDLE_VALUE) {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        startup.StartupInfo.hStdOutput = devnull;
        startup.StartupInfo.hStdError = devnull;
        inherited_handles[inherited_handle_count++] = devnull;
    }
    stdin_read = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (stdin_read == INVALID_HANDLE_VALUE) {
        sl_os_win32_close_handle(&stdout_read);
        sl_os_win32_close_handle(&stdout_write);
        sl_os_win32_close_handle(&stderr_read);
        sl_os_win32_close_handle(&stderr_write);
        sl_os_win32_close_handle(&devnull);
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    startup.StartupInfo.hStdInput = stdin_read;
    inherited_handles[inherited_handle_count++] = stdin_read;
    InitializeProcThreadAttributeList(NULL, 1U, 0U, &attribute_size);
    status = sl_arena_alloc(arena, attribute_size, _Alignof(void*), &memory);
    if (!sl_status_is_ok(status)) {
        sl_os_win32_close_handle(&stdout_read);
        sl_os_win32_close_handle(&stdout_write);
        sl_os_win32_close_handle(&stderr_read);
        sl_os_win32_close_handle(&stderr_write);
        sl_os_win32_close_handle(&stdin_read);
        sl_os_win32_close_handle(&devnull);
        return status;
    }
    startup.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)memory;
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1U, 0U, &attribute_size) ||
        !UpdateProcThreadAttribute(startup.lpAttributeList, 0U, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   inherited_handles, inherited_handle_count * sizeof(HANDLE), NULL,
                                   NULL))
    {
        sl_os_win32_close_handle(&stdout_read);
        sl_os_win32_close_handle(&stdout_write);
        sl_os_win32_close_handle(&stderr_read);
        sl_os_win32_close_handle(&stderr_write);
        sl_os_win32_close_handle(&stdin_read);
        sl_os_win32_close_handle(&devnull);
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    creation_flags |= EXTENDED_STARTUPINFO_PRESENT;
    if (!CreateProcessA(application_name.ptr, command_line, NULL, NULL, TRUE, creation_flags,
                        environment_block, cwd, (LPSTARTUPINFOA)&startup, &process))
    {
        DWORD error = GetLastError();
        DeleteProcThreadAttributeList(startup.lpAttributeList);
        sl_os_win32_close_handle(&stdout_read);
        sl_os_win32_close_handle(&stdout_write);
        sl_os_win32_close_handle(&stderr_read);
        sl_os_win32_close_handle(&stderr_write);
        sl_os_win32_close_handle(&stdin_read);
        sl_os_win32_close_handle(&devnull);
        if (error == ERROR_DIRECTORY) {
            return sl_os_win32_process_fail(
                SL_DIAG_OS_INVALID_CWD, out_diag,
                sl_str_from_cstr("process working directory is invalid"),
                sl_str_from_cstr("Validate cwd before process admission."));
        }
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return sl_os_win32_process_fail(
                SL_DIAG_OS_COMMAND_NOT_FOUND, out_diag, sl_str_from_cstr("command was not found"),
                sl_str_from_cstr("Process APIs execute explicit argv only."));
        }
        return sl_os_win32_process_fail(SL_DIAG_OS_PROCESS_START_FAILED, out_diag,
                                        sl_str_from_cstr("process start failed"),
                                        sl_str_from_cstr("native process admission failed."));
    }
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    if (capture) {
        sl_os_win32_close_handle(&stdout_write);
        sl_os_win32_close_handle(&stderr_write);
    }
    sl_os_win32_close_handle(&stdin_read);
    sl_os_win32_close_handle(&devnull);
    for (;;) {
        DWORD wait_ms = 10U;
        DWORD wait_result = WaitForSingleObject(process.hProcess, wait_ms);
        if (capture) {
            sl_os_win32_capture_read(stdout_read, stdout_buffer, stdout_capacity, &stdout_used,
                                     &out->stdout_truncated);
            sl_os_win32_capture_read(stderr_read, stderr_buffer, stderr_capacity, &stderr_used,
                                     &out->stderr_truncated);
        }
        if (wait_result == WAIT_OBJECT_0) {
            DWORD exit_code = 0U;
            (void)GetExitCodeProcess(process.hProcess, &exit_code);
            out->exit_code = (int32_t)exit_code;
            break;
        }
        if (wait_result == WAIT_FAILED) {
            if (capture) {
                sl_os_win32_close_handle(&stdout_read);
                sl_os_win32_close_handle(&stderr_read);
            }
            sl_os_win32_close_handle(&process.hThread);
            sl_os_win32_close_handle(&process.hProcess);
            return sl_os_win32_process_fail(SL_DIAG_OS_PROCESS_START_FAILED, out_diag,
                                            sl_str_from_cstr("process wait failed"),
                                            sl_str_from_cstr("native process admission failed."));
        }
        if (options->timeout_ms != 0U) {
            static const DWORD quantum_ms = 10U;
            elapsed_ms += quantum_ms;
            if (elapsed_ms >= options->timeout_ms) {
                (void)TerminateProcess(process.hProcess, 1U);
                (void)WaitForSingleObject(process.hProcess, INFINITE);
                out->timed_out = true;
                out->exit_code = 1;
                break;
            }
        }
    }
    if (capture) {
        sl_os_win32_capture_drain(stdout_read, stdout_buffer, stdout_capacity, &stdout_used,
                                  &out->stdout_truncated);
        sl_os_win32_capture_drain(stderr_read, stderr_buffer, stderr_capacity, &stderr_used,
                                  &out->stderr_truncated);
        stdout_buffer[stdout_used] = '\0';
        stderr_buffer[stderr_used] = '\0';
        out->stdout_text = (SlOwnedStr){.ptr = stdout_buffer, .length = stdout_used};
        out->stderr_text = (SlOwnedStr){.ptr = stderr_buffer, .length = stderr_used};
        sl_os_win32_close_handle(&stdout_read);
        sl_os_win32_close_handle(&stderr_read);
    }
    sl_os_win32_close_handle(&process.hThread);
    sl_os_win32_close_handle(&process.hProcess);
    if (out->timed_out) {
        return sl_os_win32_process_fail(
            SL_DIAG_OS_PROCESS_TIMEOUT, out_diag, sl_str_from_cstr("process timed out"),
            sl_str_from_cstr("Timeout and caller cancellation remain distinguishable."));
    }
    return sl_status_ok();
}
