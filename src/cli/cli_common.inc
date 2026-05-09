/*
 * src/cli/cli_common.inc
 *
 * Shared private CLI parsing, IO, escaping, and redaction helpers.
 * Included only by src/main.c to preserve existing static helper visibility.
 */
static SlCliSpan sl_cli_span_cstr(const char* text)
{
    SlStr str = sl_str_from_cstr(text);
    return (SlCliSpan){str.ptr, str.length};
}

static SlStr sl_cli_span_str(SlCliSpan span)
{
    return sl_str_from_parts(span.ptr, span.length);
}

static SlCliSpan sl_cli_span_from_str(SlStr str)
{
    return (SlCliSpan){str.ptr, str.length};
}

static bool sl_cli_span_empty(SlCliSpan span)
{
    return span.ptr == NULL || span.length == 0U;
}

static bool sl_cli_command_accepts_artifacts_plan(const char* command)
{
    return command != NULL &&
           (strcmp(command, "routes") == 0 || strcmp(command, "capabilities") == 0 ||
            strcmp(command, "doctor") == 0 || strcmp(command, "audit") == 0 ||
            strcmp(command, "openapi") == 0);
}

static bool sl_cli_span_equal(SlCliSpan left, SlCliSpan right)
{
    return sl_str_equal(sl_cli_span_str(left), sl_cli_span_str(right));
}

static bool sl_cli_span_equal_cstr(SlCliSpan left, const char* right)
{
    return sl_cli_span_equal(left, sl_cli_span_cstr(right));
}

static bool sl_cli_copy_cstr(char* buffer, size_t capacity, const char* value)
{
    SlStr view = {0};
    SlStringBuilder builder = {0};

    return sl_status_is_ok(sl_string_builder_init_fixed(&builder, buffer, capacity)) &&
           sl_status_is_ok(sl_string_builder_append_cstr(&builder, value)) &&
           sl_status_is_ok(sl_string_builder_view_with_nul(&builder, &view)) && view.ptr == buffer;
}

static bool sl_cli_join_leaf(char* buffer, size_t capacity, const char* dir, const char* leaf)
{
    SlStringBuilder builder = {0};
    SlStr dir_str = sl_str_from_cstr(dir);
    SlStr leaf_str = sl_str_from_cstr(leaf);
    SlStr joined = {0};
    bool needs_separator = true;
    SlStatus status;

    if (buffer == NULL || capacity == 0U || dir == NULL || dir[0] == '\0' || leaf == NULL ||
        leaf[0] == '\0')
    {
        return false;
    }

    needs_separator =
        dir_str.ptr[dir_str.length - 1U] != '/' && dir_str.ptr[dir_str.length - 1U] != '\\';

    status = sl_string_builder_init_fixed(&builder, buffer, capacity);
    if (!sl_status_is_ok(status) ||
        !sl_status_is_ok(sl_string_builder_append_str(&builder, dir_str)))
    {
        return false;
    }
    if (needs_separator &&
        !sl_status_is_ok(sl_string_builder_append_char(&builder, SL_PATH_SEPARATOR)))
    {
        return false;
    }
    if (!sl_status_is_ok(sl_string_builder_append_str(&builder, leaf_str))) {
        return false;
    }

    status = sl_string_builder_view_with_nul(&builder, &joined);
    return sl_status_is_ok(status) && joined.ptr == buffer;
}

static bool sl_cli_set_plan_path(SlCliOptions* out, const char* path)
{
    SlFsStat stat = {0};

    if (out == NULL || path == NULL || path[0] == '\0') {
        return false;
    }

    if (sl_status_is_ok(sl_fs_stat(sl_str_from_cstr(path), &stat, NULL)) && stat.exists &&
        stat.kind == SL_FS_NODE_DIRECTORY)
    {
        if (!sl_cli_join_leaf(out->plan_path_storage, sizeof(out->plan_path_storage), path,
                              "app.plan.json"))
        {
            return false;
        }
        out->plan_path = out->plan_path_storage;
        return true;
    }

    out->plan_path = path;
    return true;
}

