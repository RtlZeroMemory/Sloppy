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
#include "sloppy/platform.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdio.h>
#include <string.h>
#include <yyjson.h>

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

static const char* sl_sloppyrc_command_name(const char* command_name)
{
    return command_name != NULL && command_name[0] != '\0' ? command_name : "sloppy run";
}

static void sl_sloppyrc_write_command_message(const char* command_name, const char* message)
{
    fprintf(stderr, "%s: %s", sl_sloppyrc_command_name(command_name), message);
}

static const char* sl_sloppyrc_current_platform_key(void)
{
#if SL_PLATFORM_WINDOWS && (defined(_M_X64) || defined(__x86_64__))
    return "windows-x64";
#elif SL_PLATFORM_LINUX && defined(__x86_64__)
    return "linux-x64";
#elif SL_PLATFORM_APPLE && defined(__x86_64__)
    return "macos-x64";
#elif SL_PLATFORM_APPLE && defined(__aarch64__)
    return "macos-arm64";
#else
    return "";
#endif
}

static void sl_sloppyrc_write_command_error_with_value(const char* command_name,
                                                       const char* message, const char* value)
{
    fprintf(stderr, "%s: %s", sl_sloppyrc_command_name(command_name), message);
    if (value != NULL) {
        sl_sloppyrc_write_cstr(stderr, value);
    }
    sl_sloppyrc_write_cstr(stderr, "\n");
}

static bool sl_sloppyrc_path_is_absolute(SlStr path)
{
    if (path.length == 0U) {
        return false;
    }
    if (path.ptr[0] == '/' || path.ptr[0] == '\\') {
        return true;
    }
    return path.length >= 2U &&
           ((path.ptr[0] >= 'A' && path.ptr[0] <= 'Z') ||
            (path.ptr[0] >= 'a' && path.ptr[0] <= 'z')) &&
           path.ptr[1] == ':';
}

static bool sl_sloppyrc_entry_path_is_safe(const char* path)
{
    SlStr text = {0};
    size_t start = 0U;
    size_t index = 0U;

    if (path == NULL || path[0] == '\0') {
        return false;
    }
    text = sl_str_from_cstr(path);
    if (sl_sloppyrc_path_is_absolute(text)) {
        return false;
    }

    for (index = 0U; index <= text.length; index += 1U) {
        if (index == text.length || text.ptr[index] == '/' || text.ptr[index] == '\\') {
            size_t length = index - start;
            if (length == 0U) {
                return false;
            }
            if (length == 2U && text.ptr[start] == '.' && text.ptr[start + 1U] == '.') {
                return false;
            }
            start = index + 1U;
        }
    }

    return true;
}

static bool sl_sloppyrc_migration_path_is_sql_glob(const char* path)
{
    SlStr text = {0};

    if (path == NULL) {
        return false;
    }
    text = sl_str_from_cstr(path);
    return sl_str_ends_with(text, SL_STR_LITERAL("/*.sql")) ||
           sl_str_ends_with(text, SL_STR_LITERAL("\\*.sql"));
}

