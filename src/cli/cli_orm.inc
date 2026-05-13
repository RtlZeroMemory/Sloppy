/*
 * src/cli/cli_orm.inc
 *
 * Private implementation of `sloppy orm` metadata-backed commands.
 */

static bool sl_orm_json_bool(yyjson_val* object, const char* name)
{
    yyjson_val* value = yyjson_obj_get(object, name);
    return value != NULL && yyjson_is_bool(value) && yyjson_get_bool(value);
}

static const char* sl_orm_json_cstr(yyjson_val* object, const char* name)
{
    yyjson_val* value = yyjson_obj_get(object, name);
    return value != NULL && yyjson_is_str(value) ? yyjson_get_str(value) : NULL;
}

static bool sl_orm_append_quoted_identifier(SlStringBuilder* builder, SlCliDbProviderKind kind,
                                            const char* name)
{
    const char* cursor = name;
    char close = kind == SL_CLI_DB_PROVIDER_SQLSERVER ? ']' : '"';

    if (builder == NULL || name == NULL || name[0] == '\0') {
        return false;
    }
    if (!sl_status_is_ok(sl_string_builder_append_char(
            builder, kind == SL_CLI_DB_PROVIDER_SQLSERVER ? '[' : '"')))
    {
        return false;
    }
    for (; *cursor != '\0'; cursor += 1) {
        if (*cursor == close &&
            !sl_status_is_ok(sl_string_builder_append_char(builder, close)))
        {
            return false;
        }
        if (!sl_status_is_ok(sl_string_builder_append_char(builder, *cursor))) {
            return false;
        }
    }
    return sl_status_is_ok(sl_string_builder_append_char(builder, close));
}

static const char* sl_orm_sql_type(const char* type, SlCliDbProviderKind kind)
{
    if (type == NULL) {
        return NULL;
    }
    if (kind == SL_CLI_DB_PROVIDER_SQLITE) {
        if (strcmp(type, "text") == 0 || strcmp(type, "string") == 0 ||
            strcmp(type, "decimal") == 0 || strcmp(type, "uuid") == 0 ||
            strcmp(type, "instant") == 0 || strcmp(type, "timestamp") == 0 ||
            strcmp(type, "date") == 0 || strcmp(type, "json") == 0 ||
            strcmp(type, "enum") == 0)
        {
            return "text";
        }
        if (strcmp(type, "int") == 0 || strcmp(type, "integer") == 0 ||
            strcmp(type, "bigint") == 0 || strcmp(type, "bool") == 0 ||
            strcmp(type, "boolean") == 0)
        {
            return "integer";
        }
        if (strcmp(type, "number") == 0 || strcmp(type, "float") == 0) {
            return "real";
        }
        if (strcmp(type, "blob") == 0 || strcmp(type, "bytes") == 0) {
            return "blob";
        }
        return NULL;
    }
    if (kind == SL_CLI_DB_PROVIDER_POSTGRES) {
        if (strcmp(type, "text") == 0 || strcmp(type, "string") == 0 ||
            strcmp(type, "enum") == 0)
        {
            return "text";
        }
        if (strcmp(type, "int") == 0 || strcmp(type, "integer") == 0) {
            return "integer";
        }
        if (strcmp(type, "bigint") == 0) {
            return "bigint";
        }
        if (strcmp(type, "number") == 0 || strcmp(type, "float") == 0) {
            return "double precision";
        }
        if (strcmp(type, "decimal") == 0) {
            return "numeric";
        }
        if (strcmp(type, "bool") == 0 || strcmp(type, "boolean") == 0) {
            return "boolean";
        }
        if (strcmp(type, "uuid") == 0) {
            return "uuid";
        }
        if (strcmp(type, "instant") == 0 || strcmp(type, "timestamp") == 0) {
            return "timestamptz";
        }
        if (strcmp(type, "date") == 0) {
            return "date";
        }
        if (strcmp(type, "json") == 0) {
            return "jsonb";
        }
        if (strcmp(type, "blob") == 0 || strcmp(type, "bytes") == 0) {
            return "bytea";
        }
        return NULL;
    }
    if (kind == SL_CLI_DB_PROVIDER_SQLSERVER) {
        if (strcmp(type, "text") == 0 || strcmp(type, "string") == 0 ||
            strcmp(type, "json") == 0)
        {
            return "nvarchar(max)";
        }
        if (strcmp(type, "int") == 0 || strcmp(type, "integer") == 0) {
            return "int";
        }
        if (strcmp(type, "bigint") == 0) {
            return "bigint";
        }
        if (strcmp(type, "number") == 0 || strcmp(type, "float") == 0) {
            return "float";
        }
        if (strcmp(type, "decimal") == 0) {
            return "decimal(38, 18)";
        }
        if (strcmp(type, "bool") == 0 || strcmp(type, "boolean") == 0) {
            return "bit";
        }
        if (strcmp(type, "uuid") == 0) {
            return "uniqueidentifier";
        }
        if (strcmp(type, "instant") == 0 || strcmp(type, "timestamp") == 0) {
            return "datetimeoffset";
        }
        if (strcmp(type, "date") == 0) {
            return "date";
        }
        if (strcmp(type, "blob") == 0 || strcmp(type, "bytes") == 0) {
            return "varbinary(max)";
        }
        if (strcmp(type, "enum") == 0) {
            return "nvarchar(255)";
        }
        return NULL;
    }
    return NULL;
}

