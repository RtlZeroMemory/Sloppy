/*
 * src/cli/cli_dev.inc
 *
 * Private implementation of the `sloppy dev` development loop.
 * Included only by src/main.c to preserve existing static helper visibility.
 */
#define SL_DEV_WATCH_MAX_FILES 2048U
#define SL_DEV_SCAN_ARENA_BYTES 1048576U
#define SL_DEV_POLL_INTERVAL_MS 100U
#define SL_DEV_DEBOUNCE_MS 250U

typedef struct SlDevWatchedFile
{
    char path[SL_RUN_PATH_MAX_BYTES];
    uint64_t modified_nsec;
    uint64_t size;
    bool exists;
} SlDevWatchedFile;

typedef struct SlDevSnapshot
{
    SlDevWatchedFile files[SL_DEV_WATCH_MAX_FILES];
    size_t file_count;
    unsigned char arena_storage[SL_DEV_SCAN_ARENA_BYTES];
} SlDevSnapshot;

typedef struct SlDevServer
{
    SlRunApp* app;
    SlHeapBuffer transport_storage;
    SlArena transport_arena;
    SlHttpTransportServer server;
    bool active;
} SlDevServer;

static volatile sig_atomic_t sl_dev_stop_requested = 0;

static void sl_dev_signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        sl_dev_stop_requested = 1;
    }
}

static void sl_dev_clear_console(void)
{
    fputs("\033[2J\033[H", stdout);
    fflush(stdout);
}

static bool sl_dev_path_equal(const char* left, const char* right)
{
    return left != NULL && right != NULL && strcmp(left, right) == 0;
}

static bool sl_dev_load_watch_plan(const SlCliOptions* options, SlDevWatchPlan* plan)
{
    SlSloppyRunConfig config = {0};
    const SlSloppyRunConfig* project_config = NULL;
    const char* input_path = NULL;

    if (options == NULL || plan == NULL) {
        return false;
    }
    if (options->input_path != NULL) {
        input_path = options->input_path;
    }
    else {
        if (sl_sloppyrc_load_for_command(&config, "sloppy dev") != 0) {
            return false;
        }
        project_config = &config;
    }
    return sl_dev_watch_plan_build(project_config, input_path, plan);
}

static bool sl_dev_snapshot_add_file(SlDevSnapshot* snapshot, const char* path,
                                     const SlFsStat* stat)
{
    if (snapshot == NULL || path == NULL || stat == NULL ||
        snapshot->file_count >= SL_DEV_WATCH_MAX_FILES)
    {
        return false;
    }
    if (!sl_cli_copy_cstr(snapshot->files[snapshot->file_count].path,
                          sizeof(snapshot->files[snapshot->file_count].path), path))
    {
        return false;
    }
    snapshot->files[snapshot->file_count].modified_nsec = stat->modified_nsec;
    snapshot->files[snapshot->file_count].size = stat->size;
    snapshot->files[snapshot->file_count].exists = stat->exists;
    snapshot->file_count += 1U;
    return true;
}

static bool sl_dev_snapshot_scan_path(SlDevSnapshot* snapshot, SlArena* arena, const char* path,
                                      bool recursive)
{
    SlFsStat stat = {0};
    SlFsDirectoryList list = {0};
    SlArenaMark mark = {0U};

    if (!sl_status_is_ok(sl_fs_stat(sl_str_from_cstr(path), &stat, NULL)) || !stat.exists) {
        return true;
    }
    if (stat.kind == SL_FS_NODE_FILE) {
        return sl_dev_snapshot_add_file(snapshot, path, &stat);
    }
    if (stat.kind != SL_FS_NODE_DIRECTORY) {
        return true;
    }
    mark = sl_arena_mark(arena);
    if (!sl_status_is_ok(sl_fs_list_directory(arena, sl_str_from_cstr(path), &list, NULL))) {
        sl_arena_reset_to(arena, mark);
        return false;
    }
    for (size_t index = 0U; index < list.count; index += 1U) {
        char child[SL_RUN_PATH_MAX_BYTES];
        SlStr name = sl_owned_str_as_view(list.entries[index].name);
        if (!sl_run_join_path(child, sizeof(child), path, sl_cli_span_from_str(name))) {
            continue;
        }
        if (list.entries[index].kind == SL_FS_NODE_FILE) {
            SlFsStat child_stat = {.kind = SL_FS_NODE_FILE,
                                   .size = list.entries[index].size,
                                   .modified_nsec = list.entries[index].modified_nsec,
                                   .exists = true};
            if (!sl_dev_snapshot_add_file(snapshot, child, &child_stat)) {
                sl_arena_reset_to(arena, mark);
                return false;
            }
        }
        else if (recursive && list.entries[index].kind == SL_FS_NODE_DIRECTORY) {
            if (!sl_dev_snapshot_scan_path(snapshot, arena, child, true)) {
                sl_arena_reset_to(arena, mark);
                return false;
            }
        }
    }
    sl_arena_reset_to(arena, mark);
    return true;
}

