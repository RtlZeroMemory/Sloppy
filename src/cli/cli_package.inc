/*
 * src/cli/cli_package.inc
 *
 * Private implementation of `sloppy package`.
 * Included only by src/main.c to preserve existing static helper visibility.
 */
static int sl_package_copy_artifact_leaf(const char* source_dir, const char* target_dir,
                                         SlCliSpan leaf)
{
    char source_path[SL_RUN_PATH_MAX_BYTES];
    char target_path[SL_RUN_PATH_MAX_BYTES];
    SlStatus status;

    if (source_dir == NULL || target_dir == NULL || leaf.ptr == NULL || leaf.length == 0U) {
        return 1;
    }
    if (!sl_run_join_path(source_path, sizeof(source_path), source_dir, leaf) ||
        !sl_run_join_path(target_path, sizeof(target_path), target_dir, leaf))
    {
        sl_cli_write_cstr(stderr, "sloppy package: artifact path is too long\n");
        return 1;
    }

    status =
        sl_fs_copy_file(sl_str_from_cstr(source_path), sl_str_from_cstr(target_path), true, NULL);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_error_with_value("sloppy package: failed to copy artifact: ", source_path,
                                      "\n");
        return 1;
    }

    return 0;
}

static bool sl_package_append_artifact_manifest_path(SlStringBuilder* builder,
                                                     const char* artifact_dir_name, SlCliSpan leaf)
{
    char path_storage[SL_RUN_PATH_MAX_BYTES];
    SlStringBuilder path_builder = {0};
    SlStr path = {0};
    size_t index = 0U;

    if (builder == NULL || artifact_dir_name == NULL || artifact_dir_name[0] == '\0' ||
        leaf.ptr == NULL || leaf.length == 0U ||
        !sl_status_is_ok(
            sl_string_builder_init_fixed(&path_builder, path_storage, sizeof(path_storage))) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&path_builder, artifact_dir_name)) ||
        !sl_status_is_ok(sl_string_builder_append_char(&path_builder, '/')))
    {
        return false;
    }

    for (index = 0U; index < leaf.length; index += 1U) {
        char ch = leaf.ptr[index];
        if (ch == '\\') {
            ch = '/';
        }
        if (!sl_status_is_ok(sl_string_builder_append_char(&path_builder, ch))) {
            return false;
        }
    }

    if (!sl_status_is_ok(sl_string_builder_view_with_nul(&path_builder, &path))) {
        return false;
    }
    return sl_cli_json_builder_append_string(builder, sl_cli_span_from_str(path));
}

static SlCliSpan sl_package_path_leaf(const char* path)
{
    const char* start = path;
    const char* cursor = path;

    if (path == NULL) {
        return (SlCliSpan){0};
    }
    while (*cursor != '\0') {
        if (*cursor == '/' || *cursor == '\\') {
            start = cursor + 1;
        }
        cursor += 1;
    }
    return (SlCliSpan){start, (size_t)(cursor - start)};
}

static bool sl_package_source_root_from_source(char* buffer, size_t capacity,
                                               const char* source_path)
{
    const char* cursor = source_path;
    const char* last_separator = NULL;
    const char* end = NULL;
    size_t separator_offset = 0U;
    SlStringBuilder builder = {0};
    SlStr view = {0};

    if (buffer == NULL || capacity == 0U || source_path == NULL || source_path[0] == '\0') {
        return false;
    }
    while (*cursor != '\0') {
        if (*cursor == '/' || *cursor == '\\') {
            last_separator = cursor;
        }
        cursor += 1;
    }
    if (last_separator == NULL) {
        return sl_cli_copy_cstr(buffer, capacity, ".");
    }
    if (!sl_status_is_ok(sl_string_builder_init_fixed(&builder, buffer, capacity))) {
        return false;
    }
    separator_offset = (size_t)(last_separator - source_path);
    end = last_separator;
    if (separator_offset == 0U || (separator_offset == 2U && source_path[1] == ':' &&
                                   (source_path[2] == '/' || source_path[2] == '\\')))
    {
        end = last_separator + 1;
    }
    for (cursor = source_path; cursor < end; cursor += 1) {
        if (!sl_status_is_ok(sl_string_builder_append_char(&builder, *cursor))) {
            return false;
        }
    }
    if (!sl_status_is_ok(sl_string_builder_view_with_nul(&builder, &view))) {
        return false;
    }
    return view.length != 0U;
}

static int sl_package_create_parent_directory(const char* path)
{
    char parent[SL_RUN_PATH_MAX_BYTES];
    SlStringBuilder builder = {0};
    SlStr parent_view = {0};
    size_t index = 0U;
    size_t last_separator = 0U;
    SlStatus status;

    if (path == NULL || path[0] == '\0') {
        return 1;
    }
    while (path[index] != '\0') {
        if (path[index] == '/' || path[index] == '\\') {
            last_separator = index;
        }
        index += 1U;
    }
    if (last_separator == 0U || last_separator >= sizeof(parent)) {
        return 1;
    }
    status = sl_string_builder_init_fixed(&builder, parent, sizeof(parent));
    if (!sl_status_is_ok(status)) {
        return 1;
    }
    status = sl_string_builder_append_str(&builder, sl_str_from_parts(path, last_separator));
    if (!sl_status_is_ok(status)) {
        return 1;
    }
    status = sl_string_builder_view_with_nul(&builder, &parent_view);
    if (!sl_status_is_ok(status) || parent_view.ptr != parent) {
        return 1;
    }
    status = sl_fs_create_directory(sl_str_from_cstr(parent), true, NULL);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_error_with_value("sloppy package: failed to create asset directory: ", parent,
                                      "\n");
        return 1;
    }
    return 0;
}