static bool sl_cli_set_artifacts_plan_path(SlCliOptions* out, const char* path)
{
    if (out == NULL || path == NULL || path[0] == '\0') {
        return false;
    }
    if (!sl_cli_join_leaf(out->plan_path_storage, sizeof(out->plan_path_storage), path,
                          "app.plan.json"))
    {
        return false;
    }
    out->artifacts_path = path;
    out->plan_path = out->plan_path_storage;
    return true;
}

static bool sl_cli_string_builder_append_span(SlStringBuilder* builder, SlCliSpan span)
{
    return sl_status_is_ok(sl_string_builder_append_str(builder, sl_cli_span_str(span)));
}

static SlCliSpan sl_cli_json_span(yyjson_val* object, const char* name)
{
    yyjson_val* value = yyjson_obj_get(object, name);

    if (value == NULL || !yyjson_is_str(value)) {
        return (SlCliSpan){0};
    }

    return (SlCliSpan){yyjson_get_str(value), yyjson_get_len(value)};
}

static SlHandlerId sl_cli_json_handler_id(yyjson_val* object)
{
    yyjson_val* value = yyjson_obj_get(object, "handlerId");
    uint64_t id = 0U;

    if (value == NULL) {
        value = yyjson_obj_get(object, "handler");
    }
    if (value == NULL || !yyjson_is_uint(value)) {
        return SL_HANDLER_ID_INVALID;
    }

    id = yyjson_get_uint(value);
    if (id > UINT32_MAX) {
        return SL_HANDLER_ID_INVALID;
    }

    return (SlHandlerId)id;
}

static uint64_t sl_cli_json_uint_or_zero(yyjson_val* object, const char* name)
{
    yyjson_val* value = yyjson_obj_get(object, name);
    return value != NULL && yyjson_is_uint(value) ? yyjson_get_uint(value) : 0U;
}

static void sl_cli_parse_source(yyjson_val* object, SlCliSpan* path, uint64_t* line,
                                uint64_t* column)
{
    yyjson_val* source = yyjson_obj_get(object, "source");

    if (path != NULL) {
        *path = (SlCliSpan){0};
    }
    if (line != NULL) {
        *line = 0U;
    }
    if (column != NULL) {
        *column = 0U;
    }
    if (source == NULL || !yyjson_is_obj(source)) {
        return;
    }
    if (path != NULL) {
        *path = sl_cli_json_span(source, "path");
    }
    if (line != NULL) {
        *line = sl_cli_json_uint_or_zero(source, "line");
    }
    if (column != NULL) {
        *column = sl_cli_json_uint_or_zero(source, "column");
    }
}

static void sl_cli_print_version(void)
{
    printf("Sloppy %s\n", SL_VERSION_STRING);
}

static void sl_cli_print_help(void)
{
    sl_cli_print_version();
    printf("Pre-alpha runtime foundation with bounded run and metadata CLI introspection.\n\n");
    printf("Usage:\n");
    printf("  sloppy --help\n");
    printf("  sloppy --version\n");
    printf("  sloppy create <name> [--template minimal-api|full-api|dogfood] [--force]\n");
    printf("                       [--no-git] [--format text|json]\n");
    printf("  sloppy build [source.js|source.mjs|source.ts] [--out <dir>]\n");
    printf("               [--environment Development] [--host 127.0.0.1] [--port 5173]\n");
    printf("  sloppy package [source.js|source.mjs|source.ts] [--out <dir>]\n");
    printf("                 [--environment Development] [--host 127.0.0.1] [--port 5173]\n");
    printf("                 [--format text|json]\n");
    printf("  sloppy run [source.js|source.mjs|source.ts|--artifacts <dir>] [--stdlib <dir>]\n");
    printf("             [--environment Development] [--host 127.0.0.1] [--port 5173]\n");
    printf("             [--once METHOD TARGET]\n");
    printf("  sloppy routes --plan <path>|--artifacts <dir> [--format text|json]\n");
    printf("  sloppy capabilities --plan <path>|--artifacts <dir> [--format text|json]\n");
    printf("  sloppy doctor [--plan <path>|--artifacts <dir>] [--format text|json]\n");
    printf("  sloppy audit --plan <path>|--artifacts <dir> [--format text|json]\n");
    printf("  sloppy openapi --plan <path>|--artifacts <dir> [--output <path>]\n");
}

