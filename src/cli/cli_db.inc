/*
 * src/cli/cli_db.inc
 *
 * Private implementation of `sloppy db`.
 */
typedef struct SlCliDbMigrationConfig
{
    char provider[SL_SLOPPYRC_MIGRATION_NAME_MAX_BYTES];
    char provider_kind[SL_SLOPPYRC_MIGRATION_PROVIDER_MAX_BYTES];
    char path[SL_RUN_PATH_MAX_BYTES];
} SlCliDbMigrationConfig;

typedef struct SlCliDbMigrationFile
{
    char name[SL_RUN_PATH_MAX_BYTES];
    char path[SL_RUN_PATH_MAX_BYTES];
    char hash[sizeof("fnv1a32:") + 8U];
    bool applied;
    bool changed;
} SlCliDbMigrationFile;

static uint32_t sl_db_fnv1a32_bytes(SlBytes bytes)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0U; index < bytes.length; index += 1U) {
        hash ^= bytes.ptr[index];
        hash *= 16777619U;
    }
    return hash;
}

static bool sl_db_copy_span(char* buffer, size_t capacity, SlCliSpan span)
{
    SlStringBuilder builder = {0};
    SlStr view = {0};

    return buffer != NULL && capacity > 0U && span.ptr != NULL && span.length < capacity &&
           sl_status_is_ok(sl_string_builder_init_fixed(&builder, buffer, capacity)) &&
           sl_status_is_ok(sl_string_builder_append_str(&builder, sl_cli_span_str(span))) &&
           sl_status_is_ok(sl_string_builder_view_with_nul(&builder, &view)) && view.ptr == buffer;
}

static bool sl_db_format_hash(SlBytes bytes, char* buffer, size_t capacity)
{
    static const char hex[] = "0123456789abcdef";
    uint32_t hash = sl_db_fnv1a32_bytes(bytes);
    SlStringBuilder builder = {0};
    SlStr view = {0};

    if (buffer == NULL || capacity < sizeof("fnv1a32:00000000")) {
        return false;
    }
    if (!sl_status_is_ok(sl_string_builder_init_fixed(&builder, buffer, capacity)) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&builder, "fnv1a32:")))
    {
        return false;
    }
    for (int shift = 28; shift >= 0; shift -= 4) {
        if (!sl_status_is_ok(
                sl_string_builder_append_char(&builder, hex[(hash >> (uint32_t)shift) & 0x0FU])))
        {
            return false;
        }
    }
    return sl_status_is_ok(sl_string_builder_view_with_nul(&builder, &view)) && view.ptr == buffer;
}

static bool sl_db_provider_name_matches(SlCliSpan token, const char* requested)
{
    char normalized[SL_RUN_PATH_MAX_BYTES];
    SlStringBuilder builder = {0};
    SlStr view = {0};

    if (requested == NULL || requested[0] == '\0') {
        return false;
    }
    if (sl_cli_span_equal_cstr(token, requested)) {
        return true;
    }
    if (!sl_status_is_ok(sl_string_builder_init_fixed(&builder, normalized, sizeof(normalized))) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&builder, "data.")) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&builder, requested)) ||
        !sl_status_is_ok(sl_string_builder_view_with_nul(&builder, &view)))
    {
        return false;
    }
    return sl_cli_span_equal_cstr(token, normalized);
}

static const SlCliProvider* sl_db_find_provider(const SlCliMetadata* metadata,
                                                const char* provider_name)
{
    if (metadata == NULL || provider_name == NULL) {
        return NULL;
    }
    for (size_t index = 0U; index < metadata->provider_count; index += 1U) {
        if (sl_db_provider_name_matches(metadata->providers[index].token, provider_name)) {
            return &metadata->providers[index];
        }
    }
    return NULL;
}

static bool sl_db_resolve_target(const SlCliOptions* options, char* plan_path, size_t plan_capacity,
                                 char* base_path, size_t base_capacity, bool* out_package)
{
    const char* target = options->db_target_path == NULL ? ".sloppy" : options->db_target_path;
    if (sl_cli_directory_has_package_manifest(target)) {
        if (!sl_cli_join_leaf(plan_path, plan_capacity, target, "artifacts/app.plan.json") ||
            !sl_cli_copy_cstr(base_path, base_capacity, target))
        {
            return false;
        }
        *out_package = true;
        return true;
    }
    if (sl_cli_directory_has_plan(target)) {
        if (!sl_cli_join_leaf(plan_path, plan_capacity, target, "app.plan.json") ||
            !sl_cli_copy_cstr(base_path, base_capacity, "."))
        {
            return false;
        }
        *out_package = false;
        return true;
    }
    return false;
}