static bool sl_dev_snapshot_collect(const SlDevWatchPlan* plan, SlDevSnapshot* snapshot)
{
    SlArena arena = {0};

    if (plan == NULL || snapshot == NULL) {
        return false;
    }
    snapshot->file_count = 0U;
    if (!sl_status_is_ok(sl_arena_init(&arena, snapshot->arena_storage,
                                       sizeof(snapshot->arena_storage))))
    {
        return false;
    }
    for (size_t index = 0U; index < plan->root_count; index += 1U) {
        if (!sl_dev_snapshot_scan_path(snapshot, &arena, plan->roots[index].path,
                                       plan->roots[index].recursive))
        {
            return false;
        }
    }
    return true;
}

static const SlDevWatchedFile* sl_dev_snapshot_find(const SlDevSnapshot* snapshot,
                                                    const char* path)
{
    if (snapshot == NULL || path == NULL) {
        return NULL;
    }
    for (size_t index = 0U; index < snapshot->file_count; index += 1U) {
        if (sl_dev_path_equal(snapshot->files[index].path, path)) {
            return &snapshot->files[index];
        }
    }
    return NULL;
}

static bool sl_dev_snapshots_differ(const SlDevSnapshot* previous, const SlDevSnapshot* current,
                                    char changed[][SL_RUN_PATH_MAX_BYTES], size_t capacity,
                                    size_t* changed_count)
{
    bool changed_any = false;

    if (changed_count != NULL) {
        *changed_count = 0U;
    }
    for (size_t index = 0U; index < current->file_count; index += 1U) {
        const SlDevWatchedFile* old = sl_dev_snapshot_find(previous, current->files[index].path);
        bool file_changed = old == NULL || old->size != current->files[index].size ||
                            old->modified_nsec != current->files[index].modified_nsec;
        if (file_changed) {
            changed_any = true;
            if (changed_count != NULL && *changed_count < capacity) {
                if (sl_cli_copy_cstr(changed[*changed_count], SL_RUN_PATH_MAX_BYTES,
                                     current->files[index].path))
                {
                    *changed_count += 1U;
                }
            }
        }
    }
    for (size_t index = 0U; index < previous->file_count; index += 1U) {
        if (sl_dev_snapshot_find(current, previous->files[index].path) == NULL) {
            changed_any = true;
            if (changed_count != NULL && *changed_count < capacity) {
                if (sl_cli_copy_cstr(changed[*changed_count], SL_RUN_PATH_MAX_BYTES,
                                     previous->files[index].path))
                {
                    *changed_count += 1U;
                }
            }
        }
    }
    return changed_any;
}

static void sl_dev_print_changed_files(char changed[][SL_RUN_PATH_MAX_BYTES], size_t changed_count)
{
    if (changed_count == 0U) {
        return;
    }
    printf("Changed files:\n");
    for (size_t index = 0U; index < changed_count; index += 1U) {
        printf("  %s\n", changed[index]);
    }
}

static int sl_dev_refresh_openapi(const char* artifacts_path)
{
    SlCliOptions openapi_options = {0};
    char plan_path[SL_RUN_PATH_MAX_BYTES];
    char output_path[SL_RUN_PATH_MAX_BYTES];

    if (artifacts_path == NULL ||
        !sl_cli_join_leaf(plan_path, sizeof(plan_path), artifacts_path, "app.plan.json") ||
        !sl_cli_join_leaf(output_path, sizeof(output_path), artifacts_path, "openapi.json"))
    {
        sl_cli_write_cstr(stderr, "sloppy dev: OpenAPI path is too long\n");
        return 1;
    }
    openapi_options.command = "openapi";
    openapi_options.plan_path = plan_path;
    openapi_options.output_path = output_path;
    return sl_cli_command_openapi(&openapi_options);
}

static int sl_dev_build(const SlCliOptions* options, char* artifacts_path,
                        size_t artifacts_path_capacity, char* environment,
                        size_t environment_capacity)
{
    if (options == NULL || artifacts_path == NULL || environment == NULL) {
        return 1;
    }
    if (options->out_explicit && options->input_path == NULL) {
        sl_cli_write_cstr(stderr,
                          "sloppy dev: --out applies to positional source input; project dev uses "
                          "sloppy.json outDir\n");
        return 1;
    }
    return sl_run_prepare_source_input(options, artifacts_path, artifacts_path_capacity,
                                       environment, environment_capacity);
}