static const char* sl_orm_column_sql_type(yyjson_val* column, SlCliDbProviderKind kind)
{
    const char* type = sl_orm_json_cstr(column, "type");

    if (kind == SL_CLI_DB_PROVIDER_SQLSERVER &&
        (strcmp(type == NULL ? "" : type, "text") == 0 ||
         strcmp(type == NULL ? "" : type, "string") == 0) &&
        (sl_orm_json_bool(column, "unique") || sl_orm_json_bool(column, "index")))
    {
        return "nvarchar(450)";
    }
    return sl_orm_sql_type(type, kind);
}

static const char* sl_orm_default_now(SlCliDbProviderKind kind)
{
    return kind == SL_CLI_DB_PROVIDER_SQLSERVER ? "SYSUTCDATETIME()" : "CURRENT_TIMESTAMP";
}

static const char* sl_orm_table_name_for_model(yyjson_val* tables, const char* model)
{
    size_t index = 0U;
    size_t max = 0U;
    yyjson_val* table = NULL;

    if (tables == NULL || model == NULL) {
        return NULL;
    }
    yyjson_arr_foreach(tables, index, max, table)
    {
        const char* current_model = sl_orm_json_cstr(table, "model");
        const char* current_name = sl_orm_json_cstr(table, "name");
        if (current_model != NULL && current_name != NULL && strcmp(current_model, model) == 0) {
            return current_name;
        }
    }
    return model;
}

static bool sl_orm_append_column_sql(SlStringBuilder* builder, yyjson_val* tables,
                                     yyjson_val* column, SlCliDbProviderKind kind,
                                     bool inline_references)
{
    const char* name = sl_orm_json_cstr(column, "name");
    const char* sql_type = sl_orm_column_sql_type(column, kind);
    yyjson_val* reference = yyjson_obj_get(column, "reference");

    if (name == NULL || sql_type == NULL ||
        !sl_orm_append_quoted_identifier(builder, kind, name) ||
        !sl_status_is_ok(sl_string_builder_append_char(builder, ' ')) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(builder, sql_type)))
    {
        return false;
    }
    if (sl_orm_json_bool(column, "primaryKey") &&
        !sl_status_is_ok(sl_string_builder_append_cstr(builder, " primary key")))
    {
        return false;
    }
    if (sl_orm_json_bool(column, "notNull") &&
        !sl_status_is_ok(sl_string_builder_append_cstr(builder, " not null")))
    {
        return false;
    }
    if (sl_orm_json_bool(column, "unique") &&
        !sl_status_is_ok(sl_string_builder_append_cstr(builder, " unique")))
    {
        return false;
    }
    if (sl_orm_json_bool(column, "defaultNow") &&
        (!sl_status_is_ok(sl_string_builder_append_cstr(builder, " default ")) ||
         !sl_status_is_ok(sl_string_builder_append_cstr(builder, sl_orm_default_now(kind)))))
    {
        return false;
    }
    if (inline_references && reference != NULL && yyjson_is_obj(reference)) {
        const char* model = sl_orm_json_cstr(reference, "tableModel");
        const char* ref_column = sl_orm_json_cstr(reference, "column");
        const char* ref_table = sl_orm_table_name_for_model(tables, model);
        if (ref_table != NULL && ref_column != NULL &&
            (!sl_status_is_ok(sl_string_builder_append_cstr(builder, " references ")) ||
             !sl_orm_append_quoted_identifier(builder, kind, ref_table) ||
             !sl_status_is_ok(sl_string_builder_append_cstr(builder, " (")) ||
             !sl_orm_append_quoted_identifier(builder, kind, ref_column) ||
             !sl_status_is_ok(sl_string_builder_append_char(builder, ')'))))
        {
            return false;
        }
    }
    return true;
}