static const SlSloppyFfiLibraryConfig*
sl_package_find_ffi_library_config(const SlSloppyRunConfig* config, SlStr name)
{
    if (config == NULL) {
        return NULL;
    }
    for (size_t index = 0U; index < config->ffi_library_count; index += 1U) {
        if (sl_str_equal(name, sl_str_from_cstr(config->ffi_libraries[index].name))) {
            return &config->ffi_libraries[index];
        }
    }
    return NULL;
}

static bool sl_package_native_library_source_path(char* buffer, size_t capacity,
                                                  const char* config_dir,
                                                  const SlSloppyFfiLibraryConfig* library)
{
    if (buffer == NULL || capacity == 0U || library == NULL || library->path[0] == '\0') {
        return false;
    }
    if (config_dir == NULL || config_dir[0] == '\0') {
        return sl_cli_copy_cstr(buffer, capacity, library->path);
    }
    return sl_run_join_path(buffer, capacity, config_dir, sl_cli_span_cstr(library->path));
}

static bool sl_package_migration_pattern_dir(const char* pattern, char* buffer, size_t capacity)
{
    SlStringBuilder builder = {0};
    SlStr view = {0};
    SlStr path = sl_str_from_cstr(pattern);
    size_t dir_length = 0U;

    if (pattern == NULL || buffer == NULL || capacity == 0U || path.length <= 6U) {
        return false;
    }
    if (sl_str_ends_with(path, SL_STR_LITERAL("/*.sql"))) {
        dir_length = path.length - 6U;
    }
    else if (sl_str_ends_with(path, SL_STR_LITERAL("\\*.sql"))) {
        dir_length = path.length - 6U;
    }
    else {
        return false;
    }
    if (dir_length == 0U || dir_length >= capacity ||
        !sl_status_is_ok(sl_string_builder_init_fixed(&builder, buffer, capacity)) ||
        !sl_status_is_ok(
            sl_string_builder_append_str(&builder, sl_str_from_parts(path.ptr, dir_length))) ||
        !sl_status_is_ok(sl_string_builder_view_with_nul(&builder, &view)))
    {
        return false;
    }
    return view.ptr == buffer;
}

static bool sl_package_migration_entry_is_sql_file(const SlFsDirectoryEntry* entry)
{
    return entry != NULL && entry->kind == SL_FS_NODE_FILE &&
           sl_str_ends_with(sl_owned_str_as_view(entry->name), SL_STR_LITERAL(".sql"));
}

static int sl_package_copy_migrations(const char* package_dir, const char* config_dir,
                                      const SlSloppyRunConfig* project_config)
{
    unsigned char arena_storage[65536U];
    SlArena arena = {0};
    SlFsDirectoryList list = {0};
    SlStatus status;

    if (project_config == NULL || project_config->migration_count == 0U) {
        return 0;
    }
    if (!sl_status_is_ok(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)))) {
        return 1;
    }
    for (size_t migration_index = 0U; migration_index < project_config->migration_count;
         migration_index += 1U)
    {
        const SlSloppyMigrationConfig* migration = &project_config->migrations[migration_index];
        char relative_dir[SL_RUN_PATH_MAX_BYTES];
        char source_dir[SL_RUN_PATH_MAX_BYTES];
        char target_dir[SL_RUN_PATH_MAX_BYTES];
        size_t copied = 0U;

        sl_arena_reset(&arena);
        if (!sl_package_migration_pattern_dir(migration->path, relative_dir,
                                              sizeof(relative_dir)) ||
            !sl_run_join_path(source_dir, sizeof(source_dir),
                              config_dir == NULL || config_dir[0] == '\0' ? "." : config_dir,
                              sl_cli_span_cstr(relative_dir)) ||
            !sl_run_join_path(target_dir, sizeof(target_dir), package_dir,
                              sl_cli_span_cstr(relative_dir)))
        {
            sl_cli_write_cstr(stderr,
                              "sloppy package: migration path is too long or unsupported\n");
            return 1;
        }
        status = sl_fs_create_directory(sl_str_from_cstr(target_dir), true, NULL);
        if (!sl_status_is_ok(status)) {
            sl_cli_write_error_with_value(
                "sloppy package: failed to create migration directory: ", target_dir, "\n");
            return 1;
        }
        status = sl_fs_list_directory(&arena, sl_str_from_cstr(source_dir), &list, NULL);
        if (!sl_status_is_ok(status)) {
            sl_cli_write_error_with_value(
                "sloppy package: failed to read migration directory: ", source_dir, "\n");
            return 1;
        }
        for (size_t entry_index = 0U; entry_index < list.count; entry_index += 1U) {
            char source_path[SL_RUN_PATH_MAX_BYTES];
            char target_path[SL_RUN_PATH_MAX_BYTES];
            SlCliSpan file_name = {0};
            if (!sl_package_migration_entry_is_sql_file(&list.entries[entry_index])) {
                continue;
            }
            file_name = sl_cli_span_from_str(sl_owned_str_as_view(list.entries[entry_index].name));
            if (!sl_run_join_path(source_path, sizeof(source_path), source_dir, file_name) ||
                !sl_run_join_path(target_path, sizeof(target_path), target_dir, file_name))
            {
                sl_cli_write_cstr(stderr, "sloppy package: migration file path is too long\n");
                return 1;
            }
            status = sl_fs_copy_file(sl_str_from_cstr(source_path), sl_str_from_cstr(target_path),
                                     true, NULL);
            if (!sl_status_is_ok(status)) {
                sl_cli_write_error_with_value(
                    "sloppy package: failed to copy migration: ", source_path, "\n");
                return 1;
            }
            copied += 1U;
        }
        if (copied == 0U) {
            sl_cli_write_error_with_value(
                "sloppy package: migration directory has no .sql files: ", source_dir, "\n");
            return 1;
        }
    }
    return 0;
}