static int sl_sloppyrc_read_file(const char* path, unsigned char* buffer, size_t capacity,
                                 SlBytes* out, const char* command_name)
{
    unsigned char read_storage[SL_SLOPPYRC_FILE_READ_ARENA_BYTES];
    SlArena arena = {0};
    SlArena output_arena = {0};
    SlOwnedBytes bytes = {0};
    SlOwnedBytes copied = {0};
    SlStatus status;

    if (path == NULL || buffer == NULL || out == NULL || capacity > SL_SLOPPYRC_MAX_BYTES) {
        return 1;
    }
    *out = sl_bytes_from_parts(NULL, 0U);

    status = sl_arena_init(&arena, read_storage, capacity + SL_SLOPPYRC_FILE_READ_SCRATCH_BYTES);
    if (!sl_status_is_ok(status)) {
        return 1;
    }

    status = sl_fs_read_file(&arena, sl_str_from_cstr(path), &bytes, NULL);
    if (sl_status_code(status) == SL_STATUS_OUT_OF_RANGE) {
        sl_sloppyrc_write_command_error_with_value(command_name,
                                                   "project config not found: ", path);
        return 1;
    }
    if (sl_status_code(status) == SL_STATUS_CAPACITY_EXCEEDED) {
        sl_sloppyrc_write_command_error_with_value(command_name,
                                                   "project config is empty or too large: ", path);
        return 1;
    }
    if (!sl_status_is_ok(status)) {
        sl_sloppyrc_write_command_error_with_value(command_name,
                                                   "project config read failed: ", path);
        return 1;
    }
    if (bytes.length == 0U || bytes.length > capacity) {
        sl_sloppyrc_write_command_error_with_value(command_name,
                                                   "project config is empty or too large: ", path);
        return 1;
    }

    status = sl_arena_init(&output_arena, buffer, capacity);
    if (!sl_status_is_ok(status)) {
        return 1;
    }
    status = sl_bytes_copy_to_arena(&output_arena, sl_owned_bytes_as_view(bytes), &copied);
    if (!sl_status_is_ok(status)) {
        sl_sloppyrc_write_command_error_with_value(command_name,
                                                   "project config is empty or too large: ", path);
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

static bool sl_sloppyrc_capability_supported(SlStr name)
{
    return sl_str_equal(name, SL_STR_LITERAL("fs")) || sl_str_equal(name, SL_STR_LITERAL("net")) ||
           sl_str_equal(name, SL_STR_LITERAL("os")) || sl_str_equal(name, SL_STR_LITERAL("time")) ||
           sl_str_equal(name, SL_STR_LITERAL("crypto")) ||
           sl_str_equal(name, SL_STR_LITERAL("codec")) ||
           sl_str_equal(name, SL_STR_LITERAL("workers")) ||
           sl_str_equal(name, SL_STR_LITERAL("ffi"));
}

static int sl_sloppyrc_reject_unknown_fields(yyjson_val* root, const char* command_name)
{
    yyjson_obj_iter iter;
    yyjson_val* key = NULL;

    iter = yyjson_obj_iter_with(root);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL) {
        if (!sl_sloppyrc_json_string_equals_literal(key, SL_STR_LITERAL("entry")) &&
            !sl_sloppyrc_json_string_equals_literal(key, SL_STR_LITERAL("outDir")) &&
            !sl_sloppyrc_json_string_equals_literal(key, SL_STR_LITERAL("environment")) &&
            !sl_sloppyrc_json_string_equals_literal(key, SL_STR_LITERAL("kind")) &&
            !sl_sloppyrc_json_string_equals_literal(key, SL_STR_LITERAL("capabilities")) &&
            !sl_sloppyrc_json_string_equals_literal(key, SL_STR_LITERAL("migrations")) &&
            !sl_sloppyrc_json_string_equals_literal(key, SL_STR_LITERAL("moduleInclude")) &&
            !sl_sloppyrc_json_string_equals_literal(key, SL_STR_LITERAL("assetInclude")) &&
            !sl_sloppyrc_json_string_equals_literal(key, SL_STR_LITERAL("ffiLibraries")))
        {
            sl_sloppyrc_write_command_message(
                command_name,
                "invalid sloppy.json: unsupported field; supported fields are entry, outDir, "
                "environment, kind, capabilities, migrations, moduleInclude, assetInclude, and "
                "ffiLibraries\n");
            return 1;
        }
    }
    return 0;
}

static int sl_sloppyrc_read_required_string(yyjson_val* root, const char* field, char* buffer,
                                            size_t capacity, const char* command_name,
                                            const char* missing_message,
                                            const char* invalid_message)
{
    yyjson_val* value = yyjson_obj_get(root, field);

    if (value == NULL) {
        sl_sloppyrc_write_command_message(command_name, missing_message);
        return 1;
    }

    if (!yyjson_is_str(value) || yyjson_get_len(value) == 0U) {
        sl_sloppyrc_write_command_message(command_name, invalid_message);
        return 1;
    }

    if (!sl_sloppyrc_copy_json_string(buffer, capacity, value)) {
        sl_sloppyrc_write_command_message(command_name, invalid_message);
        return 1;
    }
    return 0;
}

static int sl_sloppyrc_read_optional_string(yyjson_val* root, const char* field, char* buffer,
                                            size_t capacity, const char* default_value,
                                            const char* command_name, const char* invalid_message)
{
    yyjson_val* value = yyjson_obj_get(root, field);

    if (value == NULL) {
        SlStringBuilder builder = {0};
        SlStr view = {0};
        if (buffer == NULL || default_value == NULL) {
            sl_sloppyrc_write_command_message(command_name, invalid_message);
            return 1;
        }
        if (!sl_status_is_ok(sl_string_builder_init_fixed(&builder, buffer, capacity)) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(&builder, default_value)) ||
            sl_string_builder_length(&builder) == 0U ||
            !sl_status_is_ok(sl_string_builder_view_with_nul(&builder, &view)))
        {
            sl_sloppyrc_write_command_message(command_name, invalid_message);
            return 1;
        }
        return 0;
    }

    if (!yyjson_is_str(value) || yyjson_get_len(value) == 0U ||
        !sl_sloppyrc_copy_json_string(buffer, capacity, value))
    {
        sl_sloppyrc_write_command_message(command_name, invalid_message);
        return 1;
    }
    return 0;
}