static bool sl_orm_append_index_name(SlStringBuilder* builder, SlCliDbProviderKind kind,
                                     const char* table_name, const char* column_name)
{
    char name[SL_RUN_PATH_MAX_BYTES];
    SlStringBuilder name_builder = {0};
    SlStr view = {0};

    if (!sl_status_is_ok(sl_string_builder_init_fixed(&name_builder, name, sizeof(name))) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&name_builder, "ix_")) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&name_builder, table_name)) ||
        !sl_status_is_ok(sl_string_builder_append_char(&name_builder, '_')) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&name_builder, column_name)) ||
        !sl_status_is_ok(sl_string_builder_view_with_nul(&name_builder, &view)) ||
        view.ptr != name)
    {
        return false;
    }
    return sl_orm_append_quoted_identifier(builder, kind, name);
}

static bool sl_orm_append_constraint_name(SlStringBuilder* builder, SlCliDbProviderKind kind,
                                          const char* table_name, const char* column_name,
                                          const char* ref_table, const char* ref_column)
{
    char name[SL_RUN_PATH_MAX_BYTES];
    SlStringBuilder name_builder = {0};
    SlStr view = {0};

    if (!sl_status_is_ok(sl_string_builder_init_fixed(&name_builder, name, sizeof(name))) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&name_builder, "fk_")) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&name_builder, table_name)) ||
        !sl_status_is_ok(sl_string_builder_append_char(&name_builder, '_')) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&name_builder, column_name)) ||
        !sl_status_is_ok(sl_string_builder_append_char(&name_builder, '_')) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&name_builder, ref_table)) ||
        !sl_status_is_ok(sl_string_builder_append_char(&name_builder, '_')) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&name_builder, ref_column)) ||
        !sl_status_is_ok(sl_string_builder_view_with_nul(&name_builder, &view)) ||
        view.ptr != name)
    {
        return false;
    }
    return sl_orm_append_quoted_identifier(builder, kind, name);
}

static bool sl_orm_append_min_width_u32(SlStringBuilder* builder, uint32_t value, size_t width)
{
    uint32_t threshold = 1U;
    size_t digits = 1U;

    if (builder == NULL || width == 0U) {
        return false;
    }
    while (threshold <= UINT32_MAX / 10U && value >= threshold * 10U) {
        threshold *= 10U;
        digits += 1U;
    }
    while (digits < width) {
        if (!sl_status_is_ok(sl_string_builder_append_char(builder, '0'))) {
            return false;
        }
        digits += 1U;
    }
    return sl_status_is_ok(sl_string_builder_append_u64(builder, value));
}