static void sl_cli_print_command_help(const char* command)
{
    if (strcmp(command, "routes") == 0) {
        printf("Usage: sloppy routes --plan <path>|--artifacts <dir> [--format text|json]\n");
        return;
    }
    if (strcmp(command, "capabilities") == 0) {
        printf("Usage: sloppy capabilities --plan <path>|--artifacts <dir> [--format text|json]\n");
        return;
    }
    if (strcmp(command, "run") == 0) {
        printf("Usage: sloppy run [source.js|source.mjs|source.ts|--artifacts <dir>] [--stdlib <dir>]\n");
        printf("                  [--environment Development] [--host 127.0.0.1]\n");
        printf("                  [--port 5173] [--once METHOD TARGET]\n");
        printf("\n");
        printf("Source input compiles through sloppyc, validates artifacts, then runs the "
               "artifact path.\n");
        printf("Bounded development runtime. Execution requires a V8-enabled build.\n");
        return;
    }
    if (strcmp(command, "build") == 0) {
        printf("Usage: sloppy build [source.js|source.mjs|source.ts] [--out <dir>]\n");
        printf("                    [--environment Development] [--host 127.0.0.1]\n");
        printf("                    [--port 5173]\n");
        printf("\n");
        printf("Source input compiles through sloppyc and validates Plan-backed artifacts.\n");
        printf("--artifacts is not accepted for build; build never enters V8.\n");
        return;
    }
    if (strcmp(command, "package") == 0) {
        printf("Usage: sloppy package [source.js|source.mjs|source.ts] [--out <dir>]\n");
        printf("                      [--environment Development] [--host 127.0.0.1]\n");
        printf("                      [--port 5173] [--format text|json]\n");
        printf("\n");
        printf("Compiles source input and writes a directory app package under .sloppy/package/ "
               "or <out>/package/.\n");
        printf("This is an app artifact package, not a runtime release archive.\n");
        printf("--format json and --json print the package path and artifact directory.\n");
        return;
    }
    if (strcmp(command, "create") == 0) {
        printf("Usage: sloppy create <name> [--template minimal-api|full-api|dogfood]\n");
        printf("                     [--force] [--no-git] [--format text|json]\n");
        printf("\n");
        printf("Copies a built-in project template into a new local app directory.\n");
        printf("--force copies template files over existing files; it does not delete stale files.\n");
        printf("--no-git is reserved for future git initialization; template files still copy.\n");
        return;
    }
    if (strcmp(command, "doctor") == 0) {
        printf("Usage: sloppy doctor [--plan <path>|--artifacts <dir>] [--format text|json]\n");
        return;
    }
    if (strcmp(command, "audit") == 0) {
        printf("Usage: sloppy audit --plan <path>|--artifacts <dir> [--format text|json]\n");
        return;
    }
    if (strcmp(command, "openapi") == 0) {
        printf("Usage: sloppy openapi --plan <path>|--artifacts <dir> [--output <path>]\n");
        return;
    }
    sl_cli_print_help();
}

static void sl_cli_write_cstr(FILE* file, const char* text)
{
    fputs(text, file);
}

static void sl_cli_write_error_with_value(const char* prefix, const char* value, const char* suffix)
{
    sl_cli_write_cstr(stderr, prefix);
    if (value != NULL) {
        sl_cli_write_cstr(stderr, value);
    }
    sl_cli_write_cstr(stderr, suffix);
}

static bool sl_cli_parse_port(const char* text, uint16_t* out)
{
    uint32_t value = 0U;
    size_t index = 0U;

    if (text == NULL || text[0] == '\0' || out == NULL) {
        return false;
    }

    while (text[index] != '\0') {
        unsigned char ch = (unsigned char)text[index];
        if (ch < '0' || ch > '9') {
            return false;
        }
        value = (value * 10U) + (uint32_t)(ch - '0');
        if (value == 0U || value > UINT16_MAX) {
            return false;
        }
        index += 1U;
    }

    *out = (uint16_t)value;
    return true;
}

