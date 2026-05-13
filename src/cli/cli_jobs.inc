/*
 * src/cli/cli_jobs.inc
 *
 * Private implementation of `sloppy jobs`.
 */
static SlSqliteParam sl_jobs_text_param(const char* text)
{
    SlSqliteParam param = {0};
    param.kind = SL_SQLITE_PARAM_TEXT;
    param.value.text = sl_str_from_cstr(text == NULL ? "" : text);
    return param;
}

static int sl_jobs_open(const SlCliOptions* options, SlArena* arena, SlSqliteConnection* out)
{
    SlSqliteOpenOptions open_options = {0};
    SlDiag diag = {0};
    SlStatus status;

    open_options.path = sl_str_from_cstr(options->jobs_database);
    open_options.access = SL_SQLITE_ACCESS_READWRITE;
    status = sl_sqlite_open(arena, &open_options, out, &diag);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_error_with_value("sloppy jobs: failed to open database: ",
                                      options->jobs_database, "\n");
        return 1;
    }
    return 0;
}

static int sl_jobs_exec(SlArena* arena, SlSqliteConnection* db, const char* sql,
                        const SlSqliteParam* params, size_t param_count)
{
    SlDiag diag = {0};
    SlSqliteExecResult result = {0};
    SlStatus status =
        sl_sqlite_exec(arena, db, sl_str_from_cstr(sql), params, param_count, &result, &diag);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_error_with_value("sloppy jobs: SQL failed: ", sql, "\n");
        return 1;
    }
    return 0;
}

static int sl_jobs_init_schema(SlArena* arena, SlSqliteConnection* db)
{
    static const char* statements[] = {
        "create table if not exists sloppy_job_schema (version integer not null, updated_at text not null)",
        "create table if not exists sloppy_jobs (id text primary key, name text not null, queue text not null, status text not null, payload_json text not null, payload_schema text, priority integer not null, run_at text not null, created_at text not null, updated_at text not null, locked_by text, locked_until text, attempt_count integer not null, max_attempts integer not null, retry_policy_json text not null, next_retry_at text, last_error_code text, last_error_message text, diagnostic_id text, correlation_id text, idempotency_key text, timeout_ms integer, metadata_json text not null)",
        "create table if not exists sloppy_job_attempts (id text primary key, job_id text not null, worker_id text, attempt_number integer not null, started_at text not null, finished_at text, status text not null, duration_ms integer, error_code text, error_message text, diagnostic_id text)",
        "create table if not exists sloppy_recurring_jobs (id text primary key, name text not null, job_name text not null, queue text not null, cron text not null, timezone text not null, payload_json text not null, enabled integer not null, misfire_policy text not null, last_run_at text, next_run_at text not null, created_at text not null, updated_at text not null, metadata_json text not null)",
        "create table if not exists sloppy_job_workers (id text primary key, worker_name text not null, host text, pid integer, queues text not null, started_at text not null, last_heartbeat_at text not null, status text not null)",
        "create table if not exists sloppy_job_locks (name text primary key, owner text not null, locked_until text not null, updated_at text not null)",
        "create table if not exists sloppy_job_events (id text primary key, job_id text, event_type text not null, from_status text, to_status text, created_at text not null, worker_id text, message text, data_json text not null)",
        "create index if not exists idx_sloppy_jobs_claim on sloppy_jobs (queue, status, run_at, locked_until, priority)",
        "create index if not exists idx_sloppy_jobs_status on sloppy_jobs (status, updated_at)",
        "create index if not exists idx_sloppy_job_attempts_job on sloppy_job_attempts (job_id, attempt_number)",
        "create index if not exists idx_sloppy_job_events_job on sloppy_job_events (job_id, created_at)",
        "create index if not exists idx_sloppy_recurring_due on sloppy_recurring_jobs (enabled, next_run_at)",
        "create index if not exists idx_sloppy_workers_status on sloppy_job_workers (status, last_heartbeat_at)",
        "create unique index if not exists ux_sloppy_jobs_idempotency on sloppy_jobs (idempotency_key) where idempotency_key is not null",
        "create unique index if not exists ux_sloppy_recurring_name on sloppy_recurring_jobs (name)",
        "insert into sloppy_job_schema (version, updated_at) select 1, datetime('now') where not exists (select 1 from sloppy_job_schema)"};

    for (size_t index = 0U; index < sizeof(statements) / sizeof(statements[0]); index += 1U) {
        if (sl_jobs_exec(arena, db, statements[index], NULL, 0U) != 0) {
            return 1;
        }
    }
    return 0;
}

