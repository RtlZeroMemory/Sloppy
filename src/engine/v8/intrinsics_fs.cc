/*
 * src/engine/v8/intrinsics_fs.cc
 *
 * Installs the V8-internal filesystem bridge under __sloppy.fs. Blocking filesystem work is
 * submitted to the Slop provider/offload executor; workers never enter V8, and completion
 * dispatch settles Promises on the owning isolate thread.
 */
#include "engine_v8_internal.h"
#include "string_interop.h"

#include "sloppy/fs.h"

#include <climits>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

enum class FsV8Operation
{
    ReadText,
    ReadBytes,
    WriteText,
    WriteBytes,
    AppendText,
    AppendBytes,
    Exists,
    Stat,
    Copy,
    Move,
    DeleteFile,
    AtomicWriteText,
    AtomicWriteBytes,
    DirectoryCreate,
    DirectoryList,
    DirectoryDelete,
    Symlink,
    ReadLink,
    TempFile,
    TempDirectory,
    OpenHandle,
    HandleRead,
    HandleWriteText,
    HandleWriteBytes,
    HandleSeek,
    HandleTruncate,
    HandleFlush,
    HandleSync,
    HandleClose,
    WatchOpen,
    WatchNext,
    WatchClose,
};

struct FsV8LogicalFile
{
    std::string path;
    std::vector<unsigned char> storage;
    SlArena arena = {};
    SlFsFileHandle* native_handle = nullptr;
    SlFsFileAccess access = SL_FS_FILE_ACCESS_READ;
    uint64_t position = 0U;
    bool closed = false;
    bool busy = false;
};

struct FsV8LogicalWatch
{
    std::vector<unsigned char> storage;
    SlArena arena = {};
    SlFsWatchHandle* native_watch = nullptr;
    bool closed = false;
    std::atomic_bool busy = false;
    std::atomic_bool closing = false;
};

struct FsV8Request
{
    SlV8Engine* backend = nullptr;
    FsV8Operation operation = FsV8Operation::ReadText;
    v8::Global<v8::Promise::Resolver> resolver;
    std::string path;
    std::string to_path;
    std::string text;
    std::vector<unsigned char> bytes;
    std::string result_text;
    std::vector<unsigned char> result_bytes;
    std::vector<SlFsDirectoryEntry> directory_entries;
    std::vector<std::string> directory_entry_names;
    SlResourceId resource_id = {};
    SlResourceId result_resource_id = {};
    FsV8LogicalFile* logical_file = nullptr;
    SlFsStat stat = {};
    bool bool_result = false;
    bool overwrite = false;
    bool recursive = false;
    bool create = false;
    bool directory = false;
    SlFsFileAccess access = SL_FS_FILE_ACCESS_READ;
    SlFsSeekOrigin seek_origin = SL_FS_SEEK_START;
    SlFsWatchOptions watch_options = {};
    SlFsWatchEvent watch_event = {};
    FsV8LogicalWatch* logical_watch = nullptr;
    uint64_t position = 0U;
    uint64_t truncate_size = 0U;
    int64_t seek_offset = 0;
    size_t max_bytes = 64U * 1024U;
    SlStatus status = sl_status_ok();
    std::string error;
    bool logical_file_busy_acquired = false;
};

void fs_v8_logical_file_cleanup(void* ptr, void* user)
{
    (void)user;
    FsV8LogicalFile* file = static_cast<FsV8LogicalFile*>(ptr);
    if (file != nullptr && file->native_handle != nullptr && !file->closed) {
        (void)sl_fs_file_close(file->native_handle, nullptr);
        file->native_handle = nullptr;
        file->closed = true;
    }
    delete file;
}

void fs_v8_logical_watch_cleanup(void* ptr, void* user)
{
    (void)user;
    FsV8LogicalWatch* watch = static_cast<FsV8LogicalWatch*>(ptr);
    if (watch != nullptr && watch->native_watch != nullptr && !watch->closed) {
        (void)sl_fs_watch_close(watch->native_watch, nullptr);
        watch->native_watch = nullptr;
        watch->closed = true;
    }
    delete watch;
}

SlStatus fs_v8_to_local_string(v8::Isolate* isolate, SlStr str, v8::Local<v8::String>* out)
{
    SlV8Engine* backend =
        isolate == nullptr ? nullptr : static_cast<SlV8Engine*>(isolate->GetData(0));
    return sl_v8_string_from_native_view(backend, str, out);
}

bool fs_v8_value_to_std_string(v8::Isolate* isolate, v8::Local<v8::Value> value, std::string* out)
{
    return out != nullptr && value->IsString() && sl_v8_std_string_from_value(isolate, value, out);
}

void fs_v8_throw_type_error(v8::Isolate* isolate, const char* message)
{
    v8::Local<v8::String> local_message;
    if (!sl_status_is_ok(fs_v8_to_local_string(isolate, sl_str_from_cstr(message), &local_message)))
    {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Sloppy filesystem type error")));
        return;
    }
    isolate->ThrowException(v8::Exception::TypeError(local_message));
}

std::string fs_v8_diag_message(const SlDiag& diag, const char* fallback)
{
    if (diag.message.ptr != nullptr && diag.message.length != 0U) {
        return std::string(diag.message.ptr, diag.message.length);
    }
    return fallback == nullptr ? "filesystem operation failed" : fallback;
}

SlCapabilityOperation fs_v8_access_capability(SlFsFileAccess access)
{
    switch (access) {
    case SL_FS_FILE_ACCESS_READ:
        return SL_CAPABILITY_OPERATION_READ;
    case SL_FS_FILE_ACCESS_WRITE:
        return SL_CAPABILITY_OPERATION_WRITE;
    case SL_FS_FILE_ACCESS_READWRITE:
        return SL_CAPABILITY_OPERATION_READWRITE;
    case SL_FS_FILE_ACCESS_APPEND:
        return SL_CAPABILITY_OPERATION_APPEND;
    default:
        return SL_CAPABILITY_OPERATION_READWRITE;
    }
}

SlCapabilityOperation fs_v8_capability_operation(const FsV8Request* request)
{
    FsV8Operation operation = request == nullptr ? FsV8Operation::ReadText : request->operation;

    switch (operation) {
    case FsV8Operation::ReadText:
    case FsV8Operation::ReadBytes:
        return SL_CAPABILITY_OPERATION_READ;
    case FsV8Operation::WriteText:
    case FsV8Operation::WriteBytes:
    case FsV8Operation::AtomicWriteText:
    case FsV8Operation::AtomicWriteBytes:
    case FsV8Operation::DirectoryCreate:
    case FsV8Operation::Symlink:
    case FsV8Operation::TempFile:
    case FsV8Operation::TempDirectory:
    case FsV8Operation::HandleFlush:
    case FsV8Operation::HandleSync:
    case FsV8Operation::HandleClose:
        return SL_CAPABILITY_OPERATION_WRITE;
    case FsV8Operation::OpenHandle:
        return fs_v8_access_capability(request == nullptr ? SL_FS_FILE_ACCESS_READ
                                                          : request->access);
    case FsV8Operation::HandleWriteText:
    case FsV8Operation::HandleWriteBytes:
    case FsV8Operation::HandleTruncate:
        return request != nullptr && request->logical_file != nullptr &&
                       request->logical_file->access == SL_FS_FILE_ACCESS_APPEND
                   ? SL_CAPABILITY_OPERATION_APPEND
                   : SL_CAPABILITY_OPERATION_WRITE;
    case FsV8Operation::AppendText:
    case FsV8Operation::AppendBytes:
        return SL_CAPABILITY_OPERATION_APPEND;
    case FsV8Operation::DirectoryDelete:
    case FsV8Operation::DeleteFile:
        return SL_CAPABILITY_OPERATION_DELETE;
    case FsV8Operation::DirectoryList:
        return SL_CAPABILITY_OPERATION_LIST;
    case FsV8Operation::Exists:
    case FsV8Operation::Stat:
    case FsV8Operation::ReadLink:
    case FsV8Operation::HandleSeek:
        return SL_CAPABILITY_OPERATION_METADATA;
    case FsV8Operation::HandleRead:
        return SL_CAPABILITY_OPERATION_READ;
    case FsV8Operation::WatchOpen:
    case FsV8Operation::WatchNext:
    case FsV8Operation::WatchClose:
        return SL_CAPABILITY_OPERATION_WATCH;
    case FsV8Operation::Copy:
    case FsV8Operation::Move:
        return SL_CAPABILITY_OPERATION_READWRITE;
    default:
        return SL_CAPABILITY_OPERATION_READWRITE;
    }
}