static bool sl_db_migration_path_dir(const char* pattern, char* buffer, size_t capacity)
{
    return sl_package_migration_pattern_dir(pattern, buffer, capacity);
}

static bool sl_db_manifest_migration_config(const char* manifest_path, const char* provider_name,
                                            SlCliDbMigrationConfig* out)
{
    unsigned char json_storage[SL_CLI_FILE_MAX_BYTES];
    SlBytes json = {0};
    yyjson_doc* doc = NULL;
    yyjson_val* root = NULL;
    yyjson_val* migrations = NULL;
    yyjson_arr_iter iter;
    yyjson_val* value = NULL;
    bool found = false;

    if (sl_cli_read_file(manifest_path, json_storage, sizeof(json_storage), &json) != 0) {
        return false;
    }
    doc = yyjson_read((char*)json.ptr, json.length, 0U);
    if (doc == NULL) {
        sl_cli_write_cstr(stderr, "sloppy db: package manifest is malformed JSON\n");
        return false;
    }
    root = yyjson_doc_get_root(doc);
    migrations = root == NULL ? NULL : yyjson_obj_get(root, "migrations");
    if (migrations == NULL || !yyjson_is_arr(migrations)) {
        yyjson_doc_free(doc);
        sl_cli_write_cstr(stderr, "sloppy db: package manifest has no migrations metadata\n");
        return false;
    }
    yyjson_arr_iter_init(migrations, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        SlCliSpan provider = sl_cli_json_span(value, "provider");
        SlCliSpan provider_kind = sl_cli_json_span(value, "providerKind");
        SlCliSpan path = sl_cli_json_span(value, "path");
        if (!sl_cli_span_equal_cstr(provider, provider_name)) {
            continue;
        }
        if (provider_kind.length == 0U || provider_kind.length >= sizeof(out->provider_kind) ||
            path.length == 0U || path.length >= sizeof(out->path))
        {
            yyjson_doc_free(doc);
            return false;
        }
        sl_cli_copy_cstr(out->provider, sizeof(out->provider), provider_name);
        if (!sl_db_copy_span(out->provider_kind, sizeof(out->provider_kind), provider_kind) ||
            !sl_db_copy_span(out->path, sizeof(out->path), path))
        {
            yyjson_doc_free(doc);
            return false;
        }
        found = true;
        break;
    }
    yyjson_doc_free(doc);
    return found;
}

static bool sl_db_project_migration_config(const char* provider_name, SlCliDbMigrationConfig* out)
{
    SlSloppyRunConfig config = {0};
    if (sl_sloppyrc_load_for_command(&config, "sloppy db") != 0) {
        return false;
    }
    for (size_t index = 0U; index < config.migration_count; index += 1U) {
        if (strcmp(config.migrations[index].name, provider_name) == 0) {
            sl_cli_copy_cstr(out->provider, sizeof(out->provider), config.migrations[index].name);
            sl_cli_copy_cstr(out->provider_kind, sizeof(out->provider_kind),
                             config.migrations[index].provider);
            sl_cli_copy_cstr(out->path, sizeof(out->path), config.migrations[index].path);
            return true;
        }
    }
    sl_cli_write_error_with_value("sloppy db: no migration config for provider: ", provider_name,
                                  "\n");
    return false;
}

static bool sl_db_load_migration_config(const SlCliOptions* options, const char* base_path,
                                        bool package_target, SlCliDbMigrationConfig* out)
{
    char manifest_path[SL_RUN_PATH_MAX_BYTES];
    if (package_target) {
        if (!sl_cli_join_leaf(manifest_path, sizeof(manifest_path), base_path, "manifest.json")) {
            sl_cli_write_cstr(stderr, "sloppy db: package manifest path is too long\n");
            return false;
        }
        if (!sl_db_manifest_migration_config(manifest_path, options->db_provider, out)) {
            sl_cli_write_error_with_value("sloppy db: no package migration config for provider: ",
                                          options->db_provider, "\n");
            return false;
        }
        return true;
    }
    return sl_db_project_migration_config(options->db_provider, out);
}