static int sl_sloppyrc_read_capabilities(yyjson_val* root, SlSloppyRunConfig* out,
                                         const char* command_name)
{
    yyjson_val* capabilities = yyjson_obj_get(root, "capabilities");
    yyjson_obj_iter iter;
    yyjson_val* key = NULL;

    if (capabilities == NULL) {
        return 0;
    }
    if (!yyjson_is_obj(capabilities)) {
        sl_sloppyrc_write_command_message(
            command_name,
            "invalid sloppy.json: capabilities must be an object of boolean stdlib names\n");
        return 1;
    }

    iter = yyjson_obj_iter_with(capabilities);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL) {
        yyjson_val* value = yyjson_obj_iter_get_val(key);
        SlStr name = sl_str_from_parts(yyjson_get_str(key), yyjson_get_len(key));
        if (!sl_sloppyrc_capability_supported(name)) {
            sl_sloppyrc_write_command_message(
                command_name,
                "invalid sloppy.json: capabilities supports fs, net, os, time, crypto, codec, "
                "workers, and ffi\n");
            return 1;
        }
        if (!yyjson_is_bool(value)) {
            sl_sloppyrc_write_command_message(
                command_name, "invalid sloppy.json: capability values must be true or false\n");
            return 1;
        }
        if (!yyjson_get_bool(value)) {
            continue;
        }
        if (out->capability_count >= SL_SLOPPYRC_MAX_CAPABILITIES ||
            name.length >= SL_SLOPPYRC_CAPABILITY_MAX_BYTES)
        {
            sl_sloppyrc_write_command_message(command_name,
                                              "invalid sloppy.json: too many capabilities\n");
            return 1;
        }
        for (size_t index = 0U; index < name.length; index += 1U) {
            out->capabilities[out->capability_count][index] = name.ptr[index];
        }
        out->capabilities[out->capability_count][name.length] = '\0';
        out->capability_count += 1U;
    }

    return 0;
}

static int sl_sloppyrc_read_include_array(yyjson_val* root, const char* field,
                                          char includes[][SL_SLOPPYRC_PATH_MAX_BYTES],
                                          size_t* include_count, size_t include_capacity,
                                          const char* command_name)
{
    yyjson_val* include_array = yyjson_obj_get(root, field);
    yyjson_arr_iter iter;
    yyjson_val* item = NULL;

    if (include_array == NULL) {
        return 0;
    }
    if (!yyjson_is_arr(include_array)) {
        sl_sloppyrc_write_command_error_with_value(
            command_name, "invalid sloppy.json: include field must be an array: ", field);
        return 1;
    }

    yyjson_arr_iter_init(include_array, &iter);
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        if (*include_count >= include_capacity) {
            sl_sloppyrc_write_command_error_with_value(
                command_name, "invalid sloppy.json: too many include patterns in ", field);
            return 1;
        }
        if (!sl_sloppyrc_copy_json_string(includes[*include_count], SL_SLOPPYRC_PATH_MAX_BYTES,
                                          item) ||
            !sl_sloppyrc_entry_path_is_safe(includes[*include_count]))
        {
            sl_sloppyrc_write_command_error_with_value(
                command_name,
                "invalid sloppy.json: include patterns must be project-relative strings in ",
                field);
            return 1;
        }
        *include_count += 1U;
    }

    return 0;
}