static int sl_package_copy_native_libraries(const char* package_dir, const char* config_dir,
                                            const SlRunApp* app,
                                            const SlSloppyRunConfig* project_config)
{
    char native_dir[SL_RUN_PATH_MAX_BYTES];
    SlStatus status;

    if (app == NULL || app->plan.ffi_library_count == 0U) {
        return 0;
    }
    if (!sl_run_artifact_file_path(native_dir, sizeof(native_dir), package_dir, "artifacts/native"))
    {
        sl_cli_write_cstr(stderr, "sloppy package: native library path is too long\n");
        return 1;
    }
    status = sl_fs_create_directory(sl_str_from_cstr(native_dir), true, NULL);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_error_with_value(
            "sloppy package: failed to create native directory: ", native_dir, "\n");
        return 1;
    }
    for (size_t index = 0U; index < app->plan.ffi_library_count; index += 1U) {
        const SlPlanFfiLibrary* plan_library = &app->plan.ffi_libraries[index];
        const SlSloppyFfiLibraryConfig* config =
            sl_package_find_ffi_library_config(project_config, plan_library->name);
        char source_path[SL_RUN_PATH_MAX_BYTES];
        char target_path[SL_RUN_PATH_MAX_BYTES];
        SlCliSpan leaf = {0};

        if (config == NULL) {
            continue;
        }
        if (!sl_package_native_library_source_path(source_path, sizeof(source_path), config_dir,
                                                   config))
        {
            sl_cli_write_cstr(stderr, "sloppy package: native library source path is too long\n");
            return 1;
        }
        leaf = sl_package_path_leaf(config->path);
        if (sl_cli_span_empty(leaf) ||
            !sl_run_join_path(target_path, sizeof(target_path), native_dir, leaf))
        {
            sl_cli_write_cstr(stderr, "sloppy package: native library target path is too long\n");
            return 1;
        }
        status = sl_fs_copy_file(sl_str_from_cstr(source_path), sl_str_from_cstr(target_path), true,
                                 NULL);
        if (!sl_status_is_ok(status)) {
            sl_cli_write_error_with_value(
                "sloppy package: failed to copy native library: ", source_path, "\n");
            return 1;
        }
    }
    return 0;
}

static bool sl_package_append_native_libraries_manifest(SlStringBuilder* builder,
                                                        const char* artifact_dir_name,
                                                        const char* config_dir, const SlRunApp* app,
                                                        const SlSloppyRunConfig* project_config)
{
    if (builder == NULL || app == NULL || app->plan.ffi_library_count == 0U) {
        return true;
    }
    size_t mapped_count = 0U;
    size_t emitted_count = 0U;
    for (size_t index = 0U; index < app->plan.ffi_library_count; index += 1U) {
        if (sl_package_find_ffi_library_config(project_config,
                                               app->plan.ffi_libraries[index].name) != NULL)
        {
            mapped_count += 1U;
        }
    }
    if (mapped_count == 0U) {
        return true;
    }
    if (!sl_status_is_ok(
            sl_string_builder_append_cstr(builder, ",\n  \"native\": {\n    \"libraries\": [")))
    {
        return false;
    }
    for (size_t index = 0U; index < app->plan.ffi_library_count; index += 1U) {
        const SlPlanFfiLibrary* plan_library = &app->plan.ffi_libraries[index];
        const SlSloppyFfiLibraryConfig* config =
            sl_package_find_ffi_library_config(project_config, plan_library->name);
        SlCliSpan leaf = config == NULL ? (SlCliSpan){0} : sl_package_path_leaf(config->path);
        char native_path_storage[SL_RUN_PATH_MAX_BYTES];
        SlStringBuilder native_path_builder = {0};
        SlStr native_path = {0};
        char source_path[SL_RUN_PATH_MAX_BYTES];
        char sha256[sizeof("sha256:") + 64U];
        if (config == NULL) {
            continue;
        }
        if (sl_cli_span_empty(leaf) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(
                builder, emitted_count == 0U ? "\n      { \"id\": " : ",\n      { \"id\": ")) ||
            !sl_cli_json_builder_append_string(builder, sl_cli_span_from_str(plan_library->name)) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(builder, ", \"platform\": ")) ||
            !sl_cli_json_builder_append_string(builder,
                                               sl_cli_span_from_str(app->plan.target.platform)) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(builder, ", \"path\": ")) ||
            !sl_status_is_ok(sl_string_builder_init_fixed(&native_path_builder, native_path_storage,
                                                          sizeof(native_path_storage))) ||
            !sl_status_is_ok(
                sl_string_builder_append_cstr(&native_path_builder, artifact_dir_name)) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(&native_path_builder, "/native/")))
        {
            return false;
        }
        for (size_t leaf_index = 0U; leaf_index < leaf.length; leaf_index += 1U) {
            char ch = leaf.ptr[leaf_index];
            if (ch == '\\') {
                ch = '/';
            }
            if (!sl_status_is_ok(sl_string_builder_append_char(&native_path_builder, ch))) {
                return false;
            }
        }
        if (!sl_package_native_library_source_path(source_path, sizeof(source_path), config_dir,
                                                   config) ||
            !sl_run_sha256_file_hex(source_path, sha256) ||
            !sl_status_is_ok(sl_string_builder_view_with_nul(&native_path_builder, &native_path)) ||
            !sl_cli_json_builder_append_string(builder, sl_cli_span_from_str(native_path)) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(builder, ", \"sha256\": ")) ||
            !sl_cli_json_builder_append_string(builder, sl_cli_span_cstr(sha256)) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(builder, " }")))
        {
            return false;
        }
        emitted_count += 1U;
    }
    return sl_status_is_ok(sl_string_builder_append_cstr(builder, "\n    ]\n  }"));
}