static int sl_db_read_migration_files(const char* base_path, const SlCliDbMigrationConfig* config,
                                      SlCliDbMigrationFile* files, size_t capacity,
                                      size_t* out_count)
{
    unsigned char arena_storage[65536U];
    SlArena arena = {0};
    SlFsDirectoryList list = {0};
    char relative_dir[SL_RUN_PATH_MAX_BYTES];
    char directory[SL_RUN_PATH_MAX_BYTES];
    SlStatus status;
    size_t count = 0U;

    if (out_count == NULL || files == NULL || config == NULL) {
        return 1;
    }
    *out_count = 0U;
    if (!sl_db_migration_path_dir(config->path, relative_dir, sizeof(relative_dir)) ||
        !sl_run_join_path(directory, sizeof(directory), base_path, sl_cli_span_cstr(relative_dir)))
    {
        sl_cli_write_cstr(stderr, "sloppy db: migration path is unsupported or too long\n");
        return 1;
    }
    if (!sl_status_is_ok(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)))) {
        return 1;
    }
    status = sl_fs_list_directory(&arena, sl_str_from_cstr(directory), &list, NULL);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_error_with_value("sloppy db: migration directory is missing: ", directory,
                                      "\n");
        return 1;
    }
    for (size_t entry_index = 0U; entry_index < list.count; entry_index += 1U) {
        unsigned char file_storage[SL_CLI_FILE_MAX_BYTES];
        SlBytes bytes = {0};
        SlCliDbMigrationFile file = {0};
        if (!sl_package_migration_entry_is_sql_file(&list.entries[entry_index])) {
            continue;
        }
        if (count >= capacity) {
            sl_cli_write_cstr(stderr, "sloppy db: too many migration files\n");
            return 1;
        }
        {
            SlStr entry_name = sl_owned_str_as_view(list.entries[entry_index].name);
            if (!sl_db_copy_span(file.name, sizeof(file.name), sl_cli_span_from_str(entry_name))) {
                return 1;
            }
        }
        if (!sl_run_join_path(file.path, sizeof(file.path), directory,
                              sl_cli_span_cstr(file.name)) ||
            sl_read_file_with_messages(
                file.path, file_storage, sizeof(file_storage), &bytes,
                "sloppy db: migration not found: ", "sloppy db: migration is empty or too large: ",
                "sloppy db: failed to read migration: ") != 0 ||
            !sl_db_format_hash(bytes, file.hash, sizeof(file.hash)))
        {
            return 1;
        }
        files[count] = file;
        count += 1U;
    }
    if (count == 0U) {
        sl_cli_write_error_with_value(
            "sloppy db: migration directory has no .sql files: ", directory, "\n");
        return 1;
    }
    for (size_t outer = 0U; outer < count; outer += 1U) {
        for (size_t inner = outer + 1U; inner < count; inner += 1U) {
            if (strcmp(files[outer].name, files[inner].name) > 0) {
                SlCliDbMigrationFile tmp = files[outer];
                files[outer] = files[inner];
                files[inner] = tmp;
            }
        }
    }
    *out_count = count;
    return 0;
}

static SlStatus sl_db_sqlite_exec_text(SlArena* arena, SlSqliteConnection* connection,
                                       const char* sql, SlDiag* diag)
{
    SlSqliteExecResult result = {0};
    return sl_sqlite_exec(arena, connection, sl_str_from_cstr(sql), NULL, 0U, &result, diag);
}

static int sl_db_load_applied(SlArena* arena, SlSqliteConnection* connection,
                              SlCliDbMigrationFile* files, size_t file_count)
{
    SlSqliteResult result = {0};
    SlStatus status;

    status = sl_db_sqlite_exec_text(
        arena, connection,
        "create table if not exists _sloppy_migrations (id integer primary key autoincrement, "
        "name text not null unique, hash text not null, appliedAt text not null)",
        NULL);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy db: failed to ensure migration metadata table\n");
        return 1;
    }
    status =
        sl_sqlite_query(arena, connection,
                        sl_str_from_cstr("select name, hash from _sloppy_migrations order by id"),
                        NULL, 0U, NULL, &result, NULL);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy db: failed to read migration metadata\n");
        return 1;
    }
    for (size_t row_index = 0U; row_index < result.row_count; row_index += 1U) {
        SlSqliteRow* row = &result.rows[row_index];
        if (result.column_count < 2U || row->values[0].kind != SL_SQLITE_VALUE_TEXT ||
            row->values[1].kind != SL_SQLITE_VALUE_TEXT)
        {
            continue;
        }
        for (size_t file_index = 0U; file_index < file_count; file_index += 1U) {
            if (sl_str_equal(row->values[0].value.text, sl_str_from_cstr(files[file_index].name))) {
                files[file_index].applied = true;
                files[file_index].changed = !sl_str_equal(row->values[1].value.text,
                                                          sl_str_from_cstr(files[file_index].hash));
            }
        }
    }
    return 0;
}

