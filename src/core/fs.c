/*
 * src/core/fs.c
 *
 * Filesystem path classification, root resolution, and platform-backend entrypoints.
 * OS APIs stay in platform backend files; this file owns policy shape and normalization.
 */
#include "sloppy/fs.h"

#include "fs_platform.h"

#include "sloppy/checked_math.h"
#include "sloppy/container.h"

typedef struct SlFsWatchSnapshot
{
    SlOwnedStr path;
    SlFsNodeKind kind;
    uint64_t size;
    uint64_t modified_nsec;
    bool exists;
    bool seen;
} SlFsWatchSnapshot;

struct SlFsWatchHandle
{
    SlArena* arena;
    SlOwnedStr path;
    SlRingQueue queue;
    SlFsWatchSnapshot* snapshots;
    size_t snapshot_capacity;
    size_t snapshot_count;
    bool directory;
    bool recursive;
    bool overflow_pending;
    bool closed;
};

static bool sl_fs_str_valid(SlStr value)
{
    return value.length == 0U || value.ptr != NULL;
}

static bool sl_fs_is_separator(char ch)
{
    return ch == '/' || ch == '\\';
}

static bool sl_fs_is_alpha(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

static bool sl_fs_contains_nul(SlStr value)
{
    size_t index = 0U;

    if (!sl_fs_str_valid(value)) {
        return true;
    }
    for (index = 0U; index < value.length; index += 1U) {
        if (value.ptr[index] == '\0') {
            return true;
        }
    }
    return false;
}

static bool sl_fs_watch_snapshot_matches(const SlFsWatchSnapshot* snapshot, SlStr path)
{
    return snapshot != NULL && sl_str_equal(sl_owned_str_as_view(snapshot->path), path);
}

static SlStatus sl_fs_watch_copy_path(SlArena* arena, SlStr path, SlOwnedStr* out)
{
    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (path.length == 0U) {
        *out = (SlOwnedStr){0};
        return sl_status_ok();
    }
    return sl_str_copy_to_arena(arena, path, out);
}

static SlOwnedStr sl_fs_watch_owned_view(SlStr path)
{
    return (SlOwnedStr){.ptr = (char*)path.ptr, .length = path.length};
}

static SlStatus sl_fs_watch_enqueue(SlFsWatchHandle* watch, SlFsWatchEventKind kind, SlStr path,
                                    bool is_directory)
{
    SlFsWatchEvent event;
    SlStatus status;

    if (watch == NULL || watch->closed) {
        return sl_status_from_code(SL_STATUS_STALE_RESOURCE);
    }
    event = (SlFsWatchEvent){
        .kind = kind, .path = sl_fs_watch_owned_view(path), .is_directory = is_directory};
    status = sl_ring_queue_push(&watch->queue, &event);
    if (sl_status_code(status) == SL_STATUS_CAPACITY_EXCEEDED) {
        watch->overflow_pending = true;
        return sl_status_ok();
    }
    return status;
}

static SlStatus sl_fs_watch_pop(SlFsWatchHandle* watch, SlArena* arena, SlFsWatchEvent* out)
{
    SlFsWatchEvent event = {0};
    SlStatus status;

    if (watch == NULL || arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlFsWatchEvent){0};
    if (sl_ring_queue_pop_front(&watch->queue, &event)) {
        *out = (SlFsWatchEvent){
            .kind = event.kind, .is_directory = event.is_directory, .overflow = event.overflow};
        status = sl_fs_watch_copy_path(arena, sl_owned_str_as_view(event.path), &out->path);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        return sl_status_ok();
    }
    if (watch->overflow_pending) {
        watch->overflow_pending = false;
        *out = (SlFsWatchEvent){.kind = SL_FS_WATCH_EVENT_OVERFLOW, .overflow = true};
        return sl_status_ok();
    }
    return sl_status_from_code(SL_STATUS_DEADLINE_EXCEEDED);
}

static SlFsWatchSnapshot* sl_fs_watch_find_snapshot(SlFsWatchHandle* watch, SlStr path)
{
    size_t index = 0U;

    for (index = 0U; watch != NULL && index < watch->snapshot_count; index += 1U) {
        if (sl_fs_watch_snapshot_matches(&watch->snapshots[index], path)) {
            return &watch->snapshots[index];
        }
    }
    return NULL;
}

static bool sl_fs_watch_directory_overflow_status(SlStatus status)
{
    SlStatusCode code = sl_status_code(status);

    return code == SL_STATUS_OUT_OF_MEMORY || code == SL_STATUS_CAPACITY_EXCEEDED;
}

static SlStatus sl_fs_watch_update_file_snapshot(SlFsWatchHandle* watch, const SlFsStat* stat)
{
    SlFsWatchSnapshot* snapshot = NULL;
    SlStatus status;

    if (watch == NULL || stat == NULL || watch->arena == NULL || watch->snapshots == NULL ||
        watch->snapshot_capacity == 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    snapshot = &watch->snapshots[0];
    if (snapshot->path.ptr == NULL && watch->path.ptr != NULL) {
        status =
            sl_fs_watch_copy_path(watch->arena, sl_owned_str_as_view(watch->path), &snapshot->path);
        if (!sl_status_is_ok(status)) {
            *snapshot = (SlFsWatchSnapshot){0};
            return status;
        }
    }
    snapshot->kind = stat->kind;
    snapshot->size = stat->size;
    snapshot->modified_nsec = stat->modified_nsec;
    snapshot->exists = stat->exists;
    snapshot->seen = true;
    watch->snapshot_count = 1U;
    return sl_status_ok();
}

static SlStatus sl_fs_watch_scan_file(SlFsWatchHandle* watch)
{
    SlFsStat stat = {0};
    SlStatus status;
    SlFsWatchSnapshot* previous = NULL;

    status = sl_fs_stat(sl_owned_str_as_view(watch->path), &stat, NULL);
    if (!sl_status_is_ok(status) && sl_status_code(status) != SL_STATUS_OUT_OF_RANGE) {
        return status;
    }
    previous = watch->snapshot_count == 0U ? NULL : &watch->snapshots[0];
    if (previous != NULL) {
        if (!previous->exists && stat.exists) {
            status = sl_fs_watch_enqueue(watch, SL_FS_WATCH_EVENT_CREATED,
                                         sl_owned_str_as_view(previous->path),
                                         stat.kind == SL_FS_NODE_DIRECTORY);
        }
        else if (previous->exists && !stat.exists) {
            status = sl_fs_watch_enqueue(watch, SL_FS_WATCH_EVENT_DELETED,
                                         sl_owned_str_as_view(previous->path),
                                         previous->kind == SL_FS_NODE_DIRECTORY);
        }
        else if (previous->exists && stat.exists &&
                 (previous->kind != stat.kind || previous->size != stat.size ||
                  previous->modified_nsec != stat.modified_nsec))
        {
            status = sl_fs_watch_enqueue(watch, SL_FS_WATCH_EVENT_MODIFIED,
                                         sl_owned_str_as_view(previous->path),
                                         stat.kind == SL_FS_NODE_DIRECTORY);
        }
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_fs_watch_update_file_snapshot(watch, &stat);
}

static SlStatus sl_fs_watch_add_snapshot(SlFsWatchHandle* watch, SlStr path, SlFsNodeKind kind,
                                         uint64_t size, uint64_t modified_nsec,
                                         SlFsWatchSnapshot** out_snapshot)
{
    SlFsWatchSnapshot* snapshot = NULL;
    SlStatus status;

    if (out_snapshot != NULL) {
        *out_snapshot = NULL;
    }
    if (watch == NULL || watch->arena == NULL || watch->snapshots == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (watch->snapshot_count >= watch->snapshot_capacity) {
        watch->overflow_pending = true;
        return sl_status_ok();
    }
    snapshot = &watch->snapshots[watch->snapshot_count];
    *snapshot = (SlFsWatchSnapshot){
        .kind = kind, .size = size, .modified_nsec = modified_nsec, .exists = true, .seen = true};
    status = sl_fs_watch_copy_path(watch->arena, path, &snapshot->path);
    if (!sl_status_is_ok(status)) {
        *snapshot = (SlFsWatchSnapshot){0};
        return status;
    }
    watch->snapshot_count += 1U;
    if (out_snapshot != NULL) {
        *out_snapshot = snapshot;
    }
    return sl_status_ok();
}

static SlStatus sl_fs_watch_seed_directory_snapshot(SlFsWatchHandle* watch,
                                                    const SlFsDirectoryList* list)
{
    size_t index = 0U;
    SlStatus status;

    if (watch == NULL || list == NULL || watch->snapshots == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    for (index = 0U; index < list->count; index += 1U) {
        status = sl_fs_watch_add_snapshot(watch, sl_owned_str_as_view(list->entries[index].name),
                                          list->entries[index].kind, list->entries[index].size,
                                          list->entries[index].modified_nsec, NULL);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (watch->snapshot_count >= watch->snapshot_capacity && index + 1U < list->count) {
            watch->overflow_pending = true;
            return sl_status_ok();
        }
    }
    return sl_status_ok();
}

static SlStatus sl_fs_watch_scan_directory_entry(SlFsWatchHandle* watch,
                                                 const SlFsDirectoryEntry* entry)
{
    SlFsWatchSnapshot* previous = NULL;
    SlStatus status;

    if (watch == NULL || entry == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    previous = sl_fs_watch_find_snapshot(watch, sl_owned_str_as_view(entry->name));
    if (previous == NULL) {
        status = sl_fs_watch_add_snapshot(watch, sl_owned_str_as_view(entry->name), entry->kind,
                                          entry->size, entry->modified_nsec, &previous);
        if (previous != NULL && sl_status_is_ok(status)) {
            status = sl_fs_watch_enqueue(watch, SL_FS_WATCH_EVENT_CREATED,
                                         sl_owned_str_as_view(previous->path),
                                         entry->kind == SL_FS_NODE_DIRECTORY);
        }
        return status;
    }
    previous->seen = true;
    if (previous->kind != entry->kind || previous->size != entry->size ||
        previous->modified_nsec != entry->modified_nsec)
    {
        status = sl_fs_watch_enqueue(watch, SL_FS_WATCH_EVENT_MODIFIED,
                                     sl_owned_str_as_view(previous->path),
                                     entry->kind == SL_FS_NODE_DIRECTORY);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    previous->kind = entry->kind;
    previous->size = entry->size;
    previous->modified_nsec = entry->modified_nsec;
    previous->exists = true;
    return sl_status_ok();
}

static SlStatus sl_fs_watch_prune_deleted_snapshots(SlFsWatchHandle* watch)
{
    size_t index = 0U;
    SlStatus status;

    if (watch == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    while (index < watch->snapshot_count) {
        SlFsWatchSnapshot* previous = &watch->snapshots[index];

        if (!previous->exists || previous->seen) {
            index += 1U;
            continue;
        }
        status = sl_fs_watch_enqueue(watch, SL_FS_WATCH_EVENT_DELETED,
                                     sl_owned_str_as_view(previous->path),
                                     previous->kind == SL_FS_NODE_DIRECTORY);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        watch->snapshot_count -= 1U;
        if (index < watch->snapshot_count) {
            watch->snapshots[index] = watch->snapshots[watch->snapshot_count];
        }
    }
    return sl_status_ok();
}

static SlStatus sl_fs_watch_scan_directory(SlFsWatchHandle* watch)
{
    unsigned char scan_storage[65536];
    SlArena scan_arena = {0};
    SlFsDirectoryList list = {0};
    SlStatus status;
    size_t index = 0U;

    status = sl_arena_init(&scan_arena, scan_storage, sizeof(scan_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_fs_list_directory(&scan_arena, sl_owned_str_as_view(watch->path), &list, NULL);
    if (!sl_status_is_ok(status)) {
        if (sl_fs_watch_directory_overflow_status(status)) {
            watch->overflow_pending = true;
            return sl_status_ok();
        }
        return status;
    }
    for (index = 0U; index < watch->snapshot_count; index += 1U) {
        watch->snapshots[index].seen = false;
    }
    for (index = 0U; index < list.count; index += 1U) {
        status = sl_fs_watch_scan_directory_entry(watch, &list.entries[index]);
        if (!sl_status_is_ok(status)) {
            if (sl_fs_watch_directory_overflow_status(status)) {
                watch->overflow_pending = true;
                return sl_status_ok();
            }
            return status;
        }
    }
    return sl_fs_watch_prune_deleted_snapshots(watch);
}

static SlStatus sl_fs_watch_normalize_options(const SlFsWatchOptions* options,
                                              SlFsWatchOptions* out)
{
    SlFsWatchOptions effective = {0};

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    effective = options == NULL ? (SlFsWatchOptions){0} : *options;
    if (effective.recursive) {
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    }
    if (effective.queue_capacity == 0U) {
        effective.queue_capacity = 16U;
    }
    if (effective.snapshot_capacity == 0U) {
        effective.snapshot_capacity = effective.directory ? 128U : 1U;
    }
    if (!effective.directory) {
        effective.snapshot_capacity = 1U;
    }
    if (effective.queue_capacity > 256U || effective.snapshot_capacity > 1024U) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }
    *out = effective;
    return sl_status_ok();
}

static SlStatus sl_fs_watch_alloc(SlArena* arena, SlStr path, const SlFsWatchOptions* options,
                                  SlFsWatchHandle** out)
{
    SlFsWatchHandle* watch = NULL;
    SlSlice snapshot_storage = {0};
    void* memory = NULL;
    SlStatus status;

    if (arena == NULL || options == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = NULL;
    status = sl_arena_alloc(arena, sizeof(SlFsWatchHandle), _Alignof(SlFsWatchHandle), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (memory == NULL) {
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }
    watch = (SlFsWatchHandle*)memory;
    *watch = (SlFsWatchHandle){.arena = arena,
                               .directory = options->directory,
                               .recursive = options->recursive,
                               .snapshot_capacity = options->snapshot_capacity};
    status = sl_fs_watch_copy_path(arena, path, &watch->path);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_ring_queue_init_from_arena(&watch->queue, arena, sizeof(SlFsWatchEvent),
                                           _Alignof(SlFsWatchEvent), options->queue_capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_array_alloc(arena, options->snapshot_capacity, sizeof(SlFsWatchSnapshot),
                                  _Alignof(SlFsWatchSnapshot), &snapshot_storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    watch->snapshots = (SlFsWatchSnapshot*)snapshot_storage.ptr;
    *out = watch;
    return sl_status_ok();
}

static SlStatus sl_fs_watch_seed(SlFsWatchHandle* watch, SlArena* arena, SlStr path,
                                 SlDiag* out_diag)
{
    SlFsStat stat = {0};
    SlStatus status = sl_fs_stat(path, &stat, out_diag);

    if (watch == NULL || arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (watch->directory) {
        unsigned char scan_storage[65536];
        SlArena scan_arena = {0};
        SlFsDirectoryList list = {0};

        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (!stat.exists || stat.kind != SL_FS_NODE_DIRECTORY) {
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        status = sl_arena_init(&scan_arena, scan_storage, sizeof(scan_storage));
        if (sl_status_is_ok(status)) {
            status =
                sl_fs_list_directory(&scan_arena, sl_owned_str_as_view(watch->path), &list, NULL);
        }
        if (!sl_status_is_ok(status)) {
            return status;
        }
        return sl_fs_watch_seed_directory_snapshot(watch, &list);
    }
    if (!sl_status_is_ok(status) && sl_status_code(status) != SL_STATUS_OUT_OF_RANGE) {
        return status;
    }
    return sl_fs_watch_update_file_snapshot(watch, &stat);
}

static SlDiag sl_fs_diag(SlDiagCode code, SlStr message)
{
    SlDiag diag = {0};

    diag.severity = SL_DIAG_SEVERITY_ERROR;
    diag.code = code;
    diag.message = message;
    diag.primary_span = sl_source_span_unknown();
    return diag;
}

static SlStatus sl_fs_fail(SlDiag* out_diag, SlDiagCode diag_code, SlStatusCode status_code,
                           SlStr message)
{
    if (out_diag != NULL) {
        *out_diag = sl_fs_diag(diag_code, message);
    }
    return sl_status_from_code(status_code);
}

SlFsPolicy sl_fs_development_policy(SlStr app_root)
{
    SlFsPolicy policy = {0};

    policy.app_root = app_root;
    policy.mode = SL_FS_POLICY_DEVELOPMENT;
    policy.allow_absolute = true;
    return policy;
}

SlFsPolicy sl_fs_strict_policy(SlStr app_root, const SlFsRoot* roots, size_t root_count,
                               bool allow_absolute)
{
    SlFsPolicy policy = {0};

    policy.app_root = app_root;
    policy.roots = roots;
    policy.root_count = root_count;
    policy.mode = SL_FS_POLICY_STRICT;
    policy.allow_absolute = allow_absolute;
    return policy;
}

SlStr sl_fs_path_kind_name(SlFsPathKind kind)
{
    switch (kind) {
    case SL_FS_PATH_PROJECT_RELATIVE:
        return sl_str_from_cstr("project-relative");
    case SL_FS_PATH_NAMED_ROOT:
        return sl_str_from_cstr("named-root");
    case SL_FS_PATH_ABSOLUTE:
        return sl_str_from_cstr("absolute");
    default:
        return sl_str_from_cstr("invalid");
    }
}

SlStatus sl_fs_classify_path(SlStr input, SlFsPathKind* out_kind)
{
    size_t index = 0U;

    if (out_kind == NULL || input.length == 0U || sl_fs_contains_nul(input)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_kind = SL_FS_PATH_INVALID;
    if (input.length >= 2U && sl_fs_is_separator(input.ptr[0]) && sl_fs_is_separator(input.ptr[1]))
    {
        *out_kind = SL_FS_PATH_ABSOLUTE;
        return sl_status_ok();
    }
    if (sl_fs_is_separator(input.ptr[0])) {
        *out_kind = SL_FS_PATH_ABSOLUTE;
        return sl_status_ok();
    }
    if (input.length >= 3U && sl_fs_is_alpha(input.ptr[0]) && input.ptr[1] == ':' &&
        sl_fs_is_separator(input.ptr[2]))
    {
        *out_kind = SL_FS_PATH_ABSOLUTE;
        return sl_status_ok();
    }
    if (input.length >= 2U && input.ptr[0] == '.' && sl_fs_is_separator(input.ptr[1])) {
        *out_kind = SL_FS_PATH_PROJECT_RELATIVE;
        return sl_status_ok();
    }

    for (index = 0U; index < input.length; index += 1U) {
        if (input.ptr[index] == ':') {
            if (index == 0U || index + 1U >= input.length ||
                !sl_fs_is_separator(input.ptr[index + 1U]))
            {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            *out_kind = SL_FS_PATH_NAMED_ROOT;
            return sl_status_ok();
        }
    }

    return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
}

static SlStatus sl_fs_copy_segment(SlArena* arena, SlStr value, SlOwnedStr* out)
{
    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOwnedStr){0};
    return sl_str_copy_to_arena(arena, value, out);
}

static const SlFsRoot* sl_fs_find_root(const SlFsPolicy* policy, SlStr name)
{
    size_t index = 0U;

    if (policy == NULL || (policy->root_count != 0U && policy->roots == NULL)) {
        return NULL;
    }
    for (index = 0U; index < policy->root_count; index += 1U) {
        if (sl_str_equal(policy->roots[index].name, name)) {
            return &policy->roots[index];
        }
    }
    return NULL;
}

static void sl_fs_copy_chars(char* dst, const char* src, size_t length)
{
    size_t index = 0U;

    for (index = 0U; index < length; index += 1U) {
        dst[index] = src[index];
    }
}

static SlStatus sl_fs_alloc_size_array(SlArena* arena, size_t count, size_t** out)
{
    void* memory = NULL;
    size_t bytes = 0U;
    SlStatus status;

    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out = NULL;
    status = sl_checked_array_size(count, sizeof(size_t), &bytes);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, bytes, _Alignof(size_t), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out = (size_t*)memory;
    return sl_status_ok();
}

static SlStatus sl_fs_collect_normalized_segments(SlStr input, size_t* starts, size_t* lengths,
                                                  size_t* out_count, bool* out_escaped)
{
    size_t segment_count = 0U;
    size_t cursor = 0U;

    if (starts == NULL || lengths == NULL || out_count == NULL || out_escaped == NULL ||
        !sl_fs_str_valid(input))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_count = 0U;
    *out_escaped = false;

    while (cursor < input.length) {
        size_t start = 0U;
        size_t length = 0U;

        while (cursor < input.length && sl_fs_is_separator(input.ptr[cursor])) {
            cursor += 1U;
        }
        start = cursor;
        while (cursor < input.length && !sl_fs_is_separator(input.ptr[cursor])) {
            cursor += 1U;
        }
        length = cursor - start;
        if (length == 0U || (length == 1U && input.ptr[start] == '.')) {
            continue;
        }
        if (length == 2U && input.ptr[start] == '.' && input.ptr[start + 1U] == '.') {
            if (segment_count == 0U) {
                *out_escaped = true;
                return sl_status_ok();
            }
            segment_count -= 1U;
            continue;
        }
        starts[segment_count] = start;
        lengths[segment_count] = length;
        segment_count += 1U;
    }

    *out_count = segment_count;
    return sl_status_ok();
}

static void sl_fs_write_normalized_segments(SlStr input, const size_t* starts,
                                            const size_t* lengths, size_t segment_count,
                                            SlOwnedStr* out)
{
    size_t index = 0U;

    for (index = 0U; index < segment_count; index += 1U) {
        if (index != 0U) {
            out->ptr[out->length] = '/';
            out->length += 1U;
        }
        sl_fs_copy_chars(out->ptr + out->length, input.ptr + starts[index], lengths[index]);
        out->length += lengths[index];
    }
}

static SlStatus sl_fs_normalize_tail(SlArena* arena, SlStr input, SlOwnedStr* out,
                                     bool* out_escaped)
{
    size_t* starts = NULL;
    size_t* lengths = NULL;
    size_t segment_count = 0U;
    size_t allocation_count = 0U;
    void* memory = NULL;
    SlStatus status;

    if (arena == NULL || out == NULL || out_escaped == NULL || !sl_fs_str_valid(input)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out = (SlOwnedStr){0};
    *out_escaped = false;
    status = sl_checked_add_size(input.length, 1U, &allocation_count);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, allocation_count, _Alignof(char), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_fs_alloc_size_array(arena, allocation_count, &starts);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_fs_alloc_size_array(arena, allocation_count, &lengths);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_fs_collect_normalized_segments(input, starts, lengths, &segment_count, out_escaped);
    if (!sl_status_is_ok(status) || *out_escaped) {
        return status;
    }

    out->ptr = (char*)memory;
    sl_fs_write_normalized_segments(input, starts, lengths, segment_count, out);
    return sl_status_ok();
}

static SlStatus sl_fs_join_paths(SlArena* arena, SlStr root, SlStr tail, SlOwnedStr* out)
{
    size_t total = 0U;
    void* memory = NULL;
    char* buffer = NULL;
    size_t offset = 0U;
    SlStatus status;

    if (arena == NULL || out == NULL || !sl_fs_str_valid(root) || !sl_fs_str_valid(tail)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOwnedStr){0};

    status = sl_checked_add_size(root.length, tail.length, &total);
    if (sl_status_is_ok(status)) {
        status = sl_checked_add_size(total, 2U, &total);
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, total, _Alignof(char), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    buffer = (char*)memory;
    if (root.length != 0U) {
        sl_fs_copy_chars(buffer, root.ptr, root.length);
        offset = root.length;
    }
    if (offset != 0U && tail.length != 0U && !sl_fs_is_separator(buffer[offset - 1U])) {
        buffer[offset] = '/';
        offset += 1U;
    }
    if (tail.length != 0U) {
        sl_fs_copy_chars(buffer + offset, tail.ptr, tail.length);
        offset += tail.length;
    }
    out->ptr = buffer;
    out->length = offset;
    return sl_status_ok();
}

static SlStatus sl_fs_resolve_absolute(SlArena* arena, const SlFsPolicy* policy, SlStr input,
                                       SlFsResolvedPath* out, SlDiag* out_diag, SlArenaMark mark)
{
    SlStatus status;

    if (policy->mode == SL_FS_POLICY_STRICT && !policy->allow_absolute) {
        (void)sl_arena_reset_to(arena, mark);
        return sl_fs_fail(
            out_diag, SL_DIAG_PERMISSION_DENIED, SL_STATUS_INVALID_STATE,
            sl_str_from_cstr("absolute filesystem paths require explicit allow in strict mode"));
    }
    status = sl_fs_copy_segment(arena, input, &out->path);
    if (sl_status_is_ok(status)) {
        out->development_absolute_warning = policy->mode == SL_FS_POLICY_DEVELOPMENT;
    }
    return status;
}

static SlStatus sl_fs_resolve_project_relative(SlArena* arena, const SlFsPolicy* policy,
                                               SlStr input, SlFsResolvedPath* out, SlDiag* out_diag,
                                               SlArenaMark mark)
{
    SlOwnedStr normalized = {0};
    SlStr tail = sl_str_from_parts(input.ptr + 2U, input.length - 2U);
    bool escaped = false;
    SlStatus status;

    status = sl_fs_normalize_tail(arena, tail, &normalized, &escaped);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (escaped) {
        (void)sl_arena_reset_to(arena, mark);
        return sl_fs_fail(
            out_diag, SL_DIAG_PERMISSION_DENIED, SL_STATUS_INVALID_STATE,
            sl_str_from_cstr("project-relative filesystem path escapes the app root"));
    }
    return sl_fs_join_paths(arena, policy->app_root, sl_owned_str_as_view(normalized), &out->path);
}

static SlStatus sl_fs_resolve_named_root(SlArena* arena, const SlFsPolicy* policy, SlStr input,
                                         SlFsResolvedPath* out, SlDiag* out_diag, SlArenaMark mark)
{
    size_t colon = 0U;
    SlStr name;
    SlStr tail;
    const SlFsRoot* root = NULL;
    SlOwnedStr normalized = {0};
    bool escaped = false;
    SlStatus status;

    while (colon < input.length && input.ptr[colon] != ':') {
        colon += 1U;
    }
    name = sl_str_from_parts(input.ptr, colon);
    root = sl_fs_find_root(policy, name);
    if (root == NULL) {
        (void)sl_arena_reset_to(arena, mark);
        return sl_fs_fail(out_diag, SL_DIAG_INVALID_ARGUMENT, SL_STATUS_INVALID_ARGUMENT,
                          sl_str_from_cstr("unknown filesystem root"));
    }
    tail = sl_str_from_parts(input.ptr + colon + 2U, input.length - colon - 2U);
    status = sl_fs_normalize_tail(arena, tail, &normalized, &escaped);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (escaped) {
        (void)sl_arena_reset_to(arena, mark);
        return sl_fs_fail(out_diag, SL_DIAG_PERMISSION_DENIED, SL_STATUS_INVALID_STATE,
                          sl_str_from_cstr("named-root filesystem path escapes its root"));
    }
    status = sl_fs_copy_segment(arena, name, &out->root_name);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_fs_join_paths(arena, root->path, sl_owned_str_as_view(normalized), &out->path);
}

SlStatus sl_fs_resolve_path(SlArena* arena, const SlFsPolicy* policy, SlStr input,
                            SlFsResolvedPath* out, SlDiag* out_diag)
{
    SlFsPathKind kind = SL_FS_PATH_INVALID;
    SlStatus status;
    SlArenaMark mark;

    if (out != NULL) {
        *out = (SlFsResolvedPath){0};
    }
    if (arena == NULL || policy == NULL || out == NULL || input.length == 0U ||
        !sl_fs_str_valid(policy->app_root))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    mark = sl_arena_mark(arena);
    status = sl_fs_classify_path(input, &kind);
    if (!sl_status_is_ok(status)) {
        (void)sl_arena_reset_to(arena, mark);
        return sl_fs_fail(out_diag, SL_DIAG_INVALID_ARGUMENT, SL_STATUS_INVALID_ARGUMENT,
                          sl_str_from_cstr("invalid filesystem path syntax"));
    }

    out->kind = kind;
    if (kind == SL_FS_PATH_ABSOLUTE) {
        return sl_fs_resolve_absolute(arena, policy, input, out, out_diag, mark);
    }
    if (kind == SL_FS_PATH_PROJECT_RELATIVE) {
        return sl_fs_resolve_project_relative(arena, policy, input, out, out_diag, mark);
    }
    if (kind == SL_FS_PATH_NAMED_ROOT) {
        return sl_fs_resolve_named_root(arena, policy, input, out, out_diag, mark);
    }

    (void)sl_arena_reset_to(arena, mark);
    return sl_fs_fail(out_diag, SL_DIAG_INVALID_ARGUMENT, SL_STATUS_INVALID_ARGUMENT,
                      sl_str_from_cstr("invalid filesystem path syntax"));
}

SlStatus sl_fs_read_file(SlArena* arena, SlStr path, SlOwnedBytes* out, SlDiag* out_diag)
{
    if (out != NULL) {
        *out = (SlOwnedBytes){0};
    }
    if (arena == NULL || out == NULL || path.length == 0U || sl_fs_contains_nul(path)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_read_file(arena, path, out, out_diag);
}

SlStatus sl_fs_write_file(SlStr path, SlBytes bytes, bool append, SlDiag* out_diag)
{
    if (path.length == 0U || sl_fs_contains_nul(path) || (bytes.length != 0U && bytes.ptr == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_write_file(path, bytes, append, out_diag);
}

SlStatus sl_fs_copy_file(SlStr from_path, SlStr to_path, bool overwrite, SlDiag* out_diag)
{
    if (from_path.length == 0U || to_path.length == 0U || sl_fs_contains_nul(from_path) ||
        sl_fs_contains_nul(to_path))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_copy_file(from_path, to_path, overwrite, out_diag);
}

SlStatus sl_fs_move_file(SlStr from_path, SlStr to_path, bool overwrite, SlDiag* out_diag)
{
    if (from_path.length == 0U || to_path.length == 0U || sl_fs_contains_nul(from_path) ||
        sl_fs_contains_nul(to_path))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_move_file(from_path, to_path, overwrite, out_diag);
}

SlStatus sl_fs_delete_file(SlStr path, SlDiag* out_diag)
{
    if (path.length == 0U || sl_fs_contains_nul(path)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_delete_file(path, out_diag);
}

SlStatus sl_fs_stat(SlStr path, SlFsStat* out, SlDiag* out_diag)
{
    if (out != NULL) {
        *out = (SlFsStat){0};
    }
    if (path.length == 0U || sl_fs_contains_nul(path) || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_stat(path, out, out_diag);
}

SlStatus sl_fs_exists(SlStr path, bool* out_exists, SlDiag* out_diag)
{
    SlFsStat stat = {0};
    SlStatus status;

    if (out_exists == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_exists = false;
    status = sl_fs_stat(path, &stat, out_diag);
    if (!sl_status_is_ok(status) && sl_status_code(status) != SL_STATUS_OUT_OF_RANGE) {
        return status;
    }
    *out_exists = stat.exists;
    return sl_status_ok();
}

SlStatus sl_fs_create_directory(SlStr path, bool recursive, SlDiag* out_diag)
{
    if (path.length == 0U || sl_fs_contains_nul(path)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_create_directory(path, recursive, out_diag);
}

SlStatus sl_fs_delete_directory(SlStr path, bool recursive, SlDiag* out_diag)
{
    if (path.length == 0U || sl_fs_contains_nul(path)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_delete_directory(path, recursive, out_diag);
}

SlStatus sl_fs_list_directory(SlArena* arena, SlStr path, SlFsDirectoryList* out, SlDiag* out_diag)
{
    if (out != NULL) {
        *out = (SlFsDirectoryList){0};
    }
    if (arena == NULL || out == NULL || path.length == 0U || sl_fs_contains_nul(path)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_list_directory(arena, path, out, out_diag);
}

SlStatus sl_fs_create_symlink(SlStr target_path, SlStr link_path, bool directory, SlDiag* out_diag)
{
    if (target_path.length == 0U || link_path.length == 0U || sl_fs_contains_nul(target_path) ||
        sl_fs_contains_nul(link_path))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_create_symlink(target_path, link_path, directory, out_diag);
}

SlStatus sl_fs_read_link(SlArena* arena, SlStr path, SlOwnedStr* out, SlDiag* out_diag)
{
    if (out != NULL) {
        *out = (SlOwnedStr){0};
    }
    if (arena == NULL || out == NULL || path.length == 0U || sl_fs_contains_nul(path)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_read_link(arena, path, out, out_diag);
}

SlStatus sl_fs_create_temp_file(SlArena* arena, SlStr directory, SlStr prefix, SlFsTempPath* out,
                                SlDiag* out_diag)
{
    if (out != NULL) {
        *out = (SlFsTempPath){0};
    }
    if (arena == NULL || out == NULL || directory.length == 0U || sl_fs_contains_nul(directory) ||
        sl_fs_contains_nul(prefix))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_create_temp_file(arena, directory, prefix, out, out_diag);
}

SlStatus sl_fs_create_temp_directory(SlArena* arena, SlStr directory, SlStr prefix,
                                     SlFsTempPath* out, SlDiag* out_diag)
{
    if (out != NULL) {
        *out = (SlFsTempPath){0};
    }
    if (arena == NULL || out == NULL || directory.length == 0U || sl_fs_contains_nul(directory) ||
        sl_fs_contains_nul(prefix))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_create_temp_directory(arena, directory, prefix, out, out_diag);
}

SlStatus sl_fs_atomic_write_file(SlArena* arena, SlStr path, SlBytes bytes, SlDiag* out_diag)
{
    if (arena == NULL || path.length == 0U || sl_fs_contains_nul(path) ||
        (bytes.length != 0U && bytes.ptr == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_atomic_write_file(arena, path, bytes, out_diag);
}

SlStatus sl_fs_acquire_lock(SlArena* arena, SlStr path, SlFsFileLock** out_lock, SlDiag* out_diag)
{
    if (out_lock != NULL) {
        *out_lock = NULL;
    }
    if (arena == NULL || out_lock == NULL || path.length == 0U || sl_fs_contains_nul(path)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_acquire_lock(arena, path, out_lock, out_diag);
}

SlStatus sl_fs_release_lock(SlFsFileLock* lock, SlDiag* out_diag)
{
    if (lock == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_release_lock(lock, out_diag);
}

SlStatus sl_fs_open_file(SlArena* arena, SlStr path, SlFsFileAccess access, bool create,
                         SlFsFileHandle** out_handle, SlDiag* out_diag)
{
    if (out_handle != NULL) {
        *out_handle = NULL;
    }
    if (arena == NULL || out_handle == NULL || path.length == 0U || sl_fs_contains_nul(path) ||
        access > SL_FS_FILE_ACCESS_APPEND)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_open_file(arena, path, access, create, out_handle, out_diag);
}

SlStatus sl_fs_file_read(SlFsFileHandle* handle, SlArena* arena, size_t max_bytes,
                         SlOwnedBytes* out, SlDiag* out_diag)
{
    if (out != NULL) {
        *out = (SlOwnedBytes){0};
    }
    if (handle == NULL || arena == NULL || out == NULL || max_bytes == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_file_read(handle, arena, max_bytes, out, out_diag);
}

SlStatus sl_fs_file_write(SlFsFileHandle* handle, SlBytes bytes, SlDiag* out_diag)
{
    if (handle == NULL || (bytes.length != 0U && bytes.ptr == NULL)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_file_write(handle, bytes, out_diag);
}

SlStatus sl_fs_file_seek(SlFsFileHandle* handle, int64_t offset, SlFsSeekOrigin origin,
                         uint64_t* out_position, SlDiag* out_diag)
{
    if (out_position != NULL) {
        *out_position = 0U;
    }
    if (handle == NULL || out_position == NULL || origin > SL_FS_SEEK_END) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_file_seek(handle, offset, origin, out_position, out_diag);
}

SlStatus sl_fs_file_truncate(SlFsFileHandle* handle, uint64_t size, SlDiag* out_diag)
{
    if (handle == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_file_truncate(handle, size, out_diag);
}

SlStatus sl_fs_file_flush(SlFsFileHandle* handle, SlDiag* out_diag)
{
    if (handle == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_file_flush(handle, out_diag);
}

SlStatus sl_fs_file_sync(SlFsFileHandle* handle, SlDiag* out_diag)
{
    if (handle == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_file_sync(handle, out_diag);
}

SlStatus sl_fs_file_close(SlFsFileHandle* handle, SlDiag* out_diag)
{
    if (handle == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_fs_platform_file_close(handle, out_diag);
}

SlStatus sl_fs_watch_open(SlArena* arena, SlStr path, const SlFsWatchOptions* options,
                          SlFsWatchHandle** out_watch, SlDiag* out_diag)
{
    SlFsWatchOptions effective = {0};
    SlFsWatchHandle* watch = NULL;
    SlStatus status;

    if (out_watch != NULL) {
        *out_watch = NULL;
    }
    if (arena == NULL || out_watch == NULL || path.length == 0U || sl_fs_contains_nul(path)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_fs_watch_normalize_options(options, &effective);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_fs_watch_alloc(arena, path, &effective, &watch);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_fs_watch_seed(watch, arena, path, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out_watch = watch;
    return sl_status_ok();
}

SlStatus sl_fs_watch_next(SlFsWatchHandle* watch, SlArena* arena, SlFsWatchEvent* out_event,
                          SlDiag* out_diag)
{
    SlStatus status;

    (void)out_diag;
    if (watch == NULL || arena == NULL || out_event == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (watch->closed) {
        return sl_status_from_code(SL_STATUS_STALE_RESOURCE);
    }
    status = sl_fs_watch_pop(watch, arena, out_event);
    if (sl_status_code(status) != SL_STATUS_DEADLINE_EXCEEDED) {
        return status;
    }
    status = watch->directory ? sl_fs_watch_scan_directory(watch) : sl_fs_watch_scan_file(watch);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_fs_watch_pop(watch, arena, out_event);
}

SlStatus sl_fs_watch_close(SlFsWatchHandle* watch, SlDiag* out_diag)
{
    (void)out_diag;
    if (watch == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (watch->closed) {
        return sl_status_from_code(SL_STATUS_STALE_RESOURCE);
    }
    watch->closed = true;
    return sl_status_ok();
}