static int sl_dev_server_start(SlDevServer* dev, const SlCliOptions* options,
                               const char* artifacts_path)
{
    SlDiag diag = {0};
    SlStatus status;
    SlHttpTransportConfig config;
    size_t transport_storage_bytes = 0U;
    const char* run_host = options->host;
    uint16_t run_port = options->port;

    if (dev == NULL || options == NULL || artifacts_path == NULL) {
        return 1;
    }
    *dev = (SlDevServer){0};
    dev->app = sl_run_command_app();
    if (sl_run_load_app(artifacts_path, SLOPPY_BOOTSTRAP_BUILD_DIR, NULL, dev->app) != 0) {
        sl_run_shutdown_app(dev->app);
        return 1;
    }
    if (!options->host_explicit && dev->app->config_has_host) {
        run_host = dev->app->config_host;
    }
    if (!options->port_explicit && dev->app->config_has_port) {
        run_port = dev->app->config_port;
    }
    if (dev->app->plan.kind == SL_PLAN_KIND_PROGRAM) {
        sl_cli_write_cstr(stderr, "sloppy dev: dev server requires a web Plan\n");
        sl_run_shutdown_app(dev->app);
        return 1;
    }
    config = sl_run_transport_config(run_host, run_port, dev->app);
    status = sl_http_transport_server_arena_size(&config, &transport_storage_bytes, &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag("sloppy dev: failed to size server storage: ", NULL, &diag);
        sl_run_shutdown_app(dev->app);
        return 1;
    }
    status = sl_heap_buffer_alloc(&dev->transport_storage, transport_storage_bytes);
    if (!sl_status_is_ok(status) ||
        !sl_status_is_ok(sl_arena_init(&dev->transport_arena, dev->transport_storage.ptr,
                                       dev->transport_storage.length)))
    {
        sl_cli_write_cstr(stderr, "sloppy dev: failed to allocate server storage\n");
        sl_heap_buffer_dispose(&dev->transport_storage);
        sl_run_shutdown_app(dev->app);
        return 1;
    }
    status = sl_http_transport_server_init(&dev->server, &dev->transport_arena, &config, &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag("sloppy dev: failed to initialize server: ", &dev->transport_arena,
                          &diag);
        sl_heap_buffer_dispose(&dev->transport_storage);
        sl_run_shutdown_app(dev->app);
        return 1;
    }
    status = sl_http_transport_server_listen(&dev->server, &diag);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy dev: failed to listen on requested host/port\n");
        sl_http_transport_server_dispose(&dev->server, NULL);
        sl_heap_buffer_dispose(&dev->transport_storage);
        sl_run_shutdown_app(dev->app);
        return 1;
    }
    printf("Sloppy dev server listening on %s://%s:%u\n",
           config.tls.enabled ? "https" : "http", run_host,
           (unsigned)sl_http_transport_server_bound_port(&dev->server));
    dev->active = true;
    return 0;
}

static int sl_dev_server_stop(SlDevServer* dev)
{
    int result = 0;

    if (dev == NULL || !dev->active) {
        return 0;
    }
    if (!sl_status_is_ok(sl_http_transport_server_dispose(&dev->server, NULL))) {
        result = 1;
    }
    sl_heap_buffer_dispose(&dev->transport_storage);
    if (sl_run_shutdown_app(dev->app) != 0 && result == 0) {
        result = 1;
    }
    *dev = (SlDevServer){0};
    return result;
}

static int sl_dev_server_poll(SlDevServer* dev)
{
    SlDiag diag = {0};
    SlStatus status;

    if (dev == NULL || !dev->active) {
        return 1;
    }
    status = sl_http_transport_server_poll(&dev->server, &diag);
    if (!sl_status_is_ok(status)) {
        sl_run_print_diag("sloppy dev: server polling failed: ", &dev->transport_arena, &diag);
        return 1;
    }
    return 0;
}