bool fs_v8_operation_reads(FsV8Operation operation)
{
    return operation == FsV8Operation::ReadText || operation == FsV8Operation::ReadBytes ||
           operation == FsV8Operation::DirectoryList || operation == FsV8Operation::ReadLink ||
           operation == FsV8Operation::TempFile || operation == FsV8Operation::TempDirectory ||
           operation == FsV8Operation::HandleRead || operation == FsV8Operation::WatchOpen ||
           operation == FsV8Operation::WatchNext;
}

bool fs_v8_operation_uses_path(FsV8Operation operation)
{
    return operation != FsV8Operation::HandleRead && operation != FsV8Operation::HandleWriteText &&
           operation != FsV8Operation::HandleWriteBytes && operation != FsV8Operation::HandleSeek &&
           operation != FsV8Operation::HandleTruncate && operation != FsV8Operation::HandleFlush &&
           operation != FsV8Operation::HandleSync && operation != FsV8Operation::HandleClose &&
           operation != FsV8Operation::WatchNext && operation != FsV8Operation::WatchClose;
}

bool fs_v8_apply_seek(uint64_t base, int64_t offset, uint64_t* out)
{
    if (out == nullptr) {
        return false;
    }
    if (offset < 0) {
        uint64_t magnitude = (uint64_t)(-(offset + 1)) + 1U;
        if (magnitude > base) {
            return false;
        }
        *out = base - magnitude;
        return true;
    }
    if (UINT64_MAX - base < (uint64_t)offset) {
        return false;
    }
    *out = base + (uint64_t)offset;
    return true;
}

bool fs_v8_logical_can_read(const FsV8LogicalFile* file)
{
    return file != nullptr &&
           (file->access == SL_FS_FILE_ACCESS_READ || file->access == SL_FS_FILE_ACCESS_READWRITE);
}

bool fs_v8_logical_can_write(const FsV8LogicalFile* file)
{
    return file != nullptr && (file->access == SL_FS_FILE_ACCESS_WRITE ||
                               file->access == SL_FS_FILE_ACCESS_READWRITE ||
                               file->access == SL_FS_FILE_ACCESS_APPEND);
}

void fs_v8_copy_bytes(void* dst, const unsigned char* src, size_t length)
{
    unsigned char* out = static_cast<unsigned char*>(dst);
    size_t index = 0U;

    for (index = 0U; index < length; index += 1U) {
        out[index] = src[index];
    }
}

bool fs_v8_is_valid_utf8(const unsigned char* bytes, size_t length)
{
    size_t index = 0U;

    while (index < length) {
        unsigned char ch = bytes[index];
        size_t remaining = length - index;
        size_t extra = 0U;
        unsigned char min_second = 0x80U;
        unsigned char max_second = 0xBFU;

        if (ch <= 0x7FU) {
            index += 1U;
            continue;
        }
        if (ch >= 0xC2U && ch <= 0xDFU) {
            extra = 1U;
        }
        else if (ch >= 0xE0U && ch <= 0xEFU) {
            extra = 2U;
            if (ch == 0xE0U) {
                min_second = 0xA0U;
            }
            if (ch == 0xEDU) {
                max_second = 0x9FU;
            }
        }
        else if (ch >= 0xF0U && ch <= 0xF4U) {
            extra = 3U;
            if (ch == 0xF0U) {
                min_second = 0x90U;
            }
            if (ch == 0xF4U) {
                max_second = 0x8FU;
            }
        }
        else {
            return false;
        }
        if (remaining <= extra || bytes[index + 1U] < min_second || bytes[index + 1U] > max_second)
        {
            return false;
        }
        for (size_t tail = 2U; tail <= extra; tail += 1U) {
            if (bytes[index + tail] < 0x80U || bytes[index + tail] > 0xBFU) {
                return false;
            }
        }
        index += extra + 1U;
    }
    return true;
}

SlStatus fs_v8_resolve(SlArena* arena, const FsV8Request* request, SlStr input,
                       SlFsResolvedPath* out, SlDiag* out_diag)
{
    const SlFsRoot roots[] = {
        {sl_str_from_cstr("data"), sl_str_from_cstr("./data")},
        {sl_str_from_cstr("tmp"), sl_str_from_cstr("./tmp")},
        {sl_str_from_cstr("uploads"), sl_str_from_cstr("./uploads")},
    };
    const SlV8Engine* backend = request == nullptr ? nullptr : request->backend;
    SlFsPolicy fallback = sl_fs_development_policy(sl_str_from_cstr("."));
    const SlFsPolicy* policy = backend == nullptr ? nullptr : backend->filesystem_policy;

    if (policy == nullptr) {
        fallback.roots = roots;
        fallback.root_count = sizeof(roots) / sizeof(roots[0]);
        policy = &fallback;
    }
    return sl_fs_resolve_path(arena, policy, input, out, out_diag);
}