static int sl_db_apply_file(SlArena* arena, SlSqliteConnection* connection,
                            const SlCliDbMigrationFile* file)
{
    unsigned char file_storage[SL_CLI_FILE_MAX_BYTES];
    SlBytes bytes = {0};
    SlSqliteTransaction tx = {0};
    SlSqliteExecResult result = {0};
    SlSqliteParam params[2] = {0};
    SlStatus status;

    if (sl_read_file_with_messages(
            file->path, file_storage, sizeof(file_storage), &bytes,
            "sloppy db: migration not found: ", "sloppy db: migration is empty or too large: ",
            "sloppy db: failed to read migration: ") != 0)
    {
        return 1;
    }
    status = sl_sqlite_transaction_begin(arena, connection, &tx, NULL);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy db: failed to begin migration transaction\n");
        return 1;
    }
    status = sl_sqlite_transaction_exec(arena, &tx,
                                        sl_str_from_parts((const char*)bytes.ptr, bytes.length),
                                        NULL, 0U, &result, NULL);
    if (!sl_status_is_ok(status)) {
        sl_sqlite_transaction_rollback(arena, &tx, NULL);
        sl_cli_write_error_with_value(
            "sloppy db: migration failed and was rolled back: ", file->name, "\n");
        return 1;
    }
    params[0].kind = SL_SQLITE_PARAM_TEXT;
    params[0].value.text = sl_str_from_cstr(file->name);
    params[1].kind = SL_SQLITE_PARAM_TEXT;
    params[1].value.text = sl_str_from_cstr(file->hash);
    status = sl_sqlite_transaction_exec(
        arena, &tx,
        sl_str_from_cstr("insert into _sloppy_migrations (name, hash, appliedAt) values (?, ?, "
                         "strftime('%Y-%m-%dT%H:%M:%fZ','now'))"),
        params, 2U, &result, NULL);
    if (!sl_status_is_ok(status)) {
        sl_sqlite_transaction_rollback(arena, &tx, NULL);
        sl_cli_write_error_with_value("sloppy db: failed to record migration: ", file->name, "\n");
        return 1;
    }
    status = sl_sqlite_transaction_commit(arena, &tx, NULL);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_error_with_value("sloppy db: failed to commit migration: ", file->name, "\n");
        return 1;
    }
    return 0;
}

static void sl_db_emit_status(const SlCliOptions* options, const SlCliDbMigrationFile* files,
                              size_t count, bool changed)
{
    if (options->format == SL_CLI_FORMAT_JSON) {
        printf("{\"provider\":");
        sl_cli_json_escape(stdout, sl_cli_span_cstr(options->db_provider));
        printf(",\"status\":\"%s\",\"migrations\":[", changed ? "changed" : "ok");
        for (size_t index = 0U; index < count; index += 1U) {
            printf("%s{\"name\":", index == 0U ? "" : ",");
            sl_cli_json_escape(stdout, sl_cli_span_cstr(files[index].name));
            printf(",\"status\":\"%s\",\"hash\":", files[index].changed   ? "changed"
                                                   : files[index].applied ? "applied"
                                                                          : "pending");
            sl_cli_json_escape(stdout, sl_cli_span_cstr(files[index].hash));
            printf("}");
        }
        printf("]}\n");
        return;
    }
    printf("Sloppy DB migrations for provider %s\n", options->db_provider);
    for (size_t index = 0U; index < count; index += 1U) {
        printf("[%s] %s\n",
               files[index].changed   ? "changed"
               : files[index].applied ? "applied"
                                      : "pending",
               files[index].name);
    }
}

