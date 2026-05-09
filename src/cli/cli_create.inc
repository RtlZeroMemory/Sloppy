/*
 * src/cli/cli_create.inc
 *
 * Private implementation of `sloppy create`.
 * Included only by src/main.c to preserve existing static helper visibility.
 */
static bool sl_create_template_name_supported(const char* name)
{
    return name != NULL &&
           (strcmp(name, "minimal-api") == 0 || strcmp(name, "full-api") == 0 ||
            strcmp(name, "dogfood") == 0);
}

static bool sl_create_project_name_is_valid(const char* name)
{
    size_t index = 0U;

    if (name == NULL || name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return false;
    }

    for (index = 0U; name[index] != '\0'; index += 1U) {
        unsigned char ch = (unsigned char)name[index];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_')
        {
            continue;
        }
        return false;
    }

    return true;
}

static bool sl_create_directory_exists(const char* path)
{
    SlFsStat stat = {0};

    return path != NULL && path[0] != '\0' &&
           sl_status_is_ok(sl_fs_stat(sl_str_from_cstr(path), &stat, NULL)) && stat.exists &&
           stat.kind == SL_FS_NODE_DIRECTORY;
}

static bool sl_create_destination_is_empty(const char* path)
{
    unsigned char arena_storage[SL_CREATE_ARENA_BYTES];
    SlArena arena = {0};
    SlFsDirectoryList list = {0};
    SlFsStat stat = {0};
    SlStatus status;
    bool exists = false;

    if (path == NULL || path[0] == '\0') {
        return false;
    }

    status = sl_fs_exists(sl_str_from_cstr(path), &exists, NULL);
    if (!sl_status_is_ok(status)) {
        return false;
    }
    if (!exists) {
        return true;
    }
    status = sl_fs_stat(sl_str_from_cstr(path), &stat, NULL);
    if (!sl_status_is_ok(status)) {
        return false;
    }
    if (stat.kind != SL_FS_NODE_DIRECTORY) {
        return false;
    }

    status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
    if (!sl_status_is_ok(status)) {
        return false;
    }

    status = sl_fs_list_directory(&arena, sl_str_from_cstr(path), &list, NULL);
    return sl_status_is_ok(status) && list.count == 0U;
}

static bool sl_create_join_path(char* buffer, size_t capacity, const char* dir, const char* leaf)
{
    return sl_run_join_path(buffer, capacity, dir, sl_cli_span_cstr(leaf));
}

static bool sl_create_join_span_path(char* buffer, size_t capacity, const char* dir, SlStr leaf)
{
    return sl_run_join_path(buffer, capacity, dir, sl_cli_span_from_str(leaf));
}