static int sl_dev_reload(SlDevServer* server, const SlCliOptions* options, bool openapi,
                         char* artifacts_path, size_t artifacts_path_capacity, char* environment,
                         size_t environment_capacity)
{
    SlDevServer next = {0};

    printf("Rebuilding...\n");
    if (sl_dev_build(options, artifacts_path, artifacts_path_capacity, environment,
                     environment_capacity) != 0)
    {
        sl_cli_write_cstr(stderr,
                          "sloppy dev: build failed; keeping the previous server running.\n");
        return 1;
    }
    if (openapi && sl_dev_refresh_openapi(artifacts_path) != 0) {
        sl_cli_write_cstr(stderr, "sloppy dev: OpenAPI refresh failed.\n");
    }
    if (sl_dev_server_stop(server) != 0) {
        sl_cli_write_cstr(stderr, "sloppy dev: failed to stop previous server cleanly.\n");
    }
    if (sl_dev_server_start(&next, options, artifacts_path) != 0) {
        sl_cli_write_cstr(stderr, "sloppy dev: restart failed after successful build.\n");
        return 1;
    }
    *server = next;
    printf("Restarted.\n");
    return 0;
}

static int sl_cli_command_dev(const SlCliOptions* options)
{
    SlDevWatchPlan watch_plan = {0};
    static SlDevSnapshot previous;
    static SlDevSnapshot current;
    SlDevServer server = {0};
    char changed[32U][SL_RUN_PATH_MAX_BYTES];
    size_t changed_count = 0U;
    char artifacts_path[SL_RUN_PATH_MAX_BYTES];
    char environment[SL_RUN_CONFIG_HOST_MAX_BYTES];
    void (*old_sigint)(int) = NULL;
    void (*old_sigterm)(int) = NULL;
    int result = 0;

    if (options == NULL) {
        return 1;
    }
    previous = (SlDevSnapshot){0};
    current = (SlDevSnapshot){0};
    if (options->dev_clear) {
        sl_dev_clear_console();
    }
    printf("Building...\n");
    if (sl_dev_build(options, artifacts_path, sizeof(artifacts_path), environment,
                     sizeof(environment)) != 0)
    {
        return 1;
    }
    if (options->dev_openapi && sl_dev_refresh_openapi(artifacts_path) != 0) {
        sl_cli_write_cstr(stderr, "sloppy dev: OpenAPI refresh failed.\n");
    }
    if (sl_dev_server_start(&server, options, artifacts_path) != 0) {
        return 1;
    }
    if (!sl_dev_load_watch_plan(options, &watch_plan)) {
        if (sl_dev_server_stop(&server) != 0) {
            return 1;
        }
        return 1;
    }
    if (!sl_dev_snapshot_collect(&watch_plan, &previous)) {
        sl_cli_write_cstr(stderr, "sloppy dev: failed to initialize watch snapshot\n");
        if (sl_dev_server_stop(&server) != 0) {
            return 1;
        }
        return 1;
    }
    printf("Watching %u path%s. Press Ctrl+C to stop.\n", (unsigned)watch_plan.root_count,
           watch_plan.root_count == 1U ? "" : "s");

    sl_dev_stop_requested = 0;
    old_sigint = signal(SIGINT, sl_dev_signal_handler);
    old_sigterm = signal(SIGTERM, sl_dev_signal_handler);

    while (!sl_dev_stop_requested) {
        if (sl_dev_server_poll(&server) != 0) {
            result = 1;
            break;
        }
        if (options->dev_watch) {
            if (!sl_dev_snapshot_collect(&watch_plan, &current)) {
                sl_cli_write_cstr(stderr, "sloppy dev: watch snapshot failed\n");
                result = 1;
                break;
            }
            if (sl_dev_snapshots_differ(&previous, &current, changed,
                                        sizeof(changed) / sizeof(changed[0]), &changed_count))
            {
                sl_platform_sleep_ms(SL_DEV_DEBOUNCE_MS);
                if (options->dev_clear) {
                    sl_dev_clear_console();
                }
                sl_dev_print_changed_files(changed, changed_count);
                if (sl_dev_reload(&server, options, options->dev_openapi, artifacts_path,
                                  sizeof(artifacts_path), environment, sizeof(environment)) != 0)
                {
                    previous = current;
                }
                else if (!sl_dev_snapshot_collect(&watch_plan, &previous)) {
                    sl_cli_write_cstr(stderr, "sloppy dev: watch snapshot failed\n");
                    result = 1;
                    break;
                }
            }
            else {
                previous = current;
            }
        }
        sl_platform_sleep_ms(SL_DEV_POLL_INTERVAL_MS);
    }

    if (old_sigint != SIG_ERR) {
        signal(SIGINT, old_sigint);
    }
    if (old_sigterm != SIG_ERR) {
        signal(SIGTERM, old_sigterm);
    }
    printf("Stopping dev server...\n");
    if (sl_dev_server_stop(&server) != 0 && result == 0) {
        result = 1;
    }
    return result;
}