SlStatus fs_v8_run_operation(SlArena* arena, FsV8Request* request, SlDiag* diag)
{
    SlFsResolvedPath path = {};
    SlFsResolvedPath to_path = {};
    SlStatus status = sl_status_ok();

    if (fs_v8_operation_uses_path(request->operation)) {
        status = fs_v8_resolve(arena, request,
                               sl_str_from_parts(request->path.data(), request->path.size()), &path,
                               diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    SlStr resolved = sl_owned_str_as_view(path.path);
    switch (request->operation) {
    case FsV8Operation::ReadText:
    case FsV8Operation::ReadBytes: {
        SlOwnedBytes bytes = {};
        status = sl_fs_read_file(arena, resolved, &bytes, diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (request->operation == FsV8Operation::ReadText) {
            request->result_text.assign(reinterpret_cast<const char*>(bytes.ptr), bytes.length);
        }
        else {
            request->result_bytes.assign(bytes.ptr, bytes.ptr + bytes.length);
        }
        return sl_status_ok();
    }
    case FsV8Operation::WriteText:
    case FsV8Operation::AppendText:
        return sl_fs_write_file(
            resolved,
            sl_bytes_from_parts(reinterpret_cast<const unsigned char*>(request->text.data()),
                                request->text.size()),
            request->operation == FsV8Operation::AppendText, diag);
    case FsV8Operation::WriteBytes:
    case FsV8Operation::AppendBytes:
        return sl_fs_write_file(
            resolved,
            sl_bytes_from_parts(request->bytes.empty() ? nullptr : request->bytes.data(),
                                request->bytes.size()),
            request->operation == FsV8Operation::AppendBytes, diag);
    case FsV8Operation::AtomicWriteText:
        return sl_fs_atomic_write_file(
            arena, resolved,
            sl_bytes_from_parts(reinterpret_cast<const unsigned char*>(request->text.data()),
                                request->text.size()),
            diag);
    case FsV8Operation::AtomicWriteBytes:
        return sl_fs_atomic_write_file(
            arena, resolved,
            sl_bytes_from_parts(request->bytes.empty() ? nullptr : request->bytes.data(),
                                request->bytes.size()),
            diag);
    case FsV8Operation::Exists:
        return sl_fs_exists(resolved, &request->bool_result, diag);
    case FsV8Operation::Stat:
        status = sl_fs_stat(resolved, &request->stat, diag);
        if (sl_status_code(status) == SL_STATUS_OUT_OF_RANGE) {
            request->stat = SlFsStat{};
            return sl_status_ok();
        }
        return status;
    case FsV8Operation::DeleteFile:
        return sl_fs_delete_file(resolved, diag);
    case FsV8Operation::DirectoryCreate:
        return sl_fs_create_directory(resolved, request->recursive, diag);
    case FsV8Operation::DirectoryDelete:
        return sl_fs_delete_directory(resolved, request->recursive, diag);
    case FsV8Operation::DirectoryList: {
        SlFsDirectoryList list = {};
        status = sl_fs_list_directory(arena, resolved, &list, diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        request->directory_entries.clear();
        if (list.count != 0U) {
            request->directory_entries.assign(list.entries, list.entries + list.count);
        }
        request->directory_entry_names.clear();
        request->directory_entry_names.reserve(list.count);
        for (size_t index = 0U; index < list.count; index += 1U) {
            request->directory_entry_names.emplace_back(list.entries[index].name.ptr,
                                                        list.entries[index].name.length);
        }
        return sl_status_ok();
    }
    case FsV8Operation::Symlink:
        status = fs_v8_resolve(arena, request,
                               sl_str_from_parts(request->to_path.data(), request->to_path.size()),
                               &to_path, diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        return sl_fs_create_symlink(resolved, sl_owned_str_as_view(to_path.path),
                                    request->directory, diag);
    case FsV8Operation::ReadLink: {
        SlOwnedStr target = {};
        status = sl_fs_read_link(arena, resolved, &target, diag);
        if (sl_status_is_ok(status)) {
            request->result_text.assign(target.ptr, target.length);
        }
        return status;
    }
    case FsV8Operation::TempFile:
    case FsV8Operation::TempDirectory: {
        SlFsTempPath temp = {};
        if (request->operation == FsV8Operation::TempFile) {
            status = sl_fs_create_temp_file(
                arena, resolved, sl_str_from_parts(request->text.data(), request->text.size()),
                &temp, diag);
        }
        else {
            status = sl_fs_create_temp_directory(
                arena, resolved, sl_str_from_parts(request->text.data(), request->text.size()),
                &temp, diag);
        }
        if (sl_status_is_ok(status)) {
            request->result_text.assign(temp.path.ptr, temp.path.length);
        }
        return status;
    }
    case FsV8Operation::OpenHandle: {
        std::unique_ptr<FsV8LogicalFile> handle(new (std::nothrow) FsV8LogicalFile());
        if (!handle) {
            return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
        }
        handle->storage.resize(4096U);
        status = sl_arena_init(&handle->arena, handle->storage.data(), handle->storage.size());
        if (sl_status_is_ok(status)) {
            status = sl_fs_open_file(&handle->arena, resolved, request->access, request->create,
                                     &handle->native_handle, diag);
        }
        if (!sl_status_is_ok(status)) {
            return status;
        }
        handle->path.assign(resolved.ptr, resolved.length);
        handle->access = request->access;
        handle->position = request->access == SL_FS_FILE_ACCESS_APPEND &&
                                   sl_status_is_ok(sl_fs_stat(resolved, &request->stat, diag))
                               ? request->stat.size
                               : 0U;
        handle->closed = false;
        request->logical_file = handle.release();
        return sl_status_ok();
    }
    case FsV8Operation::HandleRead: {
        SlOwnedBytes bytes = {};
        if (request->logical_file == nullptr || request->logical_file->closed ||
            request->logical_file->native_handle == nullptr)
        {
            return sl_status_from_code(SL_STATUS_STALE_RESOURCE);
        }
        if (!fs_v8_logical_can_read(request->logical_file)) {
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        status = sl_fs_file_seek(request->logical_file->native_handle,
                                 (int64_t)request->logical_file->position, SL_FS_SEEK_START,
                                 &request->position, diag);
        if (sl_status_is_ok(status)) {
            status = sl_fs_file_read(request->logical_file->native_handle, arena,
                                     request->max_bytes, &bytes, diag);
        }
        if (sl_status_is_ok(status)) {
            request->logical_file->position += bytes.length;
            request->result_bytes.assign(bytes.ptr, bytes.ptr + bytes.length);
        }
        return status;
    }
    case FsV8Operation::HandleWriteText:
    case FsV8Operation::HandleWriteBytes: {
        SlBytes payload =
            request->operation == FsV8Operation::HandleWriteText
                ? sl_bytes_from_parts(reinterpret_cast<const unsigned char*>(request->text.data()),
                                      request->text.size())
                : sl_bytes_from_parts(request->bytes.empty() ? nullptr : request->bytes.data(),
                                      request->bytes.size());
        if (request->logical_file == nullptr || request->logical_file->closed ||
            request->logical_file->native_handle == nullptr)
        {
            return sl_status_from_code(SL_STATUS_STALE_RESOURCE);
        }
        if (!fs_v8_logical_can_write(request->logical_file)) {
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        status = sl_fs_file_seek(request->logical_file->native_handle,
                                 (int64_t)request->logical_file->position, SL_FS_SEEK_START,
                                 &request->position, diag);
        if (sl_status_is_ok(status)) {
            status = sl_fs_file_write(request->logical_file->native_handle, payload, diag);
        }
        if (sl_status_is_ok(status)) {
            request->logical_file->position += payload.length;
            request->position = request->logical_file->position;
        }
        return status;
    }
    case FsV8Operation::HandleSeek:
        if (request->logical_file == nullptr || request->logical_file->closed) {
            return sl_status_from_code(SL_STATUS_STALE_RESOURCE);
        }
        if (request->seek_origin == SL_FS_SEEK_START) {
            if (!fs_v8_apply_seek(0U, request->seek_offset, &request->logical_file->position)) {
                return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
            }
        }
        else if (request->seek_origin == SL_FS_SEEK_CURRENT) {
            if (!fs_v8_apply_seek(request->logical_file->position, request->seek_offset,
                                  &request->logical_file->position))
            {
                return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
            }
        }
        else {
            status = sl_fs_stat(sl_str_from_parts(request->logical_file->path.data(),
                                                  request->logical_file->path.size()),
                                &request->stat, diag);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            if (!fs_v8_apply_seek(request->stat.size, request->seek_offset,
                                  &request->logical_file->position))
            {
                return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
            }
        }
        request->position = request->logical_file->position;
        return sl_status_ok();
    case FsV8Operation::HandleTruncate: {
        if (request->logical_file == nullptr || request->logical_file->closed ||
            request->logical_file->native_handle == nullptr)
        {
            return sl_status_from_code(SL_STATUS_STALE_RESOURCE);
        }
        if (!fs_v8_logical_can_write(request->logical_file)) {
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        return sl_fs_file_truncate(request->logical_file->native_handle, request->truncate_size,
                                   diag);
    }
    case FsV8Operation::HandleFlush:
        return request->logical_file == nullptr || request->logical_file->closed ||
                       request->logical_file->native_handle == nullptr
                   ? sl_status_from_code(SL_STATUS_STALE_RESOURCE)
                   : sl_fs_file_flush(request->logical_file->native_handle, diag);
    case FsV8Operation::HandleSync:
        return request->logical_file == nullptr || request->logical_file->closed ||
                       request->logical_file->native_handle == nullptr
                   ? sl_status_from_code(SL_STATUS_STALE_RESOURCE)
                   : sl_fs_file_sync(request->logical_file->native_handle, diag);
    case FsV8Operation::HandleClose:
        if (request->logical_file == nullptr || request->logical_file->closed ||
            request->logical_file->native_handle == nullptr)
        {
            return sl_status_from_code(SL_STATUS_STALE_RESOURCE);
        }
        status = sl_fs_file_close(request->logical_file->native_handle, diag);
        if (sl_status_is_ok(status)) {
            request->logical_file->native_handle = nullptr;
            request->logical_file->closed = true;
        }
        return status;
    case FsV8Operation::WatchOpen: {
        std::unique_ptr<FsV8LogicalWatch> watch(new (std::nothrow) FsV8LogicalWatch());
        if (!watch) {
            return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
        }
        watch->storage.resize(65536U);
        status = sl_arena_init(&watch->arena, watch->storage.data(), watch->storage.size());
        if (sl_status_is_ok(status)) {
            status = sl_fs_watch_open(&watch->arena, resolved, &request->watch_options,
                                      &watch->native_watch, diag);
        }
        if (!sl_status_is_ok(status)) {
            return status;
        }
        request->logical_watch = watch.release();
        return sl_status_ok();
    }
    case FsV8Operation::WatchNext:
        if (request->logical_watch == nullptr || request->logical_watch->closed ||
            request->logical_watch->native_watch == nullptr)
        {
            return sl_status_from_code(SL_STATUS_STALE_RESOURCE);
        }
        if (request->logical_watch->closing.load()) {
            request->bool_result = false;
            return sl_status_ok();
        }
        status = sl_fs_watch_next(request->logical_watch->native_watch, arena,
                                  &request->watch_event, diag);
        if (sl_status_code(status) == SL_STATUS_DEADLINE_EXCEEDED) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            if (request->logical_watch->closing.load()) {
                request->bool_result = false;
                return sl_status_ok();
            }
            status = sl_fs_watch_next(request->logical_watch->native_watch, arena,
                                      &request->watch_event, diag);
            if (sl_status_code(status) == SL_STATUS_DEADLINE_EXCEEDED) {
                request->bool_result = false;
                return sl_status_ok();
            }
        }
        request->bool_result = sl_status_is_ok(status);
        if (sl_status_is_ok(status)) {
            request->result_text.assign(
                request->watch_event.path.ptr == nullptr ? "" : request->watch_event.path.ptr,
                request->watch_event.path.length);
        }
        return status;
    case FsV8Operation::WatchClose:
        if (request->logical_watch == nullptr || request->logical_watch->closed ||
            request->logical_watch->native_watch == nullptr)
        {
            return sl_status_from_code(SL_STATUS_STALE_RESOURCE);
        }
        request->logical_watch->closing.store(true);
        while (request->logical_watch->busy.exchange(true)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        status = sl_fs_watch_close(request->logical_watch->native_watch, diag);
        if (sl_status_is_ok(status)) {
            request->logical_watch->native_watch = nullptr;
            request->logical_watch->closed = true;
        }
        return status;
    case FsV8Operation::Copy:
    case FsV8Operation::Move:
        status = fs_v8_resolve(arena, request,
                               sl_str_from_parts(request->to_path.data(), request->to_path.size()),
                               &to_path, diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (request->operation == FsV8Operation::Copy) {
            return sl_fs_copy_file(resolved, sl_owned_str_as_view(to_path.path), request->overwrite,
                                   diag);
        }
        return sl_fs_move_file(resolved, sl_owned_str_as_view(to_path.path), request->overwrite,
                               diag);
    default:
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
}

SlStatus fs_v8_provider_run(SlProviderOperation* operation, void* user, SlDiagCode* out_diag_code,
                            SlStr* out_message)
{
    FsV8Request* request = static_cast<FsV8Request*>(user);
    size_t capacity = 64U * 1024U;
    SlDiag diag = {};
    SlStatus status;

    (void)operation;
    if (request == nullptr || out_diag_code == nullptr || out_message == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (;;) {
        std::vector<unsigned char> storage(capacity);
        SlArena arena = {};

        diag = {};
        status = sl_arena_init(&arena, storage.data(), storage.size());
        if (!sl_status_is_ok(status)) {
            break;
        }
        status = fs_v8_run_operation(&arena, request, &diag);
        if (sl_status_code(status) != SL_STATUS_OUT_OF_MEMORY ||
            !fs_v8_operation_reads(request->operation) || capacity > (SIZE_MAX / 2U))
        {
            break;
        }
        capacity *= 2U;
    }

    request->status = status;
    if (!sl_status_is_ok(status)) {
        request->error = fs_v8_diag_message(diag, "filesystem operation failed");
        *out_diag_code = diag.code == SL_DIAG_NONE ? SL_DIAG_INTERNAL_ERROR : diag.code;
        *out_message = sl_str_from_parts(request->error.data(), request->error.size());
    }
    else {
        *out_diag_code = SL_DIAG_NONE;
        *out_message = sl_str_from_cstr("filesystem operation completed");
    }
    return status;
}

bool fs_v8_result_value(v8::Isolate* isolate, v8::Local<v8::Context> context, FsV8Request* request,
                        v8::Local<v8::Value>* out, std::string* out_error)
{
    if (out == nullptr || out_error == nullptr) {
        return false;
    }

    switch (request->operation) {
    case FsV8Operation::ReadText: {
        v8::Local<v8::String> text;
        if (request->result_text.size() > static_cast<size_t>(INT_MAX)) {
            *out_error = "File text is too large to decode";
            return false;
        }
        if (!fs_v8_is_valid_utf8(
                reinterpret_cast<const unsigned char*>(request->result_text.data()),
                request->result_text.size()))
        {
            *out_error = "Invalid UTF-8 in file";
            return false;
        }
        if (!v8::String::NewFromUtf8(isolate, request->result_text.data(),
                                     v8::NewStringType::kNormal,
                                     static_cast<int>(request->result_text.size()))
                 .ToLocal(&text))
        {
            *out_error = "Invalid UTF-8 in file";
            return false;
        }
        *out = text;
        return true;
    }
    case FsV8Operation::ReadBytes: {
        auto backing = v8::ArrayBuffer::NewBackingStore(isolate, request->result_bytes.size());
        if (!request->result_bytes.empty()) {
            fs_v8_copy_bytes(backing->Data(), request->result_bytes.data(),
                             request->result_bytes.size());
        }
        v8::Local<v8::ArrayBuffer> buffer = v8::ArrayBuffer::New(isolate, std::move(backing));
        *out = v8::Uint8Array::New(buffer, 0, request->result_bytes.size());
        return true;
    }
    case FsV8Operation::Exists:
        *out = v8::Boolean::New(isolate, request->bool_result);
        return true;
    case FsV8Operation::Stat: {
        v8::Local<v8::Object> object = v8::Object::New(isolate);
        v8::Local<v8::String> exists_key;
        v8::Local<v8::String> kind_key;
        v8::Local<v8::String> size_key;
        const char* kind = request->stat.kind == SL_FS_NODE_FILE        ? "file"
                           : request->stat.kind == SL_FS_NODE_DIRECTORY ? "directory"
                           : request->stat.kind == SL_FS_NODE_OTHER     ? "other"
                                                                        : "missing";
        (void)fs_v8_to_local_string(isolate, sl_str_from_cstr("exists"), &exists_key);
        (void)fs_v8_to_local_string(isolate, sl_str_from_cstr("kind"), &kind_key);
        (void)fs_v8_to_local_string(isolate, sl_str_from_cstr("size"), &size_key);
        (void)object->Set(context, exists_key, v8::Boolean::New(isolate, request->stat.exists));
        (void)object->Set(context, kind_key,
                          v8::String::NewFromUtf8(isolate, kind).ToLocalChecked());
        (void)object->Set(context, size_key,
                          v8::Number::New(isolate, static_cast<double>(request->stat.size)));
        *out = object;
        return true;
    }
    case FsV8Operation::DirectoryList: {
        v8::Local<v8::Array> array =
            v8::Array::New(isolate, static_cast<int>(request->directory_entries.size()));
        for (size_t index = 0U; index < request->directory_entries.size(); index += 1U) {
            v8::Local<v8::Object> entry = v8::Object::New(isolate);
            v8::Local<v8::String> name_key;
            v8::Local<v8::String> kind_key;
            v8::Local<v8::String> size_key;
            v8::Local<v8::String> name_value;
            const SlFsDirectoryEntry& native = request->directory_entries[index];
            const char* kind = native.kind == SL_FS_NODE_FILE        ? "file"
                               : native.kind == SL_FS_NODE_DIRECTORY ? "directory"
                               : native.kind == SL_FS_NODE_OTHER     ? "other"
                                                                     : "missing";

            (void)fs_v8_to_local_string(isolate, sl_str_from_cstr("name"), &name_key);
            (void)fs_v8_to_local_string(isolate, sl_str_from_cstr("kind"), &kind_key);
            (void)fs_v8_to_local_string(isolate, sl_str_from_cstr("size"), &size_key);
            if (!v8::String::NewFromUtf8(
                     isolate, request->directory_entry_names[index].c_str(),
                     v8::NewStringType::kNormal,
                     static_cast<int>(request->directory_entry_names[index].size()))
                     .ToLocal(&name_value))
            {
                *out_error = "Invalid UTF-8 in directory entry";
                return false;
            }
            (void)entry->Set(context, name_key, name_value);
            (void)entry->Set(context, kind_key,
                             v8::String::NewFromUtf8(isolate, kind).ToLocalChecked());
            (void)entry->Set(context, size_key,
                             v8::Number::New(isolate, static_cast<double>(native.size)));
            (void)array->Set(context, static_cast<uint32_t>(index), entry);
        }
        *out = array;
        return true;
    }
    case FsV8Operation::ReadLink:
    case FsV8Operation::TempFile:
    case FsV8Operation::TempDirectory: {
        v8::Local<v8::String> text;
        if (request->result_text.size() > static_cast<size_t>(INT_MAX) ||
            !v8::String::NewFromUtf8(isolate, request->result_text.data(),
                                     v8::NewStringType::kNormal,
                                     static_cast<int>(request->result_text.size()))
                 .ToLocal(&text))
        {
            *out_error = "Invalid UTF-8 in filesystem result";
            return false;
        }
        *out = text;
        return true;
    }
    case FsV8Operation::OpenHandle: {
        SlResourceId id = sl_resource_id_invalid();
        v8::Local<v8::Object> object = v8::Object::New(isolate);
        v8::Local<v8::String> slot_key;
        v8::Local<v8::String> generation_key;
        SlStatus status;

        if (request->logical_file == nullptr) {
            *out_error = "filesystem handle was not opened";
            return false;
        }
        status = sl_resource_table_insert(&request->backend->resources,
                                          SL_RESOURCE_KIND_FS_FILE_HANDLE, request->logical_file,
                                          fs_v8_logical_file_cleanup, nullptr, &id, nullptr);
        if (!sl_status_is_ok(status)) {
            fs_v8_logical_file_cleanup(request->logical_file, nullptr);
            request->logical_file = nullptr;
            *out_error = "filesystem handle table is exhausted";
            return false;
        }
        request->logical_file = nullptr;
        (void)fs_v8_to_local_string(isolate, sl_str_from_cstr("slot"), &slot_key);
        (void)fs_v8_to_local_string(isolate, sl_str_from_cstr("generation"), &generation_key);
        (void)object->Set(context, slot_key, v8::Integer::NewFromUnsigned(isolate, id.slot));
        (void)object->Set(context, generation_key,
                          v8::Integer::NewFromUnsigned(isolate, id.generation));
        *out = object;
        return true;
    }
    case FsV8Operation::WatchOpen: {
        SlResourceId id = sl_resource_id_invalid();
        v8::Local<v8::Object> object = v8::Object::New(isolate);
        v8::Local<v8::String> slot_key;
        v8::Local<v8::String> generation_key;
        SlStatus status;

        if (request->logical_watch == nullptr) {
            *out_error = "filesystem watch was not opened";
            return false;
        }
        status = sl_resource_table_insert(&request->backend->resources, SL_RESOURCE_KIND_FS_WATCH,
                                          request->logical_watch, fs_v8_logical_watch_cleanup,
                                          nullptr, &id, nullptr);
        if (!sl_status_is_ok(status)) {
            fs_v8_logical_watch_cleanup(request->logical_watch, nullptr);
            request->logical_watch = nullptr;
            *out_error = "filesystem watch table is exhausted";
            return false;
        }
        request->logical_watch = nullptr;
        (void)fs_v8_to_local_string(isolate, sl_str_from_cstr("slot"), &slot_key);
        (void)fs_v8_to_local_string(isolate, sl_str_from_cstr("generation"), &generation_key);
        (void)object->Set(context, slot_key, v8::Integer::NewFromUnsigned(isolate, id.slot));
        (void)object->Set(context, generation_key,
                          v8::Integer::NewFromUnsigned(isolate, id.generation));
        *out = object;
        return true;
    }
    case FsV8Operation::HandleRead: {
        auto backing = v8::ArrayBuffer::NewBackingStore(isolate, request->result_bytes.size());
        if (!request->result_bytes.empty()) {
            fs_v8_copy_bytes(backing->Data(), request->result_bytes.data(),
                             request->result_bytes.size());
        }
        v8::Local<v8::ArrayBuffer> buffer = v8::ArrayBuffer::New(isolate, std::move(backing));
        *out = v8::Uint8Array::New(buffer, 0, request->result_bytes.size());
        return true;
    }
    case FsV8Operation::WatchNext: {
        v8::Local<v8::Object> object = v8::Object::New(isolate);
        v8::Local<v8::String> kind_key;
        v8::Local<v8::String> path_key;
        v8::Local<v8::String> directory_key;
        v8::Local<v8::String> overflow_key;
        v8::Local<v8::String> path_value;
        const char* kind = request->watch_event.kind == SL_FS_WATCH_EVENT_CREATED    ? "created"
                           : request->watch_event.kind == SL_FS_WATCH_EVENT_MODIFIED ? "modified"
                           : request->watch_event.kind == SL_FS_WATCH_EVENT_DELETED  ? "deleted"
                                                                                     : "overflow";

        if (!request->bool_result) {
            *out = v8::Null(isolate);
            return true;
        }
        if (!v8::String::NewFromUtf8(isolate, request->result_text.c_str(),
                                     v8::NewStringType::kNormal,
                                     static_cast<int>(request->result_text.size()))
                 .ToLocal(&path_value))
        {
            *out_error = "Invalid UTF-8 in filesystem watch event";
            return false;
        }
        (void)fs_v8_to_local_string(isolate, sl_str_from_cstr("kind"), &kind_key);
        (void)fs_v8_to_local_string(isolate, sl_str_from_cstr("path"), &path_key);
        (void)fs_v8_to_local_string(isolate, sl_str_from_cstr("isDirectory"), &directory_key);
        (void)fs_v8_to_local_string(isolate, sl_str_from_cstr("overflow"), &overflow_key);
        (void)object->Set(context, kind_key,
                          v8::String::NewFromUtf8(isolate, kind).ToLocalChecked());
        (void)object->Set(context, path_key, path_value);
        (void)object->Set(context, directory_key,
                          v8::Boolean::New(isolate, request->watch_event.is_directory));
        (void)object->Set(context, overflow_key,
                          v8::Boolean::New(isolate, request->watch_event.overflow));
        *out = object;
        return true;
    }
    case FsV8Operation::HandleSeek:
    case FsV8Operation::HandleWriteText:
    case FsV8Operation::HandleWriteBytes:
        *out = v8::Number::New(isolate, static_cast<double>(request->position));
        return true;
    default:
        *out = v8::Undefined(isolate);
        return true;
    }
}

SlStatus fs_v8_completion_dispatch(SlAsyncLoop* loop, const SlAsyncCompletion* completion,
                                   void* user)
{
    SlProviderOperation* operation =
        completion == nullptr ? nullptr : static_cast<SlProviderOperation*>(completion->payload);
    FsV8Request* request =
        operation == nullptr ? nullptr : static_cast<FsV8Request*>(operation->run_user);
    SlV8Engine* backend = request == nullptr ? nullptr : request->backend;

    (void)loop;
    (void)user;
    if (request == nullptr || backend == nullptr || backend->isolate == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    v8::Isolate* isolate = backend->isolate;
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = backend->context.Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::Local<v8::Promise::Resolver> resolver = request->resolver.Get(isolate);

    if (request->logical_file_busy_acquired && request->logical_file != nullptr) {
        request->logical_file->busy = false;
    }
    if (request->logical_file_busy_acquired && request->logical_watch != nullptr) {
        request->logical_watch->busy.store(false);
    }
    if (sl_status_is_ok(request->status) && request->operation == FsV8Operation::HandleClose) {
        SlStatus close_status = sl_resource_table_close_kind(
            &backend->resources, request->resource_id, SL_RESOURCE_KIND_FS_FILE_HANDLE, nullptr);
        if (!sl_status_is_ok(close_status)) {
            request->status = close_status;
            request->error = "filesystem handle is stale or closed";
        }
        request->logical_file = nullptr;
    }
    if (sl_status_is_ok(request->status) && request->operation == FsV8Operation::WatchClose) {
        SlStatus close_status = sl_resource_table_close_kind(
            &backend->resources, request->resource_id, SL_RESOURCE_KIND_FS_WATCH, nullptr);
        if (!sl_status_is_ok(close_status)) {
            request->status = close_status;
            request->error = "filesystem watch is stale or closed";
        }
        request->logical_watch = nullptr;
    }

    bool ok = false;
    if (!sl_status_is_ok(request->status)) {
        v8::Local<v8::String> message =
            v8::String::NewFromUtf8(isolate, request->error.c_str()).ToLocalChecked();
        ok = resolver->Reject(context, v8::Exception::Error(message)).FromMaybe(false);
    }
    else {
        v8::Local<v8::Value> value;
        std::string error;
        if (fs_v8_result_value(isolate, context, request, &value, &error)) {
            ok = resolver->Resolve(context, value).FromMaybe(false);
        }
        else {
            v8::Local<v8::String> message =
                v8::String::NewFromUtf8(isolate, error.c_str()).ToLocalChecked();
            ok = resolver->Reject(context, v8::Exception::Error(message)).FromMaybe(false);
        }
    }
    request->resolver.Reset();
    if (request->logical_file != nullptr && request->operation == FsV8Operation::OpenHandle) {
        fs_v8_logical_file_cleanup(request->logical_file, nullptr);
        request->logical_file = nullptr;
    }
    if (request->logical_watch != nullptr && request->operation == FsV8Operation::WatchOpen) {
        fs_v8_logical_watch_cleanup(request->logical_watch, nullptr);
        request->logical_watch = nullptr;
    }
    delete request;
    isolate->PerformMicrotaskCheckpoint();
    return ok ? sl_status_ok() : sl_status_from_code(SL_STATUS_INVALID_STATE);
}

bool fs_v8_get_optional_bool(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             v8::Local<v8::Value> value, const char* key, bool* out)
{
    v8::Local<v8::String> local_key;
    v8::Local<v8::Value> property;

    if (out == nullptr) {
        return false;
    }
    *out = false;
    if (value->IsUndefined() || value->IsNull()) {
        return true;
    }
    if (!value->IsObject() ||
        !sl_status_is_ok(fs_v8_to_local_string(isolate, sl_str_from_cstr(key), &local_key)) ||
        !value.As<v8::Object>()->Get(context, local_key).ToLocal(&property))
    {
        return false;
    }
    if (property->IsUndefined() || property->IsNull()) {
        return true;
    }
    if (!property->IsBoolean()) {
        return false;
    }
    *out = property->BooleanValue(isolate);
    return true;
}

bool fs_v8_get_optional_size(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             v8::Local<v8::Value> value, const char* key, size_t fallback,
                             size_t min_value, size_t max_value, size_t* out)
{
    v8::Local<v8::String> local_key;
    v8::Local<v8::Value> property;
    uint32_t number = 0U;

    if (out == nullptr) {
        return false;
    }
    *out = fallback;
    if (value->IsUndefined() || value->IsNull()) {
        return true;
    }
    if (!value->IsObject() ||
        !sl_status_is_ok(fs_v8_to_local_string(isolate, sl_str_from_cstr(key), &local_key)) ||
        !value.As<v8::Object>()->Get(context, local_key).ToLocal(&property))
    {
        return false;
    }
    if (property->IsUndefined() || property->IsNull()) {
        return true;
    }
    if (!property->IsUint32()) {
        return false;
    }
    number = property.As<v8::Uint32>()->Value();
    if ((size_t)number < min_value || (size_t)number > max_value) {
        return false;
    }
    *out = (size_t)number;
    return true;
}

bool fs_v8_get_resource_id(v8::Isolate* isolate, v8::Local<v8::Context> context,
                           v8::Local<v8::Value> value, SlResourceId* out)
{
    v8::Local<v8::String> slot_key;
    v8::Local<v8::String> generation_key;
    v8::Local<v8::Value> slot;
    v8::Local<v8::Value> generation;

    if (out == nullptr || !value->IsObject() ||
        !sl_status_is_ok(fs_v8_to_local_string(isolate, sl_str_from_cstr("slot"), &slot_key)) ||
        !sl_status_is_ok(
            fs_v8_to_local_string(isolate, sl_str_from_cstr("generation"), &generation_key)) ||
        !value.As<v8::Object>()->Get(context, slot_key).ToLocal(&slot) ||
        !value.As<v8::Object>()->Get(context, generation_key).ToLocal(&generation) ||
        !slot->IsUint32() || !generation->IsUint32())
    {
        return false;
    }
    out->slot = slot.As<v8::Uint32>()->Value();
    out->generation = generation.As<v8::Uint32>()->Value();
    return sl_resource_id_is_valid(*out);
}

bool fs_v8_value_to_bytes(v8::Local<v8::Value> value, std::vector<unsigned char>* out)
{
    if (out == nullptr || !value->IsUint8Array()) {
        return false;
    }
    v8::Local<v8::Uint8Array> array = value.As<v8::Uint8Array>();
    v8::Local<v8::ArrayBuffer> buffer = array->Buffer();
    std::shared_ptr<v8::BackingStore> backing = buffer->GetBackingStore();
    size_t offset = array->ByteOffset();
    size_t length = array->ByteLength();
    const unsigned char* start = static_cast<const unsigned char*>(backing->Data()) + offset;
    out->assign(start, start + length);
    return true;
}

bool fs_v8_parse_access(v8::Isolate* isolate, v8::Local<v8::Value> value, SlFsFileAccess* out)
{
    std::string access;

    if (out == nullptr) {
        return false;
    }
    *out = SL_FS_FILE_ACCESS_READ;
    if (value->IsUndefined() || value->IsNull()) {
        return true;
    }
    if (!fs_v8_value_to_std_string(isolate, value, &access)) {
        return false;
    }
    if (access == "read") {
        *out = SL_FS_FILE_ACCESS_READ;
    }
    else if (access == "write") {
        *out = SL_FS_FILE_ACCESS_WRITE;
    }
    else if (access == "readwrite") {
        *out = SL_FS_FILE_ACCESS_READWRITE;
    }
    else if (access == "append") {
        *out = SL_FS_FILE_ACCESS_APPEND;
    }
    else {
        return false;
    }
    return true;
}

bool fs_v8_parse_seek_origin(v8::Isolate* isolate, v8::Local<v8::Value> value, SlFsSeekOrigin* out)
{
    std::string origin;

    if (out == nullptr) {
        return false;
    }
    *out = SL_FS_SEEK_START;
    if (value->IsUndefined() || value->IsNull()) {
        return true;
    }
    if (!fs_v8_value_to_std_string(isolate, value, &origin)) {
        return false;
    }
    if (origin == "start") {
        *out = SL_FS_SEEK_START;
    }
    else if (origin == "current") {
        *out = SL_FS_SEEK_CURRENT;
    }
    else if (origin == "end") {
        *out = SL_FS_SEEK_END;
    }
    else {
        return false;
    }
    return true;
}

void fs_v8_submit_callback(const v8::FunctionCallbackInfo<v8::Value>& args, FsV8Operation operation)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::unique_ptr<FsV8Request> request(new (std::nothrow) FsV8Request());
    v8::Local<v8::Promise::Resolver> resolver;

    if (backend == nullptr || backend->async_loop == nullptr || !backend->fs_executor_initialized) {
        fs_v8_throw_type_error(isolate, "__sloppy.fs is unavailable because stdlib.fs is inactive");
        return;
    }
    if (!request || !v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        fs_v8_throw_type_error(isolate, "__sloppy.fs could not create a Promise");
        return;
    }
    request->backend = backend;
    request->operation = operation;

    if (fs_v8_operation_uses_path(operation) &&
        (args.Length() < 1 || !fs_v8_value_to_std_string(isolate, args[0], &request->path) ||
         request->path.empty()))
    {
        fs_v8_throw_type_error(isolate, "__sloppy.fs requires a non-empty path string");
        return;
    }

    if (operation == FsV8Operation::WriteText || operation == FsV8Operation::AppendText ||
        operation == FsV8Operation::AtomicWriteText)
    {
        if (args.Length() < 2) {
            fs_v8_throw_type_error(isolate, "__sloppy.fs text write requires data");
            return;
        }
        if (!fs_v8_value_to_std_string(isolate, args[1], &request->text)) {
            fs_v8_throw_type_error(isolate, "__sloppy.fs text data must be a string");
            return;
        }
    }
    if (operation == FsV8Operation::WriteBytes || operation == FsV8Operation::AppendBytes ||
        operation == FsV8Operation::AtomicWriteBytes)
    {
        if (args.Length() < 2 || !fs_v8_value_to_bytes(args[1], &request->bytes)) {
            fs_v8_throw_type_error(isolate, "__sloppy.fs byte data must be a Uint8Array");
            return;
        }
    }
    if (operation == FsV8Operation::Copy || operation == FsV8Operation::Move) {
        if (args.Length() < 2 || !fs_v8_value_to_std_string(isolate, args[1], &request->to_path) ||
            request->to_path.empty())
        {
            fs_v8_throw_type_error(isolate, "__sloppy.fs copy/move requires a target path string");
            return;
        }
        if (!fs_v8_get_optional_bool(isolate, context,
                                     args.Length() > 2 ? args[2] : v8::Undefined(isolate),
                                     "overwrite", &request->overwrite))
        {
            fs_v8_throw_type_error(isolate, "__sloppy.fs overwrite option must be boolean");
            return;
        }
    }
    if (operation == FsV8Operation::DirectoryCreate || operation == FsV8Operation::DirectoryDelete)
    {
        request->recursive = args.Length() > 1 && args[1]->BooleanValue(isolate);
    }
    if (operation == FsV8Operation::Symlink) {
        if (args.Length() < 2 || !fs_v8_value_to_std_string(isolate, args[1], &request->to_path) ||
            request->to_path.empty())
        {
            fs_v8_throw_type_error(isolate, "__sloppy.fs symlink requires a link path string");
            return;
        }
        request->directory = args.Length() > 2 && args[2]->BooleanValue(isolate);
    }
    if (operation == FsV8Operation::TempFile || operation == FsV8Operation::TempDirectory) {
        request->text = "sloppy-";
        if (args.Length() > 1 && !args[1]->IsUndefined() &&
            !fs_v8_value_to_std_string(isolate, args[1], &request->text))
        {
            fs_v8_throw_type_error(isolate, "__sloppy.fs temp prefix must be a string");
            return;
        }
    }
    if (operation == FsV8Operation::OpenHandle) {
        if (!fs_v8_parse_access(isolate, args.Length() > 1 ? args[1] : v8::Undefined(isolate),
                                &request->access))
        {
            fs_v8_throw_type_error(isolate, "__sloppy.fs open access is invalid");
            return;
        }
        request->create = args.Length() > 2 && args[2]->BooleanValue(isolate);
    }
    if (operation == FsV8Operation::WatchOpen) {
        v8::Local<v8::Value> options_arg = args.Length() > 2 ? args[2] : v8::Undefined(isolate);

        request->watch_options.directory = args.Length() > 1 && args[1]->BooleanValue(isolate);
        request->watch_options.queue_capacity = 16U;
        request->watch_options.snapshot_capacity = request->watch_options.directory ? 128U : 1U;
        if (!fs_v8_get_optional_bool(isolate, context, options_arg, "recursive",
                                     &request->watch_options.recursive) ||
            !fs_v8_get_optional_size(isolate, context, options_arg, "queueCapacity",
                                     request->watch_options.queue_capacity, 1U, 256U,
                                     &request->watch_options.queue_capacity) ||
            !fs_v8_get_optional_size(isolate, context, options_arg, "snapshotCapacity",
                                     request->watch_options.snapshot_capacity, 1U, 1024U,
                                     &request->watch_options.snapshot_capacity))
        {
            fs_v8_throw_type_error(isolate, "__sloppy.fs watch options are invalid");
            return;
        }
    }
    if (!fs_v8_operation_uses_path(operation)) {
        void* ptr = nullptr;
        SlResourceKind expected_kind =
            operation == FsV8Operation::WatchNext || operation == FsV8Operation::WatchClose
                ? SL_RESOURCE_KIND_FS_WATCH
                : SL_RESOURCE_KIND_FS_FILE_HANDLE;

        if (args.Length() < 1 ||
            !fs_v8_get_resource_id(isolate, context, args[0], &request->resource_id) ||
            !sl_status_is_ok(sl_resource_table_get(&backend->resources, request->resource_id,
                                                   expected_kind, &ptr, nullptr)))
        {
            fs_v8_throw_type_error(isolate, expected_kind == SL_RESOURCE_KIND_FS_WATCH
                                                ? "__sloppy.fs watch id is stale or closed"
                                                : "__sloppy.fs handle id is stale or closed");
            return;
        }
        if (expected_kind == SL_RESOURCE_KIND_FS_WATCH) {
            request->logical_watch = static_cast<FsV8LogicalWatch*>(ptr);
        }
        else {
            request->logical_file = static_cast<FsV8LogicalFile*>(ptr);
        }
        if (operation == FsV8Operation::WatchClose) {
            request->logical_watch->closing.store(true);
            request->logical_file_busy_acquired = true;
        }
        if (operation == FsV8Operation::WatchNext) {
            if (request->logical_watch->busy.exchange(true)) {
                fs_v8_throw_type_error(isolate, "__sloppy.fs watch has a pending operation");
                return;
            }
            request->logical_file_busy_acquired = true;
        }
        if (operation == FsV8Operation::HandleClose) {
            if (request->logical_file->busy) {
                fs_v8_throw_type_error(isolate,
                                       "__sloppy.fs handle has a pending filesystem operation");
                return;
            }
            request->logical_file->busy = true;
            request->logical_file_busy_acquired = true;
        }
        if (operation == FsV8Operation::HandleRead) {
            uint64_t max_bytes = 64U * 1024U;
            if (args.Length() > 1 && args[1]->IsNumber()) {
                max_bytes = (uint64_t)args[1].As<v8::Number>()->Value();
            }
            request->max_bytes =
                max_bytes == 0U || max_bytes > (1024U * 1024U) ? 64U * 1024U : (size_t)max_bytes;
        }
        if (operation == FsV8Operation::HandleWriteText) {
            if (args.Length() < 2 || !fs_v8_value_to_std_string(isolate, args[1], &request->text)) {
                fs_v8_throw_type_error(isolate, "__sloppy.fs handle writeText requires text");
                return;
            }
        }
        if (operation == FsV8Operation::HandleWriteBytes) {
            if (args.Length() < 2 || !fs_v8_value_to_bytes(args[1], &request->bytes)) {
                fs_v8_throw_type_error(isolate, "__sloppy.fs handle writeBytes requires bytes");
                return;
            }
        }
        if (operation == FsV8Operation::HandleSeek) {
            if (args.Length() < 2 || !args[1]->IsNumber() ||
                !fs_v8_parse_seek_origin(isolate,
                                         args.Length() > 2 ? args[2] : v8::Undefined(isolate),
                                         &request->seek_origin))
            {
                fs_v8_throw_type_error(isolate, "__sloppy.fs handle seek arguments are invalid");
                return;
            }
            double offset = args[1].As<v8::Number>()->Value();
            if (!(offset >= (double)INT64_MIN && offset <= (double)INT64_MAX)) {
                fs_v8_throw_type_error(isolate, "__sloppy.fs handle seek offset is out of range");
                return;
            }
            request->seek_offset = (int64_t)offset;
        }
        if (operation == FsV8Operation::HandleTruncate) {
            if (args.Length() < 2 || !args[1]->IsNumber() ||
                !(args[1].As<v8::Number>()->Value() >= 0.0))
            {
                fs_v8_throw_type_error(isolate, "__sloppy.fs truncate size must be non-negative");
                return;
            }
            request->truncate_size = (uint64_t)args[1].As<v8::Number>()->Value();
        }
        if (expected_kind == SL_RESOURCE_KIND_FS_FILE_HANDLE &&
            operation != FsV8Operation::HandleClose && request->logical_file->busy)
        {
            fs_v8_throw_type_error(isolate,
                                   "__sloppy.fs handle has a pending filesystem operation");
            return;
        }
        if (expected_kind == SL_RESOURCE_KIND_FS_FILE_HANDLE &&
            operation != FsV8Operation::HandleClose)
        {
            request->logical_file->busy = true;
            request->logical_file_busy_acquired = true;
        }
    }

    request->resolver.Reset(isolate, resolver);
    args.GetReturnValue().Set(resolver->GetPromise());

    SlProviderOperationDescriptor descriptor = sl_provider_operation_descriptor_init(
        sl_str_from_cstr("stdlib.fs"), sl_str_from_cstr("filesystem"),
        SL_PROVIDER_OPERATION_KIND_INTERNAL, sl_str_from_cstr("fs"),
        SL_PROVIDER_EXECUTION_BLOCKING_POOL, fs_v8_completion_dispatch, nullptr);
    (void)sl_provider_operation_descriptor_attach_capability(
        &descriptor, sl_str_from_cstr("stdlib.fs"), fs_v8_capability_operation(request.get()));
    (void)sl_provider_operation_descriptor_attach_run(&descriptor, fs_v8_provider_run,
                                                      request.get());

    SlProviderOperation* provider_operation = nullptr;
    SlStatus status = sl_provider_executor_submit(&backend->fs_executor, backend->arena,
                                                  &descriptor, &provider_operation);
    if (!sl_status_is_ok(status)) {
        v8::Local<v8::String> message =
            v8::String::NewFromUtf8Literal(isolate, "filesystem operation could not be submitted");
        (void)resolver->Reject(context, v8::Exception::Error(message));
        if (request->logical_file_busy_acquired && request->logical_file != nullptr) {
            request->logical_file->busy = false;
        }
        if (request->logical_file_busy_acquired && request->logical_watch != nullptr) {
            request->logical_watch->busy.store(false);
        }
        request->resolver.Reset();
        return;
    }
    (void)provider_operation;
    request.release();
}

void fs_v8_read_text_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::ReadText);
}

void fs_v8_read_bytes_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::ReadBytes);
}

void fs_v8_write_text_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::WriteText);
}

void fs_v8_write_bytes_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::WriteBytes);
}

void fs_v8_append_text_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::AppendText);
}

void fs_v8_append_bytes_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::AppendBytes);
}