static void sl_jobs_print_value(const SlSqliteValue* value)
{
    if (value == NULL || value->kind == SL_SQLITE_VALUE_NULL) {
        printf("-");
    }
    else if (value->kind == SL_SQLITE_VALUE_INTEGER) {
        printf("%lld", (long long)value->value.integer);
    }
    else if (value->kind == SL_SQLITE_VALUE_FLOAT) {
        printf("%f", value->value.number);
    }
    else if (value->kind == SL_SQLITE_VALUE_TEXT) {
        printf("%.*s", (int)value->value.text.length, value->value.text.ptr);
    }
    else {
        printf("<blob>");
    }
}

static int sl_jobs_query_print(SlArena* arena, SlSqliteConnection* db, const char* sql,
                               const SlSqliteParam* params, size_t param_count, size_t max_rows)
{
    SlDiag diag = {0};
    SlSqliteResult result = {0};
    SlSqliteQueryOptions options = {.max_rows = max_rows};
    SlStatus status = sl_sqlite_query(arena, db, sl_str_from_cstr(sql), params, param_count,
                                      &options, &result, &diag);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_error_with_value("sloppy jobs: query failed: ", sql, "\n");
        return 1;
    }
    for (size_t row_index = 0U; row_index < result.row_count; row_index += 1U) {
        for (size_t column = 0U; column < result.column_count; column += 1U) {
            if (column != 0U) {
                printf("\t");
            }
            sl_jobs_print_value(&result.rows[row_index].values[column]);
        }
        printf("\n");
    }
    return 0;
}

static int sl_jobs_status(SlArena* arena, SlSqliteConnection* db)
{
    return sl_jobs_query_print(arena, db,
                               "select status, count(*) from sloppy_jobs group by status order by status",
                               NULL, 0U, 16U);
}

static int sl_jobs_update_job(SlArena* arena, SlSqliteConnection* db, const char* id,
                              const char* status, const char* allowed)
{
    SlSqliteParam params[3];
    char sql[512];
    SlStringBuilder builder = {0};
    SlStr sql_view = {0};
    if (!sl_status_is_ok(sl_string_builder_init_fixed(&builder, sql, sizeof(sql))) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(
            &builder,
            "update sloppy_jobs set status = ?, locked_by = null, locked_until = null, updated_at = datetime('now') where id = ? and status in (")) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&builder, allowed)) ||
        !sl_status_is_ok(sl_string_builder_append_char(&builder, ')')) ||
        !sl_status_is_ok(sl_string_builder_view_with_nul(&builder, &sql_view)))
    {
        sl_cli_write_cstr(stderr, "sloppy jobs: update SQL is too long\n");
        return 1;
    }
    params[0] = sl_jobs_text_param(status);
    params[1] = sl_jobs_text_param(id);
    if (sl_jobs_exec(arena, db, sql, params, 2U) != 0) {
        return 1;
    }
    printf("%s\t%s\n", id, status);
    return 0;
}

