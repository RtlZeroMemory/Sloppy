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

static int sl_package_write_manifest(const char* package_dir, const char* artifact_dir_name,
                                     const char* config_dir, const SlRunApp* app,
                                     const SlSloppyRunConfig* project_config)
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
        !sl_status_is_ok(sl_string_builder_append_cstr(&builder, "\",\n"
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
        !sl_status_is_ok(sl_string_builder_append_cstr(&builder, "\n"
                                                                 "  }")) ||
        !sl_package_append_native_libraries_manifest(&builder, artifact_dir_name, config_dir, app,
                                                     project_config) ||
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

static int sl_package_copy_artifacts(const char* out_dir, const char* package_dir,
                                     const SlRunApp* app, const char* config_dir,
                                     const SlSloppyRunConfig* project_config)
{
    char artifacts_dir[SL_RUN_PATH_MAX_BYTES];
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

    if (sl_package_copy_native_libraries(package_dir, config_dir, app, project_config) != 0) {
        sl_package_cleanup_partial(package_dir);
        return 1;
    }

    if (sl_package_write_manifest(package_dir, "artifacts", config_dir, app, project_config) != 0) {
        sl_package_cleanup_partial(package_dir);
        return 1;
    }

    return 0;
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
    char package_dir[SL_RUN_PATH_MAX_BYTES];
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
        sl_cli_write_error_with_value("sloppy package: unsupported source input: ", source_path,
                                      "\n");
        return 1;
    }

    if (sl_run_compile_source(source_path, out_dir, options, environment, "sloppy package",
                              config_dir, project_kind, project_config) != 0)
    {
        return 1;
    }

    result = sl_run_load_artifacts(out_dir, "sloppy package", app, false);
    if (result == 0) {
        if (!sl_run_artifact_file_path(package_dir, sizeof(package_dir), out_dir, "package")) {
            sl_cli_write_cstr(stderr, "sloppy package: package path is too long\n");
            result = 1;
        }
        else {
            result =
                sl_package_copy_artifacts(out_dir, package_dir, app, config_dir, project_config);
        }
    }
    if (sl_run_shutdown_app(app) != 0 && result == 0) {
        result = 1;
    }
    if (result != 0) {
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