static bool sl_package_append_migration_manifest_path(SlStringBuilder* builder,
                                                      const char* relative_dir, SlStr file_name)
{
    char path_storage[SL_RUN_PATH_MAX_BYTES];
    SlStringBuilder path_builder = {0};
    SlStr path = {0};

    if (builder == NULL || relative_dir == NULL || relative_dir[0] == '\0' ||
        !sl_status_is_ok(
            sl_string_builder_init_fixed(&path_builder, path_storage, sizeof(path_storage))))
    {
        return false;
    }
    for (size_t index = 0U; relative_dir[index] != '\0'; index += 1U) {
        char ch = relative_dir[index];
        if (ch == '\\') {
            ch = '/';
        }
        if (!sl_status_is_ok(sl_string_builder_append_char(&path_builder, ch))) {
            return false;
        }
    }
    if (!sl_status_is_ok(sl_string_builder_append_char(&path_builder, '/'))) {
        return false;
    }
    for (size_t index = 0U; index < file_name.length; index += 1U) {
        char ch = file_name.ptr[index];
        if (ch == '\\') {
            ch = '/';
        }
        if (!sl_status_is_ok(sl_string_builder_append_char(&path_builder, ch))) {
            return false;
        }
    }
    if (!sl_status_is_ok(sl_string_builder_view_with_nul(&path_builder, &path))) {
        return false;
    }
    return sl_cli_json_builder_append_string(builder, sl_cli_span_from_str(path));
}

static bool sl_package_append_migrations_manifest(SlStringBuilder* builder, const char* config_dir,
                                                  const SlSloppyRunConfig* project_config)
{
    unsigned char arena_storage[65536U];
    SlArena arena = {0};
    SlFsDirectoryList list = {0};
    size_t emitted_migration = 0U;

    if (builder == NULL || project_config == NULL || project_config->migration_count == 0U) {
        return true;
    }
    if (!sl_status_is_ok(sl_arena_init(&arena, arena_storage, sizeof(arena_storage))) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(builder, ",\n  \"migrations\": [")))
    {
        return false;
    }

    for (size_t migration_index = 0U; migration_index < project_config->migration_count;
         migration_index += 1U)
    {
        const SlSloppyMigrationConfig* migration = &project_config->migrations[migration_index];
        char relative_dir[SL_RUN_PATH_MAX_BYTES];
        char source_dir[SL_RUN_PATH_MAX_BYTES];
        size_t emitted_file = 0U;

        sl_arena_reset(&arena);
        if (!sl_package_migration_pattern_dir(migration->path, relative_dir,
                                              sizeof(relative_dir)) ||
            !sl_run_join_path(source_dir, sizeof(source_dir),
                              config_dir == NULL || config_dir[0] == '\0' ? "." : config_dir,
                              sl_cli_span_cstr(relative_dir)) ||
            !sl_status_is_ok(
                sl_fs_list_directory(&arena, sl_str_from_cstr(source_dir), &list, NULL)) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(
                builder,
                emitted_migration == 0U ? "\n    { \"provider\": " : ",\n    { \"provider\": ")) ||
            !sl_cli_json_builder_append_string(builder, sl_cli_span_cstr(migration->name)) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(builder, ", \"providerKind\": ")) ||
            !sl_cli_json_builder_append_string(builder, sl_cli_span_cstr(migration->provider)) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(builder, ", \"path\": ")) ||
            !sl_cli_json_builder_append_string(builder, sl_cli_span_cstr(migration->path)) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(builder, ", \"files\": [")))
        {
            return false;
        }

        for (size_t entry_index = 0U; entry_index < list.count; entry_index += 1U) {
            char source_path[SL_RUN_PATH_MAX_BYTES];
            char sha256[sizeof("sha256:") + 64U];
            SlCliSpan file_name = {0};
            if (!sl_package_migration_entry_is_sql_file(&list.entries[entry_index])) {
                continue;
            }
            file_name = sl_cli_span_from_str(sl_owned_str_as_view(list.entries[entry_index].name));
            if (!sl_run_join_path(source_path, sizeof(source_path), source_dir, file_name) ||
                !sl_run_sha256_file_hex(source_path, sha256) ||
                !sl_status_is_ok(sl_string_builder_append_cstr(
                    builder,
                    emitted_file == 0U ? "\n      { \"path\": " : ",\n      { \"path\": ")) ||
                !sl_package_append_migration_manifest_path(
                    builder, relative_dir, sl_owned_str_as_view(list.entries[entry_index].name)) ||
                !sl_status_is_ok(sl_string_builder_append_cstr(builder, ", \"sha256\": ")) ||
                !sl_cli_json_builder_append_string(builder, sl_cli_span_cstr(sha256)) ||
                !sl_status_is_ok(sl_string_builder_append_cstr(builder, " }")))
            {
                return false;
            }
            emitted_file += 1U;
        }
        if (emitted_file == 0U ||
            !sl_status_is_ok(sl_string_builder_append_cstr(builder, "\n    ] }")))
        {
            return false;
        }
        emitted_migration += 1U;
    }

    return sl_status_is_ok(sl_string_builder_append_cstr(builder, "\n  ]"));
}