static bool sl_create_copy_package_template_path(char* buffer, size_t capacity,
                                                const char* executable_path,
                                                const char* template_name)
{
    SlCliSpan executable = {0};
    SlCliSpan executable_dir = {0};
    SlCliSpan package_root = {0};
    SlCliSpan bin_leaf = {0};
    SlStringBuilder root_builder = {0};
    SlStr root_view = {0};
    char root_buffer[SL_RUN_PATH_MAX_BYTES];
    char templates_buffer[SL_RUN_PATH_MAX_BYTES];
    size_t index = 0U;
    size_t file_separator = 0U;
    size_t root_separator = 0U;
    bool has_file_separator = false;
    bool has_root_separator = false;

    if (buffer == NULL || capacity == 0U || executable_path == NULL || executable_path[0] == '\0' ||
        template_name == NULL || template_name[0] == '\0')
    {
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

    return sl_create_join_path(templates_buffer, sizeof(templates_buffer), root_buffer,
                               "templates") &&
           sl_create_join_path(buffer, capacity, templates_buffer, template_name);
}

static bool sl_create_resolve_template_path(const SlCliOptions* options, const char* template_name,
                                            char* buffer, size_t capacity)
{
    char fallback_root[SL_RUN_PATH_MAX_BYTES];

    if (options != NULL &&
        sl_create_copy_package_template_path(buffer, capacity, options->executable_path,
                                             template_name) &&
        sl_create_directory_exists(buffer))
    {
        return true;
    }

    return sl_create_join_path(fallback_root, sizeof(fallback_root), "templates", template_name) &&
           sl_cli_copy_cstr(buffer, capacity, fallback_root) && sl_create_directory_exists(buffer);
}

static int sl_create_copy_directory(const char* source_dir, const char* target_dir)
{
    unsigned char arena_storage[SL_CREATE_ARENA_BYTES];
    SlArena arena = {0};
    SlFsDirectoryList list = {0};
    SlStatus status;
    size_t index = 0U;

    if (source_dir == NULL || target_dir == NULL) {
        return 1;
    }

    status = sl_fs_create_directory(sl_str_from_cstr(target_dir), true, NULL);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_error_with_value("sloppy create: failed to create directory: ", target_dir,
                                      "\n");
        return 1;
    }

    status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
    if (!sl_status_is_ok(status)) {
        return 1;
    }
    status = sl_fs_list_directory(&arena, sl_str_from_cstr(source_dir), &list, NULL);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_error_with_value("sloppy create: failed to read template directory: ",
                                      source_dir, "\n");
        return 1;
    }

    for (index = 0U; index < list.count; index += 1U) {
        SlStr name = sl_owned_str_as_view(list.entries[index].name);
        char source_path[SL_RUN_PATH_MAX_BYTES];
        char target_path[SL_RUN_PATH_MAX_BYTES];

        if (!sl_create_join_span_path(source_path, sizeof(source_path), source_dir, name) ||
            !sl_create_join_span_path(target_path, sizeof(target_path), target_dir, name))
        {
            sl_cli_write_cstr(stderr, "sloppy create: template path is too long\n");
            return 1;
        }

        if (list.entries[index].kind == SL_FS_NODE_DIRECTORY) {
            if (sl_create_copy_directory(source_path, target_path) != 0) {
                return 1;
            }
        }
        else if (list.entries[index].kind == SL_FS_NODE_FILE) {
            status = sl_fs_copy_file(sl_str_from_cstr(source_path), sl_str_from_cstr(target_path),
                                     true, NULL);
            if (!sl_status_is_ok(status)) {
                sl_cli_write_error_with_value("sloppy create: failed to copy template file: ",
                                              source_path, "\n");
                return 1;
            }
        }
    }

    return 0;
}

static void sl_create_print_json_success(const char* project_name, const char* template_name)
{
    printf("{\"created\":true,\"path\":");
    sl_cli_json_escape(stdout, sl_cli_span_cstr(project_name));
    printf(",\"template\":");
    sl_cli_json_escape(stdout, sl_cli_span_cstr(template_name));
    printf(",\"next\":[\"cd ");
    sl_cli_write_cstr(stdout, project_name);
    printf("\",\"sloppy build\",\"sloppy run --once GET /health\"]}\n");
}

static int sl_cli_command_create(const SlCliOptions* options)
{
    const char* project_name = NULL;
    const char* template_name = NULL;
    char template_path[SL_RUN_PATH_MAX_BYTES];

    if (options == NULL) {
        return 1;
    }

    project_name = options->create_name;
    template_name = options->create_template != NULL ? options->create_template : "minimal-api";

    if (project_name == NULL || project_name[0] == '\0') {
        sl_cli_write_cstr(stderr, "sloppy create: missing project name\n");
        return 1;
    }
    if (!sl_create_project_name_is_valid(project_name)) {
        sl_cli_write_cstr(stderr,
                          "sloppy create: project name must use letters, digits, '-' or '_'\n");
        return 1;
    }
    if (!sl_create_template_name_supported(template_name)) {
        sl_cli_write_error_with_value("sloppy create: unsupported template: ", template_name,
                                      "\n");
        return 1;
    }
    if (!sl_create_resolve_template_path(options, template_name, template_path,
                                         sizeof(template_path)))
    {
        sl_cli_write_error_with_value("sloppy create: template not found: ", template_name, "\n");
        return 1;
    }
    if (!sl_create_destination_is_empty(project_name) && !options->create_force) {
        sl_cli_write_error_with_value(
            "sloppy create: destination exists and is not empty: ", project_name,
            "\nUse --force to copy template files over existing files; stale files are left alone.\n");
        return 1;
    }

    if (sl_create_copy_directory(template_path, project_name) != 0) {
        return 1;
    }

    if (options->format == SL_CLI_FORMAT_JSON) {
        sl_create_print_json_success(project_name, template_name);
        return 0;
    }

    printf("Created Sloppy app in ./%s\n\n", project_name);
    printf("Next:\n");
    printf("  cd %s\n", project_name);
    printf("  sloppy build\n");
    printf("  sloppy run --once GET /health\n");
    return 0;
}
