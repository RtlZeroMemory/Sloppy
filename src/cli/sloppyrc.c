/*
 * sloppy.json project-run configuration.
 *
 * This module deliberately owns only the bounded source-input run config shape. Application
 * configuration remains in appsettings*.json and compiler/runtime code consumes that separately.
 */
#include "sloppyrc.h"

#include "sloppy/arena.h"
#include "sloppy/builder.h"
#include "sloppy/bytes.h"
#include "sloppy/fs.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdio.h>
#include <yyjson.h>

#define SL_SLOPPYRC_FILE "sloppy.json"
#define SL_SLOPPYRC_MAX_BYTES 8192U
#define SL_SLOPPYRC_FILE_READ_SCRATCH_BYTES 65536U
#define SL_SLOPPYRC_FILE_READ_ARENA_BYTES                                                          \
    (SL_SLOPPYRC_MAX_BYTES + SL_SLOPPYRC_FILE_READ_SCRATCH_BYTES)
#define SL_SLOPPYRC_DEFAULT_OUT_DIR ".sloppy"
#define SL_SLOPPYRC_DEFAULT_ENVIRONMENT "Development"

static void sl_sloppyrc_write_cstr(FILE* file, const char* text)
{
    if (file != NULL && text != NULL) {
        fputs(text, file);
    }
}

static void sl_sloppyrc_write_error_with_value(const char* prefix, const char* value,
                                               const char* suffix)
{
    sl_sloppyrc_write_cstr(stderr, prefix);
    if (value != NULL) {
        sl_sloppyrc_write_cstr(stderr, value);
    }
    sl_sloppyrc_write_cstr(stderr, suffix);
}

static int sl_sloppyrc_read_file(const char* path, unsigned char* buffer, size_t capacity,
                                 SlBytes* out, const char* not_found_prefix,
                                 const char* size_prefix, const char* failed_read_prefix)
{
    unsigned char read_storage[SL_SLOPPYRC_FILE_READ_ARENA_BYTES];
    SlArena arena = {0};
    SlArena output_arena = {0};
    SlOwnedBytes bytes = {0};
    SlOwnedBytes copied = {0};
    SlStatus status;

    if (path == NULL || buffer == NULL || out == NULL || not_found_prefix == NULL ||
        size_prefix == NULL || failed_read_prefix == NULL || capacity > SL_SLOPPYRC_MAX_BYTES)
    {
        return 1;
    }
    *out = sl_bytes_from_parts(NULL, 0U);

    status = sl_arena_init(&arena, read_storage, capacity + SL_SLOPPYRC_FILE_READ_SCRATCH_BYTES);
    if (!sl_status_is_ok(status)) {
        return 1;
    }

    status = sl_fs_read_file(&arena, sl_str_from_cstr(path), &bytes, NULL);
    if (sl_status_code(status) == SL_STATUS_OUT_OF_RANGE) {
        sl_sloppyrc_write_error_with_value(not_found_prefix, path, "\n");
        return 1;
    }
    if (sl_status_code(status) == SL_STATUS_CAPACITY_EXCEEDED) {
        sl_sloppyrc_write_error_with_value(size_prefix, path, "\n");
        return 1;
    }
    if (!sl_status_is_ok(status)) {
        sl_sloppyrc_write_error_with_value(failed_read_prefix, path, "\n");
        return 1;
    }
    if (bytes.length == 0U || bytes.length > capacity) {
        sl_sloppyrc_write_error_with_value(size_prefix, path, "\n");
        return 1;
    }

    status = sl_arena_init(&output_arena, buffer, capacity);
    if (!sl_status_is_ok(status)) {
        return 1;
    }
    status = sl_bytes_copy_to_arena(&output_arena, sl_owned_bytes_as_view(bytes), &copied);
    if (!sl_status_is_ok(status)) {
        sl_sloppyrc_write_error_with_value(size_prefix, path, "\n");
        return 1;
    }

    *out = sl_owned_bytes_as_view(copied);
    return 0;
}

static bool sl_sloppyrc_copy_json_string(char* buffer, size_t capacity, yyjson_val* value)
{
    SlStringBuilder builder = {0};
    SlStr text = {0};
    SlStr view = {0};

    if (buffer == NULL || capacity == 0U || value == NULL || !yyjson_is_str(value)) {
        return false;
    }

    text = sl_str_from_parts(yyjson_get_str(value), yyjson_get_len(value));
    if (text.length >= capacity || !sl_status_is_ok(sl_str_validate_no_nul(text))) {
        return false;
    }

    return sl_status_is_ok(sl_string_builder_init_fixed(&builder, buffer, capacity)) &&
           sl_status_is_ok(sl_string_builder_append_str(&builder, text)) &&
           sl_status_is_ok(sl_string_builder_view_with_nul(&builder, &view));
}

static bool sl_sloppyrc_json_string_equals_literal(yyjson_val* value, SlStr expected)
{
    const char* text = NULL;
    size_t text_length = 0U;

    if (value == NULL || !yyjson_is_str(value)) {
        return false;
    }

    text = yyjson_get_str(value);
    text_length = yyjson_get_len(value);
    return sl_str_equal(sl_str_from_parts(text, text_length), expected);
}