static int sl_package_write_manifest(const char* package_dir, const char* artifact_dir_name,
                                     const char* config_dir, const SlRunApp* app,
                                     const SlSloppyRunConfig* project_config,
                                     bool has_dependency_graph, bool has_assets, bool has_openapi)
{
    char manifest_path[SL_RUN_PATH_MAX_BYTES];
    unsigned char manifest_storage[SL_PACKAGE_MANIFEST_BYTES];
    SlStringBuilder builder = {0};
    SlStr manifest = {0};
    const char* app_kind = "web";
    SlStatus status;

    if (app != NULL && app->plan.kind == SL_PLAN_KIND_PROGRAM) {
        app_kind = "program";
    }

    if (package_dir == NULL || artifact_dir_name == NULL || app == NULL ||
        !sl_run_artifact_file_path(manifest_path, sizeof(manifest_path), package_dir,
                                   "manifest.json"))
    {
        sl_cli_write_cstr(stderr, "sloppy package: manifest path is too long\n");
        return 1;
    }

    status =
        sl_string_builder_init_fixed(&builder, (char*)manifest_storage, sizeof(manifest_storage));
    if (!sl_status_is_ok(status) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&builder, "{\n"
                                                                 "  \"schema\": "
                                                                 "\"sloppy.app-package.v1\",\n"
                                                                 "  \"kind\": \"")) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&builder, app_kind)) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&builder,
                                                       "\",\n"
                                                       "  \"dependencyMode\": \"bundled\",\n"
                                                       "  \"entry\": "
                                                       "\"artifacts/app.plan.json\",\n"
                                                       "  \"artifacts\": {\n"
                                                       "    \"plan\": ")) ||
        !sl_package_append_artifact_manifest_path(&builder, artifact_dir_name,
                                                  sl_cli_span_cstr("app.plan.json")) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&builder, ",\n    \"bundle\": ")) ||
        !sl_package_append_artifact_manifest_path(&builder, artifact_dir_name,
                                                  sl_run_str_span(app->plan.bundle.path)) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&builder, ",\n    \"sourceMap\": ")) ||
        !sl_package_append_artifact_manifest_path(&builder, artifact_dir_name,
                                                  sl_run_str_span(app->plan.source_map.path)) ||
        (app->plan.has_route_dispatch_artifact &&
         (!sl_status_is_ok(sl_string_builder_append_cstr(&builder, ",\n    \"routeDispatch\": ")) ||
          !sl_package_append_artifact_manifest_path(
              &builder, artifact_dir_name,
              sl_run_str_span(app->plan.route_dispatch_artifact.path)))) ||
        (has_dependency_graph &&
         (!sl_status_is_ok(
              sl_string_builder_append_cstr(&builder, ",\n    \"dependencyGraph\": ")) ||
          !sl_package_append_artifact_manifest_path(&builder, artifact_dir_name,
                                                    sl_cli_span_cstr("deps.graph.json")))) ||
        (has_openapi &&
         (!sl_status_is_ok(sl_string_builder_append_cstr(&builder, ",\n    \"openapi\": ")) ||
          !sl_package_append_artifact_manifest_path(&builder, artifact_dir_name,
                                                    sl_cli_span_cstr("openapi.json")))) ||
        (has_assets &&
         (!sl_status_is_ok(sl_string_builder_append_cstr(&builder, ",\n    \"assets\": ")) ||
          !sl_package_append_artifact_manifest_path(&builder, artifact_dir_name,
                                                    sl_cli_span_cstr("assets")))) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&builder, "\n"
                                                                 "  }")) ||
        !sl_package_append_native_libraries_manifest(&builder, artifact_dir_name, config_dir, app,
                                                     project_config) ||
        !sl_package_append_migrations_manifest(&builder, config_dir, project_config) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&builder,
                                                       ",\n"
                                                       "  \"createdBy\": \"sloppy package\"\n"
                                                       "}\n")))
    {
        sl_cli_write_cstr(stderr, "sloppy package: failed to format manifest\n");
        return 1;
    }
    manifest = sl_string_builder_view(&builder);

    status = sl_fs_write_file(
        sl_str_from_cstr(manifest_path),
        sl_bytes_from_parts((const unsigned char*)manifest.ptr, manifest.length), false, NULL);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_error_with_value("sloppy package: failed to write manifest: ", manifest_path,
                                      "\n");
        return 1;
    }

    return 0;
}

