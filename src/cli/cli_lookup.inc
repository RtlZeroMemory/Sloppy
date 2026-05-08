/*
 * src/cli/cli_lookup.inc
 *
 * Private metadata lookup and route ordering helpers.
 * Included only by src/main.c to preserve existing static helper visibility.
 */
static const SlCliModule* sl_cli_find_module(const SlCliMetadata* metadata, SlCliSpan name)
{
    size_t index = 0U;

    for (index = 0U; index < metadata->module_count; index += 1U) {
        if (sl_cli_span_equal(metadata->modules[index].name, name)) {
            return &metadata->modules[index];
        }
    }

    return NULL;
}

static const SlCliProvider* sl_cli_find_provider(const SlCliMetadata* metadata, SlCliSpan token)
{
    size_t index = 0U;

    for (index = 0U; index < metadata->provider_count; index += 1U) {
        if (sl_cli_span_equal(metadata->providers[index].token, token)) {
            return &metadata->providers[index];
        }
    }

    return NULL;
}

static const SlCliCapability* sl_cli_find_capability(const SlCliMetadata* metadata, SlCliSpan token)
{
    size_t index = 0U;

    for (index = 0U; index < metadata->capability_count; index += 1U) {
        if (sl_cli_span_equal(metadata->capabilities[index].token, token)) {
            return &metadata->capabilities[index];
        }
    }

    return NULL;
}

static bool sl_cli_capability_allows(SlCliSpan access, SlCliSpan operation)
{
    if (sl_cli_span_equal_cstr(operation, "read")) {
        return sl_cli_span_equal_cstr(access, "read") ||
               sl_cli_span_equal_cstr(access, "readwrite");
    }
    if (sl_cli_span_equal_cstr(operation, "write")) {
        return sl_cli_span_equal_cstr(access, "write") ||
               sl_cli_span_equal_cstr(access, "readwrite");
    }
    if (sl_cli_span_equal_cstr(operation, "connect")) {
        return sl_cli_span_equal_cstr(access, "connect") ||
               sl_cli_span_equal_cstr(access, "connect-listen");
    }
    if (sl_cli_span_equal_cstr(operation, "listen")) {
        return sl_cli_span_equal_cstr(access, "listen") ||
               sl_cli_span_equal_cstr(access, "connect-listen");
    }
    return false;
}

static int sl_cli_span_compare(SlCliSpan left, SlCliSpan right)
{
    return sl_str_compare(sl_cli_span_str(left), sl_cli_span_str(right));
}

static int sl_cli_route_compare(const SlCliRoute* left, const SlCliRoute* right)
{
    int result = sl_cli_span_compare(left->method, right->method);
    if (result != 0) {
        return result;
    }
    result = sl_cli_span_compare(left->pattern, right->pattern);
    if (result != 0) {
        return result;
    }
    if (left->handler_id == right->handler_id) {
        return 0;
    }
    return left->handler_id < right->handler_id ? -1 : 1;
}

static void sl_cli_sort_routes(SlCliMetadata* metadata)
{
    size_t outer = 0U;
    size_t inner = 0U;

    for (outer = 0U; outer < metadata->route_count; outer += 1U) {
        for (inner = outer + 1U; inner < metadata->route_count; inner += 1U) {
            if (sl_cli_route_compare(&metadata->routes[inner], &metadata->routes[outer]) < 0) {
                SlCliRoute temp = metadata->routes[outer];
                metadata->routes[outer] = metadata->routes[inner];
                metadata->routes[inner] = temp;
            }
        }
    }
}
