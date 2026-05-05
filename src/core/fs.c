/*
 * src/core/fs.c
 *
 * Filesystem path classification, root resolution, and platform-backend entrypoints.
 * OS APIs stay in platform backend files; this file owns policy shape and normalization.
 */
#include "sloppy/fs.h"

#include "fs_platform.h"

#include "sloppy/checked_math.h"

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
    status = sl_checked_mul_size(count, sizeof(size_t), &bytes);
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