static void sl_package_cleanup_partial(const char* package_dir)
{
    SlStatus status;

    if (package_dir == NULL || package_dir[0] == '\0') {
        return;
    }

    status = sl_fs_delete_directory(sl_str_from_cstr(package_dir), true, NULL);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_error_with_value(
            "sloppy package: warning: failed to clean partial package: ", package_dir, "\n");
    }
}

static int sl_package_prepare_clean_directory(const char* package_dir)
{
    SlStatus status;
    bool exists = false;

    if (package_dir == NULL || package_dir[0] == '\0') {
        return 1;
    }
    status = sl_fs_exists(sl_str_from_cstr(package_dir), &exists, NULL);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_error_with_value(
            "sloppy package: failed to inspect package directory: ", package_dir, "\n");
        return 1;
    }
    if (exists) {
        status = sl_fs_delete_directory(sl_str_from_cstr(package_dir), true, NULL);
        if (!sl_status_is_ok(status)) {
            sl_cli_write_error_with_value(
                "sloppy package: failed to clean package directory: ", package_dir, "\n");
            return 1;
        }
    }
    status = sl_fs_create_directory(sl_str_from_cstr(package_dir), true, NULL);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_error_with_value(
            "sloppy package: failed to create package directory: ", package_dir, "\n");
        return 1;
    }
    return 0;
}

static int sl_package_copy_dependency_assets(const char* dependency_graph_path,
                                             const char* asset_root_dir, const char* package_dir,
                                             bool* out_has_assets)
{
    static unsigned char graph_storage[SL_CLI_FILE_MAX_BYTES];
    SlBytes graph_bytes = {0};
    yyjson_doc* doc = NULL;
    yyjson_val* root = NULL;
    yyjson_val* assets = NULL;
    yyjson_val* asset = NULL;
    size_t index = 0U;
    size_t max = 0U;
    yyjson_read_err error = {0};
    char assets_dir[SL_RUN_PATH_MAX_BYTES];
    SlStatus status;

    if (out_has_assets != NULL) {
        *out_has_assets = false;
    }
    if (dependency_graph_path == NULL || asset_root_dir == NULL || package_dir == NULL ||
        out_has_assets == NULL || !sl_cli_path_is_file(dependency_graph_path))
    {
        return 0;
    }
    if (sl_read_file_with_messages(dependency_graph_path, graph_storage, sizeof(graph_storage),
                                   &graph_bytes, "sloppy package: missing dependency graph: ",
                                   "sloppy package: dependency graph is too large: ",
                                   "sloppy package: failed to read dependency graph: ") != 0)
    {
        return 1;
    }
    doc = yyjson_read_opts((char*)graph_bytes.ptr, graph_bytes.length, 0U, NULL, &error);
    if (doc == NULL) {
        sl_cli_write_error_with_value(
            "sloppy package: malformed dependency graph: ", dependency_graph_path, "\n");
        return 1;
    }
    root = yyjson_doc_get_root(doc);
    assets = yyjson_obj_get(root, "assets");
    if (assets == NULL) {
        yyjson_doc_free(doc);
        return 0;
    }
    if (!yyjson_is_arr(assets)) {
        yyjson_doc_free(doc);
        sl_cli_write_cstr(stderr,
                          "sloppy package: dependency graph assets metadata must be an array\n");
        return 1;
    }
    if (yyjson_arr_size(assets) == 0U) {
        yyjson_doc_free(doc);
        return 0;
    }
    if (!sl_run_artifact_file_path(assets_dir, sizeof(assets_dir), package_dir, "artifacts/assets"))
    {
        yyjson_doc_free(doc);
        sl_cli_write_cstr(stderr, "sloppy package: assets path is too long\n");
        return 1;
    }
    status = sl_fs_create_directory(sl_str_from_cstr(assets_dir), true, NULL);
    if (!sl_status_is_ok(status)) {
        yyjson_doc_free(doc);
        sl_cli_write_error_with_value(
            "sloppy package: failed to create assets directory: ", assets_dir, "\n");
        return 1;
    }

    yyjson_arr_foreach(assets, index, max, asset)
    {
        yyjson_val* path_value = yyjson_obj_get(asset, "path");
        const char* path = yyjson_is_str(path_value) ? yyjson_get_str(path_value) : NULL;
        size_t path_length = yyjson_is_str(path_value) ? yyjson_get_len(path_value) : 0U;
        SlCliSpan path_span = {path, path_length};
        char source_path[SL_RUN_PATH_MAX_BYTES];
        char target_path[SL_RUN_PATH_MAX_BYTES];

        if (path == NULL || path_length == 0U || !sl_run_relative_artifact_path_is_safe(path_span))
        {
            yyjson_doc_free(doc);
            sl_cli_write_cstr(stderr,
                              "sloppy package: dependency graph contains unsafe asset path\n");
            return 1;
        }
        if (!sl_run_join_path(source_path, sizeof(source_path), asset_root_dir, path_span) ||
            !sl_run_join_path(target_path, sizeof(target_path), assets_dir, path_span))
        {
            yyjson_doc_free(doc);
            sl_cli_write_cstr(stderr, "sloppy package: asset path is too long\n");
            return 1;
        }
        if (sl_package_create_parent_directory(target_path) != 0) {
            yyjson_doc_free(doc);
            return 1;
        }
        status = sl_fs_copy_file(sl_str_from_cstr(source_path), sl_str_from_cstr(target_path), true,
                                 NULL);
        if (!sl_status_is_ok(status)) {
            yyjson_doc_free(doc);
            sl_cli_write_error_with_value("sloppy package: failed to copy asset: ", source_path,
                                          "\n");
            return 1;
        }
        *out_has_assets = true;
    }
    yyjson_doc_free(doc);
    return 0;
}

