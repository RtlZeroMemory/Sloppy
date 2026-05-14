/*
 * src/cli/cli_run.inc
 *
 * Private implementation of the development `sloppy run` command.
 * Included only by src/main.c to preserve existing static helper visibility.
 */
typedef struct SlRunApp
{
    unsigned char plan_json_storage[SL_RUN_ARTIFACT_FILE_MAX_BYTES];
    unsigned char metadata_json_storage[SL_RUN_ARTIFACT_FILE_MAX_BYTES];
    unsigned char package_manifest_json_storage[SL_RUN_ARTIFACT_FILE_MAX_BYTES];
    unsigned char route_artifact_storage[SL_RUN_ARTIFACT_FILE_MAX_BYTES];
    unsigned char openapi_artifact_storage[SL_RUN_ARTIFACT_FILE_MAX_BYTES];
    unsigned char bootstrap_js_storage[SL_RUN_STDLIB_FILE_MAX_BYTES];
    unsigned char app_js_storage[SL_RUN_ARTIFACT_FILE_MAX_BYTES];
    unsigned char file_read_storage[SL_RUN_FILE_READ_ARENA_BYTES];
    unsigned char plan_arena_storage[SL_RUN_PLAN_ARENA_BYTES];
    unsigned char route_arena_storage[SL_RUN_ROUTE_ARENA_BYTES];
    unsigned char engine_arena_storage[SL_RUN_ARENA_BYTES];
    unsigned char logging_arena_storage[SL_RUN_LOGGING_ARENA_BYTES];
    SlScopeCleanup app_cleanups[SL_RUN_APP_SCOPE_MAX_CLEANUPS];
    SlArena plan_arena;
    SlArena route_arena;
    SlArena engine_arena;
    SlArena logging_arena;
    SlPlan plan;
    SlInternTable plan_metadata_interns;
    SlRuntimeFeatureSet runtime_features;
    SlCapabilityRegistry capability_registry;
    SlBytes app_js_bytes;
    SlBytes source_map_bytes;
    SlBytes plan_json_bytes;
    SlBytes route_artifact_bytes;
    SlBytes openapi_artifact_bytes;
    SlEngine* engine;
    SlLogRuntime* logging;
    SlLogSink* logging_console_sink;
    SlLogSink* logging_file_sink;
    SlFfiLibraryOverride ffi_library_overrides[SL_CLI_MAX_FFI_LIBRARIES];
    char ffi_library_override_names[SL_CLI_MAX_FFI_LIBRARIES]
                                   [SL_SLOPPYRC_FFI_LIBRARY_NAME_MAX_BYTES];
    char ffi_library_override_paths[SL_CLI_MAX_FFI_LIBRARIES][SL_RUN_PATH_MAX_BYTES];
    size_t ffi_library_override_count;
    SlAppLifecycle lifecycle;
    SlHttpRouteTable route_table;
    char artifacts_path[SL_RUN_PATH_MAX_BYTES];
    char openapi_docs_path[SL_RUN_PATH_MAX_BYTES];
    char config_host[SL_RUN_CONFIG_HOST_MAX_BYTES];
    char config_tls_certificate_path[SL_RUN_PATH_MAX_BYTES];
    char config_tls_private_key_path[SL_RUN_PATH_MAX_BYTES];
    char config_logging_file_path[SL_RUN_PATH_MAX_BYTES];
    SlLogLevel config_logging_minimum_level;
    SlLogConsoleFormat config_logging_console_format;
    uint16_t config_port;
    uint64_t config_logging_queue_capacity;
    uint64_t config_max_connections;
    uint64_t config_max_active_requests;
    uint64_t config_connection_capacity;
    uint64_t config_backlog;
    uint64_t config_max_request_head_bytes;
    uint64_t config_max_request_body_bytes;
    uint64_t config_max_request_wire_body_bytes;
    uint64_t config_request_arena_bytes;
    uint64_t config_max_response_bytes;
    uint64_t config_max_pending_write_bytes;
    uint64_t config_http2_max_streams;
    uint64_t config_max_dispatches_per_tick;
    uint64_t config_keep_alive_idle_timeout_ms;
    uint64_t config_max_requests_per_connection;
    uint64_t config_request_timeout_ms;
    uint64_t next_request_id;
    bool config_tls_enabled;
    bool config_keep_alive_enabled;
    bool config_logging_console_enabled;
    bool config_has_host;
    bool config_has_port;
    bool config_has_max_connections;
    bool config_has_max_active_requests;
    bool config_has_connection_capacity;
    bool config_has_backlog;
    bool config_has_max_request_head_bytes;
    bool config_has_max_request_body_bytes;
    bool config_has_max_request_wire_body_bytes;
    bool config_has_request_arena_bytes;
    bool config_has_max_response_bytes;
    bool config_has_max_pending_write_bytes;
    bool config_has_http2_max_streams;
    bool config_has_dispatch_on_event_loop;
    bool config_dispatch_on_event_loop;
    bool config_has_max_dispatches_per_tick;
    bool config_has_keep_alive_enabled;
    bool config_has_keep_alive_idle_timeout_ms;
    bool config_has_max_requests_per_connection;
    bool config_has_request_timeout_ms;
    bool config_has_tls_enabled;
    bool config_has_tls_certificate_path;
    bool config_has_tls_private_key_path;
    bool config_has_logging_minimum_level;
    bool config_has_logging_queue_capacity;
    bool config_has_logging_console_enabled;
    bool config_has_logging_console_format;
    bool config_has_logging_file_path;
    bool has_dynamic_routes;
    bool has_openapi_docs_route;
} SlRunApp;

static volatile sig_atomic_t sl_run_stop_requested = 0;

static void sl_run_signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        sl_run_stop_requested = 1;
    }
}

static SlRunApp sl_run_command_app_storage;
static bool sl_run_diagnostics_json_enabled;

static void sl_run_reset_app_storage(SlRunApp* app)
{
    unsigned char* bytes = (unsigned char*)app;

    if (app == NULL) {
        return;
    }
    for (size_t index = 0U; index < sizeof(*app); index += 1U) {
        bytes[index] = 0U;
    }
}

static SlRunApp* sl_run_command_app(void)
{
    sl_run_reset_app_storage(&sl_run_command_app_storage);
    return &sl_run_command_app_storage;
}

static bool sl_run_copy_json_string(char* buffer, size_t capacity, yyjson_val* value);
static int sl_run_load_package_native_overrides(SlRunApp* app, const char* package_dir);

static bool sl_run_bytes_contains_str(SlBytes bytes, SlStr needle)
{
    if (bytes.ptr == NULL || needle.ptr == NULL || needle.length == 0U ||
        bytes.length < needle.length)
    {
        return false;
    }
    for (size_t index = 0U; index + needle.length <= bytes.length; index += 1U) {
        bool matched = true;
        for (size_t offset = 0U; offset < needle.length; offset += 1U) {
            if (bytes.ptr[index + offset] != (unsigned char)needle.ptr[offset]) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return true;
        }
    }
    return false;
}

static bool sl_run_copy_cstr(char* buffer, size_t capacity, const char* value)
{
    SlStringBuilder builder = {0};
    SlStr view = {0};

    if (buffer == NULL || capacity == 0U || value == NULL || value[0] == '\0') {
        return false;
    }

    return sl_status_is_ok(sl_string_builder_init_fixed(&builder, buffer, capacity)) &&
           sl_status_is_ok(sl_string_builder_append_cstr(&builder, value)) &&
           sl_status_is_ok(sl_string_builder_view_with_nul(&builder, &view)) && view.ptr == buffer;
}

static bool sl_run_copy_span_cstr(char* buffer, size_t capacity, SlCliSpan value)
{
    SlStringBuilder builder = {0};
    SlStr view = {0};
    size_t index = 0U;
    SlStatus status;

    if (buffer == NULL || capacity == 0U || value.ptr == NULL || value.length == 0U) {
        return false;
    }

    status = sl_string_builder_init_fixed(&builder, buffer, capacity);
    if (!sl_status_is_ok(status)) {
        return false;
    }
    for (index = 0U; index < value.length; index += 1U) {
        status = sl_string_builder_append_char(&builder, value.ptr[index]);
        if (!sl_status_is_ok(status)) {
            return false;
        }
    }

    status = sl_string_builder_view_with_nul(&builder, &view);
    return sl_status_is_ok(status) && view.ptr == buffer && view.length == value.length;
}

static bool sl_run_path_span_is_absolute(SlCliSpan path)
{
    char drive = '\0';

    if (path.ptr == NULL || path.length == 0U) {
        return false;
    }
    if (path.ptr[0] == '/' || path.ptr[0] == '\\') {
        return true;
    }
    if (path.length < 3U || path.ptr[1] != ':' || (path.ptr[2] != '/' && path.ptr[2] != '\\')) {
        return false;
    }
    drive = path.ptr[0];
    return (drive >= 'A' && drive <= 'Z') || (drive >= 'a' && drive <= 'z');
}

static bool sl_run_span_ends_with(SlCliSpan span, const char* suffix)
{
    return sl_str_ends_with(sl_cli_span_str(span), sl_str_from_cstr(suffix));
}

static bool sl_run_path_component_is_safe(SlCliSpan component)
{
    if (component.length == 0U) {
        return false;
    }
    if (component.length == 1U && component.ptr[0] == '.') {
        return false;
    }
    return !(component.length == 2U && component.ptr[0] == '.' && component.ptr[1] == '.');
}

static bool sl_run_relative_artifact_path_is_safe(SlCliSpan leaf)
{
    size_t index = 0U;
    size_t component_start = 0U;

    if (leaf.ptr == NULL || leaf.length == 0U || leaf.ptr[0] == '/' || leaf.ptr[0] == '\\') {
        return false;
    }

    for (index = 0U; index < leaf.length; index += 1U) {
        char ch = leaf.ptr[index];
        if (ch == ':') {
            return false;
        }
        if (ch == '/' || ch == '\\') {
            SlCliSpan component = {leaf.ptr + component_start, index - component_start};
            if (!sl_run_path_component_is_safe(component)) {
                return false;
            }
            component_start = index + 1U;
        }
    }

    return sl_run_path_component_is_safe(
        (SlCliSpan){leaf.ptr + component_start, leaf.length - component_start});
}

static bool sl_run_join_path(char* buffer, size_t capacity, const char* dir, SlCliSpan leaf)
{
    SlStringBuilder builder = {0};
    SlStr dir_str = sl_str_from_cstr(dir);
    SlStr joined = {0};
    size_t index = 0U;
    bool needs_separator = true;
    SlStatus status;

    if (buffer == NULL || capacity == 0U || dir == NULL || leaf.ptr == NULL || leaf.length == 0U) {
        return false;
    }

    if (!sl_run_relative_artifact_path_is_safe(leaf)) {
        return false;
    }

    if (dir_str.length == 0U) {
        return false;
    }

    needs_separator =
        dir_str.ptr[dir_str.length - 1U] != '/' && dir_str.ptr[dir_str.length - 1U] != '\\';

    status = sl_string_builder_init_fixed(&builder, buffer, capacity);
    if (!sl_status_is_ok(status)) {
        return false;
    }
    status = sl_string_builder_append_str(&builder, dir_str);
    if (!sl_status_is_ok(status)) {
        return false;
    }
    if (needs_separator) {
        status = sl_string_builder_append_char(&builder, '/');
        if (!sl_status_is_ok(status)) {
            return false;
        }
    }
    for (index = 0U; index < leaf.length; index += 1U) {
        char ch = leaf.ptr[index];
        if (ch == '\\') {
            ch = '/';
        }
        status = sl_string_builder_append_char(&builder, ch);
        if (!sl_status_is_ok(status)) {
            return false;
        }
    }

    status = sl_string_builder_view_with_nul(&builder, &joined);
    return sl_status_is_ok(status) && joined.ptr == buffer;
}

static SlCliSpan sl_run_str_span(SlStr str)
{
    return (SlCliSpan){str.ptr, str.length};
}

static bool sl_run_artifact_file_path(char* buffer, size_t capacity, const char* dir,
                                      const char* leaf)
{
    return sl_run_join_path(buffer, capacity, dir, sl_cli_span_cstr(leaf));
}

static int sl_run_read_file(SlRunApp* app, const char* path, unsigned char* buffer, size_t capacity,
                            SlBytes* out)
{
    if (app == NULL) {
        return 1;
    }

    return sl_read_file_with_storage(path, buffer, capacity, app->file_read_storage,
                                     sizeof(app->file_read_storage), out,
                                     "sloppy run: artifact path not found: ",
                                     "sloppy run: artifact file is empty or too large: ",
                                     "sloppy run: artifact file read failed: ");
}

static int sl_run_read_stdlib_file(const char* path, unsigned char* buffer, size_t capacity,
                                   SlBytes* out)
{
    return sl_read_file_with_messages(
        path, buffer, capacity, out,
        "sloppy run: stdlib asset missing: ", "sloppy run: stdlib asset is empty or too large: ",
        "sloppy run: stdlib asset read failed: ");
}

static bool sl_run_json_string_equals_cstr(yyjson_val* value, const char* expected)
{
    const char* text = NULL;
    size_t text_length = 0U;

    if (value == NULL || expected == NULL || !yyjson_is_str(value)) {
        return false;
    }

    text = yyjson_get_str(value);
    text_length = yyjson_get_len(value);

    return text != NULL &&
           sl_str_equal(sl_str_from_parts(text, text_length), sl_str_from_cstr(expected));
}

static bool sl_run_manifest_array_contains_asset(yyjson_val* array, const char* required_asset)
{
    yyjson_arr_iter iter;
    yyjson_val* value = NULL;

    if (array == NULL || required_asset == NULL || !yyjson_is_arr(array)) {
        return false;
    }

    yyjson_arr_iter_init(array, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        if (sl_run_json_string_equals_cstr(value, required_asset)) {
            return true;
        }
        if (yyjson_is_obj(value) &&
            sl_run_json_string_equals_cstr(yyjson_obj_get(value, "path"), required_asset))
        {
            return true;
        }
    }

    return false;
}

static bool sl_run_manifest_is_compatible(SlBytes manifest, const char* required_stdlib_version,
                                          const char* required_asset)
{
    yyjson_read_err error = {0};
    yyjson_doc* doc = NULL;
    yyjson_val* root = NULL;
    bool compatible = false;

    if (manifest.ptr == NULL || manifest.length == 0U || required_stdlib_version == NULL ||
        required_asset == NULL)
    {
        return false;
    }

    doc = yyjson_read_opts((char*)manifest.ptr, manifest.length, 0U, NULL, &error);
    if (doc == NULL) {
        return false;
    }

    root = yyjson_doc_get_root(doc);
    if (yyjson_is_obj(root) &&
        sl_run_json_string_equals_cstr(yyjson_obj_get(root, "stdlibVersion"),
                                       required_stdlib_version) &&
        (sl_run_manifest_array_contains_asset(yyjson_obj_get(root, "modules"), required_asset) ||
         sl_run_manifest_array_contains_asset(yyjson_obj_get(root, "assets"), required_asset)))
    {
        compatible = true;
    }

    yyjson_doc_free(doc);
    return compatible;
}

static bool sl_run_plan_config_parse_port(yyjson_val* value, uint16_t* out)
{
    uint64_t port = 0U;

    if (value == NULL || out == NULL || !yyjson_is_uint(value)) {
        return false;
    }

    port = yyjson_get_uint(value);
    if (port == 0U || port > UINT16_MAX) {
        return false;
    }

    *out = (uint16_t)port;
    return true;
}

static bool sl_run_plan_config_parse_u64(yyjson_val* value, uint64_t* out)
{
    if (value == NULL || out == NULL || !yyjson_is_uint(value)) {
        return false;
    }
    *out = yyjson_get_uint(value);
    return *out != 0U;
}

static bool sl_run_plan_config_parse_u64_allow_zero(yyjson_val* value, uint64_t* out)
{
    if (value == NULL || out == NULL || !yyjson_is_uint(value)) {
        return false;
    }
    *out = yyjson_get_uint(value);
    return true;
}

static bool sl_run_plan_config_u64_fits_size_t(uint64_t value)
{
    return value <= (uint64_t)SIZE_MAX;
}

static bool sl_run_plan_config_parse_positive_size(yyjson_val* value, uint64_t* out)
{
    return sl_run_plan_config_parse_u64(value, out) && sl_run_plan_config_u64_fits_size_t(*out);
}

static bool sl_run_plan_config_parse_bool(yyjson_val* value, bool* out)
{
    if (value == NULL || out == NULL || !yyjson_is_bool(value)) {
        return false;
    }
    *out = yyjson_get_bool(value);
    return true;
}

static bool sl_run_plan_config_copy_path(char* buffer, size_t capacity, const char* artifacts_path,
                                         yyjson_val* value)
{
    SlStr path = value != NULL && yyjson_is_str(value)
                     ? sl_str_from_parts(yyjson_get_str(value), yyjson_get_len(value))
                     : sl_str_empty();
    SlCliSpan path_span = {path.ptr, path.length};

    return sl_status_is_ok(sl_str_validate_no_nul(path)) &&
           (sl_run_path_span_is_absolute(path_span)
                ? sl_run_copy_span_cstr(buffer, capacity, path_span)
                : sl_run_join_path(buffer, capacity, artifacts_path, path_span));
}

static bool sl_run_plan_config_parse_log_level(yyjson_val* value, SlLogLevel* out)
{
    SlStr text = value != NULL && yyjson_is_str(value)
                     ? sl_str_from_parts(yyjson_get_str(value), yyjson_get_len(value))
                     : sl_str_empty();

    return out != NULL && sl_status_is_ok(sl_log_level_from_str(text, out));
}

static bool sl_run_plan_config_parse_log_console_format(yyjson_val* value, SlLogConsoleFormat* out)
{
    if (out == NULL || value == NULL || !yyjson_is_str(value)) {
        return false;
    }
    if (sl_run_json_string_equals_cstr(value, "pretty")) {
        *out = SL_LOG_CONSOLE_FORMAT_PRETTY;
        return true;
    }
    if (sl_run_json_string_equals_cstr(value, "jsonl")) {
        *out = SL_LOG_CONSOLE_FORMAT_JSONL;
        return true;
    }
    return false;
}

