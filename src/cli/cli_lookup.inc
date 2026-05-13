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

static const SlCliSchema* sl_cli_find_schema(const SlCliMetadata* metadata, SlCliSpan name)
{
    size_t index = 0U;

    if (sl_cli_span_empty(name)) {
        return NULL;
    }
    for (index = 0U; index < metadata->schema_count; index += 1U) {
        if (sl_cli_span_equal(metadata->schemas[index].name, name)) {
            return &metadata->schemas[index];
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

static bool sl_cli_route_segment_is_param(SlCliSpan segment, bool* out_constrained)
{
    size_t index = 0U;
    bool constrained = false;
    SlCliSpan kind = sl_cli_span_cstr("str");

    if (out_constrained != NULL) {
        *out_constrained = false;
    }
    if (segment.length < 3U || segment.ptr == NULL || segment.ptr[0] != '{' ||
        segment.ptr[segment.length - 1U] != '}')
    {
        return false;
    }
    for (index = 1U; index + 1U < segment.length; index += 1U) {
        if (segment.ptr[index] == ':') {
            kind = (SlCliSpan){&segment.ptr[index + 1U], segment.length - index - 2U};
            constrained = !sl_cli_span_equal_cstr(kind, "str");
            break;
        }
    }
    if (out_constrained != NULL) {
        *out_constrained = constrained;
    }
    return true;
}

static SlCliSpan sl_cli_route_segment_at(SlCliSpan pattern, size_t* cursor)
{
    size_t start = 0U;
    size_t end = 0U;

    if (cursor == NULL || pattern.length == 0U || pattern.ptr == NULL ||
        sl_cli_span_equal_cstr(pattern, "/") || *cursor >= pattern.length)
    {
        return (SlCliSpan){0};
    }

    start = *cursor == 0U ? 1U : *cursor;
    end = start;
    while (end < pattern.length && pattern.ptr[end] != '/') {
        end += 1U;
    }
    *cursor = end == pattern.length ? pattern.length : end + 1U;
    return (SlCliSpan){&pattern.ptr[start], end - start};
}

static int sl_cli_route_segment_rank(SlCliSpan segment)
{
    bool constrained = false;

    if (!sl_cli_route_segment_is_param(segment, &constrained)) {
        return 3;
    }
    return constrained ? 2 : 1;
}

static int sl_cli_route_compare(const SlCliRoute* left, const SlCliRoute* right)
{
    int result = sl_cli_span_compare(left->method, right->method);
    size_t left_cursor = 0U;
    size_t right_cursor = 0U;

    if (result != 0) {
        return result;
    }

    while (left_cursor < left->pattern.length || right_cursor < right->pattern.length) {
        SlCliSpan left_segment = sl_cli_route_segment_at(left->pattern, &left_cursor);
        SlCliSpan right_segment = sl_cli_route_segment_at(right->pattern, &right_cursor);
        int left_rank = sl_cli_route_segment_rank(left_segment);
        int right_rank = sl_cli_route_segment_rank(right_segment);

        if (left_segment.ptr == NULL || right_segment.ptr == NULL) {
            if (left_segment.ptr != right_segment.ptr) {
                return left_segment.ptr == NULL ? 1 : -1;
            }
            break;
        }
        if (left_rank != right_rank) {
            return left_rank > right_rank ? -1 : 1;
        }
        if (left_rank == 3 && !sl_cli_span_equal(left_segment, right_segment)) {
            result = sl_cli_span_compare(left_segment, right_segment);
            if (result != 0) {
                return result;
            }
        }
    }
    if (left->source_order == right->source_order) {
        return 0;
    }
    return left->source_order < right->source_order ? -1 : 1;
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