static int sl_sloppyrc_read_ffi_libraries(yyjson_val* root, SlSloppyRunConfig* out,
                                          const char* command_name)
{
    yyjson_val* libraries = yyjson_obj_get(root, "ffiLibraries");
    yyjson_obj_iter iter;
    yyjson_val* key = NULL;

    if (libraries == NULL) {
        return 0;
    }
    if (!yyjson_is_obj(libraries)) {
        sl_sloppyrc_write_command_message(
            command_name,
            "invalid sloppy.json: ffiLibraries must be an object of library names to paths\n");
        return 1;
    }

    iter = yyjson_obj_iter_with(libraries);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL) {
        yyjson_val* value = yyjson_obj_iter_get_val(key);
        yyjson_val* path_value = value;
        SlStr name = sl_str_from_parts(yyjson_get_str(key), yyjson_get_len(key));

        if (value != NULL && yyjson_is_obj(value)) {
            const char* platform_key = sl_sloppyrc_current_platform_key();
            if (platform_key[0] == '\0') {
                sl_sloppyrc_write_command_message(command_name,
                                                  "invalid sloppy.json: ffiLibraries platform "
                                                  "mapping is unsupported on this platform\n");
                return 1;
            }
            path_value = yyjson_obj_get(value, platform_key);
            if (path_value == NULL) {
                sl_sloppyrc_write_command_message(command_name,
                                                  "invalid sloppy.json: ffiLibraries entry is "
                                                  "missing the current platform path\n");
                return 1;
            }
        }
        if (out->ffi_library_count >= SL_SLOPPYRC_MAX_FFI_LIBRARIES || name.length == 0U ||
            name.length >= SL_SLOPPYRC_FFI_LIBRARY_NAME_MAX_BYTES ||
            !sl_status_is_ok(sl_str_validate_no_nul(name)) || path_value == NULL ||
            !yyjson_is_str(path_value) || yyjson_get_len(path_value) == 0U)
        {
            sl_sloppyrc_write_command_message(command_name,
                                              "invalid sloppy.json: invalid ffiLibraries entry\n");
            return 1;
        }
        for (size_t index = 0U; index < name.length; index += 1U) {
            out->ffi_libraries[out->ffi_library_count].name[index] = name.ptr[index];
        }
        out->ffi_libraries[out->ffi_library_count].name[name.length] = '\0';
        if (!sl_sloppyrc_copy_json_string(out->ffi_libraries[out->ffi_library_count].path,
                                          sizeof(out->ffi_libraries[out->ffi_library_count].path),
                                          path_value) ||
            !sl_sloppyrc_entry_path_is_safe(out->ffi_libraries[out->ffi_library_count].path))
        {
            sl_sloppyrc_write_command_message(
                command_name,
                "invalid sloppy.json: ffiLibraries paths must be relative project paths\n");
            return 1;
        }
        out->ffi_library_count += 1U;
    }

    return 0;
}

static bool sl_sloppyrc_provider_supported(SlStr provider)
{
    return sl_str_equal(provider, SL_STR_LITERAL("sqlite")) ||
           sl_str_equal(provider, SL_STR_LITERAL("postgres")) ||
           sl_str_equal(provider, SL_STR_LITERAL("sqlserver"));
}

static int sl_sloppyrc_read_migrations(yyjson_val* root, SlSloppyRunConfig* out,
                                       const char* command_name)
{
    yyjson_val* migrations = yyjson_obj_get(root, "migrations");
    yyjson_obj_iter iter;
    yyjson_val* key = NULL;

    if (migrations == NULL) {
        return 0;
    }
    if (!yyjson_is_obj(migrations)) {
        sl_sloppyrc_write_command_message(
            command_name,
            "invalid sloppy.json: migrations must be an object of provider names to configs\n");
        return 1;
    }

    iter = yyjson_obj_iter_with(migrations);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL) {
        yyjson_val* value = yyjson_obj_iter_get_val(key);
        yyjson_val* provider_value = NULL;
        yyjson_val* path_value = NULL;
        SlStr name = sl_str_from_parts(yyjson_get_str(key), yyjson_get_len(key));
        SlSloppyMigrationConfig* migration = NULL;

        if (out->migration_count >= SL_SLOPPYRC_MAX_MIGRATIONS || name.length == 0U ||
            name.length >= SL_SLOPPYRC_MIGRATION_NAME_MAX_BYTES ||
            !sl_status_is_ok(sl_str_validate_no_nul(name)))
        {
            sl_sloppyrc_write_command_message(command_name,
                                              "invalid sloppy.json: invalid migrations entry\n");
            return 1;
        }
        if (value == NULL || !yyjson_is_obj(value)) {
            sl_sloppyrc_write_command_message(
                command_name, "invalid sloppy.json: each migrations entry must be an object\n");
            return 1;
        }
        provider_value = yyjson_obj_get(value, "provider");
        path_value = yyjson_obj_get(value, "path");
        if (provider_value == NULL || !yyjson_is_str(provider_value) ||
            yyjson_get_len(provider_value) == 0U || path_value == NULL ||
            !yyjson_is_str(path_value) || yyjson_get_len(path_value) == 0U)
        {
            sl_sloppyrc_write_command_message(
                command_name,
                "invalid sloppy.json: migrations entries require provider and path strings\n");
            return 1;
        }

        migration = &out->migrations[out->migration_count];
        for (size_t index = 0U; index < name.length; index += 1U) {
            migration->name[index] = name.ptr[index];
        }
        migration->name[name.length] = '\0';
        if (!sl_sloppyrc_copy_json_string(migration->provider, sizeof(migration->provider),
                                          provider_value) ||
            !sl_sloppyrc_provider_supported(sl_str_from_cstr(migration->provider)) ||
            !sl_sloppyrc_copy_json_string(migration->path, sizeof(migration->path), path_value) ||
            !sl_sloppyrc_entry_path_is_safe(migration->path) ||
            !sl_sloppyrc_migration_path_is_sql_glob(migration->path))
        {
            sl_sloppyrc_write_command_message(
                command_name,
                "invalid sloppy.json: migrations provider must be sqlite, postgres, or sqlserver "
                "and path must be a project-relative directory glob ending in *.sql\n");
            return 1;
        }
        out->migration_count += 1U;
    }

    return 0;
}