static bool sl_orm_append_table_sql(SlStringBuilder* builder, yyjson_val* tables,
                                    yyjson_val* table, SlCliDbProviderKind kind, bool* emitted,
                                    bool inline_references)
{
    const char* table_name = sl_orm_json_cstr(table, "name");
    yyjson_val* columns = yyjson_obj_get(table, "columns");
    size_t index = 0U;
    size_t max = 0U;
    yyjson_val* column = NULL;
    bool first = true;

    if (table_name == NULL || columns == NULL || !yyjson_is_arr(columns) ||
        yyjson_arr_size(columns) == 0U)
    {
        return false;
    }
    if (*emitted && !sl_status_is_ok(sl_string_builder_append_cstr(builder, "\n\n"))) {
        return false;
    }
    if (!sl_status_is_ok(sl_string_builder_append_cstr(builder, "create table ")) ||
        !sl_orm_append_quoted_identifier(builder, kind, table_name) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(builder, " (\n")))
    {
        return false;
    }
    yyjson_arr_foreach(columns, index, max, column)
    {
        if (!first && !sl_status_is_ok(sl_string_builder_append_cstr(builder, ",\n"))) {
            return false;
        }
        if (!sl_status_is_ok(sl_string_builder_append_cstr(builder, "  ")) ||
            !sl_orm_append_column_sql(builder, tables, column, kind, inline_references))
        {
            return false;
        }
        first = false;
    }
    if (!sl_status_is_ok(sl_string_builder_append_cstr(builder, "\n);"))) {
        return false;
    }
    yyjson_arr_foreach(columns, index, max, column)
    {
        const char* column_name = sl_orm_json_cstr(column, "name");
        if (!(sl_orm_json_bool(column, "index") || sl_orm_json_bool(column, "softDelete"))) {
            continue;
        }
        if (column_name == NULL ||
            !sl_status_is_ok(sl_string_builder_append_cstr(builder, "\ncreate index ")) ||
            !sl_orm_append_index_name(builder, kind, table_name, column_name) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(builder, " on ")) ||
            !sl_orm_append_quoted_identifier(builder, kind, table_name) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(builder, " (")) ||
            !sl_orm_append_quoted_identifier(builder, kind, column_name) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(builder, ");")))
        {
            return false;
        }
    }
    *emitted = true;
    return true;
}

static bool sl_orm_table_depends_on(yyjson_val* tables, yyjson_val* table, const char* target_name)
{
    yyjson_val* columns = yyjson_obj_get(table, "columns");
    const char* table_name = sl_orm_json_cstr(table, "name");
    size_t index = 0U;
    size_t max = 0U;
    yyjson_val* column = NULL;

    if (tables == NULL || table == NULL || target_name == NULL || columns == NULL ||
        !yyjson_is_arr(columns))
    {
        return false;
    }
    yyjson_arr_foreach(columns, index, max, column)
    {
        yyjson_val* reference = yyjson_obj_get(column, "reference");
        const char* model = reference == NULL ? NULL : sl_orm_json_cstr(reference, "tableModel");
        const char* ref_table = sl_orm_table_name_for_model(tables, model);
        if (ref_table != NULL && strcmp(ref_table, target_name) == 0 &&
            (table_name == NULL || strcmp(table_name, target_name) != 0))
        {
            return true;
        }
    }
    return false;
}

