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

typedef enum SlCliDbProviderKind
{
    SL_CLI_DB_PROVIDER_SQLITE,
    SL_CLI_DB_PROVIDER_POSTGRES,
    SL_CLI_DB_PROVIDER_SQLSERVER,
    SL_CLI_DB_PROVIDER_UNSUPPORTED
} SlCliDbProviderKind;

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

static const char* sl_db_migration_provider_name(const char* requested)
{
    SlStr view = {0};
    if (requested == NULL) {
        return NULL;
    }
    view = sl_str_from_cstr(requested);
    if (sl_str_starts_with(view, SL_STR_LITERAL("data.")) && view.length > 5U) {
        return requested + 5U;
    }
    return requested;
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
        SlStr target_view = sl_str_from_cstr(target);
        size_t length = target_view.length;
        size_t last_separator = SIZE_MAX;
        size_t base_length = 0U;
        SlStringBuilder base_builder = {0};
        SlStr base_view = {0};

        while (length > 0U && (target[length - 1U] == '/' || target[length - 1U] == '\\')) {
            length -= 1U;
        }
        for (size_t index = 0U; index < length; index += 1U) {
            if (target[index] == '/' || target[index] == '\\') {
                last_separator = index;
            }
        }
        if (last_separator == SIZE_MAX) {
            if (!sl_cli_join_leaf(plan_path, plan_capacity, target, "app.plan.json") ||
                !sl_cli_copy_cstr(base_path, base_capacity, "."))
            {
                return false;
            }
        }
        else {
            base_length = last_separator;
            if (last_separator == 0U || (last_separator == 2U && target[1] == ':' &&
                                         (target[2] == '/' || target[2] == '\\')))
            {
                base_length = last_separator + 1U;
            }
            if (!sl_cli_join_leaf(plan_path, plan_capacity, target, "app.plan.json") ||
                base_length + 1U > base_capacity)
            {
                return false;
            }
            if (!sl_status_is_ok(
                    sl_string_builder_init_fixed(&base_builder, base_path, base_capacity)) ||
                !sl_status_is_ok(sl_string_builder_append_str(
                    &base_builder, sl_str_from_parts(target, base_length))) ||
                !sl_status_is_ok(sl_string_builder_view_with_nul(&base_builder, &base_view)) ||
                base_view.ptr != base_path)
            {
                return false;
            }
        }
        if (base_path[0] == '\0') {
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

static bool sl_db_project_migration_config(const char* base_path, const char* provider_name,
                                           SlCliDbMigrationConfig* out)
{
    SlSloppyRunConfig config = {0};
    char config_path[SL_RUN_PATH_MAX_BYTES];
    if (!sl_cli_join_leaf(config_path, sizeof(config_path), base_path, SL_SLOPPYRC_FILE) ||
        sl_sloppyrc_load_path_for_command(&config, config_path, "sloppy db") != 0)
    {
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
    const char* migration_provider =
        options == NULL ? NULL : sl_db_migration_provider_name(options->db_provider);
    if (package_target) {
        if (!sl_cli_join_leaf(manifest_path, sizeof(manifest_path), base_path, "manifest.json")) {
            sl_cli_write_cstr(stderr, "sloppy db: package manifest path is too long\n");
            return false;
        }
        if (!sl_db_manifest_migration_config(manifest_path, migration_provider, out)) {
            sl_cli_write_error_with_value("sloppy db: no package migration config for provider: ",
                                          options->db_provider, "\n");
            return false;
        }
        return true;
    }
    return sl_db_project_migration_config(base_path, migration_provider, out);
}

static SlCliDbProviderKind sl_db_provider_kind(const SlCliProvider* provider,
                                               const SlCliDbMigrationConfig* config)
{
    if (provider != NULL) {
        if (sl_cli_span_equal_cstr(provider->provider_kind, "sqlite") ||
            sl_cli_span_equal_cstr(provider->provider, "sqlite"))
        {
            return SL_CLI_DB_PROVIDER_SQLITE;
        }
        if (sl_cli_span_equal_cstr(provider->provider_kind, "postgres") ||
            sl_cli_span_equal_cstr(provider->provider, "postgres"))
        {
            return SL_CLI_DB_PROVIDER_POSTGRES;
        }
        if (sl_cli_span_equal_cstr(provider->provider_kind, "sqlserver") ||
            sl_cli_span_equal_cstr(provider->provider, "sqlserver"))
        {
            return SL_CLI_DB_PROVIDER_SQLSERVER;
        }
    }
    if (config != NULL) {
        if (strcmp(config->provider_kind, "sqlite") == 0) {
            return SL_CLI_DB_PROVIDER_SQLITE;
        }
        if (strcmp(config->provider_kind, "postgres") == 0) {
            return SL_CLI_DB_PROVIDER_POSTGRES;
        }
        if (strcmp(config->provider_kind, "sqlserver") == 0) {
            return SL_CLI_DB_PROVIDER_SQLSERVER;
        }
    }
    return SL_CLI_DB_PROVIDER_UNSUPPORTED;
}

static const char* sl_db_provider_kind_name(SlCliDbProviderKind kind)
{
    switch (kind) {
    case SL_CLI_DB_PROVIDER_SQLITE:
        return "sqlite";
    case SL_CLI_DB_PROVIDER_POSTGRES:
        return "postgres";
    case SL_CLI_DB_PROVIDER_SQLSERVER:
        return "sqlserver";
    case SL_CLI_DB_PROVIDER_UNSUPPORTED:
    default:
        return "";
    }
}

static bool sl_db_config_key_env_name(char* buffer, size_t capacity, SlCliSpan key)
{
    SlStringBuilder builder = {0};
    SlStr view = {0};

    if (buffer == NULL || capacity == 0U || sl_cli_span_empty(key) ||
        !sl_status_is_ok(sl_string_builder_init_fixed(&builder, buffer, capacity)))
    {
        return false;
    }
    for (size_t index = 0U; index < key.length; index += 1U) {
        if (key.ptr[index] == ':') {
            if (!sl_status_is_ok(sl_string_builder_append_cstr(&builder, "__"))) {
                return false;
            }
        }
        else if (!sl_status_is_ok(sl_string_builder_append_char(&builder, key.ptr[index]))) {
            return false;
        }
    }
    return sl_status_is_ok(sl_string_builder_view_with_nul(&builder, &view)) && view.ptr == buffer;
}

static bool sl_db_default_connection_env_name(char* buffer, size_t capacity, const char* provider,
                                              const char* name)
{
    SlStringBuilder builder = {0};
    SlStr view = {0};

    return buffer != NULL && capacity > 0U && provider != NULL && provider[0] != '\0' &&
           name != NULL && name[0] != '\0' &&
           sl_status_is_ok(sl_string_builder_init_fixed(&builder, buffer, capacity)) &&
           sl_status_is_ok(sl_string_builder_append_cstr(&builder, "Sloppy__Providers__")) &&
           sl_status_is_ok(sl_string_builder_append_cstr(&builder, provider)) &&
           sl_status_is_ok(sl_string_builder_append_cstr(&builder, "__")) &&
           sl_status_is_ok(sl_string_builder_append_cstr(&builder, name)) &&
           sl_status_is_ok(sl_string_builder_append_cstr(&builder, "__connectionString")) &&
           sl_status_is_ok(sl_string_builder_view_with_nul(&builder, &view)) && view.ptr == buffer;
}

static bool sl_db_connection_env_name(const SlCliProvider* provider,
                                      const SlCliDbMigrationConfig* config, char* buffer,
                                      size_t capacity)
{
    if (provider != NULL && !sl_cli_span_empty(provider->config_key)) {
        return sl_db_config_key_env_name(buffer, capacity, provider->config_key);
    }
    if (config == NULL) {
        return false;
    }
    return sl_db_default_connection_env_name(buffer, capacity, config->provider_kind,
                                             config->provider);
}

static const char* sl_db_connection_string_from_env(const SlCliProvider* provider,
                                                    const SlCliDbMigrationConfig* config,
                                                    char* env_name, size_t env_name_capacity)
{
    const char* value = NULL;

    if (!sl_db_connection_env_name(provider, config, env_name, env_name_capacity)) {
        sl_cli_write_cstr(stderr, "sloppy db: connection-string environment name is too long\n");
        return NULL;
    }
    value = sl_run_getenv(env_name);
    if (value == NULL || value[0] == '\0') {
        sl_cli_write_error_with_value(
            "sloppy db: connection string environment variable is not set: ", env_name, "\n");
        return NULL;
    }
    return value;
}

static void sl_db_write_diag_hint(SlStr hint)
{
    if (sl_str_is_empty(hint)) {
        return;
    }
    sl_cli_write_cstr(stderr, "  ");
    fwrite(hint.ptr, 1U, hint.length, stderr);
    sl_cli_write_cstr(stderr, "\n");
}

static void sl_db_write_postgres_open_failure(const SlDiag* diag)
{
    sl_cli_write_cstr(stderr, "sloppy db: PostgreSQL provider is unavailable\n");
    sl_cli_write_cstr(
        stderr,
        "This only matters for apps that use PostgreSQL migrations or the PostgreSQL provider.\n");
    sl_cli_write_cstr(stderr, "Fix: install the optional PostgreSQL provider package when "
                              "available, or install system libpq for your platform.\n");
    if (diag != NULL) {
        for (size_t index = 0U; index < diag->hint_count; index += 1U) {
            sl_db_write_diag_hint(diag->hints[index]);
        }
    }
}

static void sl_db_write_sqlserver_open_failure(const SlDiag* diag)
{
    sl_cli_write_cstr(stderr, "sloppy db: SQL Server provider is unavailable\n");
    sl_cli_write_cstr(
        stderr,
        "This only matters for apps that use SQL Server migrations or the SQL Server provider.\n");
    sl_cli_write_cstr(stderr,
                      "Required: Microsoft ODBC Driver 17 or 18, installed from Microsoft's "
                      "platform package or your organization's managed driver deployment.\n");
    if (diag != NULL) {
        for (size_t index = 0U; index < diag->hint_count; index += 1U) {
            sl_db_write_diag_hint(diag->hints[index]);
        }
    }
}

static int sl_db_read_migration_files(const char* base_path, const SlCliDbMigrationConfig* config,
                                      SlCliDbMigrationFile* files, size_t capacity,
                                      size_t* out_count)
{
    unsigned char arena_storage[65536U];
    SlArena arena = {0};
    SlFsDirectoryList list = {0};
    unsigned char file_storage[SL_CLI_FILE_MAX_BYTES];
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
    SlSqliteQueryOptions query_options = {.max_rows = file_count + 1U};
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
                        NULL, 0U, &query_options, &result, NULL);
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

static bool sl_db_sql_script_has_sql(const char* sql, size_t length)
{
    for (size_t index = 0U; index < length; index += 1U) {
        char ch = sql[index];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
            return true;
        }
    }
    return false;
}

static SlStatus sl_db_sqlite_transaction_exec_script(SlArena* arena, SlSqliteTransaction* tx,
                                                     SlStr sql, SlSqliteExecResult* result)
{
    const char* text = sql.ptr;
    size_t statement_start = 0U;
    bool in_string = false;
    char quote = '\0';

    if (text == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    for (size_t index = 0U; index <= sql.length; index += 1U) {
        bool at_end = index == sql.length;
        char ch = '\0';
        if (!at_end) {
            ch = text[index];
        }
        if (!at_end && in_string) {
            if (ch == quote) {
                if (index + 1U < sql.length && text[index + 1U] == quote) {
                    index += 1U;
                }
                else {
                    in_string = false;
                }
            }
            continue;
        }
        if (!at_end && (ch == '\'' || ch == '"')) {
            in_string = true;
            quote = ch;
            continue;
        }
        if (at_end || ch == ';') {
            size_t statement_length = index - statement_start;
            if (sl_db_sql_script_has_sql(text + statement_start, statement_length)) {
                SlStatus status = sl_sqlite_transaction_exec(
                    arena, tx, sl_str_from_parts(text + statement_start, statement_length), NULL,
                    0U, result, NULL);
                if (!sl_status_is_ok(status)) {
                    return status;
                }
            }
            statement_start = index + 1U;
        }
    }
    return sl_status_ok();
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
    status = sl_db_sqlite_transaction_exec_script(
        arena, &tx, sl_str_from_parts((const char*)bytes.ptr, bytes.length), &result);
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

static int sl_db_load_applied_postgres(SlArena* arena, SlPostgresConnection* connection,
                                       SlCliDbMigrationFile* files, size_t file_count)
{
    SlPostgresResult result = {0};
    SlPostgresExecResult exec_result = {0};
    SlStatus status;

    status = sl_postgres_exec(
        arena, connection,
        sl_str_from_cstr("create table if not exists _sloppy_migrations (id bigint generated by "
                         "default as identity primary key, name text not null unique, hash text "
                         "not null, appliedAt text not null)"),
        NULL, 0U, &exec_result, NULL);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy db: failed to ensure PostgreSQL migration table\n");
        return 1;
    }
    status =
        sl_postgres_query(arena, connection,
                          sl_str_from_cstr("select name, hash from _sloppy_migrations order by id"),
                          NULL, 0U, NULL, &result, NULL);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy db: failed to read PostgreSQL migration metadata\n");
        return 1;
    }
    for (size_t row_index = 0U; row_index < result.row_count; row_index += 1U) {
        SlPostgresRow* row = &result.rows[row_index];
        if (result.column_count < 2U || row->values[0].kind != SL_POSTGRES_VALUE_TEXT ||
            row->values[1].kind != SL_POSTGRES_VALUE_TEXT)
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

static int sl_db_apply_file_postgres(SlArena* arena, SlPostgresConnection* connection,
                                     const SlCliDbMigrationFile* file)
{
    unsigned char file_storage[SL_CLI_FILE_MAX_BYTES];
    SlBytes bytes = {0};
    SlPostgresTransaction tx = {0};
    SlPostgresExecResult result = {0};
    SlPostgresParam params[2] = {0};
    SlStatus status;

    if (sl_read_file_with_messages(
            file->path, file_storage, sizeof(file_storage), &bytes,
            "sloppy db: migration not found: ", "sloppy db: migration is empty or too large: ",
            "sloppy db: failed to read migration: ") != 0)
    {
        return 1;
    }
    status = sl_postgres_transaction_begin(arena, connection, &tx, NULL);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy db: failed to begin PostgreSQL migration transaction\n");
        return 1;
    }
    status = sl_postgres_transaction_exec_batch(
        arena, &tx, sl_str_from_parts((const char*)bytes.ptr, bytes.length), &result, NULL);
    if (!sl_status_is_ok(status)) {
        sl_postgres_transaction_rollback(arena, &tx, NULL);
        sl_cli_write_error_with_value(
            "sloppy db: PostgreSQL migration failed and was rolled back: ", file->name, "\n");
        return 1;
    }
    params[0].kind = SL_POSTGRES_PARAM_TEXT;
    params[0].value.text = sl_str_from_cstr(file->name);
    params[1].kind = SL_POSTGRES_PARAM_TEXT;
    params[1].value.text = sl_str_from_cstr(file->hash);
    status = sl_postgres_transaction_exec(
        arena, &tx,
        sl_str_from_cstr("insert into _sloppy_migrations (name, hash, appliedAt) values ($1, $2, "
                         "to_char(clock_timestamp() at time zone 'UTC', "
                         "'YYYY-MM-DD\"T\"HH24:MI:SS.MS\"Z\"'))"),
        params, 2U, &result, NULL);
    if (!sl_status_is_ok(status)) {
        sl_postgres_transaction_rollback(arena, &tx, NULL);
        sl_cli_write_error_with_value(
            "sloppy db: failed to record PostgreSQL migration: ", file->name, "\n");
        return 1;
    }
    status = sl_postgres_transaction_commit(arena, &tx, NULL);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_error_with_value(
            "sloppy db: failed to commit PostgreSQL migration: ", file->name, "\n");
        return 1;
    }
    return 0;
}

static int sl_db_load_applied_sqlserver(SlArena* arena, SlSqlServerConnection* connection,
                                        SlCliDbMigrationFile* files, size_t file_count)
{
    SlSqlServerResult result = {0};
    SlSqlServerExecResult exec_result = {0};
    SlStatus status;

    status = sl_sqlserver_exec(
        arena, connection,
        sl_str_from_cstr("if object_id(N'dbo._sloppy_migrations', N'U') is null create table "
                         "dbo._sloppy_migrations (id bigint identity(1,1) primary key, name "
                         "nvarchar(450) not null unique, hash nvarchar(64) not null, appliedAt "
                         "nvarchar(64) not null)"),
        NULL, 0U, &exec_result, NULL);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy db: failed to ensure SQL Server migration table\n");
        return 1;
    }
    status = sl_sqlserver_query(
        arena, connection,
        sl_str_from_cstr("select name, hash from dbo._sloppy_migrations order by id"), NULL, 0U,
        NULL, &result, NULL);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy db: failed to read SQL Server migration metadata\n");
        return 1;
    }
    for (size_t row_index = 0U; row_index < result.row_count; row_index += 1U) {
        SlSqlServerRow* row = &result.rows[row_index];
        if (result.column_count < 2U || row->values[0].kind != SL_SQLSERVER_VALUE_TEXT ||
            row->values[1].kind != SL_SQLSERVER_VALUE_TEXT)
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

static bool sl_db_sqlserver_batch_has_sql(const char* sql, size_t length)
{
    for (size_t index = 0U; index < length; index += 1U) {
        char ch = sql[index];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
            return true;
        }
    }
    return false;
}

static bool sl_db_sqlserver_is_go_line(const char* sql, size_t start, size_t end)
{
    while (start < end && (sql[start] == ' ' || sql[start] == '\t' || sql[start] == '\r')) {
        start += 1U;
    }
    while (end > start && (sql[end - 1U] == ' ' || sql[end - 1U] == '\t' || sql[end - 1U] == '\r'))
    {
        end -= 1U;
    }
    if (end - start != 2U) {
        return false;
    }
    return (sql[start] == 'G' || sql[start] == 'g') &&
           (sql[start + 1U] == 'O' || sql[start + 1U] == 'o');
}

static SlStatus sl_db_sqlserver_exec_migration_batches(SlArena* arena, SlSqlServerTransaction* tx,
                                                       SlStr sql, SlSqlServerExecResult* result)
{
    const char* text = sql.ptr;
    size_t batch_start = 0U;
    size_t line_start = 0U;

    if (text == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    for (size_t index = 0U; index <= sql.length; index += 1U) {
        bool at_end = index == sql.length;
        bool at_line_end = at_end || text[index] == '\n';
        if (!at_line_end) {
            continue;
        }
        if (sl_db_sqlserver_is_go_line(text, line_start, index)) {
            size_t batch_length = line_start - batch_start;
            if (sl_db_sqlserver_batch_has_sql(text + batch_start, batch_length)) {
                SlStatus status = sl_sqlserver_transaction_exec_batch(
                    arena, tx, sl_str_from_parts(text + batch_start, batch_length), result, NULL);
                if (!sl_status_is_ok(status)) {
                    return status;
                }
            }
            batch_start = at_end ? index : index + 1U;
        }
        line_start = at_end ? index : index + 1U;
    }
    if (batch_start < sql.length &&
        sl_db_sqlserver_batch_has_sql(text + batch_start, sql.length - batch_start))
    {
        return sl_sqlserver_transaction_exec_batch(
            arena, tx, sl_str_from_parts(text + batch_start, sql.length - batch_start), result,
            NULL);
    }
    return sl_status_ok();
}

static int sl_db_apply_file_sqlserver(SlArena* arena, SlSqlServerConnection* connection,
                                      const SlCliDbMigrationFile* file)
{
    unsigned char file_storage[SL_CLI_FILE_MAX_BYTES];
    SlBytes bytes = {0};
    SlSqlServerTransaction tx = {0};
    SlSqlServerExecResult result = {0};
    SlSqlServerParam params[2] = {0};
    SlStatus status;

    if (sl_read_file_with_messages(
            file->path, file_storage, sizeof(file_storage), &bytes,
            "sloppy db: migration not found: ", "sloppy db: migration is empty or too large: ",
            "sloppy db: failed to read migration: ") != 0)
    {
        return 1;
    }
    status = sl_sqlserver_transaction_begin(arena, connection, &tx, NULL);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy db: failed to begin SQL Server migration transaction\n");
        return 1;
    }
    status = sl_db_sqlserver_exec_migration_batches(
        arena, &tx, sl_str_from_parts((const char*)bytes.ptr, bytes.length), &result);
    if (!sl_status_is_ok(status)) {
        sl_sqlserver_transaction_rollback(arena, &tx, NULL);
        sl_cli_write_error_with_value(
            "sloppy db: SQL Server migration failed and was rolled back: ", file->name, "\n");
        return 1;
    }
    params[0].kind = SL_SQLSERVER_PARAM_TEXT;
    params[0].value.text = sl_str_from_cstr(file->name);
    params[1].kind = SL_SQLSERVER_PARAM_TEXT;
    params[1].value.text = sl_str_from_cstr(file->hash);
    status = sl_sqlserver_transaction_exec(
        arena, &tx,
        sl_str_from_cstr("insert into dbo._sloppy_migrations (name, hash, appliedAt) values (?, ?, "
                         "convert(nvarchar(64), sysutcdatetime(), 127) + N'Z')"),
        params, 2U, &result, NULL);
    if (!sl_status_is_ok(status)) {
        sl_sqlserver_transaction_rollback(arena, &tx, NULL);
        sl_cli_write_error_with_value(
            "sloppy db: failed to record SQL Server migration: ", file->name, "\n");
        return 1;
    }
    status = sl_sqlserver_transaction_commit(arena, &tx, NULL);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_error_with_value(
            "sloppy db: failed to commit SQL Server migration: ", file->name, "\n");
        return 1;
    }
    return 0;
}