int sl_sloppyrc_load_path_for_command(SlSloppyRunConfig* out, const char* path,
                                      const char* command_name)
{
    unsigned char json_storage[SL_SLOPPYRC_MAX_BYTES];
    SlBytes json = {0};
    yyjson_doc* doc = NULL;
    yyjson_read_err error = {0};
    yyjson_val* root = NULL;
    int result = 1;

    if (out == NULL || path == NULL || path[0] == '\0') {
        return 1;
    }
    *out = (SlSloppyRunConfig){0};

    if (sl_sloppyrc_read_file(path, json_storage, sizeof(json_storage), &json, command_name) != 0) {
        return 1;
    }

    doc = yyjson_read_opts((char*)json.ptr, json.length, 0U, NULL, &error);
    if (doc == NULL) {
        sl_sloppyrc_write_command_message(command_name, "invalid sloppy.json: malformed JSON\n");
        return 1;
    }

    root = yyjson_doc_get_root(doc);
    if (root == NULL || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        sl_sloppyrc_write_command_message(command_name,
                                          "invalid sloppy.json: root must be an object\n");
        return 1;
    }

    if (sl_sloppyrc_reject_unknown_fields(root, command_name) == 0 &&
        sl_sloppyrc_read_required_string(
            root, "entry", out->entry, sizeof(out->entry), command_name,
            "missing entry in sloppy.json\n",
            "invalid sloppy.json: entry must be a non-empty string\n") == 0 &&
        sl_sloppyrc_read_optional_string(root, "outDir", out->out_dir, sizeof(out->out_dir),
                                         SL_SLOPPYRC_DEFAULT_OUT_DIR, command_name,
                                         "invalid sloppy.json: outDir must be a string\n") == 0 &&
        sl_sloppyrc_read_optional_string(
            root, "environment", out->environment, sizeof(out->environment),
            SL_SLOPPYRC_DEFAULT_ENVIRONMENT, command_name,
            "invalid sloppy.json: environment must be a string\n") == 0 &&
        sl_sloppyrc_read_optional_string(
            root, "kind", out->kind, sizeof(out->kind), "web", command_name,
            "invalid sloppy.json: kind must be web or program\n") == 0 &&
        sl_sloppyrc_read_capabilities(root, out, command_name) == 0 &&
        sl_sloppyrc_read_include_array(root, "moduleInclude", out->module_includes,
                                       &out->module_include_count, SL_SLOPPYRC_MAX_MODULE_INCLUDES,
                                       command_name) == 0 &&
        sl_sloppyrc_read_include_array(root, "assetInclude", out->asset_includes,
                                       &out->asset_include_count, SL_SLOPPYRC_MAX_ASSET_INCLUDES,
                                       command_name) == 0 &&
        sl_sloppyrc_read_migrations(root, out, command_name) == 0 &&
        sl_sloppyrc_read_ffi_libraries(root, out, command_name) == 0)
    {
        if (strcmp(out->kind, "web") != 0 && strcmp(out->kind, "program") != 0) {
            sl_sloppyrc_write_command_message(command_name,
                                              "invalid sloppy.json: kind must be web or program\n");
        }
        else if (!sl_sloppyrc_entry_path_is_safe(out->entry)) {
            sl_sloppyrc_write_command_message(
                command_name,
                "invalid sloppy.json: entry must be a relative path inside the project root\n");
        }
        else {
            result = 0;
        }
    }

    yyjson_doc_free(doc);
    return result;
}

int sl_sloppyrc_load_for_command(SlSloppyRunConfig* out, const char* command_name)
{
    return sl_sloppyrc_load_path_for_command(out, SL_SLOPPYRC_FILE, command_name);
}

int sl_sloppyrc_load(SlSloppyRunConfig* out)
{
    return sl_sloppyrc_load_for_command(out, "sloppy run");
}