void fs_v8_exists_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::Exists);
}

void fs_v8_stat_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::Stat);
}

void fs_v8_copy_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::Copy);
}

void fs_v8_move_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::Move);
}

void fs_v8_delete_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::DeleteFile);
}

void fs_v8_atomic_write_text_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::AtomicWriteText);
}

void fs_v8_atomic_write_bytes_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::AtomicWriteBytes);
}

void fs_v8_directory_create_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::DirectoryCreate);
}

void fs_v8_directory_list_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::DirectoryList);
}

void fs_v8_directory_delete_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::DirectoryDelete);
}

void fs_v8_symlink_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::Symlink);
}

void fs_v8_read_link_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::ReadLink);
}

void fs_v8_temp_file_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::TempFile);
}

void fs_v8_temp_directory_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::TempDirectory);
}

void fs_v8_open_handle_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::OpenHandle);
}

void fs_v8_handle_read_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::HandleRead);
}

void fs_v8_handle_write_text_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::HandleWriteText);
}

void fs_v8_handle_write_bytes_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::HandleWriteBytes);
}

void fs_v8_handle_seek_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::HandleSeek);
}

void fs_v8_handle_truncate_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::HandleTruncate);
}

void fs_v8_handle_flush_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::HandleFlush);
}

void fs_v8_handle_sync_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::HandleSync);
}

