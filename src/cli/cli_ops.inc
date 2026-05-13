/*
 * src/cli/cli_ops.inc
 *
 * Health and metrics Plan introspection commands.
 * Included only by src/main.c to preserve existing static helper visibility.
 */
static bool sl_cli_ops_route_is_metrics(const SlCliRoute* route)
{
    return route != NULL && (sl_cli_span_equal_cstr(route->name, "Management.Metrics") ||
                             sl_cli_span_equal_cstr(route->name, "Management.MetricsJson"));
}

static int sl_cli_ops_load_metadata(const SlCliOptions* options, const char* command,
                                    yyjson_doc** doc, SlCliMetadata* metadata)
{
    unsigned char* json_storage = sl_cli_scratch_json_storage();
    SlArena plan_arena = {0};

    if (options->plan_path == NULL) {
        sl_cli_write_error_with_value("sloppy ", command, ": --plan <path> is required\n");
        return 1;
    }
    if (sl_cli_init_scratch_plan_arena(command, &plan_arena) != 0) {
        return 1;
    }
    if (sl_cli_load_metadata(options->plan_path, json_storage, &plan_arena, true, doc, metadata) !=
        0)
    {
        if (*doc != NULL) {
            yyjson_doc_free(*doc);
            *doc = NULL;
        }
        return 1;
    }
    return 0;
}

static void sl_cli_health_emit_json_route(const SlCliRoute* route, size_t emitted)
{
    size_t check = 0U;

    printf("%s\n    { \"method\": ", emitted == 0U ? "" : ",");
    sl_cli_json_escape(stdout, route->method);
    printf(", \"path\": ");
    sl_cli_json_escape(stdout, route->pattern);
    printf(", \"kind\": ");
    sl_cli_json_escape(stdout, route->health_kind);
    printf(", \"name\": ");
    sl_cli_json_escape(stdout, route->name);
    printf(", \"checks\": [");
    for (check = 0U; check < route->health_check_count; check += 1U) {
        printf("%s", check == 0U ? "" : ", ");
        sl_cli_json_escape(stdout, route->health_checks[check]);
    }
    printf("] }");
}

static int sl_cli_command_health(const SlCliOptions* options)
{
    yyjson_doc* doc = NULL;
    SlCliMetadata* metadata = sl_cli_scratch_metadata();
    size_t index = 0U;
    size_t emitted = 0U;

    if (sl_cli_ops_load_metadata(options, "health", &doc, metadata) != 0) {
        return 1;
    }

    if (options->format == SL_CLI_FORMAT_JSON) {
        printf("{\n  \"health\": { \"endpoints\": [");
        for (index = 0U; index < metadata->route_count; index += 1U) {
            if (sl_cli_span_empty(metadata->routes[index].health_kind)) {
                continue;
            }
            sl_cli_health_emit_json_route(&metadata->routes[index], emitted);
            emitted += 1U;
        }
        printf("\n  ], \"count\": %zu }\n}\n", emitted);
    }
    else {
        printf("Sloppy Health\n\n");
        for (index = 0U; index < metadata->route_count; index += 1U) {
            const SlCliRoute* route = &metadata->routes[index];
            if (sl_cli_span_empty(route->health_kind)) {
                continue;
            }
            printf("%-6.*s  %-24.*s  %-10.*s  %.*s\n", (int)route->method.length, route->method.ptr,
                   (int)route->pattern.length, route->pattern.ptr, (int)route->health_kind.length,
                   route->health_kind.ptr, (int)route->name.length, route->name.ptr);
            emitted += 1U;
        }
        if (emitted == 0U) {
            printf("No health endpoints in Plan metadata.\n");
        }
    }

    yyjson_doc_free(doc);
    return 0;
}

static void sl_cli_metrics_emit_json_route(const SlCliRoute* route, size_t emitted)
{
    printf("%s\n    { \"method\": ", emitted == 0U ? "" : ",");
    sl_cli_json_escape(stdout, route->method);
    printf(", \"path\": ");
    sl_cli_json_escape(stdout, route->pattern);
    printf(", \"name\": ");
    sl_cli_json_escape(stdout, route->name);
    printf(", \"format\": ");
    sl_cli_json_escape(stdout, sl_cli_span_equal_cstr(route->name, "Management.MetricsJson")
                                   ? sl_cli_span_cstr("json")
                                   : sl_cli_span_cstr("prometheus"));
    printf(" }");
}

static int sl_cli_command_metrics(const SlCliOptions* options)
{
    yyjson_doc* doc = NULL;
    SlCliMetadata* metadata = sl_cli_scratch_metadata();
    size_t index = 0U;
    size_t emitted = 0U;

    if (sl_cli_ops_load_metadata(options, "metrics", &doc, metadata) != 0) {
        return 1;
    }

    if (options->format == SL_CLI_FORMAT_JSON) {
        printf("{\n  \"metrics\": { \"endpoints\": [");
        for (index = 0U; index < metadata->route_count; index += 1U) {
            if (!sl_cli_ops_route_is_metrics(&metadata->routes[index])) {
                continue;
            }
            sl_cli_metrics_emit_json_route(&metadata->routes[index], emitted);
            emitted += 1U;
        }
        printf("\n  ], \"count\": %zu }\n}\n", emitted);
    }
    else {
        printf("Sloppy Metrics\n\n");
        for (index = 0U; index < metadata->route_count; index += 1U) {
            const SlCliRoute* route = &metadata->routes[index];
            SlCliSpan format = sl_cli_span_equal_cstr(route->name, "Management.MetricsJson")
                                   ? sl_cli_span_cstr("json")
                                   : sl_cli_span_cstr("prometheus");
            if (!sl_cli_ops_route_is_metrics(route)) {
                continue;
            }
            printf("%-6.*s  %-24.*s  %-10.*s  %.*s\n", (int)route->method.length, route->method.ptr,
                   (int)route->pattern.length, route->pattern.ptr, (int)format.length, format.ptr,
                   (int)route->name.length, route->name.ptr);
            emitted += 1U;
        }
        if (emitted == 0U) {
            printf("No metrics endpoints in Plan metadata.\n");
        }
    }

    yyjson_doc_free(doc);
    return 0;
}