static int sl_run_apply_logging_config_metadata_entry(SlRunApp* app, yyjson_val* key,
                                                      yyjson_val* value)
{
    if (sl_run_json_string_equals_cstr(key, "Sloppy:Logging:MinimumLevel")) {
        if (!sl_run_plan_config_parse_log_level(value, &app->config_logging_minimum_level)) {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Logging:MinimumLevel must be a log level string\n");
            return 1;
        }
        app->config_has_logging_minimum_level = true;
        return 0;
    }
    if (sl_run_json_string_equals_cstr(key, "Sloppy:Logging:QueueCapacity") ||
        sl_run_json_string_equals_cstr(key, "Sloppy:Logging:queueCapacity"))
    {
        if (!sl_run_plan_config_parse_u64(value, &app->config_logging_queue_capacity) ||
            !sl_run_plan_config_u64_fits_size_t(app->config_logging_queue_capacity))
        {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Logging:QueueCapacity must be a positive integer\n");
            return 1;
        }
        app->config_has_logging_queue_capacity = true;
        return 0;
    }
    if (sl_run_json_string_equals_cstr(key, "Sloppy:Logging:Console:Enabled")) {
        if (!sl_run_plan_config_parse_bool(value, &app->config_logging_console_enabled)) {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Logging:Console:Enabled must be a boolean\n");
            return 1;
        }
        app->config_has_logging_console_enabled = true;
        return 0;
    }
    if (sl_run_json_string_equals_cstr(key, "Sloppy:Logging:Console:Format")) {
        if (!sl_run_plan_config_parse_log_console_format(value,
                                                         &app->config_logging_console_format))
        {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Logging:Console:Format must be pretty or jsonl\n");
            return 1;
        }
        app->config_has_logging_console_format = true;
        return 0;
    }
    if (sl_run_json_string_equals_cstr(key, "Sloppy:Logging:File:Path")) {
        if (!sl_run_plan_config_copy_path(app->config_logging_file_path,
                                          sizeof(app->config_logging_file_path),
                                          app->artifacts_path, value))
        {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Logging:File:Path must be a safe path string\n");
            return 1;
        }
        app->config_has_logging_file_path = true;
        return 0;
    }
    if (sl_run_json_string_equals_cstr(key, "Sloppy:Logging:File:Format")) {
        if (!sl_run_json_string_equals_cstr(value, "jsonl")) {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Logging:File:Format must be jsonl\n");
            return 1;
        }
        return 0;
    }
    return -1;
}

static int sl_run_apply_tls_config_metadata_entry(SlRunApp* app, yyjson_val* key, yyjson_val* value)
{
    if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:Tls:Enabled")) {
        if (!sl_run_plan_config_parse_bool(value, &app->config_tls_enabled)) {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Server:Tls:Enabled must be a boolean\n");
            return 1;
        }
        app->config_has_tls_enabled = true;
        return 0;
    }
    if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:Tls:CertificatePath")) {
        SlStr path = value != NULL && yyjson_is_str(value)
                         ? sl_str_from_parts(yyjson_get_str(value), yyjson_get_len(value))
                         : sl_str_empty();
        SlCliSpan path_span = {path.ptr, path.length};
        if (!sl_status_is_ok(sl_str_validate_no_nul(path)) ||
            !(sl_run_path_span_is_absolute(path_span)
                  ? sl_run_copy_span_cstr(app->config_tls_certificate_path,
                                          sizeof(app->config_tls_certificate_path), path_span)
                  : sl_run_join_path(app->config_tls_certificate_path,
                                     sizeof(app->config_tls_certificate_path), app->artifacts_path,
                                     path_span)))
        {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Server:Tls:CertificatePath must be a safe path "
                                      "string\n");
            return 1;
        }
        app->config_has_tls_certificate_path = true;
        return 0;
    }
    if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:Tls:PrivateKeyPath")) {
        SlStr path = value != NULL && yyjson_is_str(value)
                         ? sl_str_from_parts(yyjson_get_str(value), yyjson_get_len(value))
                         : sl_str_empty();
        SlCliSpan path_span = {path.ptr, path.length};
        if (!sl_status_is_ok(sl_str_validate_no_nul(path)) ||
            !(sl_run_path_span_is_absolute(path_span)
                  ? sl_run_copy_span_cstr(app->config_tls_private_key_path,
                                          sizeof(app->config_tls_private_key_path), path_span)
                  : sl_run_join_path(app->config_tls_private_key_path,
                                     sizeof(app->config_tls_private_key_path), app->artifacts_path,
                                     path_span)))
        {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Server:Tls:PrivateKeyPath must be a safe path "
                                      "string\n");
            return 1;
        }
        app->config_has_tls_private_key_path = true;
        return 0;
    }
    return -1;
}

static int sl_run_apply_config_metadata_entry(SlRunApp* app, yyjson_val* key, yyjson_val* value)
{
    int tls_result = 0;
    int logging_result = 0;

    if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:Host")) {
        if (!sl_run_copy_json_string(app->config_host, sizeof(app->config_host), value) ||
            app->config_host[0] == '\0')
        {
            sl_cli_write_cstr(stderr,
                              "sloppy run: app.plan.json Sloppy:Server:Host must be a string\n");
            return 1;
        }
        app->config_has_host = true;
    }
    else if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:Port")) {
        if (!sl_run_plan_config_parse_port(value, &app->config_port)) {
            sl_cli_write_cstr(
                stderr, "sloppy run: app.plan.json Sloppy:Server:Port must be an integer port\n");
            return 1;
        }
        app->config_has_port = true;
    }
    else if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:MaxConnections")) {
        if (!sl_run_plan_config_parse_positive_size(value, &app->config_max_connections)) {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Server:MaxConnections must be a positive integer\n");
            return 1;
        }
        app->config_has_max_connections = true;
    }
    else if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:MaxActiveRequests")) {
        if (!sl_run_plan_config_parse_positive_size(value, &app->config_max_active_requests)) {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Server:MaxActiveRequests must be a positive "
                                      "integer\n");
            return 1;
        }
        app->config_has_max_active_requests = true;
    }
    else if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:ConnectionCapacity")) {
        if (!sl_run_plan_config_parse_positive_size(value, &app->config_connection_capacity)) {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Server:ConnectionCapacity must be a positive "
                                      "integer\n");
            return 1;
        }
        app->config_has_connection_capacity = true;
    }
    else if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:Backlog")) {
        if (!sl_run_plan_config_parse_u64(value, &app->config_backlog) ||
            app->config_backlog == 0U || app->config_backlog > (uint64_t)INT_MAX)
        {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Server:Backlog must be a positive integer that "
                                      "fits int\n");
            return 1;
        }
        app->config_has_backlog = true;
    }
    else if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:MaxRequestHeadBytes")) {
        if (!sl_run_plan_config_parse_positive_size(value, &app->config_max_request_head_bytes)) {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Server:MaxRequestHeadBytes must be a positive "
                                      "integer\n");
            return 1;
        }
        app->config_has_max_request_head_bytes = true;
    }
    else if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:MaxRequestBodyBytes")) {
        if (!sl_run_plan_config_parse_positive_size(value, &app->config_max_request_body_bytes)) {
            sl_cli_write_cstr(stderr,
                              "sloppy run: app.plan.json "
                              "Sloppy:Server:MaxRequestBodyBytes must be a positive integer\n");
            return 1;
        }
        app->config_has_max_request_body_bytes = true;
    }
    else if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:MaxRequestWireBodyBytes")) {
        if (!sl_run_plan_config_parse_positive_size(value,
                                                    &app->config_max_request_wire_body_bytes))
        {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Server:MaxRequestWireBodyBytes must be a positive "
                                      "integer\n");
            return 1;
        }
        app->config_has_max_request_wire_body_bytes = true;
    }
    else if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:RequestArenaBytes")) {
        if (!sl_run_plan_config_parse_positive_size(value, &app->config_request_arena_bytes)) {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Server:RequestArenaBytes must be a positive "
                                      "integer\n");
            return 1;
        }
        app->config_has_request_arena_bytes = true;
    }
    else if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:MaxResponseBytes")) {
        if (!sl_run_plan_config_parse_positive_size(value, &app->config_max_response_bytes)) {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Server:MaxResponseBytes must be a positive "
                                      "integer\n");
            return 1;
        }
        app->config_has_max_response_bytes = true;
    }
    else if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:MaxPendingWriteBytes")) {
        if (!sl_run_plan_config_parse_positive_size(value, &app->config_max_pending_write_bytes)) {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Server:MaxPendingWriteBytes must be a positive "
                                      "integer\n");
            return 1;
        }
        app->config_has_max_pending_write_bytes = true;
    }
    else if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:Http2MaxStreams")) {
        if (!sl_run_plan_config_parse_positive_size(value, &app->config_http2_max_streams)) {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Server:Http2MaxStreams must be a positive "
                                      "integer\n");
            return 1;
        }
        app->config_has_http2_max_streams = true;
    }
    else if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:DispatchOnEventLoop")) {
        if (!sl_run_plan_config_parse_bool(value, &app->config_dispatch_on_event_loop)) {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Server:DispatchOnEventLoop must be boolean\n");
            return 1;
        }
        app->config_has_dispatch_on_event_loop = true;
    }
    else if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:MaxDispatchesPerTick")) {
        if (!sl_run_plan_config_parse_positive_size(value, &app->config_max_dispatches_per_tick)) {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Server:MaxDispatchesPerTick must be a positive "
                                      "integer\n");
            return 1;
        }
        app->config_has_max_dispatches_per_tick = true;
    }
    else if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:KeepAliveEnabled")) {
        if (!sl_run_plan_config_parse_bool(value, &app->config_keep_alive_enabled)) {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Server:KeepAliveEnabled must be a boolean\n");
            return 1;
        }
        app->config_has_keep_alive_enabled = true;
    }
    else if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:KeepAliveIdleTimeoutMs")) {
        if (!sl_run_plan_config_parse_u64(value, &app->config_keep_alive_idle_timeout_ms)) {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Server:KeepAliveIdleTimeoutMs must be a positive "
                                      "integer\n");
            return 1;
        }
        app->config_has_keep_alive_idle_timeout_ms = true;
    }
    else if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:MaxRequestsPerConnection")) {
        if (!sl_run_plan_config_parse_u64_allow_zero(value,
                                                     &app->config_max_requests_per_connection) ||
            !sl_run_plan_config_u64_fits_size_t(app->config_max_requests_per_connection))
        {
            sl_cli_write_cstr(stderr,
                              "sloppy run: app.plan.json "
                              "Sloppy:Server:MaxRequestsPerConnection must be a non-negative "
                              "integer\n");
            return 1;
        }
        app->config_has_max_requests_per_connection = true;
    }
    else if (sl_run_json_string_equals_cstr(key, "Sloppy:Server:RequestTimeoutMs")) {
        if (!sl_run_plan_config_parse_u64(value, &app->config_request_timeout_ms)) {
            sl_cli_write_cstr(stderr, "sloppy run: app.plan.json "
                                      "Sloppy:Server:RequestTimeoutMs must be a positive "
                                      "integer\n");
            return 1;
        }
        app->config_has_request_timeout_ms = true;
    }
    else {
        tls_result = sl_run_apply_tls_config_metadata_entry(app, key, value);
        if (tls_result >= 0) {
            return tls_result;
        }
        logging_result = sl_run_apply_logging_config_metadata_entry(app, key, value);
        if (logging_result >= 0) {
            return logging_result;
        }
    }

    return 0;
}

static int sl_run_load_config_metadata(SlRunApp* app, SlBytes json, const char* plan_path)
{
    yyjson_read_err error = {0};
    yyjson_doc* doc = NULL;
    yyjson_val* root = NULL;
    yyjson_val* configuration = NULL;
    yyjson_val* keys = NULL;
    yyjson_arr_iter iter;
    yyjson_val* entry = NULL;
    int result = 0;

    if (app == NULL || json.ptr == NULL || json.length == 0U) {
        return 1;
    }

    doc = yyjson_read_opts((char*)json.ptr, json.length, 0U, NULL, &error);
    if (doc == NULL) {
        sl_cli_write_error_with_value("sloppy run: malformed app.plan.json: ", plan_path, "\n");
        return 1;
    }

    root = yyjson_doc_get_root(doc);
    configuration = yyjson_is_obj(root) ? yyjson_obj_get(root, "configuration") : NULL;
    keys = yyjson_is_obj(configuration) ? yyjson_obj_get(configuration, "keys") : NULL;
    if (keys == NULL) {
        yyjson_doc_free(doc);
        return 0;
    }
    if (!yyjson_is_arr(keys)) {
        sl_cli_write_cstr(stderr,
                          "sloppy run: app.plan.json configuration.keys must be an array\n");
        yyjson_doc_free(doc);
        return 1;
    }

    yyjson_arr_iter_init(keys, &iter);
    while ((entry = yyjson_arr_iter_next(&iter)) != NULL) {
        yyjson_val* key = NULL;
        yyjson_val* value = NULL;

        if (!yyjson_is_obj(entry)) {
            sl_cli_write_cstr(
                stderr, "sloppy run: app.plan.json configuration.keys entries must be objects\n");
            result = 1;
            break;
        }

        key = yyjson_obj_get(entry, "key");
        value = yyjson_obj_get(entry, "value");
        if (sl_run_apply_config_metadata_entry(app, key, value) != 0) {
            result = 1;
            break;
        }
    }

    yyjson_doc_free(doc);
    return result;
}

typedef struct SlRunSha256
{
    uint32_t state[8];
    uint64_t bit_count;
    unsigned char buffer[64];
    size_t buffer_len;
} SlRunSha256;

static uint32_t sl_run_rotr32(uint32_t value, uint32_t shift)
{
    return (value >> shift) | (value << (32U - shift));
}

static void sl_run_sha256_transform(SlRunSha256* ctx, const unsigned char block[64])
{
    static const uint32_t k[64] = {
        UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
        UINT32_C(0x3956c25b), UINT32_C(0x59f111f1), UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
        UINT32_C(0xd807aa98), UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
        UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
        UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786), UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
        UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
        UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
        UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147), UINT32_C(0x06ca6351), UINT32_C(0x14292967),
        UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
        UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
        UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b), UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
        UINT32_C(0xd192e819), UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
        UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
        UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a), UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
        UINT32_C(0x748f82ee), UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
        UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2)};
    uint32_t w[64];
    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];
    size_t index = 0U;

    for (index = 0U; index < 16U; index += 1U) {
        size_t offset = index * 4U;
        w[index] = ((uint32_t)block[offset] << 24U) | ((uint32_t)block[offset + 1U] << 16U) |
                   ((uint32_t)block[offset + 2U] << 8U) | (uint32_t)block[offset + 3U];
    }
    for (index = 16U; index < 64U; index += 1U) {
        uint32_t s0 = sl_run_rotr32(w[index - 15U], 7U) ^ sl_run_rotr32(w[index - 15U], 18U) ^
                      (w[index - 15U] >> 3U);
        uint32_t s1 = sl_run_rotr32(w[index - 2U], 17U) ^ sl_run_rotr32(w[index - 2U], 19U) ^
                      (w[index - 2U] >> 10U);
        w[index] = w[index - 16U] + s0 + w[index - 7U] + s1;
    }

    for (index = 0U; index < 64U; index += 1U) {
        uint32_t s1 = sl_run_rotr32(e, 6U) ^ sl_run_rotr32(e, 11U) ^ sl_run_rotr32(e, 25U);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + k[index] + w[index];
        uint32_t s0 = sl_run_rotr32(a, 2U) ^ sl_run_rotr32(a, 13U) ^ sl_run_rotr32(a, 22U);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sl_run_sha256_init(SlRunSha256* ctx)
{
    *ctx = (SlRunSha256){0};
    ctx->state[0] = UINT32_C(0x6a09e667);
    ctx->state[1] = UINT32_C(0xbb67ae85);
    ctx->state[2] = UINT32_C(0x3c6ef372);
    ctx->state[3] = UINT32_C(0xa54ff53a);
    ctx->state[4] = UINT32_C(0x510e527f);
    ctx->state[5] = UINT32_C(0x9b05688c);
    ctx->state[6] = UINT32_C(0x1f83d9ab);
    ctx->state[7] = UINT32_C(0x5be0cd19);
}

static void sl_run_sha256_update(SlRunSha256* ctx, const unsigned char* bytes, size_t length)
{
    size_t index = 0U;

    ctx->bit_count += (uint64_t)length * 8U;
    while (index < length) {
        size_t available = 64U - ctx->buffer_len;
        size_t chunk = length - index < available ? length - index : available;
        for (size_t chunk_index = 0U; chunk_index < chunk; chunk_index += 1U) {
            ctx->buffer[ctx->buffer_len + chunk_index] = bytes[index + chunk_index];
        }
        ctx->buffer_len += chunk;
        index += chunk;
        if (ctx->buffer_len == 64U) {
            sl_run_sha256_transform(ctx, ctx->buffer);
            ctx->buffer_len = 0U;
        }
    }
}

static void sl_run_sha256_finish(SlRunSha256* ctx, unsigned char out[32])
{
    uint64_t bit_count = ctx->bit_count;
    size_t index = 0U;

    ctx->buffer[ctx->buffer_len] = 0x80U;
    ctx->buffer_len += 1U;
    if (ctx->buffer_len > 56U) {
        while (ctx->buffer_len < 64U) {
            ctx->buffer[ctx->buffer_len] = 0U;
            ctx->buffer_len += 1U;
        }
        sl_run_sha256_transform(ctx, ctx->buffer);
        ctx->buffer_len = 0U;
    }
    while (ctx->buffer_len < 56U) {
        ctx->buffer[ctx->buffer_len] = 0U;
        ctx->buffer_len += 1U;
    }
    for (index = 0U; index < 8U; index += 1U) {
        ctx->buffer[63U - index] = (unsigned char)(bit_count >> (index * 8U));
    }
    sl_run_sha256_transform(ctx, ctx->buffer);

    for (index = 0U; index < 8U; index += 1U) {
        size_t out_index = index * 4U;
        out[out_index] = (unsigned char)(ctx->state[index] >> 24U);
        out[out_index + 1U] = (unsigned char)(ctx->state[index] >> 16U);
        out[out_index + 2U] = (unsigned char)(ctx->state[index] >> 8U);
        out[out_index + 3U] = (unsigned char)ctx->state[index];
    }
}

static void sl_run_sha256_format_hex(const unsigned char digest[32],
                                     char out[sizeof("sha256:") + 64U])
{
    static const char hex[] = "0123456789abcdef";
    size_t index = 0U;

    out[0] = 's';
    out[1] = 'h';
    out[2] = 'a';
    out[3] = '2';
    out[4] = '5';
    out[5] = '6';
    out[6] = ':';
    for (index = 0U; index < 32U; index += 1U) {
        out[7U + (index * 2U)] = hex[(digest[index] >> 4U) & 0x0FU];
        out[7U + (index * 2U) + 1U] = hex[digest[index] & 0x0FU];
    }
    out[71] = '\0';
}

static bool sl_run_sha256_file_hex(const char* path, char out[sizeof("sha256:") + 64U])
{
    unsigned char buffer[8192];
    unsigned char digest[32];
    SlRunSha256 ctx;
    FILE* file = NULL;
    bool ok = true;

    if (path == NULL || path[0] == '\0' || out == NULL) {
        return false;
    }

#ifdef _MSC_VER
    if (fopen_s(&file, path, "rb") != 0) {
        file = NULL;
    }
#else
    file = fopen(path, "rb");
#endif
    if (file == NULL) {
        return false;
    }
    sl_run_sha256_init(&ctx);
    for (;;) {
        size_t read = fread(buffer, 1U, sizeof(buffer), file);
        if (read > 0U) {
            sl_run_sha256_update(&ctx, buffer, read);
        }
        if (read < sizeof(buffer)) {
            if (ferror(file) != 0) {
                ok = false;
            }
            break;
        }
    }
    if (fclose(file) != 0) {
        ok = false;
    }
    if (!ok) {
        return false;
    }
    sl_run_sha256_finish(&ctx, digest);
    sl_run_sha256_format_hex(digest, out);
    return true;
}

static bool sl_run_hash_matches(SlStr expected, SlBytes bytes)
{
    unsigned char digest[32];
    char actual[sizeof("sha256:") + 64U];
    SlRunSha256 ctx;

    if (expected.length != 71U || !sl_str_starts_with(expected, SL_STR_LITERAL("sha256:"))) {
        return false;
    }

    sl_run_sha256_init(&ctx);
    sl_run_sha256_update(&ctx, bytes.ptr, bytes.length);
    sl_run_sha256_finish(&ctx, digest);
    sl_run_sha256_format_hex(digest, actual);

    return sl_str_equal(expected, sl_str_from_parts(actual, 71U));
}

static bool sl_run_hash_file_matches(SlStr expected, const char* path)
{
    char actual[sizeof("sha256:") + 64U];

    if (expected.length != 71U || !sl_str_starts_with(expected, SL_STR_LITERAL("sha256:"))) {
        return false;
    }
    if (!sl_run_sha256_file_hex(path, actual)) {
        return false;
    }
    return sl_str_equal(expected, sl_str_from_parts(actual, 71U));
}

static void sl_run_print_diag(const char* prefix, SlArena* arena, const SlDiag* diag)
{
    SlArenaMark mark = {0U};
    SlStr rendered = {0};
    SlDiagReportContext report_context = {0};
    bool should_reset = false;

    if (arena != NULL) {
        mark = sl_arena_mark(arena);
        should_reset = true;
    }
    if (sl_run_diagnostics_json_enabled && diag != NULL && arena != NULL) {
        report_context.command = sl_str_from_cstr("run");
        report_context.runtime_version = sl_str_from_cstr(SL_VERSION_STRING);
        report_context.cause_message = prefix != NULL ? sl_str_from_cstr(prefix) : sl_str_empty();
        if (sl_status_is_ok(sl_diag_render_report_json(arena, diag, &report_context, &rendered))) {
            sl_cli_write_str(stderr, rendered);
            if (should_reset) {
                sl_arena_reset_to(arena, mark);
            }
            return;
        }
    }
    sl_cli_write_cstr(stderr, prefix);
    if (diag != NULL && arena != NULL &&
        sl_status_is_ok(sl_diag_render_text(arena, diag, &rendered)))
    {
        sl_cli_write_str(stderr, rendered);
        if (should_reset) {
            sl_arena_reset_to(arena, mark);
        }
        return;
    }
    if (should_reset) {
        sl_arena_reset_to(arena, mark);
    }
    if (diag != NULL && !sl_str_is_empty(diag->message)) {
        sl_cli_write_str(stderr, diag->message);
        sl_cli_write_cstr(stderr, "\n");
        return;
    }
    sl_cli_write_cstr(stderr, "operation failed\n");
}

static const char* sl_run_command_name_or_default(const char* command_name)
{
    return command_name != NULL && command_name[0] != '\0' ? command_name : "sloppy run";
}

static void sl_run_write_command_message(const char* command_name, const char* message)
{
    sl_cli_write_cstr(stderr, sl_run_command_name_or_default(command_name));
    sl_cli_write_cstr(stderr, ": ");
    sl_cli_write_cstr(stderr, message);
}

static void sl_run_print_command_diag(const char* command_name, const char* message, SlArena* arena,
                                      const SlDiag* diag)
{
    sl_cli_write_cstr(stderr, sl_run_command_name_or_default(command_name));
    sl_cli_write_cstr(stderr, ": ");
    sl_run_print_diag(message, arena, diag);
}

static SlEngineOptions sl_run_v8_options(const SlRunApp* app, const char* app_js_path)
{
    SlEngineOptions options = {0};

    options.kind = SL_ENGINE_KIND_V8;
    options.runtime_name = sl_str_from_cstr("sloppy-run");
    options.runtime_version = sl_str_from_cstr(SL_VERSION_STRING);
    options.target_platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    options.target_engine = sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8);
    /*
     * These are borrowed from SlRunApp; the app must stay alive for the engine lifetime.
     */
    options.plan = app == NULL ? NULL : &app->plan;
    options.capabilities = app == NULL ? NULL : &app->capability_registry;
    options.runtime_features = app == NULL ? NULL : &app->runtime_features;
    options.ffi_library_overrides =
        app == NULL || app->ffi_library_override_count == 0U ? NULL : app->ffi_library_overrides;
    options.ffi_library_override_count = app == NULL ? 0U : app->ffi_library_override_count;
    options.logging = app == NULL ? NULL : app->logging;
    options.source_map = app == NULL ? (SlBytes){0} : app->source_map_bytes;
    options.source_map_source_name =
        app_js_path == NULL ? sl_str_empty() : sl_str_from_cstr(app_js_path);
    return options;
}