static int sl_cli_command_db(const SlCliOptions* options)
{
    char plan_path[SL_RUN_PATH_MAX_BYTES];
    char base_path[SL_RUN_PATH_MAX_BYTES];
    bool package_target = false;
    SlCliMetadata* metadata = sl_cli_scratch_metadata();
    unsigned char* json_storage = sl_cli_scratch_json_storage();
    yyjson_doc* doc = NULL;
    SlCliDbMigrationConfig migration_config = {0};
    SlCliDbMigrationFile files[128U] = {0};
    size_t file_count = 0U;
    const SlCliProvider* provider = NULL;
    SlSqliteConnection connection = {0};
    SlSqliteOpenOptions open_options = {0};
    unsigned char arena_storage[SL_CLI_ARENA_BYTES];
    SlArena arena = {0};
    bool changed = false;
    int result = 0;

    if (options == NULL) {
        return 1;
    }
    if (!sl_db_resolve_target(options, plan_path, sizeof(plan_path), base_path, sizeof(base_path),
                              &package_target))
    {
        sl_cli_write_cstr(
            stderr, "sloppy db: target must be an artifacts directory or package directory\n");
        return 1;
    }
    if (sl_cli_load_metadata(plan_path, json_storage, NULL, false, &doc, metadata) != 0) {
        return 1;
    }
    provider = sl_db_find_provider(metadata, options->db_provider);
    if (provider == NULL) {
        sl_cli_write_error_with_value(
            "sloppy db: provider not found in Plan: ", options->db_provider, "\n");
        yyjson_doc_free(doc);
        return 1;
    }
    if (!sl_cli_span_equal_cstr(provider->provider_kind, "sqlite") &&
        !sl_cli_span_equal_cstr(provider->provider, "sqlite"))
    {
        sl_cli_write_cstr(stderr,
                          "sloppy db: migrations currently execute only SQLite providers\n");
        yyjson_doc_free(doc);
        return 1;
    }
    if (sl_cli_span_empty(provider->database)) {
        sl_cli_write_cstr(stderr,
                          "sloppy db: SQLite provider database path is missing from Plan\n");
        yyjson_doc_free(doc);
        return 1;
    }
    if (!sl_db_load_migration_config(options, base_path, package_target, &migration_config)) {
        yyjson_doc_free(doc);
        return 1;
    }
    if (strcmp(migration_config.provider_kind, "sqlite") != 0) {
        sl_cli_write_cstr(stderr,
                          "sloppy db: migrations currently execute only SQLite providers\n");
        yyjson_doc_free(doc);
        return 1;
    }
    if (sl_db_read_migration_files(base_path, &migration_config, files,
                                   sizeof(files) / sizeof(files[0]), &file_count) != 0)
    {
        yyjson_doc_free(doc);
        return 1;
    }
    if (!sl_status_is_ok(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)))) {
        yyjson_doc_free(doc);
        return 1;
    }
    open_options.path = sl_cli_span_str(provider->database);
    open_options.access = SL_SQLITE_ACCESS_READWRITE;
    if (!sl_status_is_ok(sl_sqlite_open(&arena, &open_options, &connection, NULL))) {
        sl_cli_write_cstr(stderr, "sloppy db: failed to open SQLite database\n");
        yyjson_doc_free(doc);
        return 1;
    }
    result = sl_db_load_applied(&arena, &connection, files, file_count);
    if (result == 0) {
        for (size_t index = 0U; index < file_count; index += 1U) {
            changed = changed || files[index].changed;
        }
        if (changed) {
            sl_db_emit_status(options, files, file_count, true);
            sl_cli_write_cstr(stderr, "sloppy db: applied migration hash changed\n");
            result = 1;
        }
    }
    if (result == 0 && strcmp(options->db_action, "migrate") == 0) {
        for (size_t index = 0U; index < file_count; index += 1U) {
            if (files[index].applied) {
                continue;
            }
            if (sl_db_apply_file(&arena, &connection, &files[index]) != 0) {
                result = 1;
                break;
            }
            files[index].applied = true;
        }
    }
    if (result == 0) {
        sl_db_emit_status(options, files, file_count, false);
    }
    if (!sl_status_is_ok(sl_sqlite_close(&connection)) && result == 0) {
        result = 1;
    }
    yyjson_doc_free(doc);
    return result;
}