static int sl_package_copy_artifacts(const char* out_dir, const char* package_dir,
                                     const SlRunApp* app, const char* config_dir,
                                     const SlSloppyRunConfig* project_config,
                                     const char* asset_root_dir)
{
    char artifacts_dir[SL_RUN_PATH_MAX_BYTES];
    char dependency_graph_path[SL_RUN_PATH_MAX_BYTES];
    char openapi_path[SL_RUN_PATH_MAX_BYTES];
    bool has_dependency_graph = false;
    bool has_assets = false;
    bool has_openapi = false;
    SlStatus status;

    if (out_dir == NULL || package_dir == NULL || app == NULL) {
        return 1;
    }
    if (!sl_run_artifact_file_path(artifacts_dir, sizeof(artifacts_dir), package_dir, "artifacts"))
    {
        sl_cli_write_cstr(stderr, "sloppy package: artifacts path is too long\n");
        return 1;
    }

    if (sl_package_prepare_clean_directory(package_dir) != 0) {
        return 1;
    }
    status = sl_fs_create_directory(sl_str_from_cstr(artifacts_dir), true, NULL);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_error_with_value(
            "sloppy package: failed to create artifacts directory: ", artifacts_dir, "\n");
        return 1;
    }

    if (sl_package_copy_artifact_leaf(out_dir, artifacts_dir, sl_cli_span_cstr("app.plan.json")) !=
            0 ||
        sl_package_copy_artifact_leaf(out_dir, artifacts_dir,
                                      sl_run_str_span(app->plan.bundle.path)) != 0 ||
        sl_package_copy_artifact_leaf(out_dir, artifacts_dir,
                                      sl_run_str_span(app->plan.source_map.path)) != 0)
    {
        sl_package_cleanup_partial(package_dir);
        return 1;
    }

    if (app->plan.has_route_dispatch_artifact &&
        sl_package_copy_artifact_leaf(out_dir, artifacts_dir,
                                      sl_run_str_span(app->plan.route_dispatch_artifact.path)) != 0)
    {
        sl_package_cleanup_partial(package_dir);
        return 1;
    }

    if (sl_run_artifact_file_path(dependency_graph_path, sizeof(dependency_graph_path), out_dir,
                                  "deps.graph.json") &&
        sl_cli_path_is_file(dependency_graph_path))
    {
        if (sl_package_copy_artifact_leaf(out_dir, artifacts_dir,
                                          sl_cli_span_cstr("deps.graph.json")) != 0)
        {
            sl_package_cleanup_partial(package_dir);
            return 1;
        }
        has_dependency_graph = true;
        if (sl_package_copy_dependency_assets(dependency_graph_path, asset_root_dir, package_dir,
                                              &has_assets) != 0)
        {
            sl_package_cleanup_partial(package_dir);
            return 1;
        }
    }

    if (sl_cli_openapi_refresh_docs_artifact(out_dir, "sloppy package") != 0) {
        sl_package_cleanup_partial(package_dir);
        return 1;
    }

    if (app->plan.kind != SL_PLAN_KIND_PROGRAM &&
        sl_run_artifact_file_path(openapi_path, sizeof(openapi_path), out_dir, "openapi.json") &&
        sl_cli_path_is_file(openapi_path))
    {
        if (sl_package_copy_artifact_leaf(out_dir, artifacts_dir,
                                          sl_cli_span_cstr("openapi.json")) != 0)
        {
            sl_package_cleanup_partial(package_dir);
            return 1;
        }
        has_openapi = true;
    }

    if (sl_package_copy_native_libraries(package_dir, config_dir, app, project_config) != 0) {
        sl_package_cleanup_partial(package_dir);
        return 1;
    }
    if (sl_package_copy_migrations(package_dir, config_dir, project_config) != 0) {
        sl_package_cleanup_partial(package_dir);
        return 1;
    }

    if (sl_package_write_manifest(package_dir, "artifacts", config_dir, app, project_config,
                                  has_dependency_graph, has_assets, has_openapi) != 0)
    {
        sl_package_cleanup_partial(package_dir);
        return 1;
    }

    return 0;
}