static void sl_run_engine_cleanup(void* payload, void* user)
{
    SlEngine** engine = (SlEngine**)payload;

    (void)user;
    if (engine != NULL && *engine != NULL) {
        sl_engine_destroy(*engine);
        *engine = NULL;
    }
}

static SlStatus sl_run_log_console_writer(SlBytes bytes, void* user)
{
    FILE* stream = user == NULL ? stderr : (FILE*)user;

    if (bytes.length != 0U && bytes.ptr == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return fwrite(bytes.ptr, 1U, bytes.length, stream) == bytes.length
               ? sl_status_ok()
               : sl_status_from_code(SL_STATUS_INTERNAL);
}

static void sl_run_logging_cleanup(void* payload, void* user)
{
    SlLogRuntime** runtime = (SlLogRuntime**)payload;

    (void)user;
    if (runtime != NULL && *runtime != NULL) {
        sl_log_runtime_shutdown(*runtime);
        *runtime = NULL;
    }
}

static int sl_run_setup_logging(SlRunApp* app)
{
    SlLogRuntimeConfig config = sl_log_runtime_config_default();
    SlDiag diag = {0};
    SlStatus status;

    if (app == NULL) {
        return 1;
    }

    config.minimum_level = app->config_logging_minimum_level;
    config.queue_capacity = (size_t)app->config_logging_queue_capacity;
    config.sink_capacity = app->config_has_logging_file_path ? 2U : 1U;

    status = sl_log_runtime_create(&app->logging_arena, &config, &app->logging);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy run: failed to initialize logging runtime\n");
        return 1;
    }

    if (app->config_logging_console_enabled) {
        status = sl_log_console_sink_create(&app->logging_arena, app->config_logging_console_format,
                                            sl_run_log_console_writer, stderr,
                                            &app->logging_console_sink);
        if (sl_status_is_ok(status)) {
            status = sl_log_runtime_add_sink(app->logging, app->logging_console_sink);
        }
        if (!sl_status_is_ok(status)) {
            sl_cli_write_cstr(stderr, "sloppy run: failed to initialize logging console sink\n");
            return 1;
        }
    }

    if (app->config_has_logging_file_path) {
        status = sl_log_file_sink_create(&app->logging_arena,
                                         sl_str_from_cstr(app->config_logging_file_path),
                                         SL_LOG_DEFAULT_FILE_BUFFER_BYTES, &app->logging_file_sink);
        if (sl_status_is_ok(status)) {
            status = sl_log_runtime_add_sink(app->logging, app->logging_file_sink);
        }
        if (!sl_status_is_ok(status)) {
            sl_cli_write_cstr(stderr, "sloppy run: failed to initialize logging file sink\n");
            return 1;
        }
    }

    status = sl_log_runtime_start(app->logging);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy run: failed to start logging dispatcher\n");
        return 1;
    }

    status = sl_app_lifecycle_add_cleanup(&app->lifecycle, sl_run_logging_cleanup,
                                          (void*)&app->logging, NULL, &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag(
            "sloppy run: failed to register logging shutdown cleanup: ", &app->engine_arena, &diag);
        sl_log_runtime_shutdown(app->logging);
        app->logging = NULL;
        return 1;
    }

    return 0;
}

static int sl_run_load_plan(SlRunApp* app, const char* plan_path)
{
    SlBytes json = {0};
    SlPlanParseOptions parse_options = {0};
    SlDiag diag = {0};
    SlStatus status;

    sl_breadcrumb_global_record(SL_DIAG_SUBSYSTEM_PLAN, SL_BREADCRUMB_EVENT_PLAN_LOAD_START,
                                SL_STATUS_OK, 0U, 0U, 0U, 0U,
                                plan_path != NULL ? sl_str_from_cstr(plan_path) : sl_str_empty());

    if (sl_run_read_file(app, plan_path, app->plan_json_storage, sizeof(app->plan_json_storage),
                         &json) != 0)
    {
        sl_breadcrumb_global_record(
            SL_DIAG_SUBSYSTEM_PLAN, SL_BREADCRUMB_EVENT_PLAN_LOAD_FAILED, SL_STATUS_OUT_OF_RANGE,
            0U, 0U, 0U, 0U, plan_path != NULL ? sl_str_from_cstr(plan_path) : sl_str_empty());
        return 1;
    }
    app->plan_json_bytes = json;
    app->has_dynamic_routes =
        sl_run_bytes_contains_str(json, sl_str_from_cstr("\"dynamicRoutes\": [")) ||
        sl_run_bytes_contains_str(json, sl_str_from_cstr("\"dynamicRoutes\":["));

    parse_options.source_name = sl_str_from_cstr(plan_path);
    status = sl_plan_parse_json(&app->plan_arena, json, &parse_options, &app->plan, &diag);
    if (!sl_status_is_ok(status)) {
        sl_breadcrumb_global_record(
            SL_DIAG_SUBSYSTEM_PLAN, SL_BREADCRUMB_EVENT_PLAN_LOAD_FAILED, sl_status_code(status),
            0U, 0U, 0U, 0U, plan_path != NULL ? sl_str_from_cstr(plan_path) : sl_str_empty());
        sl_run_print_diag("sloppy run: malformed app.plan.json: ", &app->engine_arena, &diag);
        return 1;
    }

    if (!sl_str_equal(app->plan.target.engine, sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8))) {
        sl_cli_write_cstr(stderr, "sloppy run: app.plan.json target.engine must be v8\n");
        return 1;
    }
    if (!sl_str_equal(app->plan.target.platform,
                      sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64)))
    {
        sl_cli_write_cstr(stderr, "sloppy run: app.plan.json target.platform is unsupported\n");
        return 1;
    }
    if (!sl_str_equal(app->plan.runtime_min_version,
                      sl_str_from_cstr(SL_PLAN_RUNTIME_MIN_VERSION_0_1_0)))
    {
        sl_cli_write_cstr(stderr, "sloppy run: unsupported app.plan.json runtimeMinimumVersion\n");
        return 1;
    }

    if (sl_run_load_config_metadata(app, json, plan_path) != 0) {
        return 1;
    }

    sl_breadcrumb_global_record(SL_DIAG_SUBSYSTEM_PLAN, SL_BREADCRUMB_EVENT_PLAN_LOAD_DONE,
                                SL_STATUS_OK, 0U, 0U, 0U, 0U,
                                plan_path != NULL ? sl_str_from_cstr(plan_path) : sl_str_empty());
    return 0;
}