void fs_v8_handle_close_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::HandleClose);
}

void fs_v8_watch_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::WatchOpen);
}

void fs_v8_watch_next_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::WatchNext);
}

void fs_v8_watch_close_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::WatchClose);
}

bool fs_v8_set_function(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> object, const char* name,
                        v8::FunctionCallback callback)
{
    v8::Local<v8::String> key;
    v8::Local<v8::FunctionTemplate> function_template;
    v8::Local<v8::Function> function;

    if (!sl_status_is_ok(fs_v8_to_local_string(isolate, sl_str_from_cstr(name), &key))) {
        return false;
    }
    function_template = v8::FunctionTemplate::New(isolate, callback);
    if (!function_template->GetFunction(context).ToLocal(&function)) {
        return false;
    }
    return object->Set(context, key, function).FromMaybe(false);
}

} // namespace

bool sl_v8_install_fs_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context,
                                 v8::Local<v8::Object> sloppy)
{
    v8::Isolate* isolate = backend == nullptr ? nullptr : backend->isolate;
    v8::Local<v8::String> fs_key;

    if (backend == nullptr || isolate == nullptr) {
        return false;
    }
    v8::Local<v8::Object> fs = v8::Object::New(isolate);
    if (!backend->has_runtime_features ||
        !sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_STDLIB_FS))
    {
        return true;
    }
    if (!sl_status_is_ok(fs_v8_to_local_string(isolate, sl_str_from_cstr("fs"), &fs_key))) {
        return false;
    }
    if (!fs_v8_set_function(isolate, context, fs, "readText", fs_v8_read_text_callback) ||
        !fs_v8_set_function(isolate, context, fs, "readBytes", fs_v8_read_bytes_callback) ||
        !fs_v8_set_function(isolate, context, fs, "writeText", fs_v8_write_text_callback) ||
        !fs_v8_set_function(isolate, context, fs, "writeBytes", fs_v8_write_bytes_callback) ||
        !fs_v8_set_function(isolate, context, fs, "appendText", fs_v8_append_text_callback) ||
        !fs_v8_set_function(isolate, context, fs, "appendBytes", fs_v8_append_bytes_callback) ||
        !fs_v8_set_function(isolate, context, fs, "exists", fs_v8_exists_callback) ||
        !fs_v8_set_function(isolate, context, fs, "stat", fs_v8_stat_callback) ||
        !fs_v8_set_function(isolate, context, fs, "copy", fs_v8_copy_callback) ||
        !fs_v8_set_function(isolate, context, fs, "move", fs_v8_move_callback) ||
        !fs_v8_set_function(isolate, context, fs, "delete", fs_v8_delete_callback) ||
        !fs_v8_set_function(isolate, context, fs, "atomicWriteText",
                            fs_v8_atomic_write_text_callback) ||
        !fs_v8_set_function(isolate, context, fs, "atomicWriteBytes",
                            fs_v8_atomic_write_bytes_callback) ||
        !fs_v8_set_function(isolate, context, fs, "directoryCreate",
                            fs_v8_directory_create_callback) ||
        !fs_v8_set_function(isolate, context, fs, "directoryList", fs_v8_directory_list_callback) ||
        !fs_v8_set_function(isolate, context, fs, "directoryDelete",
                            fs_v8_directory_delete_callback) ||
        !fs_v8_set_function(isolate, context, fs, "symlink", fs_v8_symlink_callback) ||
        !fs_v8_set_function(isolate, context, fs, "readLink", fs_v8_read_link_callback) ||
        !fs_v8_set_function(isolate, context, fs, "tempFile", fs_v8_temp_file_callback) ||
        !fs_v8_set_function(isolate, context, fs, "tempDirectory", fs_v8_temp_directory_callback) ||
        !fs_v8_set_function(isolate, context, fs, "openHandle", fs_v8_open_handle_callback) ||
        !fs_v8_set_function(isolate, context, fs, "handleRead", fs_v8_handle_read_callback) ||
        !fs_v8_set_function(isolate, context, fs, "handleWriteText",
                            fs_v8_handle_write_text_callback) ||
        !fs_v8_set_function(isolate, context, fs, "handleWriteBytes",
                            fs_v8_handle_write_bytes_callback) ||
        !fs_v8_set_function(isolate, context, fs, "handleSeek", fs_v8_handle_seek_callback) ||
        !fs_v8_set_function(isolate, context, fs, "handleTruncate",
                            fs_v8_handle_truncate_callback) ||
        !fs_v8_set_function(isolate, context, fs, "handleFlush", fs_v8_handle_flush_callback) ||
        !fs_v8_set_function(isolate, context, fs, "handleSync", fs_v8_handle_sync_callback) ||
        !fs_v8_set_function(isolate, context, fs, "handleClose", fs_v8_handle_close_callback) ||
        !fs_v8_set_function(isolate, context, fs, "watch", fs_v8_watch_callback) ||
        !fs_v8_set_function(isolate, context, fs, "watchNext", fs_v8_watch_next_callback) ||
        !fs_v8_set_function(isolate, context, fs, "watchClose", fs_v8_watch_close_callback) ||
        !sloppy->Set(context, fs_key, fs).FromMaybe(false))
    {
        return false;
    }
    return true;
}