static void sl_package_write_failure_report(const char* source_path, const char* out_dir,
                                            const char* package_dir, const char* message)
{
    unsigned char arena_storage[SL_CLI_DIAG_ARENA_BYTES];
    SlArena arena = {0};
    SlDiagBuilder builder = {0};
    SlDiag diag = {0};
    SlDiagReportContext context = {0};

    if (!sl_status_is_ok(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)))) {
        return;
    }
    if (!sl_status_is_ok(sl_diag_builder_init(
            &builder, &arena, SL_DIAG_SEVERITY_ERROR, SL_DIAG_PACKAGE_ARTIFACT_MISSING,
            sl_str_from_cstr(message != NULL ? message : "sloppy package failed"))))
    {
        return;
    }
    if (source_path != NULL) {
        if (!sl_status_is_ok(sl_diag_builder_add_hint(&builder, sl_str_from_cstr(source_path)))) {
            return;
        }
    }
    if (!sl_status_is_ok(sl_diag_builder_finish(&builder, &diag))) {
        return;
    }
    context.command = sl_str_from_cstr("package");
    context.runtime_version = sl_str_from_cstr(SL_VERSION_STRING);
    context.artifact_path = out_dir != NULL ? sl_str_from_cstr(out_dir) : sl_str_empty();
    context.package_path = package_dir != NULL ? sl_str_from_cstr(package_dir) : sl_str_empty();
    context.cause_message = sl_str_from_cstr(message != NULL ? message : "sloppy package failed");
    sl_breadcrumb_global_record(SL_DIAG_SUBSYSTEM_PACKAGE, SL_BREADCRUMB_EVENT_PACKAGE_REPORT,
                                SL_STATUS_INTERNAL, 0U, 0U, 0U, 0U, context.cause_message);
    sl_cli_write_diagnostic_report_file("package-diagnostic.json", &diag, &context,
                                        sl_breadcrumb_global_ring());
}

static int sl_cli_command_package(const SlCliOptions* options)
{
    SlSloppyRunConfig config = {0};
    SlRunApp* app = NULL;
    const char* source_path = NULL;
    const char* out_dir = NULL;
    const char* environment = NULL;
    const char* config_dir = NULL;
    const char* project_kind = NULL;
    const SlSloppyRunConfig* project_config = NULL;
    char package_dir[SL_RUN_PATH_MAX_BYTES] = {0};
    char asset_root_dir[SL_RUN_PATH_MAX_BYTES];
    int result = 0;

    if (options == NULL) {
        return 1;
    }
    app = sl_run_command_app();

    if (options->input_path == NULL) {
        if (options->out_explicit) {
            sl_cli_write_cstr(stderr, "sloppy package: --out applies to positional source input; "
                                      "project packaging uses sloppy.json outDir\n");
            return 1;
        }
        if (sl_sloppyrc_load_for_command(&config, "sloppy package") != 0) {
            return 1;
        }
        source_path = config.entry;
        out_dir = config.out_dir;
        environment = options->environment_explicit ? options->environment : config.environment;
        config_dir = ".";
        if (!sl_cli_copy_cstr(asset_root_dir, sizeof(asset_root_dir), config_dir)) {
            sl_cli_write_cstr(stderr, "sloppy package: asset root path is too long\n");
            return 1;
        }
        project_kind = config.kind;
        project_config = &config;
    }
    else {
        source_path = options->input_path;
        out_dir = options->out_explicit ? options->out_dir : SL_RUN_DEFAULT_SOURCE_OUT_DIR;
        environment =
            options->environment_explicit ? options->environment : SL_RUN_DEFAULT_ENVIRONMENT;
        if (!sl_package_source_root_from_source(asset_root_dir, sizeof(asset_root_dir),
                                                source_path))
        {
            sl_cli_write_cstr(stderr, "sloppy package: asset root path is too long\n");
            return 1;
        }
    }

    if (!sl_run_source_input_extension_supported(source_path)) {
        sl_cli_write_error_with_value("sloppy package: unsupported source input: ", source_path,
                                      "\n");
        sl_package_write_failure_report(source_path, out_dir, NULL, "unsupported source input");
        return 1;
    }

    if (sl_run_compile_source(source_path, out_dir, options, environment, "sloppy package",
                              config_dir, project_kind, project_config) != 0)
    {
        sl_package_write_failure_report(source_path, out_dir, NULL, "source compilation failed");
        return 1;
    }

    result = sl_run_load_artifacts(out_dir, "sloppy package", app, false);
    if (result == 0) {
        if (!sl_run_artifact_file_path(package_dir, sizeof(package_dir), out_dir, "package")) {
            sl_cli_write_cstr(stderr, "sloppy package: package path is too long\n");
            result = 1;
        }
        else {
            result = sl_package_copy_artifacts(out_dir, package_dir, app, config_dir,
                                               project_config, asset_root_dir);
        }
    }
    if (sl_run_shutdown_app(app) != 0 && result == 0) {
        result = 1;
    }
    if (result != 0) {
        sl_package_write_failure_report(source_path, out_dir, package_dir, "package write failed");
        return result;
    }

    if (options->format == SL_CLI_FORMAT_JSON) {
        printf("{\"packaged\":true,\"path\":");
        sl_cli_json_escape(stdout, sl_cli_span_cstr(package_dir));
        printf(",\"artifacts\":\"artifacts\"}\n");
    }
    else {
        printf("Created Sloppy app package: %s\n", package_dir);
    }
    return 0;
}