static int sl_cli_parse_run_path_option(int argc, char** argv, int* index, SlCliOptions* out)
{
    if (strcmp(argv[*index], "--artifacts") == 0) {
        if (*index + 1 >= argc) {
            sl_cli_write_cstr(stderr, "sloppy run: --artifacts requires a directory\n");
            return -1;
        }
        if (out->artifacts_path != NULL || out->input_path != NULL) {
            sl_cli_write_cstr(
                stderr,
                "sloppy run: expected either --artifacts <dir> or one positional artifact path\n");
            return -1;
        }
        out->artifacts_path = argv[*index + 1];
        *index += 2;
        return 1;
    }

    if (argv[*index][0] != '-') {
        if (out->input_path != NULL || out->artifacts_path != NULL) {
            sl_cli_write_cstr(
                stderr,
                "sloppy run: expected either --artifacts <dir> or one positional artifact path\n");
            return -1;
        }
        out->input_path = argv[*index];
        *index += 1;
        return 1;
    }

    return 0;
}

static int sl_cli_parse_run_option(int argc, char** argv, int* index, SlCliOptions* out)
{
    int path_result = 0;

    if (argc <= 0 || argv == NULL || index == NULL || out == NULL ||
        strcmp(out->command, "run") != 0)
    {
        return 0;
    }

    if (strcmp(argv[*index], "--host") == 0) {
        if (*index + 1 >= argc) {
            sl_cli_write_cstr(stderr, "sloppy run: --host requires an IPv4 address\n");
            return -1;
        }
        out->host = argv[*index + 1];
        out->host_explicit = true;
        *index += 2;
        return 1;
    }

    if (strcmp(argv[*index], "--environment") == 0) {
        if (*index + 1 >= argc || argv[*index + 1][0] == '\0') {
            sl_cli_write_cstr(stderr, "sloppy run: --environment requires a name\n");
            return -1;
        }
        out->environment = argv[*index + 1];
        out->environment_explicit = true;
        *index += 2;
        return 1;
    }

    if (strcmp(argv[*index], "--stdlib") == 0) {
        if (*index + 1 >= argc) {
            sl_cli_write_cstr(stderr,
                              "sloppy run: --stdlib requires a bootstrap stdlib directory\n");
            return -1;
        }
        out->stdlib_path = argv[*index + 1];
        *index += 2;
        return 1;
    }

    if (strcmp(argv[*index], "--port") == 0) {
        if (*index + 1 >= argc || !sl_cli_parse_port(argv[*index + 1], &out->port)) {
            sl_cli_write_cstr(stderr, "sloppy run: --port requires a value from 1 to 65535\n");
            return -1;
        }
        out->port_explicit = true;
        *index += 2;
        return 1;
    }

    if (strcmp(argv[*index], "--once") == 0) {
        if (*index + 2 >= argc) {
            sl_cli_write_cstr(stderr, "sloppy run: --once requires METHOD and TARGET\n");
            return -1;
        }
        out->once_method = argv[*index + 1];
        out->once_target = argv[*index + 2];
        *index += 3;
        return 1;
    }

    path_result = sl_cli_parse_run_path_option(argc, argv, index, out);
    if (path_result != 0) {
        return path_result;
    }

    return 0;
}

static int sl_cli_parse_build_path_option(char** argv, int* index, SlCliOptions* out)
{
    const char* command_name = out->command != NULL ? out->command : "build";

    if (strcmp(argv[*index], "--artifacts") == 0) {
        sl_cli_write_cstr(stderr, "sloppy ");
        sl_cli_write_cstr(stderr, command_name);
        sl_cli_write_cstr(stderr,
                          ": --artifacts is not supported; this command compiles source input "
                          "only\n");
        return -1;
    }

    if (argv[*index][0] != '-') {
        if (out->input_path != NULL) {
            sl_cli_write_cstr(stderr, "sloppy ");
            sl_cli_write_cstr(stderr, command_name);
            sl_cli_write_cstr(stderr, ": accepts at most one source input\n");
            return -1;
        }
        out->input_path = argv[*index];
        *index += 1;
        return 1;
    }

    return 0;
}