static bool sl_orm_order_tables(SlArena* arena, yyjson_val* tables, yyjson_val*** out_ordered,
                                size_t* out_count)
{
    SlSlice item_slice = {0};
    SlSlice ordered_slice = {0};
    SlSlice emitted_slice = {0};
    yyjson_val** items = NULL;
    bool* emitted = NULL;
    size_t table_count = 0U;
    size_t index = 0U;
    size_t max = 0U;
    yyjson_val* table = NULL;

    if (arena == NULL || tables == NULL || out_ordered == NULL || out_count == NULL ||
        !yyjson_is_arr(tables))
    {
        return false;
    }
    table_count = yyjson_arr_size(tables);
    if (table_count == 0U ||
        !sl_status_is_ok(sl_arena_array_alloc(arena, table_count, sizeof(yyjson_val*),
                                              _Alignof(yyjson_val*), &item_slice)) ||
        !sl_status_is_ok(sl_arena_array_alloc(arena, table_count, sizeof(yyjson_val*),
                                              _Alignof(yyjson_val*), &ordered_slice)) ||
        !sl_status_is_ok(sl_arena_array_alloc(arena, table_count, sizeof(bool), _Alignof(bool),
                                              &emitted_slice)))
    {
        return false;
    }
    items = (yyjson_val**)item_slice.ptr;
    *out_ordered = (yyjson_val**)ordered_slice.ptr;
    emitted = (bool*)emitted_slice.ptr;
    table_count = 0U;
    yyjson_arr_foreach(tables, index, max, table)
    {
        items[table_count++] = table;
    }
    *out_count = 0U;
    while (*out_count < table_count) {
        bool progress = false;
        for (size_t candidate = 0U; candidate < table_count; candidate += 1U) {
            bool blocked = false;
            const char* candidate_name = sl_orm_json_cstr(items[candidate], "name");
            if (emitted[candidate]) {
                continue;
            }
            for (size_t dependency = 0U; dependency < table_count; dependency += 1U) {
                const char* dependency_name = sl_orm_json_cstr(items[dependency], "name");
                if (dependency == candidate || emitted[dependency] || dependency_name == NULL) {
                    continue;
                }
                if (sl_orm_table_depends_on(tables, items[candidate], dependency_name)) {
                    blocked = true;
                    break;
                }
            }
            if (!blocked && candidate_name != NULL) {
                (*out_ordered)[(*out_count)++] = items[candidate];
                emitted[candidate] = true;
                progress = true;
            }
        }
        if (!progress) {
            for (size_t candidate = 0U; candidate < table_count; candidate += 1U) {
                if (!emitted[candidate]) {
                    (*out_ordered)[(*out_count)++] = items[candidate];
                    emitted[candidate] = true;
                }
            }
        }
    }
    return true;
}

static bool sl_orm_append_deferred_fk_sql(SlStringBuilder* builder, yyjson_val* tables,
                                          yyjson_val* table, SlCliDbProviderKind kind,
                                          bool* emitted_any)
{
    const char* table_name = sl_orm_json_cstr(table, "name");
    yyjson_val* columns = yyjson_obj_get(table, "columns");
    size_t index = 0U;
    size_t max = 0U;
    yyjson_val* column = NULL;

    if (table_name == NULL || columns == NULL || !yyjson_is_arr(columns)) {
        return false;
    }
    yyjson_arr_foreach(columns, index, max, column)
    {
        yyjson_val* reference = yyjson_obj_get(column, "reference");
        const char* column_name = sl_orm_json_cstr(column, "name");
        const char* model = reference == NULL ? NULL : sl_orm_json_cstr(reference, "tableModel");
        const char* ref_column = reference == NULL ? NULL : sl_orm_json_cstr(reference, "column");
        const char* ref_table = sl_orm_table_name_for_model(tables, model);
        if (reference == NULL || !yyjson_is_obj(reference)) {
            continue;
        }
        if (column_name == NULL || ref_table == NULL || ref_column == NULL) {
            return false;
        }
        if (!sl_status_is_ok(sl_string_builder_append_cstr(builder, *emitted_any ? "\n" : "\n\n")) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(builder, "alter table ")) ||
            !sl_orm_append_quoted_identifier(builder, kind, table_name) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(builder, " add constraint ")) ||
            !sl_orm_append_constraint_name(builder, kind, table_name, column_name, ref_table,
                                           ref_column) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(builder, " foreign key (")) ||
            !sl_orm_append_quoted_identifier(builder, kind, column_name) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(builder, ") references ")) ||
            !sl_orm_append_quoted_identifier(builder, kind, ref_table) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(builder, " (")) ||
            !sl_orm_append_quoted_identifier(builder, kind, ref_column) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(builder, ");")))
        {
            return false;
        }
        *emitted_any = true;
    }
    return true;
}