static int sl_jobs_recurring_trigger(SlArena* arena, SlSqliteConnection* db, const char* name)
{
    SlSqliteParam select_params[1] = {sl_jobs_text_param(name)};
    SlSqliteQueryOneResult row = {0};
    SlDiag diag = {0};
    SlStatus status = sl_sqlite_query_one(
        arena, db,
        sl_str_from_cstr("select job_name, queue, payload_json, metadata_json from sloppy_recurring_jobs where name = ?"),
        select_params, 1U, &row, &diag);
    char job_id[128];
    char timestamp_storage[32];
    SlStr timestamp = {0};
    SlStringBuilder id_builder = {0};
    SlStr id_view = {0};
    SlSqliteParam params[23] = {0};

    if (!sl_status_is_ok(status) || !row.found) {
        sl_cli_write_error_with_value("sloppy jobs recurring trigger: unknown recurring job: ", name,
                                      "\n");
        return 1;
    }
    if (!sl_status_is_ok(sl_string_format_i64(timestamp_storage, sizeof(timestamp_storage),
                                              (int64_t)time(NULL), &timestamp)) ||
        !sl_status_is_ok(sl_string_builder_init_fixed(&id_builder, job_id, sizeof(job_id))) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&id_builder, "cli_job_")) ||
        !sl_status_is_ok(sl_string_builder_append_str(&id_builder, timestamp)) ||
        !sl_status_is_ok(sl_string_builder_view_with_nul(&id_builder, &id_view)))
    {
        sl_cli_write_cstr(stderr, "sloppy jobs recurring trigger: failed to format job id\n");
        return 1;
    }
    params[0] = sl_jobs_text_param(job_id);
    params[1] = row.values[0].kind == SL_SQLITE_VALUE_TEXT
                    ? (SlSqliteParam){.kind = SL_SQLITE_PARAM_TEXT, .value.text = row.values[0].value.text}
                    : sl_jobs_text_param("");
    params[2] = row.values[1].kind == SL_SQLITE_VALUE_TEXT
                    ? (SlSqliteParam){.kind = SL_SQLITE_PARAM_TEXT, .value.text = row.values[1].value.text}
                    : sl_jobs_text_param("default");
    params[3] = sl_jobs_text_param("queued");
    params[4] = row.values[2].kind == SL_SQLITE_VALUE_TEXT
                    ? (SlSqliteParam){.kind = SL_SQLITE_PARAM_TEXT, .value.text = row.values[2].value.text}
                    : sl_jobs_text_param("null");
    params[5] = sl_jobs_text_param("");
    params[6].kind = SL_SQLITE_PARAM_INTEGER;
    params[6].value.integer = 0;
    params[7] = sl_jobs_text_param("");
    params[8] = sl_jobs_text_param("");
    params[9] = sl_jobs_text_param("");
    params[10].kind = SL_SQLITE_PARAM_NULL;
    params[11].kind = SL_SQLITE_PARAM_NULL;
    params[12].kind = SL_SQLITE_PARAM_INTEGER;
    params[12].value.integer = 0;
    params[13].kind = SL_SQLITE_PARAM_INTEGER;
    params[13].value.integer = 1;
    params[14] = sl_jobs_text_param("{\"maxAttempts\":1,\"backoff\":\"none\",\"initialDelayMs\":0,\"maxDelayMs\":0,\"jitter\":false}");
    for (size_t index = 15U; index < 22U; index += 1U) {
        params[index].kind = SL_SQLITE_PARAM_NULL;
    }
    params[22] = sl_jobs_text_param("{\"triggered\":true}");
    return sl_jobs_exec(arena, db,
                        "insert into sloppy_jobs (id, name, queue, status, payload_json, payload_schema, priority, run_at, created_at, updated_at, locked_by, locked_until, attempt_count, max_attempts, retry_policy_json, next_retry_at, last_error_code, last_error_message, diagnostic_id, correlation_id, idempotency_key, timeout_ms, metadata_json) values (?, ?, ?, ?, ?, ?, ?, datetime('now'), datetime('now'), datetime('now'), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                        params, 23U);
}