static int sl_cli_parse_build_option(int argc, char** argv, int* index, SlCliOptions* out)
{
    int path_result = 0;
    const char* command_name = NULL;

    if (argc <= 0 || argv == NULL || index == NULL || out == NULL ||
        (strcmp(out->command, "build") != 0 && strcmp(out->command, "package") != 0))
    {
        return 0;
    }

    command_name = out->command != NULL ? out->command : "build";

    if (strcmp(argv[*index], "--out") == 0) {
        if (*index + 1 >= argc || argv[*index + 1][0] == '\0') {
            sl_cli_write_cstr(stderr, "sloppy ");
            sl_cli_write_cstr(stderr, command_name);
            sl_cli_write_cstr(stderr, ": --out requires a directory\n");
            return -1;
        }
        out->out_dir = argv[*index + 1];
        out->out_explicit = true;
        *index += 2;
        return 1;
    }

    if (strcmp(argv[*index], "--host") == 0) {
        if (*index + 1 >= argc) {
            sl_cli_write_cstr(stderr, "sloppy ");
            sl_cli_write_cstr(stderr, command_name);
            sl_cli_write_cstr(stderr, ": --host requires an IPv4 address\n");
            return -1;
        }
        out->host = argv[*index + 1];
        out->host_explicit = true;
        *index += 2;
        return 1;
    }

    if (strcmp(argv[*index], "--environment") == 0) {
        if (*index + 1 >= argc || argv[*index + 1][0] == '\0') {
            sl_cli_write_cstr(stderr, "sloppy ");
            sl_cli_write_cstr(stderr, command_name);
            sl_cli_write_cstr(stderr, ": --environment requires a name\n");
            return -1;
        }
        out->environment = argv[*index + 1];
        out->environment_explicit = true;
        *index += 2;
        return 1;
    }

    if (strcmp(argv[*index], "--port") == 0) {
        if (*index + 1 >= argc || !sl_cli_parse_port(argv[*index + 1], &out->port)) {
            sl_cli_write_cstr(stderr, "sloppy ");
            sl_cli_write_cstr(stderr, command_name);
            sl_cli_write_cstr(stderr, ": --port requires a value from 1 to 65535\n");
            return -1;
        }
        out->port_explicit = true;
        *index += 2;
        return 1;
    }

    path_result = sl_cli_parse_build_path_option(argv, index, out);
    if (path_result != 0) {
        return path_result;
    }

    return 0;
}

static int sl_cli_parse_create_option(int argc, char** argv, int* index, SlCliOptions* out)
{
    if (argc <= 0 || argv == NULL || index == NULL || out == NULL ||
        strcmp(out->command, "create") != 0)
    {
        return 0;
    }

    if (strcmp(argv[*index], "--template") == 0) {
        if (*index + 1 >= argc || argv[*index + 1][0] == '\0') {
            sl_cli_write_cstr(stderr, "sloppy create: --template requires a template name\n");
            return -1;
        }
        out->create_template = argv[*index + 1];
        *index += 2;
        return 1;
    }

    if (strcmp(argv[*index], "--force") == 0) {
        out->create_force = true;
        *index += 1;
        return 1;
    }

    if (strcmp(argv[*index], "--no-git") == 0) {
        out->create_no_git = true;
        *index += 1;
        return 1;
    }

    if (argv[*index][0] != '-') {
        if (out->create_name != NULL) {
            sl_cli_write_cstr(stderr, "sloppy create: accepts exactly one project name\n");
            return -1;
        }
        out->create_name = argv[*index];
        *index += 1;
        return 1;
    }

    return 0;
}

