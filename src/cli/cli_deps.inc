/*
 * src/cli/cli_deps.inc
 *
 * Private dependency graph command emitter.
 * Included only by src/main.c to preserve existing static helper visibility.
 */
static int sl_cli_deps_load_json(const char* path, yyjson_doc** out_doc, yyjson_val** out_root)
{
    unsigned char* json_storage = sl_cli_scratch_json_storage();
    SlBytes json = {0};
    yyjson_read_err error = {0};

    if (path == NULL || out_doc == NULL || out_root == NULL) {
        return 1;
    }

    *out_doc = NULL;
    *out_root = NULL;
    if (sl_cli_read_file(path, json_storage, SL_CLI_FILE_MAX_BYTES, &json) != 0) {
        return 1;
    }

    *out_doc = yyjson_read_opts((char*)json.ptr, json.length, 0U, NULL, &error);
    if (*out_doc == NULL) {
        sl_cli_write_error_with_value("sloppy deps: malformed JSON: ", path, "\n");
        return 1;
    }
    *out_root = yyjson_doc_get_root(*out_doc);
    if (*out_root == NULL || !yyjson_is_obj(*out_root)) {
        yyjson_doc_free(*out_doc);
        *out_doc = NULL;
        sl_cli_write_error_with_value("sloppy deps: JSON root must be an object: ", path, "\n");
        return 1;
    }
    return 0;
}

static int sl_cli_deps_load_graph(const SlCliOptions* options, yyjson_doc** out_doc,
                                  yyjson_val** out_graph)
{
    yyjson_val* root = NULL;
    char graph_path[SL_RUN_PATH_MAX_BYTES];

    if (options == NULL || options->plan_path == NULL || out_doc == NULL || out_graph == NULL) {
        sl_cli_write_cstr(stderr, "sloppy deps: --plan <path> is required\n");
        return 1;
    }

    if (sl_cli_deps_load_json(options->plan_path, out_doc, &root) != 0) {
        return 1;
    }

    *out_graph = yyjson_obj_get(root, "dependencyGraph");
    if (*out_graph != NULL && yyjson_is_obj(*out_graph)) {
        return 0;
    }

    if (options->artifacts_path != NULL &&
        sl_cli_join_leaf(graph_path, sizeof(graph_path), options->artifacts_path,
                         "deps.graph.json") &&
        sl_cli_path_is_file(graph_path))
    {
        yyjson_doc_free(*out_doc);
        *out_doc = NULL;
        return sl_cli_deps_load_json(graph_path, out_doc, out_graph);
    }

    yyjson_doc_free(*out_doc);
    *out_doc = NULL;
    sl_cli_write_cstr(stderr, "sloppy deps: Plan has no dependencyGraph metadata\n");
    return 1;
}

static int sl_cli_deps_emit_json(yyjson_val* graph)
{
    size_t length = 0U;
    char* json = yyjson_val_write(graph, YYJSON_WRITE_PRETTY, &length);
    if (json == NULL) {
        sl_cli_write_cstr(stderr, "sloppy deps: failed to serialize dependency graph JSON\n");
        return 1;
    }
    if ((length != 0U && fwrite(json, 1U, length, stdout) != length) || fputc('\n', stdout) == EOF)
    {
        sl_cli_write_cstr(stderr, "sloppy deps: failed to write dependency graph JSON\n");
        sl_alloc_release(json);
        return 1;
    }
    sl_alloc_release(json);
    return 0;
}