static SlStatus sl_run_plan_intern_capacity(const SlPlan* plan, size_t* out_capacity)
{
    size_t capacity = SL_RUN_PLAN_INTERN_BASE_FIELDS;
    size_t addend = 0U;
    SlStatus status;

    if (plan == NULL || out_capacity == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_checked_mul_size(plan->handler_count, 2U, &addend);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_checked_add_size(capacity, addend, &capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_checked_mul_size(plan->route_count, 6U, &addend);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_checked_add_size(capacity, addend, &capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (plan->has_route_dispatch_artifact) {
        status = sl_checked_add_size(capacity, 3U, &capacity);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    for (size_t index = 0U; index < plan->route_count; index += 1U) {
        status = sl_checked_mul_size(plan->routes[index].binding_count, 4U, &addend);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_checked_add_size(capacity, addend, &capacity);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    status = sl_checked_mul_size(plan->data_provider_count, 4U, &addend);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_checked_add_size(capacity, addend, &capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_checked_mul_size(plan->capability_count, 4U, &addend);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_checked_add_size(capacity, addend, &capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_checked_add_size(capacity, plan->required_feature_count, &capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (size_t library_index = 0U; library_index < plan->ffi_library_count; library_index += 1U) {
        status = sl_checked_add_size(capacity, 1U, &capacity);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status =
            sl_checked_mul_size(plan->ffi_libraries[library_index].function_count, 4U, &addend);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_checked_add_size(capacity, addend, &capacity);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    for (size_t struct_index = 0U; struct_index < plan->ffi_struct_count; struct_index += 1U) {
        status = sl_checked_add_size(capacity, 2U, &capacity);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status =
            sl_checked_add_size(capacity, plan->ffi_structs[struct_index].field_count, &capacity);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    *out_capacity = capacity;
    return sl_status_ok();
}

static int sl_run_intern_plan_metadata(SlRunApp* app)
{
    SlPlan interned_plan = {0};
    SlInternTable intern_table = {0};
    size_t capacity = 0U;
    SlStatus status;

    if (app == NULL) {
        return 1;
    }

    status = sl_run_plan_intern_capacity(&app->plan, &capacity);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy run: failed to size stable app plan metadata table\n");
        return 1;
    }

    intern_table = app->plan_metadata_interns;
    status = sl_plan_intern_metadata(&app->plan_arena, &app->plan, capacity, capacity,
                                     &interned_plan, &intern_table);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy run: failed to intern stable app plan metadata\n");
        return 1;
    }

    app->plan = interned_plan;
    app->plan_metadata_interns = intern_table;
    return 0;
}

static int sl_run_validate_startup(SlRunApp* app)
{
    SlAppHostStartupValidation validation = {0};
    SlDiag diag = {0};
    SlStatus status;

    if (app == NULL) {
        return 1;
    }

    validation.diag_arena = &app->plan_arena;
    validation.allow_dynamic_route_metadata = app->has_dynamic_routes;
    validation.require_runnable_route =
        app->plan.kind == SL_PLAN_KIND_WEB && !app->has_dynamic_routes;
    validation.max_runnable_routes = SL_RUN_MAX_ROUTES;

    status = sl_app_host_validate_startup(&app->plan, &validation, &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag("sloppy run: app graph startup validation failed: ", &app->engine_arena,
                          &diag);
        return 1;
    }

    return 0;
}

static int sl_run_validate_runtime_features(SlRunApp* app)
{
    SlAppHostStartupValidation validation = {0};
    SlDiag diag = {0};
    SlStatus status;

    if (app == NULL) {
        return 1;
    }

    if (app->plan.kind == SL_PLAN_KIND_PROGRAM) {
        status = sl_runtime_feature_activate_plan(&app->plan, NULL, &app->plan_arena,
                                                  &app->runtime_features, &diag);
        if (!sl_status_is_ok(status)) {
            sl_run_print_diag("sloppy run: runtime feature activation failed: ", &app->engine_arena,
                              &diag);
            return 1;
        }
        return 0;
    }

    validation.diag_arena = &app->plan_arena;
    validation.allow_dynamic_route_metadata = app->has_dynamic_routes;
    validation.require_runnable_route =
        app->plan.kind == SL_PLAN_KIND_WEB && !app->has_dynamic_routes;
    validation.max_runnable_routes = SL_RUN_MAX_ROUTES;
    validation.validate_runtime_features = true;
    validation.out_runtime_features = &app->runtime_features;

    status = sl_app_host_validate_startup(&app->plan, &validation, &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag("sloppy run: runtime feature activation failed: ", &app->engine_arena,
                          &diag);
        return 1;
    }

    return 0;
}

static int sl_run_init_capability_registry(SlRunApp* app)
{
    SlStatus status;

    if (app == NULL) {
        return 1;
    }

    status = sl_capability_registry_init_from_plan(&app->plan, &app->capability_registry);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy run: capability registry initialization failed\n");
        return 1;
    }

    return 0;
}

static int sl_run_prepare_routes(SlRunApp* app)
{
    SlDiag diag = {0};
    SlStatus status;

    if (app->plan.route_count > SL_RUN_MAX_ROUTES) {
        sl_cli_write_cstr(stderr,
                          "sloppy run: app.plan.json contains more routes than the dev runtime can "
                          "materialize\n");
        return 1;
    }

    if (app->has_dynamic_routes && app->plan.route_count == 0U) {
        return 0;
    }

    if (app->plan.has_route_dispatch_artifact) {
        SlRouteArtifactSummary summary = {0};
        status = sl_route_artifact_validate(&app->engine_arena, app->route_artifact_bytes,
                                            app->plan.route_dispatch_artifact.hash, &app->plan,
                                            &summary, &diag);
        if (!sl_status_is_ok(status)) {
            sl_run_print_diag("sloppy run: invalid route dispatch artifact: ", &app->engine_arena,
                              &diag);
            return 1;
        }
    }

    status = sl_http_route_table_build(&app->route_arena, &app->plan, &app->route_table, &diag);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy run: failed to build HTTP route table\n");
        return 1;
    }

    if (app->route_table.route_count == 0U) {
        sl_cli_write_cstr(stderr,
                          "sloppy run: app.plan.json does not contain runnable route metadata\n");
        return 1;
    }

    return 0;
}

static int sl_run_load_route_artifact(SlRunApp* app, const char* artifacts_path,
                                      const char* command_name)
{
    char route_artifact_path[SL_RUN_PATH_MAX_BYTES];

    if (app == NULL || artifacts_path == NULL || command_name == NULL) {
        return 1;
    }
    if (!app->plan.has_route_dispatch_artifact) {
        return 0;
    }
    if (!sl_str_equal(app->plan.route_dispatch_artifact.kind, sl_str_from_cstr("slrt"))) {
        sl_run_write_command_message(command_name,
                                     "unsupported route dispatch artifact kind in app.plan.json\n");
        return 1;
    }
    if (!sl_run_join_path(route_artifact_path, sizeof(route_artifact_path), artifacts_path,
                          sl_run_str_span(app->plan.route_dispatch_artifact.path)))
    {
        sl_run_write_command_message(command_name,
                                     "invalid route dispatch artifact path in app.plan.json\n");
        return 1;
    }
    return sl_run_read_file(app, route_artifact_path, app->route_artifact_storage,
                            sizeof(app->route_artifact_storage), &app->route_artifact_bytes);
}

static int sl_run_cache_openapi_docs_route(SlRunApp* app, const char* command_name)
{
    yyjson_doc* doc = NULL;
    yyjson_val* root = NULL;
    yyjson_val* routes = NULL;
    yyjson_val* route = NULL;
    size_t index = 0U;
    size_t max = 0U;

    if (app == NULL || app->plan_json_bytes.ptr == NULL || app->plan_json_bytes.length == 0U) {
        return 0;
    }

    doc = yyjson_read_opts((char*)app->plan_json_bytes.ptr, app->plan_json_bytes.length, 0U, NULL,
                           NULL);
    if (doc == NULL) {
        sl_run_write_command_message(command_name, "failed to parse app.plan.json\n");
        return 1;
    }
    root = yyjson_doc_get_root(doc);
    routes = yyjson_obj_get(root, "routes");
    if (routes == NULL || !yyjson_is_arr(routes)) {
        yyjson_doc_free(doc);
        return 0;
    }
    yyjson_arr_foreach(routes, index, max, route)
    {
        yyjson_val* docs_internal = yyjson_obj_get(route, "docsInternal");
        yyjson_val* docs = yyjson_obj_get(route, "docs");
        yyjson_val* kind =
            docs != NULL && yyjson_is_obj(docs) ? yyjson_obj_get(docs, "kind") : NULL;
        yyjson_val* pattern = yyjson_obj_get(route, "pattern");

        if (docs_internal == NULL || !yyjson_is_bool(docs_internal) ||
            !yyjson_get_bool(docs_internal) || kind == NULL || !yyjson_is_str(kind) ||
            !sl_str_equal(sl_str_from_parts(yyjson_get_str(kind), yyjson_get_len(kind)),
                          sl_str_from_cstr("openapi")) ||
            pattern == NULL || !yyjson_is_str(pattern))
        {
            continue;
        }
        if (!sl_run_copy_span_cstr(app->openapi_docs_path, sizeof(app->openapi_docs_path),
                                   (SlCliSpan){yyjson_get_str(pattern), yyjson_get_len(pattern)}))
        {
            yyjson_doc_free(doc);
            sl_run_write_command_message(command_name,
                                         "invalid OpenAPI docs route in app.plan.json\n");
            return 1;
        }
        app->has_openapi_docs_route = true;
        break;
    }
    yyjson_doc_free(doc);
    return 0;
}

static int sl_run_load_bootstrap_runtime(SlRunApp* app, const char* stdlib_root)
{
    char bootstrap_path[1024];
    char manifest_path[1024];
    SlBytes manifest = {0};
    SlBytes js = {0};
    SlStr source = {0};
    SlDiag diag = {0};
    SlStatus status;

    if (stdlib_root == NULL || stdlib_root[0] == '\0') {
        sl_cli_write_cstr(stderr, "sloppy run: bootstrap stdlib root is not configured\n");
        return 1;
    }

    if (!sl_run_artifact_file_path(bootstrap_path, sizeof(bootstrap_path), stdlib_root,
                                   "internal/runtime-classic.js"))
    {
        sl_cli_write_cstr(stderr, "sloppy run: invalid bootstrap stdlib directory\n");
        return 1;
    }

    if (!sl_run_artifact_file_path(manifest_path, sizeof(manifest_path), stdlib_root,
                                   "bootstrap.manifest.json"))
    {
        sl_cli_write_cstr(stderr, "sloppy run: invalid bootstrap stdlib directory\n");
        return 1;
    }

    if (sl_run_read_stdlib_file(manifest_path, app->bootstrap_js_storage,
                                sizeof(app->bootstrap_js_storage), &manifest) != 0)
    {
        return 1;
    }

    if (!sl_run_manifest_is_compatible(manifest, "0.1.0", "sloppy/internal/runtime-classic.js")) {
        sl_cli_write_cstr(
            stderr, "sloppy run: bootstrap stdlib manifest is incompatible with this runtime\n");
        return 1;
    }

    if (sl_run_read_stdlib_file(bootstrap_path, app->bootstrap_js_storage,
                                sizeof(app->bootstrap_js_storage), &js) != 0)
    {
        return 1;
    }

    source = sl_str_from_parts((const char*)js.ptr, js.length);
    status = sl_engine_eval_source(app->engine, sl_str_from_cstr(bootstrap_path), source, &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag("sloppy run: failed to evaluate bootstrap stdlib: ", &app->engine_arena,
                          &diag);
        return 1;
    }

    return 0;
}

static int sl_run_load_engine(SlRunApp* app, const char* stdlib_root, const char* app_js_path)
{
    SlStr source = {0};
    SlEngineOptions options = {0};
    SlDiag diag = {0};
    SlStatus status;

    if (sl_run_setup_logging(app) != 0) {
        return 1;
    }
    options = sl_run_v8_options(app, app_js_path);
    status = sl_engine_create(&options, &app->engine_arena, &app->engine);
    if (sl_status_code(status) == SL_STATUS_UNSUPPORTED) {
        sl_cli_write_cstr(stderr, "sloppy run: sloppy run requires V8-enabled build\n");
        return 1;
    }
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy run: failed to create V8 engine\n");
        return 1;
    }

    /* app->lifecycle runs LIFO; engine teardown runs before logging shutdown. */
    status = sl_app_lifecycle_add_cleanup(&app->lifecycle, sl_run_engine_cleanup,
                                          (void*)&app->engine, NULL, &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag(
            "sloppy run: failed to register engine shutdown cleanup: ", &app->engine_arena, &diag);
        sl_engine_destroy(app->engine);
        app->engine = NULL;
        return 1;
    }

    if (sl_run_load_bootstrap_runtime(app, stdlib_root) != 0) {
        return 1;
    }

    source = sl_str_from_parts((const char*)app->app_js_bytes.ptr, app->app_js_bytes.length);
    status = sl_engine_eval_source(app->engine, sl_str_from_cstr(app_js_path), source, &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag("sloppy run: failed to evaluate app.js: ", &app->engine_arena, &diag);
        return 1;
    }

    if (app->plan.kind == SL_PLAN_KIND_WEB) {
        status = sl_engine_validate_registered_handlers(app->engine, &app->plan, &diag);
        if (!sl_status_is_ok(status)) {
            sl_run_print_diag(
                "sloppy run: registered handler validation failed: ", &app->engine_arena, &diag);
            return 1;
        }
    }

    return 0;
}

static int sl_run_load_artifacts(const char* artifacts_path, const char* command_name,
                                 SlRunApp* app, bool validate_runtime_features)
{
    char plan_path[1024];
    char app_js_path[1024];
    char source_map_path[1024];
    SlBytes app_js = {0};
    SlBytes source_map = {0};
    SlDiag diag = {0};

    if (artifacts_path == NULL || app == NULL) {
        return 1;
    }

    sl_run_reset_app_storage(app);
    app->config_logging_minimum_level = SL_LOG_LEVEL_INFO;
    app->config_logging_queue_capacity = SL_LOG_DEFAULT_QUEUE_CAPACITY;
    app->config_logging_console_enabled = true;
    app->config_logging_console_format = SL_LOG_CONSOLE_FORMAT_PRETTY;
    if (!sl_run_copy_cstr(app->artifacts_path, sizeof(app->artifacts_path), artifacts_path)) {
        sl_run_write_command_message(command_name, "invalid artifacts directory\n");
        return 1;
    }
    if (!sl_status_is_ok(sl_arena_init(&app->plan_arena, app->plan_arena_storage,
                                       sizeof(app->plan_arena_storage))) ||
        !sl_status_is_ok(sl_arena_init(&app->route_arena, app->route_arena_storage,
                                       sizeof(app->route_arena_storage))) ||
        !sl_status_is_ok(sl_arena_init(&app->engine_arena, app->engine_arena_storage,
                                       sizeof(app->engine_arena_storage))) ||
        !sl_status_is_ok(sl_arena_init(&app->logging_arena, app->logging_arena_storage,
                                       sizeof(app->logging_arena_storage))))
    {
        return 1;
    }

    if (!sl_status_is_ok(sl_app_lifecycle_start(
            &app->lifecycle, app->app_cleanups,
            sizeof(app->app_cleanups) / sizeof(app->app_cleanups[0]), &diag)))
    {
        sl_run_print_command_diag(command_name,
                                  "failed to start app lifecycle: ", &app->engine_arena, &diag);
        return 1;
    }

    if (!sl_run_artifact_file_path(plan_path, sizeof(plan_path), artifacts_path, "app.plan.json")) {
        sl_run_write_command_message(command_name, "invalid artifacts directory\n");
        return 1;
    }

    if (sl_run_load_plan(app, plan_path) != 0 ||
        (app->plan.kind == SL_PLAN_KIND_WEB && sl_run_validate_startup(app) != 0) ||
        sl_run_intern_plan_metadata(app) != 0 || sl_run_init_capability_registry(app) != 0)
    {
        return 1;
    }
    if (app->plan.kind == SL_PLAN_KIND_WEB &&
        sl_run_load_route_artifact(app, artifacts_path, command_name) != 0)
    {
        return 1;
    }
    if (app->plan.kind == SL_PLAN_KIND_WEB && sl_run_prepare_routes(app) != 0) {
        return 1;
    }
    if (app->plan.kind == SL_PLAN_KIND_WEB &&
        sl_run_cache_openapi_docs_route(app, command_name) != 0)
    {
        return 1;
    }
    {
        char openapi_path[1024];
        if (sl_run_artifact_file_path(openapi_path, sizeof(openapi_path), artifacts_path,
                                      "openapi.json") &&
            sl_cli_path_is_file(openapi_path) &&
            sl_run_read_file(app, openapi_path, app->openapi_artifact_storage,
                             sizeof(app->openapi_artifact_storage),
                             &app->openapi_artifact_bytes) != 0)
        {
            return 1;
        }
    }

    if (!sl_run_join_path(app_js_path, sizeof(app_js_path), artifacts_path,
                          sl_run_str_span(app->plan.bundle.path)))
    {
        sl_run_write_command_message(command_name, "invalid bundle path in app.plan.json\n");
        return 1;
    }

    if (!sl_run_join_path(source_map_path, sizeof(source_map_path), artifacts_path,
                          sl_run_str_span(app->plan.source_map.path)))
    {
        sl_run_write_command_message(command_name, "invalid source map path in app.plan.json\n");
        return 1;
    }

    if (sl_run_read_file(app, app_js_path, app->app_js_storage, sizeof(app->app_js_storage),
                         &app_js) != 0)
    {
        return 1;
    }
    if (sl_run_read_file(app, source_map_path, app->metadata_json_storage,
                         sizeof(app->metadata_json_storage), &source_map) != 0)
    {
        return 1;
    }
    if (!sl_run_hash_matches(app->plan.bundle.hash, app_js)) {
        sl_run_write_command_message(command_name, "bundle hash mismatch in app.plan.json\n");
        return 1;
    }
    if (!sl_run_hash_matches(app->plan.source_map.hash, source_map)) {
        sl_run_write_command_message(command_name, "source map hash mismatch in app.plan.json\n");
        return 1;
    }
    app->app_js_bytes = app_js;
    app->source_map_bytes = source_map;

    if (validate_runtime_features && sl_run_validate_runtime_features(app) != 0) {
        return 1;
    }

    return 0;
}

static int sl_run_load_app(const char* artifacts_path, const char* stdlib_path,
                           const char* package_path, SlRunApp* app)
{
    char app_js_path[1024];
    bool is_package = package_path != NULL;

    if (sl_run_load_artifacts(artifacts_path, "sloppy run", app, !is_package) != 0) {
        return 1;
    }
    if (is_package && sl_run_load_package_native_overrides(app, package_path) != 0) {
        return 1;
    }
    if (is_package && sl_run_validate_runtime_features(app) != 0) {
        return 1;
    }
    if (!sl_run_join_path(app_js_path, sizeof(app_js_path), artifacts_path,
                          sl_run_str_span(app->plan.bundle.path)))
    {
        sl_cli_write_cstr(stderr, "sloppy run: invalid bundle path in app.plan.json\n");
        return 1;
    }

    return sl_run_load_engine(app, stdlib_path, app_js_path);
}

static int sl_run_write_response_with_options(char* buffer, size_t capacity, unsigned status,
                                              const char* content_type, SlStr body,
                                              bool suppress_body)
{
    SlHttpResponse response = {0};
    SlHttpResponseWriteOptions options = {0};
    SlBytes bytes = {0};
    SlStatus write_status;

    if (buffer == NULL || content_type == NULL || status > UINT16_MAX) {
        return -1;
    }

    response = sl_http_response_text((uint16_t)status, body);
    response.content_type = sl_str_from_cstr(content_type);
    options.suppress_body = suppress_body;
    write_status = sl_http_response_write_with_options(&response, &options, (unsigned char*)buffer,
                                                       capacity, &bytes);
    if (!sl_status_is_ok(write_status) || bytes.length > (size_t)INT32_MAX) {
        return -1;
    }

    return (int)bytes.length;
}

static SlStatus sl_run_method_not_allowed_response(SlRunApp* app, SlArena* arena,
                                                   const SlHttpRequestHead* request,
                                                   SlHttpResponse* out_response)
{
    SlHttpHeader* allow_header = NULL;
    SlStr allow = sl_str_empty();
    SlStatus status;
    void* header_storage = NULL;

    if (out_response != NULL) {
        *out_response = sl_http_response_text(405U, sl_str_from_cstr("Method Not Allowed\n"));
    }
    if (app == NULL || arena == NULL || request == NULL || out_response == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_http_dispatch_allow_header_for_path(arena, &app->route_table.dispatch,
                                                    request->path, &allow);
    if (!sl_status_is_ok(status) || sl_str_is_empty(allow)) {
        return status;
    }

    status = sl_arena_alloc(arena, sizeof(SlHttpHeader), _Alignof(SlHttpHeader), &header_storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    allow_header = (SlHttpHeader*)header_storage;
    allow_header->name = sl_str_from_cstr("Allow");
    allow_header->value = allow;
    out_response->headers = allow_header;
    out_response->header_count = 1U;
    return sl_status_ok();
}

static int sl_run_write_method_not_allowed_response(SlRunApp* app, SlArena* arena,
                                                    const SlHttpRequestHead* request,
                                                    char* response, size_t response_capacity,
                                                    bool suppress_body)
{
    SlHttpResponse method_response = {0};
    SlHttpResponseWriteOptions options = {0};
    SlBytes response_bytes = {0};
    SlStatus status;

    if (response == NULL) {
        return -1;
    }

    status = sl_run_method_not_allowed_response(app, arena, request, &method_response);
    if (sl_status_is_ok(status)) {
        options.suppress_body = suppress_body;
        status = sl_http_response_write_with_options(&method_response, &options,
                                                     (unsigned char*)response, response_capacity,
                                                     &response_bytes);
        if (sl_status_is_ok(status) && response_bytes.length <= (size_t)INT32_MAX) {
            return (int)response_bytes.length;
        }
    }

    return sl_run_write_response_with_options(
        response, response_capacity, 405U, "text/plain; charset=utf-8",
        sl_str_from_cstr("Method Not Allowed\n"), suppress_body);
}

static SlHttpMethod sl_run_method_from_cstr(const char* method)
{
    if (method == NULL) {
        return SL_HTTP_METHOD_UNKNOWN;
    }
    if (strcmp(method, "GET") == 0) {
        return SL_HTTP_METHOD_GET;
    }
    if (strcmp(method, "POST") == 0) {
        return SL_HTTP_METHOD_POST;
    }
    if (strcmp(method, "PUT") == 0) {
        return SL_HTTP_METHOD_PUT;
    }
    if (strcmp(method, "DELETE") == 0) {
        return SL_HTTP_METHOD_DELETE;
    }
    if (strcmp(method, "PATCH") == 0) {
        return SL_HTTP_METHOD_PATCH;
    }
    if (strcmp(method, "OPTIONS") == 0) {
        return SL_HTTP_METHOD_OPTIONS;
    }
    if (strcmp(method, "HEAD") == 0) {
        return SL_HTTP_METHOD_HEAD;
    }
    return SL_HTTP_METHOD_UNKNOWN;
}

static const char* sl_run_method_to_cstr(SlHttpMethod method)
{
    switch (method) {
    case SL_HTTP_METHOD_GET:
        return "GET";
    case SL_HTTP_METHOD_POST:
        return "POST";
    case SL_HTTP_METHOD_PUT:
        return "PUT";
    case SL_HTTP_METHOD_DELETE:
        return "DELETE";
    case SL_HTTP_METHOD_PATCH:
        return "PATCH";
    case SL_HTTP_METHOD_OPTIONS:
        return "OPTIONS";
    case SL_HTTP_METHOD_HEAD:
        return "HEAD";
    case SL_HTTP_METHOD_UNKNOWN:
    default:
        return NULL;
    }
}

static SlStatus sl_run_append_js_string(SlStringBuilder* builder, SlStr value)
{
    if (builder == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_json_writer_append_escaped_string(builder, value);
}

static SlStr sl_run_target_path(const char* target)
{
    size_t index = 0U;

    if (target == NULL) {
        return sl_str_empty();
    }

    while (target[index] != '\0' && target[index] != '?') {
        index += 1U;
    }

    return sl_str_from_parts(target, index);
}

static bool sl_run_request_targets_openapi_docs_route(SlRunApp* app,
                                                      const SlHttpRequestHead* request)
{
    if (app == NULL || request == NULL || request->path.ptr == NULL || !app->has_openapi_docs_route)
    {
        return false;
    }
    if (request->method != SL_HTTP_METHOD_GET && request->method != SL_HTTP_METHOD_HEAD) {
        return false;
    }
    return sl_str_equal(request->path, sl_str_from_cstr(app->openapi_docs_path));
}

static SlStatus sl_run_openapi_artifact_response(SlRunApp* app, const SlHttpRequestHead* request,
                                                 SlHttpResponse* out_response)
{
    if (app == NULL || request == NULL || out_response == NULL ||
        app->openapi_artifact_bytes.ptr == NULL || app->openapi_artifact_bytes.length == 0U ||
        !sl_run_request_targets_openapi_docs_route(app, request))
    {
        return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
    }
    *out_response =
        sl_http_response_text(200U, sl_str_from_parts((const char*)app->openapi_artifact_bytes.ptr,
                                                      app->openapi_artifact_bytes.length));
    out_response->content_type = sl_str_from_cstr("application/json; charset=utf-8");
    return sl_status_ok();
}

typedef struct SlRunDispatchContext
{
    SlRunApp* app;
    SlArena* arena;
    const SlHttpRequestHead* request;
    const SlHttpRequestLifecycle* lifecycle;
    SlHttpResponse* response;
} SlRunDispatchContext;

static SlStatus sl_run_dispatch_with_request_scope(SlAppRequestScope* request_scope, void* user,
                                                   SlDiag* out_diag)
{
    SlRunDispatchContext* context = (SlRunDispatchContext*)user;
    SlEngineResult result = {0};
    SlStatus status;

    /* The scope is represented by the callback boundary; no request cleanups are registered here.
     */
    (void)request_scope;
    if (context == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (context->lifecycle != NULL) {
        status = sl_http_dispatch_request_lifecycle(
            context->arena, context->app->engine, &context->app->plan,
            &context->app->route_table.dispatch, context->lifecycle, &result, out_diag);
    }
    else {
        status = sl_http_dispatch_request_head(
            context->arena, context->app->engine, &context->app->plan,
            &context->app->route_table.dispatch, context->request, &result, out_diag);
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (context->response == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (result.payload_kind != SL_ENGINE_RESULT_PAYLOAD_RESPONSE) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    *context->response = result.response;
    return sl_status_ok();
}

static SlStatus sl_run_dispatch_response_with_storage(
    SlRunApp* app, const SlHttpRequestHead* request, const SlHttpRequestLifecycle* lifecycle,
    SlArena* dispatch_arena, SlHttpResponse* out_response, SlDiag* out_diag)
{
    SlScopeCleanup request_cleanups[SL_RUN_REQUEST_SCOPE_MAX_CLEANUPS];
    SlRunDispatchContext dispatch_context = {0};

    if (app == NULL || (request == NULL && lifecycle == NULL) || dispatch_arena == NULL ||
        out_response == NULL)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (request != NULL &&
        sl_status_is_ok(sl_run_openapi_artifact_response(app, request, out_response)))
    {
        return sl_status_ok();
    }

    dispatch_context.app = app;
    dispatch_context.arena = dispatch_arena;
    dispatch_context.request = request;
    dispatch_context.lifecycle = lifecycle;
    dispatch_context.response = out_response;

    if (app->next_request_id == UINT64_MAX) {
        return sl_status_from_code(SL_STATUS_OVERFLOW);
    }
    /* Request ID 0 is invalid; request scopes receive monotonic IDs or fail on overflow. */
    app->next_request_id += 1U;
    return sl_app_request_scope_execute_for_app(
        &app->lifecycle, app->next_request_id, request_cleanups,
        sizeof(request_cleanups) / sizeof(request_cleanups[0]), sl_run_dispatch_with_request_scope,
        &dispatch_context, out_diag);
}

static bool sl_run_dispatch_failure_is_cli_mapped(SlStatus status, const SlDiag* diag)
{
    if (diag == NULL) {
        return false;
    }

    if (sl_status_code(status) == SL_STATUS_UNSUPPORTED &&
        (diag->code == SL_DIAG_HTTP_UNSUPPORTED_METHOD ||
         diag->code == SL_DIAG_HTTP_UNSUPPORTED_BODY ||
         diag->code == SL_DIAG_HTTP_UNSUPPORTED_MEDIA_TYPE))
    {
        return true;
    }
    if (sl_status_code(status) == SL_STATUS_CAPACITY_EXCEEDED &&
        diag->code == SL_DIAG_HTTP_BODY_LIMIT)
    {
        return true;
    }
    if (sl_status_code(status) == SL_STATUS_INVALID_ARGUMENT &&
        (diag->code == SL_DIAG_MALFORMED_JSON || diag->code == SL_DIAG_INVALID_HTTP_REQUEST))
    {
        return true;
    }
    return sl_status_code(status) == SL_STATUS_OUT_OF_RANGE &&
           diag->code == SL_DIAG_HTTP_ROUTE_NOT_FOUND;
}

static SlStatus sl_run_dispatch_transport_status(SlStatus status, const SlDiag* diag)
{
    return sl_run_dispatch_failure_is_cli_mapped(status, diag)
               ? status
               : sl_status_from_code(SL_STATUS_INTERNAL);
}

static int sl_run_write_dispatch_failure_response(SlRunApp* app, SlArena* dispatch_arena,
                                                  const SlHttpRequestHead* request, char* response,
                                                  size_t response_capacity, SlStatus status,
                                                  const SlDiag* diag, bool suppress_body)
{
    if (app == NULL || dispatch_arena == NULL || request == NULL || response == NULL ||
        diag == NULL)
    {
        return -1;
    }

    if (sl_status_code(status) == SL_STATUS_UNSUPPORTED &&
        diag->code == SL_DIAG_HTTP_UNSUPPORTED_METHOD)
    {
        return sl_run_write_method_not_allowed_response(app, dispatch_arena, request, response,
                                                        response_capacity, suppress_body);
    }

    if (sl_status_code(status) == SL_STATUS_UNSUPPORTED &&
        diag->code == SL_DIAG_HTTP_UNSUPPORTED_BODY)
    {
        return sl_run_write_response_with_options(
            response, response_capacity, 501U, "text/plain; charset=utf-8",
            sl_str_from_cstr("Request body framing is not supported\n"), suppress_body);
    }

    if (sl_status_code(status) == SL_STATUS_CAPACITY_EXCEEDED &&
        diag->code == SL_DIAG_HTTP_BODY_LIMIT)
    {
        return sl_run_write_response_with_options(
            response, response_capacity, 413U, "text/plain; charset=utf-8",
            sl_str_from_cstr("Payload Too Large\n"), suppress_body);
    }

    if (sl_status_code(status) == SL_STATUS_UNSUPPORTED &&
        diag->code == SL_DIAG_HTTP_UNSUPPORTED_MEDIA_TYPE)
    {
        return sl_run_write_response_with_options(
            response, response_capacity, 415U, "text/plain; charset=utf-8",
            sl_str_from_cstr("Unsupported Media Type\n"), suppress_body);
    }

    if (sl_status_code(status) == SL_STATUS_INVALID_ARGUMENT &&
        diag->code == SL_DIAG_MALFORMED_JSON)
    {
        return sl_run_write_response_with_options(
            response, response_capacity, 400U, "application/problem+json",
            sl_str_from_cstr(
                "{\"type\":\"https://sloppy.dev/problems/validation\",\"title\":\"Validation "
                "failed\",\"status\":400,\"code\":\"SLOPPY_E_VALIDATION_FAILED\",\"errors\":[{"
                "\"path\":\"body\",\"code\":\"json.malformed\",\"message\":\"Expected a valid "
                "JSON request body.\"}]}\n"),
            suppress_body);
    }

    if (sl_status_code(status) == SL_STATUS_OUT_OF_RANGE &&
        diag->code == SL_DIAG_HTTP_ROUTE_NOT_FOUND)
    {
        return sl_run_write_response_with_options(response, response_capacity, 404U,
                                                  "text/plain; charset=utf-8",
                                                  sl_str_from_cstr("Not Found\n"), suppress_body);
    }

    return sl_run_write_response_with_options(
        response, response_capacity, 500U, "text/plain; charset=utf-8",
        sl_str_from_cstr("Sloppy handler failed\n"), suppress_body);
}

static int sl_run_dispatch_head_with_storage(SlRunApp* app, const SlHttpRequestHead* request,
                                             char* response, size_t response_capacity,
                                             unsigned char* dispatch_storage,
                                             size_t dispatch_storage_size)
{
    SlArena dispatch_arena = {0};
    SlHttpResponse result = {0};
    SlHttpResponseWriteOptions write_options = {0};
    SlBytes response_bytes = {0};
    SlDiag diag = {0};
    bool suppress_body = false;
    SlStatus status;

    if (app == NULL || request == NULL || response == NULL || dispatch_storage == NULL ||
        !sl_status_is_ok(sl_arena_init(&dispatch_arena, dispatch_storage, dispatch_storage_size)))
    {
        return -1;
    }

    suppress_body = request->method == SL_HTTP_METHOD_HEAD;
    write_options.suppress_body = suppress_body;
    status =
        sl_run_dispatch_response_with_storage(app, request, NULL, &dispatch_arena, &result, &diag);
    if (sl_status_is_ok(status)) {
        status = sl_http_response_write_with_options(
            &result, &write_options, (unsigned char*)response, response_capacity, &response_bytes);
        if (!sl_status_is_ok(status) || response_bytes.length > (size_t)INT32_MAX) {
            status = sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
        }
        else {
            return (int)response_bytes.length;
        }
    }

    return sl_run_write_dispatch_failure_response(app, &dispatch_arena, request, response,
                                                  response_capacity, status, &diag, suppress_body);
}

static int sl_run_dispatch_head(SlRunApp* app, const SlHttpRequestHead* request, char* response,
                                size_t response_capacity)
{
    unsigned char dispatch_storage[SL_RUN_ARENA_BYTES];

    return sl_run_dispatch_head_with_storage(app, request, response, response_capacity,
                                             dispatch_storage, sizeof(dispatch_storage));
}

static SlStatus sl_run_dispatch_dynamic_response(SlRunApp* app, SlArena* arena,
                                                 const SlHttpRequestHead* request,
                                                 SlHttpResponse* out_response, SlDiag* out_diag)
{
    char eval_source[SL_RUN_RESPONSE_MAX_BYTES];
    SlStringBuilder builder;
    SlStr eval_view = {0};
    SlEngineResult result = {0};
    SlStatus status;
    const char* method;
    SlStr target;

    if (app == NULL || arena == NULL || request == NULL || out_response == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    method = sl_run_method_to_cstr(request->method);
    if (method == NULL) {
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    }
    target = request->raw_target.length != 0U ? request->raw_target : request->path;

    status = sl_string_builder_init_fixed(&builder, eval_source, sizeof(eval_source));
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_cstr(
            &builder, "globalThis.__sloppy_dynamic_transport = function() { return "
                      "globalThis.__sloppy_dispatch_dynamic_result(");
    }
    if (sl_status_is_ok(status)) {
        status = sl_run_append_js_string(&builder, sl_str_from_cstr(method));
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_cstr(&builder, ", ");
    }
    if (sl_status_is_ok(status)) {
        status = sl_run_append_js_string(&builder, target);
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_cstr(&builder, "); };");
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_view_with_nul(&builder, &eval_view);
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_engine_eval_source(app->engine, sl_str_from_cstr("sloppy:dynamic-transport"),
                                   eval_view, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_engine_call_function0(
        app->engine, arena, sl_str_from_cstr("__sloppy_dynamic_transport"), &result, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (result.payload_kind != SL_ENGINE_RESULT_PAYLOAD_RESPONSE) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    *out_response = result.response;
    return sl_status_ok();
}

static bool sl_run_once_header_name_char(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
           ch == '!' || ch == '#' || ch == '$' || ch == '%' || ch == '&' || ch == '\'' ||
           ch == '*' || ch == '+' || ch == '-' || ch == '.' || ch == '^' || ch == '_' ||
           ch == '`' || ch == '|' || ch == '~';
}

static bool sl_run_once_header_has_crlf(SlStr value)
{
    size_t index = 0U;

    if (value.ptr == NULL && value.length != 0U) {
        return true;
    }
    for (index = 0U; index < value.length; index += 1U) {
        if (value.ptr[index] == '\r' || value.ptr[index] == '\n') {
            return true;
        }
    }
    return false;
}

static bool sl_run_once_header_name_valid(SlStr name)
{
    size_t index = 0U;

    if (name.ptr == NULL || name.length == 0U ||
        name.length > SL_HTTP_DEFAULT_MAX_HEADER_NAME_LENGTH)
    {
        return false;
    }
    for (index = 0U; index < name.length; index += 1U) {
        if (!sl_run_once_header_name_char(name.ptr[index])) {
            return false;
        }
    }
    return true;
}

static int sl_run_once_parse_header(const char* text, SlHttpHeader* out_header)
{
    const char* colon = NULL;
    const char* value_start = NULL;
    SlStr name = {0};
    SlStr value = {0};

    if (text == NULL || out_header == NULL) {
        return 1;
    }
    colon = strchr(text, ':');
    if (colon == NULL) {
        return 1;
    }
    value_start = colon + 1;
    while (*value_start == ' ' || *value_start == '\t') {
        value_start += 1;
    }
    name = sl_str_from_parts(text, (size_t)(colon - text));
    value = sl_str_from_cstr(value_start);
    if (!sl_run_once_header_name_valid(name) ||
        value.length > SL_HTTP_DEFAULT_MAX_HEADER_VALUE_LENGTH ||
        sl_run_once_header_has_crlf(name) || sl_run_once_header_has_crlf(value))
    {
        return 1;
    }
    *out_header = (SlHttpHeader){.name = name, .value = value};
    return 0;
}

static bool sl_run_once_has_header(const SlHttpHeader* headers, size_t header_count,
                                   const char* name)
{
    size_t index = 0U;
    SlStr needle = sl_str_from_cstr(name);

    if (headers == NULL || name == NULL) {
        return false;
    }
    for (index = 0U; index < header_count; index += 1U) {
        if (sl_str_equal_ci_ascii(headers[index].name, needle)) {
            return true;
        }
    }
    return false;
}

static int sl_run_once_append_header(SlHttpHeader* headers, size_t* header_count, const char* name,
                                     const char* value)
{
    if (headers == NULL || header_count == NULL || name == NULL || value == NULL) {
        return 1;
    }
    if (*header_count >= SL_HTTP_DEFAULT_MAX_HEADERS) {
        sl_cli_write_cstr(stderr, "sloppy run: too many --once request headers\n");
        return 1;
    }
    headers[*header_count] =
        (SlHttpHeader){.name = sl_str_from_cstr(name), .value = sl_str_from_cstr(value)};
    *header_count += 1U;
    return 0;
}

static int sl_run_once_read_body_file(SlRunApp* app, const char* path, unsigned char* body_storage,
                                      size_t body_capacity, SlBytes* out_body)
{
    SlArena arena = {0};
    SlByteBuilder builder = {0};
    SlOwnedBytes bytes = {0};
    SlBytes view = {0};
    SlStatus status;

    if (app == NULL || path == NULL || body_storage == NULL || out_body == NULL) {
        return 1;
    }
    *out_body = sl_bytes_from_parts(NULL, 0U);

    status = sl_arena_init(&arena, app->file_read_storage, sizeof(app->file_read_storage));
    if (!sl_status_is_ok(status)) {
        return 1;
    }
    status = sl_fs_read_file(&arena, sl_str_from_cstr(path), &bytes, NULL);
    if (sl_status_code(status) == SL_STATUS_OUT_OF_RANGE) {
        sl_cli_write_error_with_value("sloppy run: missing --body-file: ", path, "\n");
        return 1;
    }
    if (!sl_status_is_ok(status)) {
        sl_cli_write_error_with_value("sloppy run: failed to read --body-file: ", path, "\n");
        return 1;
    }
    view = sl_owned_bytes_as_view(bytes);
    if (view.length > body_capacity) {
        sl_cli_write_error_with_value("sloppy run: --body-file exceeds request body limit: ", path,
                                      "\n");
        return 1;
    }
    status = sl_byte_builder_init_fixed(&builder, body_storage, body_capacity);
    if (!sl_status_is_ok(status)) {
        return 1;
    }
    status = sl_byte_builder_append_bytes(&builder, view);
    if (!sl_status_is_ok(status)) {
        return 1;
    }
    *out_body = sl_bytes_from_parts(body_storage, view.length);
    return 0;
}

static int sl_run_once_prepare_request(SlRunApp* app, const SlCliOptions* options,
                                       SlHttpRequestHead* request, SlHttpHeader* headers,
                                       size_t header_capacity, unsigned char* body_storage,
                                       size_t body_capacity, char* content_length_storage,
                                       size_t content_length_capacity)
{
    size_t index = 0U;
    SlBytes body = {0};
    const char* body_text = NULL;

    if (app == NULL || options == NULL || request == NULL || headers == NULL ||
        body_storage == NULL || content_length_storage == NULL)
    {
        return 1;
    }

    for (index = 0U; index < options->once_header_count; index += 1U) {
        if (request->header_count >= header_capacity ||
            sl_run_once_parse_header(options->once_headers[index],
                                     &headers[request->header_count]) != 0)
        {
            sl_cli_write_cstr(stderr, "sloppy run: --header requires a valid name:value header\n");
            return 1;
        }
        request->header_count += 1U;
    }

    if (options->once_json != NULL) {
        body_text = options->once_json;
        if (sl_run_once_has_header(headers, request->header_count, "Content-Type")) {
            sl_cli_write_cstr(
                stderr,
                "sloppy run: --json sets content-type; do not pass a Content-Type header\n");
            return 1;
        }
        if (sl_run_once_append_header(headers, &request->header_count, "Content-Type",
                                      "application/json; charset=utf-8") != 0)
        {
            return 1;
        }
    }
    else if (options->once_body != NULL) {
        body_text = options->once_body;
    }
    else if (options->once_body_file != NULL) {
        if (sl_run_once_read_body_file(app, options->once_body_file, body_storage, body_capacity,
                                       &body) != 0)
        {
            return 1;
        }
    }

    if (body_text != NULL) {
        size_t body_length = sl_str_from_cstr(body_text).length;
        if (body_length > body_capacity) {
            sl_cli_write_cstr(stderr, "sloppy run: --once request body exceeds limit\n");
            return 1;
        }
        body = sl_bytes_from_parts((const unsigned char*)body_text, body_length);
    }

    if (body.length != 0U) {
        SlStr content_length = {0};
        if (sl_run_once_has_header(headers, request->header_count, "Content-Length")) {
            sl_cli_write_cstr(
                stderr,
                "sloppy run: --once sets content-length; do not pass a Content-Length header\n");
            return 1;
        }
        if (!sl_status_is_ok(sl_string_format_size(content_length_storage, content_length_capacity,
                                                   body.length, &content_length)) ||
            sl_run_once_append_header(headers, &request->header_count, "Content-Length",
                                      content_length_storage) != 0)
        {
            return 1;
        }
    }

    request->headers = headers;
    request->body = body;
    return 0;
}

static int sl_run_once(SlRunApp* app, const SlCliOptions* options)
{
    unsigned char request_storage[SL_RUN_ARENA_BYTES];
    unsigned char body_storage[SL_HTTP_DEFAULT_MAX_BODY_LENGTH];
    SlArena request_arena = {0};
    SlHttpRequestHead request = {0};
    SlHttpHeader headers[SL_HTTP_DEFAULT_MAX_HEADERS];
    char content_length_storage[32];
    char response[SL_RUN_RESPONSE_MAX_BYTES];
    int response_length;
    const char* method = options == NULL ? NULL : options->once_method;
    const char* target = options == NULL ? NULL : options->once_target;

    if (target == NULL || target[0] != '/') {
        sl_cli_write_cstr(stderr, "sloppy run: --once target must start with /\n");
        return 1;
    }

    if (!sl_status_is_ok(sl_arena_init(&request_arena, request_storage, sizeof(request_storage)))) {
        return 1;
    }

    request.method = sl_run_method_from_cstr(method);
    request.raw_target = sl_str_from_cstr(target);
    request.path = sl_run_target_path(target);
    if (request.method == SL_HTTP_METHOD_UNKNOWN) {
        sl_cli_write_cstr(stderr,
                          "sloppy run: --once method is unsupported by the bounded parser\n");
        return 1;
    }

    if (sl_run_once_prepare_request(
            app, options, &request, headers, sizeof(headers) / sizeof(headers[0]), body_storage,
            sizeof(body_storage), content_length_storage, sizeof(content_length_storage)) != 0)
    {
        return 1;
    }

    if (app->has_dynamic_routes) {
        char eval_source[SL_RUN_RESPONSE_MAX_BYTES];
        unsigned char dispatch_storage[SL_RUN_ARENA_BYTES];
        SlArena dispatch_arena = {0};
        SlHttpResponse native_response = {0};
        SlHttpResponseWriteOptions write_options = {0};
        SlBytes native_response_bytes = {0};
        SlStringBuilder builder;
        SlStr eval_view = {0};
        SlEngineResult result = {0};
        SlDiag diag = {0};
        SlStatus status;

        if (app->route_table.route_count != 0U) {
            if (!sl_status_is_ok(
                    sl_arena_init(&dispatch_arena, dispatch_storage, sizeof(dispatch_storage))))
            {
                return 1;
            }
            status = sl_run_dispatch_response_with_storage(app, &request, NULL, &dispatch_arena,
                                                           &native_response, &diag);
            if (sl_status_is_ok(status)) {
                write_options.suppress_body = request.method == SL_HTTP_METHOD_HEAD;
                status = sl_http_response_write_with_options(
                    &native_response, &write_options, (unsigned char*)response, sizeof(response),
                    &native_response_bytes);
                if (!sl_status_is_ok(status) ||
                    native_response_bytes.length > (size_t)SL_RUN_RESPONSE_MAX_BYTES)
                {
                    sl_cli_write_cstr(stderr, "sloppy run: failed to write response\n");
                    return 1;
                }
                if (native_response_bytes.length != 0U &&
                    fwrite(response, 1U, native_response_bytes.length, stdout) !=
                        native_response_bytes.length)
                {
                    sl_cli_write_cstr(stderr, "sloppy run: failed to write response\n");
                    return 1;
                }
                return 0;
            }
            if (!(sl_status_code(status) == SL_STATUS_OUT_OF_RANGE &&
                  diag.code == SL_DIAG_HTTP_ROUTE_NOT_FOUND))
            {
                response_length = sl_run_write_dispatch_failure_response(
                    app, &dispatch_arena, &request, response, sizeof(response), status, &diag,
                    request.method == SL_HTTP_METHOD_HEAD);
                if (response_length < 0) {
                    sl_cli_write_cstr(stderr, "sloppy run: failed to write response\n");
                    return 1;
                }
                if (response_length != 0 && fwrite(response, 1U, (size_t)response_length, stdout) !=
                                                (size_t)response_length)
                {
                    sl_cli_write_cstr(stderr, "sloppy run: failed to write response\n");
                    return 1;
                }
                return 0;
            }
        }

        if (options->once_header_count != 0U || options->once_body != NULL ||
            options->once_body_file != NULL || options->once_json != NULL)
        {
            sl_cli_write_cstr(stderr, "sloppy run: --once headers and bodies require static route "
                                      "metadata for dynamic fallback routes today\n");
            return 1;
        }
        if (strchr(method, '"') != NULL || strchr(method, '\\') != NULL ||
            strchr(target, '"') != NULL || strchr(target, '\\') != NULL)
        {
            sl_cli_write_cstr(stderr, "sloppy run: dynamic --once method and target must not "
                                      "contain quote or backslash\n");
            return 1;
        }
        status = sl_string_builder_init_fixed(&builder, eval_source, sizeof(eval_source));
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(
                &builder, "globalThis.__sloppy_dynamic_once = function() { return "
                          "globalThis.__sloppy_dispatch_dynamic(\"");
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(&builder, method);
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(&builder, "\", \"");
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(&builder, target);
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(&builder, "\"); };");
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_view_with_nul(&builder, &eval_view);
        }
        if (!sl_status_is_ok(status)) {
            sl_cli_write_cstr(stderr, "sloppy run: dynamic --once dispatch source is too long\n");
            return 1;
        }
        status = sl_engine_eval_source(app->engine, sl_str_from_cstr("sloppy:dynamic-once"),
                                       eval_view, &diag);
        if (!sl_status_is_ok(status)) {
            sl_run_print_diag(
                "sloppy run: failed to prepare dynamic dispatch: ", &app->engine_arena, &diag);
            return 1;
        }
        status = sl_engine_call_function0(
            app->engine, &request_arena, sl_str_from_cstr("__sloppy_dynamic_once"), &result, &diag);
        if (!sl_status_is_ok(status) || result.kind != SL_ENGINE_RESULT_TEXT ||
            result.payload_kind != SL_ENGINE_RESULT_PAYLOAD_TEXT)
        {
            sl_run_print_diag("sloppy run: dynamic dispatch failed: ", &app->engine_arena, &diag);
            return 1;
        }
        if (result.text.length != 0U &&
            fwrite(result.text.ptr, 1U, result.text.length, stdout) != result.text.length)
        {
            sl_cli_write_cstr(stderr, "sloppy run: failed to write response\n");
            return 1;
        }
        return 0;
    }

    response_length = sl_run_dispatch_head(app, &request, response, sizeof(response));
    if (response_length < 0) {
        sl_cli_write_cstr(stderr, "sloppy run: failed to write response\n");
        return 1;
    }

    if (response_length != 0 &&
        fwrite(response, 1U, (size_t)response_length, stdout) != (size_t)response_length)
    {
        sl_cli_write_cstr(stderr, "sloppy run: failed to write response\n");
        return 1;
    }
    return 0;
}

static SlStatus sl_run_transport_dispatch(SlHttpTransportConnection* connection, SlArena* arena,
                                          const SlHttpRequestLifecycle* request,
                                          SlHttpResponse* out_response, SlDiag* out_diag,
                                          void* user)
{
    SlRunApp* app = (SlRunApp*)user;
    SlStatus status;
    SlStatus transport_status;

    (void)connection;
    if (app == NULL || arena == NULL || request == NULL || out_response == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_run_dispatch_response_with_storage(app, &request->head, request, arena,
                                                   out_response, out_diag);
    if (sl_status_is_ok(status)) {
        return status;
    }

    if (app->has_dynamic_routes && sl_status_code(status) == SL_STATUS_OUT_OF_RANGE &&
        out_diag != NULL && out_diag->code == SL_DIAG_HTTP_ROUTE_NOT_FOUND)
    {
        status =
            sl_run_dispatch_dynamic_response(app, arena, &request->head, out_response, out_diag);
        if (sl_status_is_ok(status)) {
            return status;
        }
    }

    if (sl_status_code(status) == SL_STATUS_UNSUPPORTED && out_diag != NULL &&
        out_diag->code == SL_DIAG_HTTP_UNSUPPORTED_METHOD)
    {
        SlStatus response_status =
            sl_run_method_not_allowed_response(app, arena, &request->head, out_response);
        if (sl_status_is_ok(response_status)) {
            return response_status;
        }
    }

    transport_status = sl_run_dispatch_transport_status(status, out_diag);
    if (sl_status_code(transport_status) == SL_STATUS_INTERNAL && out_diag != NULL) {
        sl_run_print_diag("sloppy run: HTTP dispatch failed\n", arena, out_diag);
    }
    return transport_status;
}

static SlStatus sl_run_transport_websocket_upgrade(SlHttpTransportConnection* connection,
                                                   SlArena* arena,
                                                   const SlHttpRequestLifecycle* request,
                                                   SlWebSocketHandshakeResult* out_result,
                                                   SlDiag* out_diag, void* user)
{
    SlRunApp* app = (SlRunApp*)user;
    SlHttpDispatchRouteMatch match = {0};
    SlStatus status;

    (void)connection;
    if (out_result != NULL) {
        *out_result = (SlWebSocketHandshakeResult){0};
    }
    if (app == NULL || arena == NULL || request == NULL || out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_http_dispatch_match_route(arena, &app->plan, &app->route_table.dispatch,
                                          &request->head, &match, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (match.route == NULL) {
        out_result->http_status = match.method_mismatch ? 405U : 404U;
        return sl_status_ok();
    }
    if (!sl_plan_route_is_websocket(match.route)) {
        out_result->http_status = 400U;
        return sl_status_ok();
    }

    return sl_websocket_build_server_handshake(arena, &request->head, match.route, out_result,
                                               out_diag);
}

static SlStatus sl_run_websocket_bridge_send(void* user,
                                             const SlWebSocketFrameWriteOptions* options,
                                             SlDiag* out_diag)
{
    return sl_http_transport_websocket_send((SlHttpTransportConnection*)user, options, out_diag);
}

static SlStatus sl_run_websocket_bridge_close(void* user, uint16_t code, SlStr reason,
                                              SlDiag* out_diag)
{
    SlHttpTransportConnection* connection = (SlHttpTransportConnection*)user;
    unsigned char payload[125];
    SlWebSocketFrameWriteOptions options = {0};
    bool previous_close_after_write = false;
    SlStatus status;

    if (connection == NULL || reason.length + 2U > sizeof(payload)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    payload[0] = (unsigned char)((code >> 8U) & 0xFFU);
    payload[1] = (unsigned char)(code & 0xFFU);
    if (reason.length != 0U) {
        for (size_t index = 0U; index < reason.length; index += 1U) {
            payload[2U + index] = (unsigned char)reason.ptr[index];
        }
    }
    options.fin = true;
    options.opcode = SL_WEBSOCKET_OPCODE_CLOSE;
    options.payload = sl_bytes_from_parts(payload, reason.length + 2U);
    previous_close_after_write = connection->close_after_write;
    connection->close_after_write = true;
    status = sl_http_transport_websocket_send(connection, &options, out_diag);
    if (!sl_status_is_ok(status)) {
        connection->close_after_write = previous_close_after_write;
    }
    return status;
}

static SlStr sl_run_extract_query_string(SlStr raw_target)
{
    if (raw_target.ptr == NULL) {
        return sl_str_empty();
    }
    for (size_t index = 0U; index < raw_target.length; index += 1U) {
        if (raw_target.ptr[index] == '?') {
            return sl_str_from_parts(raw_target.ptr + index + 1U, raw_target.length - index - 1U);
        }
    }
    return sl_str_empty();
}

static SlStatus sl_run_transport_websocket_open(SlHttpTransportConnection* connection,
                                                SlArena* arena,
                                                const SlHttpRequestLifecycle* request,
                                                SlDiag* out_diag, void* user)
{
    SlRunApp* app = (SlRunApp*)user;
    SlHttpDispatchRouteMatch match = {0};
    SlHttpQuery query = {0};
    SlHttpRequestContext context = {0};
    SlEngineWebSocketBridge bridge = {0};
    SlEngineWebSocketSession* session = NULL;
    SlStatus status;

    if (connection == NULL || arena == NULL || request == NULL || app == NULL ||
        app->engine == NULL)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_http_dispatch_match_route(arena, &app->plan, &app->route_table.dispatch,
                                          &request->head, &match, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (match.route == NULL || !sl_plan_route_is_websocket(match.route)) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    status = sl_http_query_parse(arena, request->head.raw_target, &query);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    context.request = &request->head;
    context.request_id = request->id;
    context.connection_id = request->connection == NULL ? 0U : request->connection->id;
    context.scheme = sl_str_is_empty(request->scheme) ? sl_str_from_cstr("http") : request->scheme;
    context.protocol = sl_str_from_cstr("HTTP/1.1");
    context.route_params = match.route_match.params;
    context.route_param_count = match.route_match.param_count;
    context.query_string = sl_run_extract_query_string(request->head.raw_target);
    context.query_params = query.params;
    context.query_param_count = query.param_count;
    context.cancellation = &request->cancellation;
    context.route_name = match.route->name;
    context.route_pattern = match.binding == NULL || match.binding->pattern == NULL
                                ? match.route->pattern
                                : match.binding->pattern->source;
    context.needs_route_params = true;
    context.needs_query_params = true;
    context.needs_headers = true;
    context.needs_header_facade = true;
    context.needs_request = true;
    context.needs_connection = true;
    context.needs_signal = true;
    context.needs_log = true;
    context.needs_metadata = true;

    bridge.user = connection;
    bridge.protocol = connection->websocket_selected_protocol;
    bridge.send = sl_run_websocket_bridge_send;
    bridge.close = sl_run_websocket_bridge_close;

    status = sl_engine_call_registered_websocket_handler_with_context(
        app->engine, arena, match.route->handler_id, &context, &bridge, &session, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    connection->websocket_user_state = session;
    return sl_status_ok();
}

static SlStatus sl_run_transport_websocket_frame(SlHttpTransportConnection* connection,
                                                 SlArena* arena, const SlWebSocketFrame* frame,
                                                 SlDiag* out_diag, void* user)
{
    SlRunApp* app = (SlRunApp*)user;
    SlEngineWebSocketSession* session =
        connection == NULL ? NULL : (SlEngineWebSocketSession*)connection->websocket_user_state;

    (void)arena;
    if (app == NULL || app->engine == NULL || session == NULL || frame == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (frame->opcode == SL_WEBSOCKET_OPCODE_CLOSE) {
        SlStr reason = sl_str_empty();
        uint16_t code = 1000U;
        if (frame->payload.length >= 2U) {
            code = (uint16_t)(((uint16_t)frame->payload.ptr[0] << 8U) | frame->payload.ptr[1]);
            if (frame->payload.length > 2U) {
                reason = sl_str_from_parts((const char*)frame->payload.ptr + 2U,
                                           frame->payload.length - 2U);
            }
        }
        return sl_engine_websocket_close(app->engine, session, code, reason, out_diag);
    }
    return sl_engine_websocket_receive(app->engine, session, frame, out_diag);
}

static SlHttpTransportConfig sl_run_transport_config(const char* host, uint16_t port, SlRunApp* app)
{
    SlHttpTransportConfig config = {0};
    size_t configured_body_bytes = SL_RUN_REQUEST_MAX_BYTES;

    config.host = sl_str_from_cstr(host);
    config.port = port;
    config.max_connections = SL_HTTP_BACKEND_DEFAULT_MAX_CONNECTIONS;
    config.max_active_requests = SL_HTTP_BACKEND_DEFAULT_MAX_ACTIVE_REQUESTS;
    config.connection_capacity = SL_HTTP_BACKEND_DEFAULT_MAX_CONNECTIONS;
    config.backlog = SL_HTTP_TRANSPORT_DEFAULT_BACKLOG;
    config.max_request_head_bytes = SL_RUN_REQUEST_MAX_BYTES;
    config.request_arena_bytes = SL_RUN_ARENA_BYTES;
    config.read_chunk_bytes = SL_HTTP_TRANSPORT_DEFAULT_READ_CHUNK_BYTES;
    config.max_response_bytes = SL_RUN_RESPONSE_MAX_BYTES;
    config.dispatch_mode = SL_HTTP_TRANSPORT_DISPATCH_MODE_EVENT_LOOP;
    config.max_dispatches_per_tick = SL_HTTP_TRANSPORT_DEFAULT_MAX_DISPATCHES_PER_TICK;
    config.parse.max_headers = SL_HTTP_DEFAULT_MAX_HEADERS;
    config.parse.max_request_line_length = SL_HTTP_DEFAULT_MAX_REQUEST_LINE_LENGTH;
    config.parse.max_target_length = SL_HTTP_DEFAULT_MAX_TARGET_LENGTH;
    config.parse.max_header_name_length = SL_HTTP_DEFAULT_MAX_HEADER_NAME_LENGTH;
    config.parse.max_header_value_length = SL_HTTP_DEFAULT_MAX_HEADER_VALUE_LENGTH;
    config.parse.max_total_header_bytes = SL_HTTP_DEFAULT_MAX_TOTAL_HEADER_BYTES;
    config.parse.max_body_length = SL_RUN_REQUEST_MAX_BYTES;
    if (app != NULL && app->config_has_max_connections) {
        config.max_connections = (size_t)app->config_max_connections;
        if (!app->config_has_connection_capacity) {
            config.connection_capacity = (size_t)app->config_max_connections;
        }
        if (!app->config_has_backlog) {
            config.backlog = app->config_max_connections > (uint64_t)INT_MAX
                                 ? INT_MAX
                                 : (int)app->config_max_connections;
        }
        if (config.backlog < SL_HTTP_TRANSPORT_DEFAULT_BACKLOG) {
            config.backlog = SL_HTTP_TRANSPORT_DEFAULT_BACKLOG;
        }
    }
    if (app != NULL && app->config_has_max_active_requests) {
        config.max_active_requests = (size_t)app->config_max_active_requests;
    }
    if (app != NULL && app->config_has_connection_capacity) {
        config.connection_capacity = (size_t)app->config_connection_capacity;
    }
    if (app != NULL && app->config_has_backlog) {
        config.backlog = (int)app->config_backlog;
    }
    if (app != NULL && app->config_has_max_request_head_bytes) {
        config.max_request_head_bytes = (size_t)app->config_max_request_head_bytes;
    }
    if (app != NULL && app->config_has_max_request_body_bytes) {
        config.parse.max_body_length = (size_t)app->config_max_request_body_bytes;
    }
    configured_body_bytes = config.parse.max_body_length;
    if (app != NULL && app->config_has_request_arena_bytes) {
        config.request_arena_bytes = (size_t)app->config_request_arena_bytes;
    }
    else if (config.request_arena_bytes < configured_body_bytes) {
        config.request_arena_bytes = configured_body_bytes;
    }
    if (app != NULL && app->config_has_max_request_wire_body_bytes) {
        config.max_request_wire_body_bytes = (size_t)app->config_max_request_wire_body_bytes;
    }
    else {
        config.max_request_wire_body_bytes =
            configured_body_bytes > SIZE_MAX - SL_RUN_REQUEST_WIRE_BODY_OVERHEAD_BYTES
                ? configured_body_bytes
                : configured_body_bytes + SL_RUN_REQUEST_WIRE_BODY_OVERHEAD_BYTES;
    }
    if (app != NULL && app->config_has_max_response_bytes) {
        config.max_response_bytes = (size_t)app->config_max_response_bytes;
    }
    if (app != NULL && app->config_has_max_pending_write_bytes) {
        config.max_pending_write_bytes = (size_t)app->config_max_pending_write_bytes;
    }
    if (app != NULL && app->config_has_http2_max_streams) {
        config.http2_max_streams = (size_t)app->config_http2_max_streams;
    }
    if (app != NULL && app->config_has_dispatch_on_event_loop) {
        config.dispatch_mode = app->config_dispatch_on_event_loop
                                   ? SL_HTTP_TRANSPORT_DISPATCH_MODE_EVENT_LOOP
                                   : SL_HTTP_TRANSPORT_DISPATCH_MODE_INLINE;
    }
    if (app != NULL && app->config_has_max_dispatches_per_tick) {
        config.max_dispatches_per_tick = (size_t)app->config_max_dispatches_per_tick;
    }
    if (app != NULL && app->config_has_keep_alive_enabled) {
        config.keep_alive_disabled = !app->config_keep_alive_enabled;
    }
    if (app != NULL && app->config_has_keep_alive_idle_timeout_ms) {
        config.keep_alive_idle_timeout_ms = app->config_keep_alive_idle_timeout_ms;
    }
    if (app != NULL && app->config_has_max_requests_per_connection) {
        config.max_requests_per_connection = (size_t)app->config_max_requests_per_connection;
    }
    if (app != NULL && app->config_has_request_timeout_ms) {
        config.request_timeout_ms = app->config_request_timeout_ms;
    }
    if (app != NULL && app->config_has_tls_enabled) {
        config.tls.enabled = app->config_tls_enabled;
        if (app->config_tls_enabled) {
            config.tls.backend = SL_HTTP_TRANSPORT_TLS_BACKEND_OPENSSL;
        }
    }
    if (app != NULL && app->config_has_tls_certificate_path) {
        config.tls.certificate_path = sl_str_from_cstr(app->config_tls_certificate_path);
    }
    if (app != NULL && app->config_has_tls_private_key_path) {
        config.tls.private_key_path = sl_str_from_cstr(app->config_tls_private_key_path);
    }
    config.dispatch = sl_run_transport_dispatch;
    config.dispatch_user = app;
    config.websocket_upgrade = sl_run_transport_websocket_upgrade;
    config.websocket_upgrade_user = app;
    config.websocket_open = sl_run_transport_websocket_open;
    config.websocket_open_user = app;
    config.websocket_frame = sl_run_transport_websocket_frame;
    config.websocket_frame_user = app;
    return config;
}

static int sl_run_server(SlRunApp* app, const char* host, uint16_t port)
{
    SlHeapBuffer transport_storage = {0};
    size_t transport_storage_bytes = 0U;
    SlArena transport_arena = {0};
    SlHttpTransportServer server = {0};
    SlHttpTransportConfig config = sl_run_transport_config(host, port, app);
    SlDiag diag = {0};
    SlStatus status;
    int result = 0;

    status = sl_http_transport_server_arena_size(&config, &transport_storage_bytes, &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag("sloppy run: failed to size dev server storage: ", NULL, &diag);
        return 1;
    }
    status = sl_heap_buffer_alloc(&transport_storage, transport_storage_bytes);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy run: failed to allocate dev server storage\n");
        return 1;
    }

    if (!sl_status_is_ok(
            sl_arena_init(&transport_arena, transport_storage.ptr, transport_storage.length)))
    {
        sl_cli_write_cstr(stderr, "sloppy run: failed to initialize dev server storage\n");
        sl_heap_buffer_dispose(&transport_storage);
        return 1;
    }

    status = sl_http_transport_server_init(&server, &transport_arena, &config, &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag("sloppy run: failed to initialize dev server: ", &transport_arena, &diag);
        sl_heap_buffer_dispose(&transport_storage);
        return 1;
    }
    status = sl_http_transport_server_listen(&server, &diag);
    if (!sl_status_is_ok(status)) {
        if (diag.code == SL_DIAG_HTTP_TRANSPORT_CONFIG) {
            sl_cli_write_cstr(stderr, "sloppy run: --host must be an IPv4 address\n");
        }
        else {
            sl_cli_write_cstr(stderr, "sloppy run: failed to listen on requested host/port\n");
        }
        sl_http_transport_server_dispose(&server, NULL);
        sl_heap_buffer_dispose(&transport_storage);
        return 1;
    }

    printf("Sloppy dev server listening on %s://%s:%u\n", config.tls.enabled ? "https" : "http",
           host, (unsigned)port);
    printf("Development server: HTTP/1.1%s, local development only.\n",
           config.tls.enabled ? " over TLS" : "");
    {
        sl_run_stop_requested = 0;
        void (*old_sigint)(int) = signal(SIGINT, sl_run_signal_handler);
        void (*old_sigterm)(int) = signal(SIGTERM, sl_run_signal_handler);
        while (!sl_run_stop_requested) {
            status = sl_http_transport_server_poll(&server, &diag);
            if (!sl_status_is_ok(status)) {
                sl_run_print_diag("sloppy run: HTTP server poll failed: ", &transport_arena, &diag);
                result = 1;
                break;
            }
            if (sl_http_transport_server_active_connections(&server) == 0U) {
                sl_platform_sleep_ms(1U);
            }
        }
        if (old_sigint != SIG_ERR) {
            signal(SIGINT, old_sigint);
        }
        if (old_sigterm != SIG_ERR) {
            signal(SIGTERM, old_sigterm);
        }
        if (result == 0 && sl_run_stop_requested) {
            status = sl_http_transport_server_stop(&server, &diag);
            if (!sl_status_is_ok(status)) {
                sl_run_print_diag("sloppy run: HTTP server stop failed: ", &transport_arena, &diag);
                result = 1;
            }
        }
    }
    sl_http_transport_server_dispose(&server, NULL);
    sl_heap_buffer_dispose(&transport_storage);
    return result;
}

static int sl_run_shutdown_app(SlRunApp* app)
{
    SlDiag diag = {0};
    SlStatus status;

    if (app == NULL) {
        return 1;
    }

    status = sl_app_lifecycle_shutdown(&app->lifecycle, &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag("sloppy run: app shutdown failed: ", &app->engine_arena, &diag);
        return 1;
    }

    return 0;
}

static bool sl_run_source_input_extension_supported(const char* path)
{
    SlCliSpan input = sl_cli_span_cstr(path);

    return sl_run_span_ends_with(input, ".js") || sl_run_span_ends_with(input, ".mjs") ||
           sl_run_span_ends_with(input, ".ts");
}

static bool sl_run_file_exists(const char* path)
{
    SlFsStat stat = {0};

    if (path == NULL || path[0] == '\0') {
        return false;
    }

    return sl_status_is_ok(sl_fs_stat(sl_str_from_cstr(path), &stat, NULL)) && stat.exists &&
           stat.kind == SL_FS_NODE_FILE;
}

static bool sl_run_copy_current_directory(char* buffer, size_t capacity)
{
    SlStatus status;

    if (buffer == NULL || capacity == 0U) {
        return false;
    }
    status = sl_platform_process_current_directory(buffer, capacity);
    if (!sl_status_is_ok(status) || buffer[0] == '\0') {
        return false;
    }
    return true;
}

static bool sl_run_append_program_args_json(SlStringBuilder* builder, const SlCliOptions* options)
{
    size_t index = 0U;

    if (builder == NULL || !sl_status_is_ok(sl_string_builder_append_char(builder, '['))) {
        return false;
    }
    if (options != NULL) {
        for (index = 0U; index < options->program_arg_count; index += 1U) {
            const char* arg = options->program_args != NULL ? options->program_args[index] : "";
            if (index != 0U && !sl_status_is_ok(sl_string_builder_append_char(builder, ','))) {
                return false;
            }
            if (!sl_cli_json_builder_append_string(builder, sl_cli_span_cstr(arg))) {
                return false;
            }
        }
    }
    return sl_status_is_ok(sl_string_builder_append_char(builder, ']'));
}

static int sl_run_set_program_invocation_context(SlRunApp* app, const SlCliOptions* options,
                                                 const char* environment)
{
    char source_storage[SL_RUN_PROGRAM_CONTEXT_BYTES];
    char cwd[SL_RUN_PATH_MAX_BYTES];
    SlStringBuilder builder = {0};
    SlStr source = {0};
    SlDiag diag = {0};
    SlStatus status;

    if (app == NULL || app->engine == NULL) {
        return 1;
    }
    if (!sl_run_copy_current_directory(cwd, sizeof(cwd))) {
        sl_cli_write_cstr(stderr, "sloppy run: failed to resolve program working directory\n");
        return 1;
    }
    if (environment == NULL || environment[0] == '\0') {
        environment = SL_RUN_DEFAULT_ENVIRONMENT;
    }

    status = sl_string_builder_init_fixed(&builder, source_storage, sizeof(source_storage));
    if (!sl_status_is_ok(status) ||
        !sl_status_is_ok(
            sl_string_builder_append_cstr(&builder, "(function(){const __sloppyArgs=")) ||
        !sl_run_append_program_args_json(&builder, options) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(
            &builder, ";const __sloppyFrozenArgs=Object.freeze(__sloppyArgs.slice());"
                      "globalThis.__sloppy_program_args=__sloppyFrozenArgs;"
                      "globalThis.__sloppy_program_context=Object.freeze({kind:\"program\",args:"
                      "__sloppyFrozenArgs,cwd:")) ||
        !sl_cli_json_builder_append_string(&builder, sl_cli_span_cstr(cwd)) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&builder, ",environment:")) ||
        !sl_cli_json_builder_append_string(&builder, sl_cli_span_cstr(environment)) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(
            &builder,
            ",plan:Object.freeze({kind:\"program\",metadataCompleteness:\"opaque\"})});})();")) ||
        !sl_status_is_ok(sl_string_builder_view_with_nul(&builder, &source)))
    {
        sl_cli_write_cstr(stderr, "sloppy run: program invocation context is too large\n");
        return 1;
    }

    status = sl_engine_eval_source(app->engine, sl_str_from_cstr("sloppy:program-context"), source,
                                   &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag("sloppy run: failed to stage program context: ", &app->engine_arena,
                          &diag);
        return 1;
    }
    return 0;
}

static bool sl_run_package_native_hash_matches(const char* path, yyjson_val* sha256)
{
    SlStr expected = {0};

    if (path == NULL || !yyjson_is_str(sha256)) {
        return false;
    }
    expected = sl_str_from_parts(yyjson_get_str(sha256), yyjson_get_len(sha256));
    return sl_run_hash_file_matches(expected, path);
}

static int sl_run_load_package_native_overrides(SlRunApp* app, const char* package_dir)
{
    char manifest_path[SL_RUN_PATH_MAX_BYTES];
    SlBytes manifest = {0};
    yyjson_read_err error = {0};
    yyjson_doc* doc = NULL;
    yyjson_val* root = NULL;
    yyjson_val* native = NULL;
    yyjson_val* libraries = NULL;
    yyjson_val* entry = NULL;
    yyjson_arr_iter iter;
    size_t index = 0U;
    int result = 0;

    if (app == NULL || package_dir == NULL || package_dir[0] == '\0') {
        return 0;
    }
    if (!sl_run_artifact_file_path(manifest_path, sizeof(manifest_path), package_dir,
                                   "manifest.json"))
    {
        sl_cli_write_cstr(stderr, "sloppy run: package manifest path is too long\n");
        return 1;
    }
    if (sl_run_read_file(app, manifest_path, app->package_manifest_json_storage,
                         sizeof(app->package_manifest_json_storage), &manifest) != 0)
    {
        return 1;
    }
    doc = yyjson_read_opts((char*)manifest.ptr, manifest.length, 0U, NULL, &error);
    if (doc == NULL) {
        sl_cli_write_error_with_value("sloppy run: malformed package manifest: ", manifest_path,
                                      "\n");
        return 1;
    }
    root = yyjson_doc_get_root(doc);
    native = yyjson_is_obj(root) ? yyjson_obj_get(root, "native") : NULL;
    libraries = yyjson_is_obj(native) ? yyjson_obj_get(native, "libraries") : NULL;
    if (libraries == NULL) {
        yyjson_doc_free(doc);
        return 0;
    }
    if (!yyjson_is_arr(libraries) || yyjson_arr_size(libraries) > SL_CLI_MAX_FFI_LIBRARIES) {
        sl_cli_write_cstr(stderr, "sloppy run: package manifest native.libraries is invalid\n");
        yyjson_doc_free(doc);
        return 1;
    }
    yyjson_arr_iter_init(libraries, &iter);
    while ((entry = yyjson_arr_iter_next(&iter)) != NULL) {
        yyjson_val* id = yyjson_is_obj(entry) ? yyjson_obj_get(entry, "id") : NULL;
        yyjson_val* path = yyjson_is_obj(entry) ? yyjson_obj_get(entry, "path") : NULL;
        yyjson_val* sha256 = yyjson_is_obj(entry) ? yyjson_obj_get(entry, "sha256") : NULL;
        SlCliSpan path_span = {0};

        if (id == NULL || path == NULL || !yyjson_is_str(id) || !yyjson_is_str(path) ||
            yyjson_get_len(id) == 0U ||
            yyjson_get_len(id) >= SL_SLOPPYRC_FFI_LIBRARY_NAME_MAX_BYTES)
        {
            sl_cli_write_cstr(stderr,
                              "sloppy run: package manifest native library entry is invalid\n");
            result = 1;
            break;
        }
        if (!sl_run_copy_json_string(app->ffi_library_override_names[index],
                                     sizeof(app->ffi_library_override_names[index]), id))
        {
            sl_cli_write_cstr(stderr, "sloppy run: package native library id is too long\n");
            result = 1;
            break;
        }
        path_span = (SlCliSpan){yyjson_get_str(path), yyjson_get_len(path)};
        if (!sl_run_join_path(app->ffi_library_override_paths[index],
                              sizeof(app->ffi_library_override_paths[index]), package_dir,
                              path_span))
        {
            sl_cli_write_cstr(stderr, "sloppy run: package native library path is too long\n");
            result = 1;
            break;
        }
        if (!sl_run_package_native_hash_matches(app->ffi_library_override_paths[index], sha256)) {
            sl_cli_write_cstr(stderr, "sloppy run: package native library hash mismatch\n");
            result = 1;
            break;
        }
        app->ffi_library_overrides[index].name =
            sl_str_from_cstr(app->ffi_library_override_names[index]);
        app->ffi_library_overrides[index].path =
            sl_str_from_cstr(app->ffi_library_override_paths[index]);
        index += 1U;
    }
    app->ffi_library_override_count = index;
    yyjson_doc_free(doc);
    return result;
}

static int sl_run_resolve_package_artifacts_path(const char* artifacts_path,
                                                 char* resolved_artifacts_path,
                                                 size_t resolved_artifacts_path_capacity,
                                                 const char** out_package_path,
                                                 const char** out_artifacts_path)
{
    if (out_artifacts_path == NULL) {
        return 1;
    }
    *out_artifacts_path = artifacts_path;
    if (out_package_path != NULL) {
        *out_package_path = NULL;
    }
    if (artifacts_path == NULL || artifacts_path[0] == '\0') {
        return 1;
    }
    if (!sl_cli_path_is_directory(artifacts_path)) {
        return 0;
    }
    if (sl_cli_directory_has_plan(artifacts_path)) {
        return 0;
    }
    if (!sl_cli_directory_has_package_manifest(artifacts_path)) {
        sl_cli_write_cstr(stderr, "sloppy run: artifact or package directory must contain "
                                  "app.plan.json or manifest.json\n");
        return 1;
    }
    if (out_package_path != NULL) {
        *out_package_path = artifacts_path;
    }
    if (!sl_run_artifact_file_path(resolved_artifacts_path, resolved_artifacts_path_capacity,
                                   artifacts_path, "artifacts"))
    {
        sl_cli_write_cstr(stderr, "sloppy run: package artifacts path is too long\n");
        return 1;
    }
    if (!sl_cli_directory_has_plan(resolved_artifacts_path)) {
        sl_cli_write_cstr(
            stderr, "sloppy run: package manifest points to missing artifacts/app.plan.json\n");
        return 1;
    }
    *out_artifacts_path = resolved_artifacts_path;
    return 0;
}

static bool sl_run_copy_package_stdlib_path(char* buffer, size_t capacity,
                                            const char* executable_path)
{
    SlCliSpan executable = {0};
    SlCliSpan executable_dir = {0};
    SlCliSpan package_root = {0};
    SlCliSpan bin_leaf = {0};
    SlStringBuilder root_builder = {0};
    SlStr root_view = {0};
    char root_buffer[SL_RUN_PATH_MAX_BYTES];
    size_t index = 0U;
    size_t file_separator = 0U;
    size_t root_separator = 0U;
    bool has_file_separator = false;
    bool has_root_separator = false;

    if (buffer == NULL || capacity == 0U || executable_path == NULL || executable_path[0] == '\0') {
        return false;
    }

    executable = sl_cli_span_cstr(executable_path);
    for (index = 0U; index < executable.length; index += 1U) {
        if (executable.ptr[index] == '/' || executable.ptr[index] == '\\') {
            file_separator = index;
            has_file_separator = true;
        }
    }
    if (!has_file_separator || file_separator == 0U) {
        return false;
    }

    executable_dir = (SlCliSpan){executable.ptr, file_separator};
    for (index = 0U; index < executable_dir.length; index += 1U) {
        if (executable_dir.ptr[index] == '/' || executable_dir.ptr[index] == '\\') {
            root_separator = index;
            has_root_separator = true;
        }
    }
    if (!has_root_separator || root_separator == 0U) {
        return false;
    }

    bin_leaf = (SlCliSpan){executable_dir.ptr + root_separator + 1U,
                           executable_dir.length - root_separator - 1U};
    if (!sl_cli_span_equal_cstr(bin_leaf, "bin")) {
        return false;
    }

    package_root = (SlCliSpan){executable_dir.ptr, root_separator};
    if (!sl_status_is_ok(
            sl_string_builder_init_fixed(&root_builder, root_buffer, sizeof(root_buffer))) ||
        !sl_cli_string_builder_append_span(&root_builder, package_root) ||
        !sl_status_is_ok(sl_string_builder_view_with_nul(&root_builder, &root_view)))
    {
        return false;
    }
    (void)root_view;

    return sl_run_join_path(buffer, capacity, root_buffer, sl_cli_span_cstr("stdlib/sloppy"));
}

static bool sl_run_try_package_stdlib_path(const SlCliOptions* options, char* buffer,
                                           size_t capacity)
{
    char manifest_path[SL_RUN_PATH_MAX_BYTES];

    if (options == NULL ||
        !sl_run_copy_package_stdlib_path(buffer, capacity, options->executable_path))
    {
        return false;
    }
    if (!sl_run_artifact_file_path(manifest_path, sizeof(manifest_path), buffer,
                                   "bootstrap.manifest.json"))
    {
        return false;
    }

    return sl_run_file_exists(manifest_path);
}

static bool sl_run_copy_json_string(char* buffer, size_t capacity, yyjson_val* value)
{
    SlStringBuilder builder = {0};
    SlStr view = {0};
    SlStr text = {0};
    size_t index = 0U;
    SlStatus status;

    if (buffer == NULL || capacity == 0U || value == NULL || !yyjson_is_str(value)) {
        return false;
    }

    status = sl_string_builder_init_fixed(&builder, buffer, capacity);
    if (!sl_status_is_ok(status)) {
        return false;
    }

    text = sl_str_from_parts(yyjson_get_str(value), yyjson_get_len(value));
    if (!sl_status_is_ok(sl_str_validate_no_nul(text))) {
        return false;
    }
    for (index = 0U; index < text.length; index += 1U) {
        status = sl_string_builder_append_char(&builder, text.ptr[index]);
        if (!sl_status_is_ok(status)) {
            return false;
        }
    }

    status = sl_string_builder_view_with_nul(&builder, &view);
    return sl_status_is_ok(status) && view.ptr == buffer && view.length == text.length;
}

static const char* sl_run_getenv(const char* name)
{
    const char* value = NULL;
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#ifdef _MSC_VER
#pragma warning(suppress : 4996)
#endif
    value = getenv(name);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    return value;
}

static const char* sl_run_resolve_compiler_path(const char* command_name)
{
    const char* explicit_path = sl_run_getenv("SLOPPY_SLOPPYC");
    const char* built_path = SLOPPY_COMPILER_BUILD_PATH;
    const char* prefix = command_name != NULL ? command_name : "sloppy run";

    if (explicit_path != NULL && explicit_path[0] != '\0') {
        if (!sl_run_file_exists(explicit_path)) {
            sl_cli_write_cstr(stderr, prefix);
            sl_cli_write_error_with_value(": compiler unavailable: ", explicit_path, "\n");
            return NULL;
        }
        return explicit_path;
    }

    if (built_path[0] != '\0' && sl_run_file_exists(built_path)) {
        return built_path;
    }

    return "sloppyc";
}

static int sl_run_compile_source(const char* source_path, const char* out_dir,
                                 const SlCliOptions* options, const char* environment,
                                 const char* command_name, const char* config_dir,
                                 const char* project_kind, const SlSloppyRunConfig* project_config)
{
    const char* compiler_path = NULL;
    char* compiler_argv[160];
    char port_text[6];
    size_t arg_count = 0U;
    size_t capability_index = 0U;
    size_t include_index = 0U;
    SlPlatformProcessArgs process_args = {0};
    SlStatus status;
    int exit_code = 1;
    const char* prefix = command_name != NULL ? command_name : "sloppy run";

    if (source_path == NULL || out_dir == NULL || source_path[0] == '\0' || out_dir[0] == '\0') {
        sl_cli_write_cstr(stderr, prefix);
        sl_cli_write_cstr(stderr, ": source-input handoff is missing source or outDir\n");
        return 1;
    }

    if (!sl_run_file_exists(source_path)) {
        sl_cli_write_cstr(stderr, prefix);
        sl_cli_write_error_with_value(": missing source file: ", source_path, "\n");
        return 1;
    }

    compiler_path = sl_run_resolve_compiler_path(prefix);
    if (compiler_path == NULL) {
        return 1;
    }

    compiler_argv[arg_count++] = (char*)compiler_path;
    compiler_argv[arg_count++] = "build";
    compiler_argv[arg_count++] = (char*)source_path;
    compiler_argv[arg_count++] = "--out";
    compiler_argv[arg_count++] = (char*)out_dir;
    if (options != NULL && options->kind_explicit) {
        compiler_argv[arg_count++] = "--kind";
        compiler_argv[arg_count++] = (char*)options->kind;
    }
    else if (project_kind != NULL && project_kind[0] != '\0') {
        compiler_argv[arg_count++] = "--kind";
        compiler_argv[arg_count++] = (char*)project_kind;
    }
    if (environment != NULL && environment[0] != '\0') {
        compiler_argv[arg_count++] = "--environment";
        compiler_argv[arg_count++] = (char*)environment;
    }
    if (config_dir != NULL && config_dir[0] != '\0') {
        compiler_argv[arg_count++] = "--config-dir";
        compiler_argv[arg_count++] = (char*)config_dir;
    }
    if (project_config != NULL) {
        if (project_config->capability_count > 0U) {
            compiler_argv[arg_count++] = "--capability-origin";
            compiler_argv[arg_count++] = "sloppy.json";
        }
        for (capability_index = 0U; capability_index < project_config->capability_count;
             capability_index += 1U)
        {
            compiler_argv[arg_count++] = "--capability";
            compiler_argv[arg_count++] = (char*)project_config->capabilities[capability_index];
        }
        for (include_index = 0U; include_index < project_config->module_include_count;
             include_index += 1U)
        {
            compiler_argv[arg_count++] = "--module-include";
            compiler_argv[arg_count++] = (char*)project_config->module_includes[include_index];
        }
        for (include_index = 0U; include_index < project_config->asset_include_count;
             include_index += 1U)
        {
            compiler_argv[arg_count++] = "--asset-include";
            compiler_argv[arg_count++] = (char*)project_config->asset_includes[include_index];
        }
    }
    if (options != NULL && options->host_explicit) {
        compiler_argv[arg_count++] = "--host";
        compiler_argv[arg_count++] = (char*)options->host;
    }
    if (options != NULL && options->port_explicit) {
        SlStringBuilder port_builder = {0};
        SlStr port_view = {0};
        if (!sl_status_is_ok(
                sl_string_builder_init_fixed(&port_builder, port_text, sizeof(port_text))) ||
            !sl_status_is_ok(sl_string_builder_append_u64(&port_builder, options->port)) ||
            !sl_status_is_ok(sl_string_builder_view_with_nul(&port_builder, &port_view)))
        {
            sl_cli_write_cstr(stderr, prefix);
            sl_cli_write_cstr(stderr, ": failed to format compiler port override\n");
            return 1;
        }
        (void)port_view;
        compiler_argv[arg_count++] = "--port";
        compiler_argv[arg_count++] = port_text;
    }
    compiler_argv[arg_count] = NULL;

    process_args.file = compiler_path;
    process_args.argv = compiler_argv;
    status = sl_platform_process_run(&process_args, &exit_code);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, prefix);
        sl_cli_write_error_with_value(": compiler unavailable: ", compiler_path, "\n");
        return 1;
    }
    if (exit_code != 0) {
        sl_cli_write_cstr(stderr, prefix);
        sl_cli_write_cstr(stderr,
                          ": compiler handoff failed at the source-input command boundary\n");
        return 1;
    }

    return 0;
}

static int sl_run_prepare_source_input(const SlCliOptions* options, char* artifacts_path,
                                       size_t artifacts_path_capacity, char* environment_out,
                                       size_t environment_out_capacity)
{
    SlSloppyRunConfig config = {0};
    const char* source_path = NULL;
    const char* out_dir = NULL;
    const char* environment = NULL;
    const char* config_dir = NULL;
    const char* project_kind = NULL;
    const SlSloppyRunConfig* project_config = NULL;

    if (options == NULL || artifacts_path == NULL || artifacts_path_capacity == 0U) {
        return 1;
    }

    if (options->input_path != NULL) {
        source_path = options->input_path;
        out_dir = SL_RUN_DEFAULT_SOURCE_OUT_DIR;
        environment =
            options->environment_explicit ? options->environment : SL_RUN_DEFAULT_ENVIRONMENT;
    }
    else {
        if (sl_sloppyrc_load(&config) != 0) {
            return 1;
        }
        source_path = config.entry;
        out_dir = config.out_dir;
        environment = options->environment_explicit ? options->environment : config.environment;
        config_dir = ".";
        project_kind = config.kind;
        project_config = &config;
    }

    if (!sl_run_source_input_extension_supported(source_path)) {
        sl_cli_write_error_with_value("sloppy run: unsupported source input: ", source_path, "\n");
        return 1;
    }

    if (environment_out != NULL && environment_out_capacity != 0U &&
        !sl_cli_copy_cstr(environment_out, environment_out_capacity, environment))
    {
        sl_cli_write_cstr(stderr, "sloppy run: environment name is too long\n");
        return 1;
    }

    if (sl_run_compile_source(source_path, out_dir, options, environment, "sloppy run", config_dir,
                              project_kind, project_config) != 0)
    {
        return 1;
    }
    if (sl_cli_openapi_refresh_docs_artifact(out_dir, "sloppy run") != 0) {
        return 1;
    }

    {
        SlStringBuilder builder = {0};
        SlStr view = {0};
        if (!sl_status_is_ok(
                sl_string_builder_init_fixed(&builder, artifacts_path, artifacts_path_capacity)) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(&builder, out_dir)) ||
            !sl_status_is_ok(sl_string_builder_view_with_nul(&builder, &view)))
        {
            sl_cli_write_cstr(stderr,
                              "sloppy run: source-input artifact directory path is too long\n");
            return 1;
        }
    }

    return 0;
}

static int sl_cli_command_build(const SlCliOptions* options)
{
    SlSloppyRunConfig config = {0};
    SlRunApp* app = NULL;
    const char* source_path = NULL;
    const char* out_dir = NULL;
    const char* environment = NULL;
    const char* config_dir = NULL;
    const char* project_kind = NULL;
    const SlSloppyRunConfig* project_config = NULL;
    int result = 0;

    if (options == NULL) {
        return 1;
    }
    app = sl_run_command_app();

    if (options->input_path == NULL) {
        if (options->out_explicit) {
            sl_cli_write_cstr(stderr,
                              "sloppy build: --out applies to positional source input; project "
                              "build uses sloppy.json outDir\n");
            return 1;
        }
        if (sl_sloppyrc_load_for_command(&config, "sloppy build") != 0) {
            return 1;
        }
        source_path = config.entry;
        out_dir = config.out_dir;
        environment = options->environment_explicit ? options->environment : config.environment;
        config_dir = ".";
        project_kind = config.kind;
        project_config = &config;
    }
    else {
        source_path = options->input_path;
        out_dir = options->out_explicit ? options->out_dir : SL_RUN_DEFAULT_SOURCE_OUT_DIR;
        environment =
            options->environment_explicit ? options->environment : SL_RUN_DEFAULT_ENVIRONMENT;
    }

    if (!sl_run_source_input_extension_supported(source_path)) {
        sl_cli_write_error_with_value("sloppy build: unsupported source input: ", source_path,
                                      "\n");
        return 1;
    }

    if (sl_run_compile_source(source_path, out_dir, options, environment, "sloppy build",
                              config_dir, project_kind, project_config) != 0)
    {
        return 1;
    }

    result = sl_run_load_artifacts(out_dir, "sloppy build", app, false);
    if (result == 0) {
        result = sl_cli_openapi_refresh_docs_artifact(out_dir, "sloppy build");
    }
    if (sl_run_shutdown_app(app) != 0 && result == 0) {
        result = 1;
    }
    return result;
}

static bool sl_run_program_json_object_has_true(yyjson_val* object, const char* name)
{
    yyjson_val* value = object != NULL ? yyjson_obj_get(object, name) : NULL;
    return value != NULL && yyjson_is_bool(value) && yyjson_get_bool(value);
}

static int sl_run_program_json_exit_code(yyjson_val* object, int* out)
{
    yyjson_val* value = object != NULL ? yyjson_obj_get(object, "exitCode") : NULL;
    int64_t code = 0;

    if (out == NULL) {
        return 1;
    }
    *out = 0;
    if (value == NULL || yyjson_is_null(value)) {
        return 0;
    }
    if (!yyjson_is_int(value)) {
        sl_cli_write_cstr(stderr,
                          "sloppy run: program result exitCode must be an integer from 0 to 255\n");
        return 1;
    }
    code = yyjson_get_sint(value);
    if (code < 0 || code > 255) {
        sl_cli_write_cstr(stderr,
                          "sloppy run: program result exitCode must be an integer from 0 to 255\n");
        return 1;
    }
    *out = (int)code;
    return 0;
}

static int sl_run_program_emit_json_result(SlBytes body, bool* handled, int* out_exit_code)
{
    yyjson_read_err error = {0};
    yyjson_doc* doc = NULL;
    yyjson_val* root = NULL;
    yyjson_val* events = NULL;
    yyjson_arr_iter iter;
    yyjson_val* event = NULL;
    int exit_code = 0;

    if (handled == NULL || out_exit_code == NULL) {
        return 1;
    }
    *handled = false;
    *out_exit_code = 0;
    if (body.ptr == NULL || body.length == 0U) {
        return 0;
    }

    doc = yyjson_read_opts((char*)body.ptr, body.length, 0U, NULL, &error);
    if (doc == NULL) {
        return 0;
    }
    root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root) || !sl_run_program_json_object_has_true(root, "__sloppyProgramResult"))
    {
        yyjson_doc_free(doc);
        return 0;
    }

    if (sl_run_program_json_exit_code(root, &exit_code) != 0) {
        yyjson_doc_free(doc);
        return 1;
    }

    events = yyjson_obj_get(root, "events");
    if (events != NULL && !yyjson_is_arr(events)) {
        sl_cli_write_cstr(stderr, "sloppy run: program result events must be an array\n");
        yyjson_doc_free(doc);
        return 1;
    }

    if (events != NULL) {
        yyjson_arr_iter_init(events, &iter);
        while ((event = yyjson_arr_iter_next(&iter)) != NULL) {
            yyjson_val* stream_value =
                yyjson_is_obj(event) ? yyjson_obj_get(event, "stream") : NULL;
            yyjson_val* text_value = yyjson_is_obj(event) ? yyjson_obj_get(event, "text") : NULL;
            FILE* stream = NULL;
            const char* text = NULL;
            size_t text_length = 0U;

            if (stream_value == NULL || text_value == NULL || !yyjson_is_str(stream_value) ||
                !yyjson_is_str(text_value))
            {
                sl_cli_write_cstr(stderr,
                                  "sloppy run: program result events must contain stream/text\n");
                yyjson_doc_free(doc);
                return 1;
            }
            if (sl_run_json_string_equals_cstr(stream_value, "stdout")) {
                stream = stdout;
            }
            else if (sl_run_json_string_equals_cstr(stream_value, "stderr")) {
                stream = stderr;
            }
            else {
                sl_cli_write_cstr(stderr,
                                  "sloppy run: program result stream must be stdout or stderr\n");
                yyjson_doc_free(doc);
                return 1;
            }

            text = yyjson_get_str(text_value);
            text_length = yyjson_get_len(text_value);
            if (text_length != 0U && fwrite(text, 1U, text_length, stream) != text_length) {
                yyjson_doc_free(doc);
                return 1;
            }
        }
    }

    yyjson_doc_free(doc);
    *handled = true;
    *out_exit_code = exit_code;
    return 0;
}

static int sl_run_program_main(SlRunApp* app, const SlCliOptions* options, const char* environment)
{
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlStatus status;
    bool handled_program_result = false;
    int program_exit_code = 0;

    if (app == NULL || app->engine == NULL) {
        return 1;
    }

    if (sl_run_set_program_invocation_context(app, options, environment) != 0) {
        return 1;
    }

    status = sl_engine_call_function0(app->engine, &app->engine_arena,
                                      sl_str_from_cstr("__sloppy_program_main"), &result, &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag("sloppy run: program execution failed: ", &app->engine_arena, &diag);
        return 1;
    }

    if (result.kind == SL_ENGINE_RESULT_TEXT &&
        result.payload_kind == SL_ENGINE_RESULT_PAYLOAD_TEXT)
    {
        if (!sl_cli_write_str(stdout, result.text)) {
            return 1;
        }
        sl_cli_write_cstr(stdout, "\n");
    }
    else if (result.kind == SL_ENGINE_RESULT_TEXT &&
             result.payload_kind == SL_ENGINE_RESULT_PAYLOAD_RESPONSE)
    {
        if (result.response.body.length != 0U &&
            fwrite(result.response.body.ptr, 1U, result.response.body.length, stdout) !=
                result.response.body.length)
        {
            return 1;
        }
        sl_cli_write_cstr(stdout, "\n");
    }
    else if (result.kind == SL_ENGINE_RESULT_JSON &&
             result.payload_kind == SL_ENGINE_RESULT_PAYLOAD_RESPONSE)
    {
        if (sl_run_program_emit_json_result(result.response.body, &handled_program_result,
                                            &program_exit_code) != 0)
        {
            return 1;
        }
        if (handled_program_result) {
            return program_exit_code;
        }
        if (result.response.body.length != 0U &&
            fwrite(result.response.body.ptr, 1U, result.response.body.length, stdout) !=
                result.response.body.length)
        {
            return 1;
        }
        sl_cli_write_cstr(stdout, "\n");
    }
    else if (result.kind == SL_ENGINE_RESULT_BYTES &&
             result.payload_kind == SL_ENGINE_RESULT_PAYLOAD_RESPONSE)
    {
        if (result.response.body.length != 0U &&
            fwrite(result.response.body.ptr, 1U, result.response.body.length, stdout) !=
                result.response.body.length)
        {
            return 1;
        }
    }
    else {
        fprintf(stderr,
                "sloppy run: engine returned unsupported program result payload: kind=%d "
                "payload_kind=%d\n",
                (int)result.kind, (int)result.payload_kind);
        return 1;
    }

    return 0;
}

static int sl_cli_command_run(const SlCliOptions* options)
{
    SlRunApp* app = NULL;
    const char* artifacts_path = NULL;
    const char* stdlib_path = NULL;
    const char* run_host = NULL;
    uint16_t run_port = 0U;
    char source_artifacts_path[SL_RUN_PATH_MAX_BYTES];
    char resolved_artifacts_path[SL_RUN_PATH_MAX_BYTES];
    char package_stdlib_path[SL_RUN_PATH_MAX_BYTES];
    char program_environment[SL_RUN_CONFIG_HOST_MAX_BYTES];
    const char* package_path = NULL;
    int result = 0;

    if (options == NULL) {
        return 1;
    }
    sl_run_diagnostics_json_enabled = options->diagnostics_json;
    sl_breadcrumb_ring_init(sl_breadcrumb_global_ring());
    sl_breadcrumb_global_record(SL_DIAG_SUBSYSTEM_CLI, SL_BREADCRUMB_EVENT_PROCESS_START,
                                SL_STATUS_OK, 0U, 0U, 0U, 0U, sl_str_from_cstr("sloppy run"));
    app = sl_run_command_app();
    artifacts_path = options->artifacts_path;
    stdlib_path = options->stdlib_path;
    run_host = options->host;
    run_port = options->port;
    if (!sl_cli_copy_cstr(program_environment, sizeof(program_environment),
                          options->environment_explicit ? options->environment
                                                        : SL_RUN_DEFAULT_ENVIRONMENT))
    {
        sl_cli_write_cstr(stderr, "sloppy run: environment name is too long\n");
        return 1;
    }

    if (options->environment_explicit && artifacts_path == NULL && options->input_path != NULL &&
        !sl_run_source_input_extension_supported(options->input_path))
    {
        sl_cli_write_cstr(
            stderr, "sloppy run: --environment only applies to source input or sloppy.json\n");
        return 1;
    }
    if (options->environment_explicit && artifacts_path != NULL) {
        sl_cli_write_cstr(
            stderr, "sloppy run: --environment only applies to source input or sloppy.json\n");
        return 1;
    }

    if (artifacts_path == NULL) {
        if (options->input_path == NULL && !sl_run_file_exists("sloppy.json") &&
            sl_cli_directory_has_plan(".sloppy"))
        {
            artifacts_path = ".sloppy";
        }
    }

    if (artifacts_path == NULL) {
        if (options->input_path == NULL ||
            sl_run_source_input_extension_supported(options->input_path))
        {
            if (sl_run_prepare_source_input(options, source_artifacts_path,
                                            sizeof(source_artifacts_path), program_environment,
                                            sizeof(program_environment)) != 0)
            {
                return 1;
            }
            artifacts_path = source_artifacts_path;
        }
        else {
            sl_cli_write_error_with_value(
                "sloppy run: unsupported source input: ", options->input_path, "\n");
            return 1;
        }
    }

    if (artifacts_path == NULL) {
        sl_cli_write_cstr(stderr,
                          "sloppy run: expected source input, sloppy.json, or --artifacts <dir>\n");
        return 1;
    }

    if (sl_run_resolve_package_artifacts_path(artifacts_path, resolved_artifacts_path,
                                              sizeof(resolved_artifacts_path), &package_path,
                                              &artifacts_path) != 0)
    {
        return 1;
    }

    if (stdlib_path == NULL) {
        if (sl_run_try_package_stdlib_path(options, package_stdlib_path,
                                           sizeof(package_stdlib_path)))
        {
            stdlib_path = package_stdlib_path;
        }
        else {
            stdlib_path = SLOPPY_BOOTSTRAP_BUILD_DIR;
        }
    }

    {
        SlCrashReportOptions report_options = sl_crash_report_default_options();
        SlCrashReportContext report_context = {0};
        report_context.report_context.command = sl_str_from_cstr("run");
        report_context.report_context.runtime_version = sl_str_from_cstr(SL_VERSION_STRING);
        report_context.report_context.artifact_path =
            artifacts_path != NULL ? sl_str_from_cstr(artifacts_path) : sl_str_empty();
        report_context.report_context.package_path =
            package_path != NULL ? sl_str_from_cstr(package_path) : sl_str_empty();
        sl_crash_report_set_default_context(&report_options, &report_context,
                                            sl_breadcrumb_global_ring());
    }
    if (sl_run_load_app(artifacts_path, stdlib_path, package_path, app) != 0) {
        sl_run_shutdown_app(app);
        return 1;
    }

    if (app->plan.kind == SL_PLAN_KIND_PROGRAM) {
        if (options->once_method != NULL) {
            sl_cli_write_cstr(stderr, "sloppy run: --once is only valid for web Plans\n");
            result = 1;
        }
        else {
            result = sl_run_program_main(app, options, program_environment);
        }
    }
    else if (options->once_method != NULL) {
        if (options->program_args_explicit) {
            sl_cli_write_cstr(
                stderr,
                "sloppy run: program arguments after -- are only valid for Program Plans\n");
            result = 1;
        }
        else {
            result = sl_run_once(app, options);
        }
    }
    else if (options->program_args_explicit) {
        sl_cli_write_cstr(
            stderr, "sloppy run: program arguments after -- are only valid for Program Plans\n");
        result = 1;
    }
    else {
        if (!options->host_explicit && app->config_has_host) {
            run_host = app->config_host;
        }
        if (!options->port_explicit && app->config_has_port) {
            run_port = app->config_port;
        }
        result = sl_run_server(app, run_host, run_port);
    }

    if (sl_run_shutdown_app(app) != 0 && result == 0) {
        result = 1;
    }
    return result;
}