static int sl_cli_parse_common_option(int argc, char** argv, int* index, SlCliOptions* out)
{
    if (argc <= 0 || argv == NULL || index == NULL || out == NULL) {
        return 0;
    }

    if (strcmp(argv[*index], "--help") == 0 || strcmp(argv[*index], "-h") == 0) {
        out->help = true;
        *index += 1;
        return 1;
    }

    if (strcmp(argv[*index], "--json") == 0) {
        out->format = SL_CLI_FORMAT_JSON;
        *index += 1;
        return 1;
    }

    if (strcmp(argv[*index], "--format") == 0) {
        if (*index + 1 >= argc) {
            sl_cli_write_cstr(stderr, "sloppy: --format requires text or json\n");
            return -1;
        }
        if (strcmp(argv[*index + 1], "text") == 0) {
            out->format = SL_CLI_FORMAT_TEXT;
        }
        else if (strcmp(argv[*index + 1], "json") == 0) {
            out->format = SL_CLI_FORMAT_JSON;
        }
        else {
            sl_cli_write_error_with_value("sloppy: unsupported format '", argv[*index + 1], "'\n");
            return -1;
        }
        *index += 2;
        return 1;
    }

    if (strcmp(argv[*index], "--plan") == 0 || strcmp(argv[*index], "--app") == 0) {
        if (*index + 1 >= argc) {
            sl_cli_write_error_with_value("sloppy: ", argv[*index], " requires a path\n");
            return -1;
        }
        if (out->plan_path != NULL) {
            sl_cli_write_cstr(stderr, "sloppy: expected only one --plan or --artifacts path\n");
            return -1;
        }
        if (!sl_cli_set_plan_path(out, argv[*index + 1])) {
            sl_cli_write_cstr(stderr, "sloppy: metadata path is too long\n");
            return -1;
        }
        *index += 2;
        return 1;
    }

    if (strcmp(argv[*index], "--artifacts") == 0 &&
        sl_cli_command_accepts_artifacts_plan(out->command))
    {
        if (*index + 1 >= argc) {
            sl_cli_write_cstr(stderr, "sloppy: --artifacts requires a directory\n");
            return -1;
        }
        if (out->plan_path != NULL) {
            sl_cli_write_cstr(stderr, "sloppy: expected only one --plan or --artifacts path\n");
            return -1;
        }
        if (!sl_cli_set_artifacts_plan_path(out, argv[*index + 1])) {
            sl_cli_write_cstr(stderr, "sloppy: artifacts path is too long\n");
            return -1;
        }
        *index += 2;
        return 1;
    }

    if (strcmp(argv[*index], "--output") == 0) {
        if (*index + 1 >= argc) {
            sl_cli_write_cstr(stderr, "sloppy: --output requires a path\n");
            return -1;
        }
        out->output_path = argv[*index + 1];
        *index += 2;
        return 1;
    }

    return 0;
}

static int sl_cli_parse_options(int argc, char** argv, SlCliOptions* out)
{
    int index = 2;

    if (out == NULL) {
        return 1;
    }

    *out = (SlCliOptions){0};
    if (!sl_status_is_ok(sl_platform_process_executable_path(out->executable_path,
                                                             sizeof(out->executable_path))) &&
        argc > 0 && argv != NULL && argv[0] != NULL)
    {
        (void)sl_cli_copy_cstr(out->executable_path, sizeof(out->executable_path), argv[0]);
    }
    out->format = SL_CLI_FORMAT_TEXT;
    out->host = SL_RUN_DEFAULT_HOST;
    out->port = (uint16_t)SL_RUN_DEFAULT_PORT;

    if (argc <= 1) {
        out->help = true;
        return 0;
    }
    if (argv == NULL || argv[1] == NULL) {
        return 1;
    }

    if (strcmp(argv[1], "--version") == 0) {
        out->command = "--version";
        return 0;
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        out->help = true;
        return 0;
    }

    out->command = argv[1];

    while (index < argc) {
        int common_parse = sl_cli_parse_common_option(argc, argv, &index, out);
        int run_parse = 0;
        if (common_parse < 0) {
            return 1;
        }
        if (common_parse > 0) {
            continue;
        }

        run_parse = sl_cli_parse_run_option(argc, argv, &index, out);
        if (run_parse < 0) {
            return 1;
        }
        if (run_parse > 0) {
            continue;
        }

        run_parse = sl_cli_parse_create_option(argc, argv, &index, out);
        if (run_parse < 0) {
            return 1;
        }
        if (run_parse > 0) {
            continue;
        }

        run_parse = sl_cli_parse_build_option(argc, argv, &index, out);
        if (run_parse < 0) {
            return 1;
        }
        if (run_parse == 0) {
            sl_cli_write_error_with_value("sloppy: unknown option '", argv[index], "'\n");
            return 1;
        }
    }

    return 0;
}