static void sl_cli_deps_emit_text_array(yyjson_val* graph, const char* label, const char* name,
                                        const char* primary)
{
    yyjson_val* array = yyjson_obj_get(graph, name);
    yyjson_arr_iter iter;
    yyjson_val* entry = NULL;
    size_t count = array != NULL && yyjson_is_arr(array) ? yyjson_arr_size(array) : 0U;

    printf("%s (%zu)\n", label, count);
    if (count == 0U) {
        printf("  -\n");
        return;
    }

    yyjson_arr_iter_init(array, &iter);
    while ((entry = yyjson_arr_iter_next(&iter)) != NULL) {
        SlCliSpan value = sl_cli_json_span(entry, primary);
        SlCliSpan version = sl_cli_json_span(entry, "version");
        SlCliSpan root = sl_cli_json_span(entry, "root");
        SlCliSpan entry_path = sl_cli_json_span(entry, "entry");
        SlCliSpan package_name = sl_cli_json_span(entry, "package");
        SlCliSpan format = sl_cli_json_span(entry, "format");
        SlCliSpan status = sl_cli_json_span(entry, "status");
        SlCliSpan severity = sl_cli_json_span(entry, "severity");
        SlCliSpan backing = sl_cli_json_span(entry, "backing");
        SlCliSpan message = sl_cli_json_span(entry, "message");
        SlCliSpan source = sl_cli_json_span(entry, "source");

        printf("  ");
        sl_cli_write_span(stdout, value);
        if (!sl_cli_span_empty(version)) {
            printf("@");
            sl_cli_write_span(stdout, version);
        }
        if (!sl_cli_span_empty(format)) {
            printf("  ");
            sl_cli_write_span(stdout, format);
        }
        if (!sl_cli_span_empty(root)) {
            printf("  root=");
            sl_cli_write_span(stdout, root);
        }
        if (!sl_cli_span_empty(entry_path)) {
            printf("  entry=");
            sl_cli_write_span(stdout, entry_path);
        }
        if (!sl_cli_span_empty(package_name)) {
            printf("  package=");
            sl_cli_write_span(stdout, package_name);
        }
        if (!sl_cli_span_empty(status)) {
            printf("  ");
            sl_cli_write_span(stdout, status);
        }
        if (!sl_cli_span_empty(severity)) {
            printf("  ");
            sl_cli_write_span(stdout, severity);
        }
        if (!sl_cli_span_empty(backing)) {
            printf(" -> ");
            sl_cli_write_span(stdout, backing);
        }
        if (!sl_cli_span_empty(message)) {
            printf("  ");
            sl_cli_write_span(stdout, message);
        }
        if (!sl_cli_span_empty(source)) {
            printf("  ");
            sl_cli_write_span(stdout, source);
        }
        printf("\n");
    }
}

static void sl_cli_deps_emit_text(yyjson_val* graph)
{
    yyjson_val* packages = yyjson_obj_get(graph, "packages");
    yyjson_val* modules = yyjson_obj_get(graph, "modules");
    yyjson_val* assets = yyjson_obj_get(graph, "assets");
    yyjson_val* node_builtins = yyjson_obj_get(graph, "nodeBuiltins");
    yyjson_val* findings = yyjson_obj_get(graph, "compatibilityFindings");

    printf("Dependency graph: %zu package(s), %zu module(s), %zu asset(s), %zu Node binding(s), "
           "%zu finding(s)\n",
           packages != NULL && yyjson_is_arr(packages) ? yyjson_arr_size(packages) : 0U,
           modules != NULL && yyjson_is_arr(modules) ? yyjson_arr_size(modules) : 0U,
           assets != NULL && yyjson_is_arr(assets) ? yyjson_arr_size(assets) : 0U,
           node_builtins != NULL && yyjson_is_arr(node_builtins) ? yyjson_arr_size(node_builtins)
                                                                 : 0U,
           findings != NULL && yyjson_is_arr(findings) ? yyjson_arr_size(findings) : 0U);
    sl_cli_deps_emit_text_array(graph, "Packages", "packages", "name");
    sl_cli_deps_emit_text_array(graph, "Modules", "modules", "id");
    sl_cli_deps_emit_text_array(graph, "Assets", "assets", "path");
    sl_cli_deps_emit_text_array(graph, "Node compatibility", "nodeBuiltins", "specifier");
    sl_cli_deps_emit_text_array(graph, "Findings", "compatibilityFindings", "code");
}

static int sl_cli_command_deps(const SlCliOptions* options)
{
    yyjson_doc* doc = NULL;
    yyjson_val* graph = NULL;

    if (sl_cli_deps_load_graph(options, &doc, &graph) != 0) {
        return 1;
    }

    if (options->format == SL_CLI_FORMAT_JSON) {
        if (sl_cli_deps_emit_json(graph) != 0) {
            yyjson_doc_free(doc);
            return 1;
        }
    }
    else {
        sl_cli_deps_emit_text(graph);
    }

    yyjson_doc_free(doc);
    return 0;
}