static void sl_db_emit_status(const SlCliOptions* options, const SlCliDbMigrationFile* files,
                              size_t count, bool changed)
{
    bool pending = false;
    for (size_t index = 0U; index < count; index += 1U) {
        pending = pending || (!files[index].applied && !files[index].changed);
    }
    if (options->format == SL_CLI_FORMAT_JSON) {
        printf("{\"provider\":");
        sl_cli_json_escape(stdout, sl_cli_span_cstr(options->db_provider));
        printf(",\"status\":\"%s\",\"migrations\":[", changed   ? "changed"
                                                      : pending ? "pending"
                                                                : "current");
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
    SlCliDbProviderKind provider_kind = SL_CLI_DB_PROVIDER_UNSUPPORTED;
    SlSqliteConnection sqlite_connection = {0};
    SlPostgresConnection postgres_connection = {0};
    SlSqlServerConnection sqlserver_connection = {0};
    static unsigned char arena_storage[SL_CLI_ARENA_BYTES * 4U];
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
    if (!sl_db_load_migration_config(options, base_path, package_target, &migration_config)) {
        yyjson_doc_free(doc);
        return 1;
    }
    provider_kind = sl_db_provider_kind(provider, &migration_config);
    if (provider_kind == SL_CLI_DB_PROVIDER_UNSUPPORTED) {
        sl_cli_write_cstr(stderr, "sloppy db: unsupported database provider for migrations\n");
        yyjson_doc_free(doc);
        return 1;
    }
    if (strcmp(migration_config.provider_kind, sl_db_provider_kind_name(provider_kind)) != 0) {
        sl_cli_write_cstr(stderr,
                          "sloppy db: migration config provider does not match Plan provider\n");
        yyjson_doc_free(doc);
        return 1;
    }
    if (provider_kind == SL_CLI_DB_PROVIDER_SQLITE && sl_cli_span_empty(provider->database)) {
        sl_cli_write_cstr(stderr,
                          "sloppy db: SQLite provider database path is missing from Plan\n");
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

    if (provider_kind == SL_CLI_DB_PROVIDER_SQLITE) {
        SlSqliteOpenOptions open_options = {0};
        char sqlite_database_path[SL_RUN_PATH_MAX_BYTES];
        const char* sqlite_database_base_path = base_path;
        if (sl_cli_span_equal_cstr(provider->database, ":memory:") ||
            sl_run_path_span_is_absolute(provider->database))
        {
            if (!sl_db_copy_span(sqlite_database_path, sizeof(sqlite_database_path),
                                 provider->database))
            {
                yyjson_doc_free(doc);
                return 1;
            }
        }
        else if (!sl_run_join_path(sqlite_database_path, sizeof(sqlite_database_path),
                                   sqlite_database_base_path, provider->database))
        {
            sl_cli_write_cstr(stderr, "sloppy db: SQLite database path is too long\n");
            yyjson_doc_free(doc);
            return 1;
        }
        open_options.path = sl_str_from_cstr(sqlite_database_path);
        open_options.access = SL_SQLITE_ACCESS_READWRITE;
        if (!sl_status_is_ok(sl_sqlite_open(&arena, &open_options, &sqlite_connection, NULL))) {
            sl_cli_write_cstr(stderr, "sloppy db: failed to open SQLite database\n");
            yyjson_doc_free(doc);
            return 1;
        }
        result = sl_db_load_applied(&arena, &sqlite_connection, files, file_count);
    }
    else if (provider_kind == SL_CLI_DB_PROVIDER_POSTGRES) {
        char env_name[SL_RUN_PATH_MAX_BYTES];
        const char* connection_string = sl_db_connection_string_from_env(
            provider, &migration_config, env_name, sizeof(env_name));
        SlPostgresOpenOptions open_options = {0};
        SlDiag diag = {0};
        if (connection_string == NULL) {
            yyjson_doc_free(doc);
            return 1;
        }
        open_options =
            sl_postgres_open_options_connection_string(sl_str_from_cstr(connection_string));
        open_options.access = SL_POSTGRES_ACCESS_READWRITE;
        if (!sl_status_is_ok(sl_postgres_open(&arena, &open_options, &postgres_connection, &diag)))
        {
            sl_db_write_postgres_open_failure(&diag);
            yyjson_doc_free(doc);
            return 1;
        }
        result = sl_db_load_applied_postgres(&arena, &postgres_connection, files, file_count);
    }
    else if (provider_kind == SL_CLI_DB_PROVIDER_SQLSERVER) {
        char env_name[SL_RUN_PATH_MAX_BYTES];
        const char* connection_string = sl_db_connection_string_from_env(
            provider, &migration_config, env_name, sizeof(env_name));
        SlSqlServerOpenOptions open_options = {0};
        SlDiag diag = {0};
        if (connection_string == NULL) {
            yyjson_doc_free(doc);
            return 1;
        }
        open_options =
            sl_sqlserver_open_options_connection_string(sl_str_from_cstr(connection_string));
        open_options.access = SL_SQLSERVER_ACCESS_READWRITE;
        if (!sl_status_is_ok(
                sl_sqlserver_open(&arena, &open_options, &sqlserver_connection, &diag)))
        {
            sl_db_write_sqlserver_open_failure(&diag);
            yyjson_doc_free(doc);
            return 1;
        }
        result = sl_db_load_applied_sqlserver(&arena, &sqlserver_connection, files, file_count);
    }

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
            if (provider_kind == SL_CLI_DB_PROVIDER_SQLITE &&
                sl_db_apply_file(&arena, &sqlite_connection, &files[index]) != 0)
            {
                result = 1;
                break;
            }
            if (provider_kind == SL_CLI_DB_PROVIDER_POSTGRES &&
                sl_db_apply_file_postgres(&arena, &postgres_connection, &files[index]) != 0)
            {
                result = 1;
                break;
            }
            if (provider_kind == SL_CLI_DB_PROVIDER_SQLSERVER &&
                sl_db_apply_file_sqlserver(&arena, &sqlserver_connection, &files[index]) != 0)
            {
                result = 1;
                break;
            }
            files[index].applied = true;
        }
    }
    if (result == 0) {
        sl_db_emit_status(options, files, file_count, false);
    }
    if (sqlite_connection.open && !sl_status_is_ok(sl_sqlite_close(&sqlite_connection)) &&
        result == 0)
    {
        result = 1;
    }
    if (postgres_connection.open && !sl_status_is_ok(sl_postgres_close(&postgres_connection)) &&
        result == 0)
    {
        result = 1;
    }
    if (sqlserver_connection.open && !sl_status_is_ok(sl_sqlserver_close(&sqlserver_connection)) &&
        result == 0)
    {
        result = 1;
    }
    yyjson_doc_free(doc);
    return result;
}