static bool sl_orm_build_migration_script(SlArena* arena, yyjson_doc* doc,
                                          SlCliDbProviderKind kind, SlCliSpan* out)
{
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* orm = root == NULL ? NULL : yyjson_obj_get(root, "orm");
    yyjson_val* tables = orm == NULL ? NULL : yyjson_obj_get(orm, "tables");
    SlStringBuilder builder = {0};
    SlStr view = {0};
    yyjson_val** ordered = NULL;
    size_t table_count = 0U;
    bool emitted = false;
    bool deferred_fk_emitted = false;
    bool inline_references = kind == SL_CLI_DB_PROVIDER_SQLITE;

    if (out != NULL) {
        *out = (SlCliSpan){0};
    }
    if (arena == NULL || out == NULL || tables == NULL ||
        !yyjson_is_arr(tables) || yyjson_arr_size(tables) == 0U ||
        !sl_status_is_ok(sl_string_builder_init_arena(&builder, arena, 4096U,
                                                      ((size_t)SL_CLI_ARENA_BYTES * 4U))))
    {
        return false;
    }
    if (!sl_orm_order_tables(arena, tables, &ordered, &table_count)) {
        return false;
    }
    for (size_t index = 0U; index < table_count; index += 1U) {
        if (!sl_orm_append_table_sql(&builder, tables, ordered[index], kind, &emitted,
                                     inline_references))
        {
            return false;
        }
    }
    if (!inline_references) {
        for (size_t index = 0U; index < table_count; index += 1U) {
            if (!sl_orm_append_deferred_fk_sql(&builder, tables, ordered[index], kind,
                                               &deferred_fk_emitted))
            {
                return false;
            }
        }
    }
    if (!sl_status_is_ok(sl_string_builder_append_char(&builder, '\n')) ||
        !sl_status_is_ok(sl_string_builder_view_with_nul(&builder, &view)))
    {
        return false;
    }
    *out = sl_cli_span_from_str(view);
    return emitted;
}

static bool sl_orm_plan_has_static_tables(yyjson_doc* doc)
{
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* orm = root == NULL ? NULL : yyjson_obj_get(root, "orm");
    yyjson_val* tables = orm == NULL ? NULL : yyjson_obj_get(orm, "tables");
    return tables != NULL && yyjson_is_arr(tables) && yyjson_arr_size(tables) > 0U;
}

static bool sl_orm_migration_directory(const char* base_path, const SlCliDbMigrationConfig* config,
                                       char* directory, size_t capacity)
{
    char relative_dir[SL_RUN_PATH_MAX_BYTES];
    return base_path != NULL && config != NULL &&
           sl_db_migration_path_dir(config->path, relative_dir, sizeof(relative_dir)) &&
           sl_cli_join_leaf(directory, capacity, base_path, relative_dir);
}

static uint32_t sl_orm_existing_migration_max(const char* directory)
{
    unsigned char arena_storage[SL_CLI_DIAG_ARENA_BYTES];
    SlArena arena = {0};
    SlFsDirectoryList list = {0};
    uint32_t max = 0U;

    if (!sl_status_is_ok(sl_arena_init(&arena, arena_storage, sizeof(arena_storage))) ||
        !sl_status_is_ok(sl_fs_list_directory(&arena, sl_str_from_cstr(directory), &list, NULL)))
    {
        return 0U;
    }
    for (size_t index = 0U; index < list.count; index += 1U) {
        uint32_t value = 0U;
        size_t cursor = 0U;
        SlStr name = sl_owned_str_as_view(list.entries[index].name);
        if (list.entries[index].kind != SL_FS_NODE_FILE ||
            !sl_str_ends_with(name, sl_str_from_cstr(".sql")))
        {
            continue;
        }
        while (cursor < name.length && name.ptr[cursor] >= '0' && name.ptr[cursor] <= '9') {
            uint32_t digit = (uint32_t)(name.ptr[cursor] - '0');
            if (value > (UINT32_MAX - digit) / 10U) {
                return UINT32_MAX;
            }
            value = (value * 10U) + digit;
            cursor += 1U;
        }
        if (cursor > 0U && value > max) {
            max = value;
        }
    }
    return max;
}