static int sl_cli_command_jobs(const SlCliOptions* options)
{
    unsigned char arena_storage[SL_CLI_ARENA_BYTES];
    SlArena arena = {0};
    SlSqliteConnection db = {0};
    int result = 0;

    if (!sl_status_is_ok(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)))) {
        sl_cli_write_cstr(stderr, "sloppy jobs: failed to initialize arena\n");
        return 1;
    }
    if (sl_jobs_open(options, &arena, &db) != 0) {
        return 1;
    }
    if (strcmp(options->jobs_action, "init") == 0) {
        result = sl_jobs_init_schema(&arena, &db);
        if (result == 0) {
            printf("jobs schema initialized\t%s\n", options->jobs_database);
        }
    }
    else if (strcmp(options->jobs_action, "status") == 0) {
        result = sl_jobs_status(&arena, &db);
    }
    else if (strcmp(options->jobs_action, "list") == 0) {
        result = sl_jobs_query_print(&arena, &db,
                                     "select id, name, queue, status, run_at from sloppy_jobs order by created_at desc limit 50",
                                     NULL, 0U, 50U);
    }
    else if (strcmp(options->jobs_action, "show") == 0) {
        SlSqliteParam params[1] = {sl_jobs_text_param(options->jobs_target)};
        result = sl_jobs_query_print(&arena, &db,
                                     "select id, name, queue, status, run_at, attempt_count, last_error_code, last_error_message from sloppy_jobs where id = ?",
                                     params, 1U, 1U);
    }
    else if (strcmp(options->jobs_action, "retry") == 0) {
        result = sl_jobs_update_job(&arena, &db, options->jobs_target, "queued",
                                    "'failed','dead'");
    }
    else if (strcmp(options->jobs_action, "cancel") == 0) {
        result = sl_jobs_update_job(&arena, &db, options->jobs_target, "cancelled",
                                    "'queued','scheduled','processing','retrying'");
    }
    else if (strcmp(options->jobs_action, "delete") == 0) {
        result = sl_jobs_update_job(&arena, &db, options->jobs_target, "deleted",
                                    "'queued','dead','succeeded','cancelled'");
    }
    else if (strcmp(options->jobs_action, "workers") == 0) {
        result = sl_jobs_query_print(&arena, &db,
                                     "select id, worker_name, status, last_heartbeat_at from sloppy_job_workers order by started_at desc limit 50",
                                     NULL, 0U, 50U);
    }
    else if (strcmp(options->jobs_action, "locks") == 0) {
        result = sl_jobs_query_print(&arena, &db,
                                     "select name, owner, locked_until from sloppy_job_locks order by name limit 50",
                                     NULL, 0U, 50U);
    }
    else if (strcmp(options->jobs_action, "recurring") == 0) {
        if (strcmp(options->jobs_subaction, "list") == 0) {
            result = sl_jobs_query_print(&arena, &db,
                                         "select name, job_name, enabled, next_run_at from sloppy_recurring_jobs order by name limit 50",
                                         NULL, 0U, 50U);
        }
        else if (strcmp(options->jobs_subaction, "pause") == 0 ||
                 strcmp(options->jobs_subaction, "resume") == 0)
        {
            SlSqliteParam params[2];
            params[0].kind = SL_SQLITE_PARAM_INTEGER;
            params[0].value.integer = strcmp(options->jobs_subaction, "resume") == 0 ? 1 : 0;
            params[1] = sl_jobs_text_param(options->jobs_target);
            result = sl_jobs_exec(&arena, &db,
                                  "update sloppy_recurring_jobs set enabled = ?, updated_at = datetime('now') where name = ?",
                                  params, 2U);
            if (result == 0) {
                printf("%s\t%s\n", options->jobs_target, options->jobs_subaction);
            }
        }
        else if (strcmp(options->jobs_subaction, "trigger") == 0) {
            result = sl_jobs_recurring_trigger(&arena, &db, options->jobs_target);
            if (result == 0) {
                printf("%s\ttriggered\n", options->jobs_target);
            }
        }
        else {
            sl_cli_write_cstr(stderr,
                              "sloppy jobs recurring: expected list, pause, resume, or trigger\n");
            result = 1;
        }
    }
    else {
        sl_cli_write_error_with_value("sloppy jobs: unknown action: ", options->jobs_action, "\n");
        result = 1;
    }
    sl_sqlite_close(&db);
    return result;
}