static int sl_read_file_with_storage(const char* path, unsigned char* buffer, size_t capacity,
                                     unsigned char* read_storage, size_t read_storage_capacity,
                                     SlBytes* out, const char* not_found_prefix,
                                     const char* size_prefix, const char* failed_read_prefix)
{
    SlArena arena = {0};
    SlArena output_arena = {0};
    SlOwnedBytes bytes = {0};
    SlOwnedBytes copied = {0};
    SlStatus status;
    size_t read_arena_capacity = 0U;

    if (path == NULL || buffer == NULL || read_storage == NULL || out == NULL ||
        not_found_prefix == NULL || size_prefix == NULL || failed_read_prefix == NULL)
    {
        return 1;
    }
    *out = sl_bytes_from_parts(NULL, 0U);

    status = sl_checked_add_size(capacity, SL_CLI_FILE_READ_SCRATCH_BYTES, &read_arena_capacity);
    if (!sl_status_is_ok(status) || read_arena_capacity > read_storage_capacity) {
        sl_cli_write_error_with_value(size_prefix, path, "\n");
        return 1;
    }

    status = sl_arena_init(&arena, read_storage, read_arena_capacity);
    if (!sl_status_is_ok(status)) {
        return 1;
    }

    /*
     * Trusted runtime/tooling file reads use the Sloppy-owned native filesystem backend directly.
     * They intentionally do not go through the public V8 `sloppy/fs` feature/capability path.
     */
    status = sl_fs_read_file(&arena, sl_str_from_cstr(path), &bytes, NULL);
    if (sl_status_code(status) == SL_STATUS_OUT_OF_RANGE) {
        sl_cli_write_error_with_value(not_found_prefix, path, "\n");
        return 1;
    }
    if (sl_status_code(status) == SL_STATUS_CAPACITY_EXCEEDED) {
        sl_cli_write_error_with_value(size_prefix, path, "\n");
        return 1;
    }
    if (!sl_status_is_ok(status)) {
        sl_cli_write_error_with_value(failed_read_prefix, path, "\n");
        return 1;
    }

    if (bytes.length == 0U || bytes.length > capacity) {
        sl_cli_write_error_with_value(size_prefix, path, "\n");
        return 1;
    }

    status = sl_arena_init(&output_arena, buffer, capacity);
    if (!sl_status_is_ok(status)) {
        return 1;
    }
    status = sl_bytes_copy_to_arena(&output_arena, sl_owned_bytes_as_view(bytes), &copied);
    if (!sl_status_is_ok(status)) {
        return 1;
    }
    *out = sl_owned_bytes_as_view(copied);
    return 0;
}

static int sl_read_file_with_messages(const char* path, unsigned char* buffer, size_t capacity,
                                      SlBytes* out, const char* not_found_prefix,
                                      const char* size_prefix, const char* failed_read_prefix)
{
    static unsigned char read_storage[SL_CLI_FILE_READ_ARENA_BYTES];

    return sl_read_file_with_storage(path, buffer, capacity, read_storage, sizeof(read_storage),
                                     out, not_found_prefix, size_prefix, failed_read_prefix);
}

static unsigned char* sl_cli_scratch_json_storage(void)
{
    static unsigned char storage[SL_CLI_FILE_MAX_BYTES];

    return storage;
}

static unsigned char* sl_cli_scratch_plan_arena_storage(void)
{
    static unsigned char storage[SL_CLI_ARENA_BYTES];

    return storage;
}

static SlCliMetadata* sl_cli_scratch_metadata(void)
{
    static SlCliMetadata metadata;

    metadata = (SlCliMetadata){0};
    return &metadata;
}