static int sl_sloppyrc_reject_unknown_fields(yyjson_val* root)
{
    yyjson_obj_iter iter;
    yyjson_val* key = NULL;

    iter = yyjson_obj_iter_with(root);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL) {
        if (!sl_sloppyrc_json_string_equals_literal(key, SL_STR_LITERAL("entry")) &&
            !sl_sloppyrc_json_string_equals_literal(key, SL_STR_LITERAL("outDir")) &&
            !sl_sloppyrc_json_string_equals_literal(key, SL_STR_LITERAL("environment")))
        {
            sl_sloppyrc_write_cstr(stderr,
                                   "sloppy run: invalid sloppy.json: unsupported field; supported "
                                   "fields are entry, outDir, and environment\n");
            return 1;
        }
    }
    return 0;
}

static int sl_sloppyrc_read_required_string(yyjson_val* root, const char* field, char* buffer,
                                            size_t capacity, const char* missing_message,
                                            const char* invalid_message)
{
    yyjson_val* value = yyjson_obj_get(root, field);

    if (value == NULL) {
        sl_sloppyrc_write_cstr(stderr, missing_message);
        return 1;
    }

    if (!yyjson_is_str(value) || yyjson_get_len(value) == 0U) {
        sl_sloppyrc_write_cstr(stderr, invalid_message);
        return 1;
    }

    if (!sl_sloppyrc_copy_json_string(buffer, capacity, value)) {
        sl_sloppyrc_write_cstr(stderr, invalid_message);
        return 1;
    }
    return 0;
}

static int sl_sloppyrc_read_optional_string(yyjson_val* root, const char* field, char* buffer,
                                            size_t capacity, const char* default_value,
                                            const char* invalid_message)
{
    yyjson_val* value = yyjson_obj_get(root, field);

    if (value == NULL) {
        SlStringBuilder builder = {0};
        SlStr view = {0};
        if (buffer == NULL || default_value == NULL) {
            sl_sloppyrc_write_cstr(stderr, invalid_message);
            return 1;
        }
        if (!sl_status_is_ok(sl_string_builder_init_fixed(&builder, buffer, capacity)) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(&builder, default_value)) ||
            sl_string_builder_length(&builder) == 0U ||
            !sl_status_is_ok(sl_string_builder_view_with_nul(&builder, &view)))
        {
            sl_sloppyrc_write_cstr(stderr, invalid_message);
            return 1;
        }
        return 0;
    }

    if (!yyjson_is_str(value) || yyjson_get_len(value) == 0U ||
        !sl_sloppyrc_copy_json_string(buffer, capacity, value))
    {
        sl_sloppyrc_write_cstr(stderr, invalid_message);
        return 1;
    }
    return 0;
}

int sl_sloppyrc_load(SlSloppyRunConfig* out)
{
    unsigned char json_storage[SL_SLOPPYRC_MAX_BYTES];
    SlBytes json = {0};
    yyjson_doc* doc = NULL;
    yyjson_read_err error = {0};
    yyjson_val* root = NULL;
    int result = 1;

    if (out == NULL) {
        return 1;
    }
    *out = (SlSloppyRunConfig){0};

    if (sl_sloppyrc_read_file(SL_SLOPPYRC_FILE, json_storage, sizeof(json_storage), &json,
                              "sloppy run: project config not found: ",
                              "sloppy run: project config is empty or too large: ",
                              "sloppy run: project config read failed: ") != 0)
    {
        return 1;
    }

    doc = yyjson_read_opts((char*)json.ptr, json.length, 0U, NULL, &error);
    if (doc == NULL) {
        sl_sloppyrc_write_cstr(stderr, "sloppy run: invalid sloppy.json: malformed JSON\n");
        return 1;
    }

    root = yyjson_doc_get_root(doc);
    if (root == NULL || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        sl_sloppyrc_write_cstr(stderr, "sloppy run: invalid sloppy.json: root must be an object\n");
        return 1;
    }

    if (sl_sloppyrc_reject_unknown_fields(root) == 0 &&
        sl_sloppyrc_read_required_string(
            root, "entry", out->entry, sizeof(out->entry),
            "sloppy run: missing entry in sloppy.json\n",
            "sloppy run: invalid sloppy.json: entry must be a non-empty string\n") == 0 &&
        sl_sloppyrc_read_optional_string(
            root, "outDir", out->out_dir, sizeof(out->out_dir), SL_SLOPPYRC_DEFAULT_OUT_DIR,
            "sloppy run: invalid sloppy.json: outDir must be a string\n") == 0 &&
        sl_sloppyrc_read_optional_string(
            root, "environment", out->environment, sizeof(out->environment),
            SL_SLOPPYRC_DEFAULT_ENVIRONMENT,
            "sloppy run: invalid sloppy.json: environment must be a string\n") == 0)
    {
        result = 0;
    }

    yyjson_doc_free(doc);
    return result;
}