static bool sl_orm_slugify(char* buffer, size_t capacity, const char* name)
{
    size_t out = 0U;
    bool pending_separator = false;
    bool previous_alnum = false;

    if (buffer == NULL || capacity == 0U || name == NULL || name[0] == '\0') {
        return false;
    }
    for (const char* cursor = name; *cursor != '\0'; cursor += 1) {
        char ch = *cursor;
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            bool uppercase_boundary = ch >= 'A' && ch <= 'Z' && previous_alnum;
            if ((pending_separator || uppercase_boundary) && out > 0U && out + 1U < capacity &&
                buffer[out - 1U] != '_')
            {
                buffer[out++] = '_';
            }
            pending_separator = false;
            if (out + 1U >= capacity) {
                return false;
            }
            buffer[out++] = (char)(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
            previous_alnum = true;
        }
        else {
            pending_separator = true;
            previous_alnum = false;
        }
    }
    if (out == 0U) {
        return sl_cli_copy_cstr(buffer, capacity, "migration");
    }
    buffer[out] = '\0';
    return true;
}

static int sl_orm_write_migration_file(const SlCliOptions* options,
                                       const SlCliDbMigrationConfig* config,
                                       const char* base_path, SlCliSpan script)
{
    char directory[SL_RUN_PATH_MAX_BYTES];
    char slug[128U];
    char file_name[SL_RUN_PATH_MAX_BYTES];
    char file_path[SL_RUN_PATH_MAX_BYTES];
    uint32_t next = 0U;

    if (!sl_orm_migration_directory(base_path, config, directory, sizeof(directory))) {
        sl_cli_write_cstr(stderr, "sloppy orm: migration path is unsupported or too long\n");
        return 1;
    }
    if (!sl_status_is_ok(sl_fs_create_directory(sl_str_from_cstr(directory), true, NULL))) {
        sl_cli_write_error_with_value("sloppy orm: failed to create migration directory: ",
                                      directory, "\n");
        return 1;
    }
    if (options->create_name == NULL || options->create_name[0] == '\0') {
        sl_cli_write_cstr(stderr, "sloppy orm: migration name is required\n");
        return 1;
    }
    if (!sl_orm_slugify(slug, sizeof(slug), options->create_name)) {
        sl_cli_write_cstr(stderr, "sloppy orm: migration name is too long\n");
        return 1;
    }
    next = sl_orm_existing_migration_max(directory);
    if (next == UINT32_MAX) {
        sl_cli_write_cstr(stderr, "sloppy orm: migration number overflow\n");
        return 1;
    }
    next += 1U;
    {
        SlStringBuilder file_name_builder = {0};
        SlStr file_name_view = {0};

        if (!sl_status_is_ok(sl_string_builder_init_fixed(&file_name_builder, file_name,
                                                          sizeof(file_name))) ||
            !sl_orm_append_min_width_u32(&file_name_builder, next, 4U) ||
            !sl_status_is_ok(sl_string_builder_append_char(&file_name_builder, '_')) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(&file_name_builder, slug)) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(&file_name_builder, ".sql")) ||
            !sl_status_is_ok(sl_string_builder_view_with_nul(&file_name_builder,
                                                             &file_name_view)) ||
            file_name_view.ptr != file_name)
        {
            sl_cli_write_cstr(stderr, "sloppy orm: migration file path is too long\n");
            return 1;
        }
    }
    if (!sl_cli_join_leaf(file_path, sizeof(file_path), directory, file_name)) {
        sl_cli_write_cstr(stderr, "sloppy orm: migration file path is too long\n");
        return 1;
    }
    if (sl_cli_path_is_file(file_path)) {
        sl_cli_write_error_with_value("sloppy orm: migration file already exists: ", file_path,
                                      "\n");
        return 1;
    }
    if (!sl_status_is_ok(sl_fs_write_file(sl_str_from_cstr(file_path),
                                          sl_bytes_from_parts(
                                              (const unsigned char*)script.ptr, script.length),
                                          false, NULL)))
    {
        sl_cli_write_error_with_value("sloppy orm: failed to write migration: ", file_path, "\n");
        return 1;
    }
    if (options->format == SL_CLI_FORMAT_JSON) {
        printf("{\"provider\":");
        sl_cli_json_escape(stdout, sl_cli_span_cstr(options->db_provider));
        printf(",\"path\":");
        sl_cli_json_escape(stdout, sl_cli_span_cstr(file_path));
        printf("}\n");
    }
    else {
        printf("created %s\n", file_path);
    }
    return 0;
}