static int sl_cli_read_file(const char* path, unsigned char* buffer, size_t capacity, SlBytes* out)
{
    return sl_read_file_with_messages(
        path, buffer, capacity, out, "sloppy: metadata path not found: ",
        "sloppy: metadata file is empty or too large: ", "sloppy: metadata file read failed: ");
}

static bool sl_cli_write_span(FILE* file, SlCliSpan span)
{
    return span.length == 0U || fwrite(span.ptr, 1U, span.length, file) == span.length;
}

static bool sl_cli_write_str(FILE* file, SlStr str)
{
    return str.length == 0U || fwrite(str.ptr, 1U, str.length, file) == str.length;
}

static void sl_cli_json_escape(FILE* file, SlCliSpan span)
{
    static const char hex[] = "0123456789abcdef";
    size_t index = 0U;

    fputc('"', file);
    for (index = 0U; index < span.length; index += 1U) {
        unsigned char ch = (unsigned char)span.ptr[index];
        if (ch == '"' || ch == '\\') {
            fputc('\\', file);
            fputc((int)ch, file);
        }
        else if (ch == '\n') {
            fputs("\\n", file);
        }
        else if (ch == '\r') {
            fputs("\\r", file);
        }
        else if (ch == '\t') {
            fputs("\\t", file);
        }
        else if (ch == '\b') {
            fputs("\\b", file);
        }
        else if (ch == '\f') {
            fputs("\\f", file);
        }
        else if (ch < 0x20U) {
            fputs("\\u00", file);
            fputc(hex[(ch >> 4U) & 0x0FU], file);
            fputc(hex[ch & 0x0FU], file);
        }
        else {
            fputc((int)ch, file);
        }
    }
    fputc('"', file);
}

static SlStr sl_cli_redacted_span(SlArena* arena, SlCliSpan text)
{
    SlStr redacted = sl_diag_redacted();
    SlStatus status;

    if (arena == NULL) {
        return redacted;
    }

    status = sl_diag_redact_secrets(arena, sl_cli_span_str(text), &redacted);
    return sl_status_is_ok(status) ? redacted : sl_diag_redacted();
}

static void sl_cli_write_redacted(FILE* file, SlArena* arena, SlCliSpan text)
{
    SlArenaMark mark = {0U};
    SlStr redacted = sl_diag_redacted();
    bool should_reset = false;

    if (arena != NULL) {
        mark = sl_arena_mark(arena);
        should_reset = true;
        redacted = sl_cli_redacted_span(arena, text);
    }
    (void)sl_cli_write_str(file, redacted);
    if (should_reset) {
        (void)sl_arena_reset_to(arena, mark);
    }
}

static void sl_cli_json_redacted(FILE* file, SlArena* arena, SlCliSpan text)
{
    SlArenaMark mark = {0U};
    SlStr redacted = sl_diag_redacted();
    bool should_reset = false;

    if (arena != NULL) {
        mark = sl_arena_mark(arena);
        should_reset = true;
        redacted = sl_cli_redacted_span(arena, text);
    }
    sl_cli_json_escape(file, sl_cli_span_from_str(redacted));
    if (should_reset) {
        (void)sl_arena_reset_to(arena, mark);
    }
}

static void sl_cli_json_escape_lower(FILE* file, SlCliSpan span)
{
    size_t index = 0U;

    fputc('"', file);
    for (index = 0U; index < span.length; index += 1U) {
        char ch = span.ptr[index];
        if (ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch - 'A' + 'a');
        }
        if (ch == '"' || ch == '\\') {
            fputc('\\', file);
        }
        fputc(ch, file);
    }
    fputc('"', file);
}

static void sl_cli_write_u64(FILE* file, uint64_t value)
{
    char digits[32];
    size_t count = 0U;

    do {
        digits[count] = (char)('0' + (value % 10U));
        value /= 10U;
        count += 1U;
    } while (value > 0U && count < sizeof(digits));

    while (count > 0U) {
        count -= 1U;
        fputc(digits[count], file);
    }
}