static int sl_cli_command_orm(const SlCliOptions* options)
{
    char plan_path[SL_RUN_PATH_MAX_BYTES];
    char base_path[SL_RUN_PATH_MAX_BYTES];
    bool package_target = false;
    SlCliMetadata* metadata = sl_cli_scratch_metadata();
    unsigned char* json_storage = sl_cli_scratch_json_storage();
    static unsigned char script_arena_storage[SL_CLI_ARENA_BYTES * 4U];
    SlArena script_arena = {0};
    yyjson_doc* doc = NULL;
    SlCliDbMigrationConfig migration_config = {0};
    const SlCliProvider* provider = NULL;
    SlCliDbProviderKind provider_kind = SL_CLI_DB_PROVIDER_UNSUPPORTED;
    SlCliSpan script = {0};
    int result = 0;

    if (options == NULL || options->db_action == NULL) {
        return 1;
    }
    if (strcmp(options->db_action, "status") == 0 || strcmp(options->db_action, "migrate") == 0) {
        return sl_cli_command_db(options);
    }
    if (!sl_db_resolve_target(options, plan_path, sizeof(plan_path), base_path, sizeof(base_path),
                              &package_target))
    {
        sl_cli_write_cstr(
            stderr, "sloppy orm: target must be an artifacts directory or package directory\n");
        return 1;
    }
    if (sl_cli_load_metadata(plan_path, json_storage, NULL, false, &doc, metadata) != 0) {
        return 1;
    }
    provider = sl_db_find_provider(metadata, options->db_provider);
    if (provider == NULL) {
        sl_cli_write_error_with_value("sloppy orm: provider not found in Plan: ",
                                      options->db_provider, "\n");
        yyjson_doc_free(doc);
        return 1;
    }
    if (!sl_db_load_migration_config(options, base_path, package_target, &migration_config)) {
        yyjson_doc_free(doc);
        return 1;
    }
    provider_kind = sl_db_provider_kind(provider, &migration_config);
    if (provider_kind == SL_CLI_DB_PROVIDER_UNSUPPORTED ||
        strcmp(migration_config.provider_kind, sl_db_provider_kind_name(provider_kind)) != 0)
    {
        sl_cli_write_cstr(stderr,
                          "sloppy orm: migration config provider does not match Plan provider\n");
        yyjson_doc_free(doc);
        return 1;
    }
    if (!sl_orm_plan_has_static_tables(doc)) {
        sl_cli_write_cstr(stderr, "sloppy orm: Plan has no static ORM table metadata to script\n");
        yyjson_doc_free(doc);
        return 1;
    }
    if (!sl_status_is_ok(sl_arena_init(&script_arena, script_arena_storage,
                                       sizeof(script_arena_storage))) ||
        !sl_orm_build_migration_script(&script_arena, doc, provider_kind, &script))
    {
        sl_cli_write_cstr(
            stderr,
            "sloppy orm: migration script is too large or contains unsupported ORM metadata\n");
        yyjson_doc_free(doc);
        return 1;
    }
    if (strcmp(options->db_action, "script") == 0) {
        if (options->format == SL_CLI_FORMAT_JSON) {
            printf("{\"provider\":");
            sl_cli_json_escape(stdout, sl_cli_span_cstr(options->db_provider));
            printf(",\"sql\":");
            sl_cli_json_escape(stdout, script);
            printf("}\n");
        }
        else {
            fwrite(script.ptr, 1U, script.length, stdout);
        }
    }
    else if (strcmp(options->db_action, "add") == 0) {
        result = sl_orm_write_migration_file(options, &migration_config, base_path, script);
    }
    else {
        sl_cli_write_cstr(stderr, "sloppy orm: expected migration add, apply, script, or status\n");
        result = 1;
    }
    yyjson_doc_free(doc);
    return result;
}
