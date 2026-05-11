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

static bool sl_cli_route_segment_is_param(SlCliSpan segment, bool* out_constrained)
{
    size_t index = 0U;
    bool constrained = false;

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
            constrained = true;
            break;
        }
    }
    if (out_constrained != NULL) {
        *out_constrained = constrained;
    }
    return true;
}

static void sl_cli_route_specificity(const SlCliRoute* route, bool* out_has_params,
                                     size_t* out_static_segments, size_t* out_constrained_params,
                                     size_t* out_segments)
{
    size_t index = 1U;
    bool has_params = false;
    size_t static_segments = 0U;
    size_t constrained_params = 0U;
    size_t segments = 0U;

    if (route == NULL || route->pattern.length == 0U || route->pattern.ptr == NULL ||
        sl_cli_span_equal_cstr(route->pattern, "/"))
    {
        goto done;
    }

    while (index <= route->pattern.length) {
        size_t start = index;
        size_t end = index;
        bool constrained = false;
        SlCliSpan segment = {0};
        while (end < route->pattern.length && route->pattern.ptr[end] != '/') {
            end += 1U;
        }
        segment = (SlCliSpan){&route->pattern.ptr[start], end - start};
        if (sl_cli_route_segment_is_param(segment, &constrained)) {
            has_params = true;
            if (constrained) {
                constrained_params += 1U;
            }
        }
        else {
            static_segments += 1U;
        }
        segments += 1U;
        if (end == route->pattern.length) {
            break;
        }
        index = end + 1U;
    }

done:
    *out_has_params = has_params;
    *out_static_segments = static_segments;
    *out_constrained_params = constrained_params;
    *out_segments = segments;
}

static int sl_cli_route_compare(const SlCliRoute* left, const SlCliRoute* right)
{
    int result = sl_cli_span_compare(left->method, right->method);
    bool left_params = false;
    bool right_params = false;
    size_t left_static = 0U;
    size_t right_static = 0U;
    size_t left_constrained = 0U;
    size_t right_constrained = 0U;
    size_t left_segments = 0U;
    size_t right_segments = 0U;

    if (result != 0) {
        return result;
    }
    sl_cli_route_specificity(left, &left_params, &left_static, &left_constrained, &left_segments);
    sl_cli_route_specificity(right, &right_params, &right_static, &right_constrained,
                             &right_segments);
    if (left_params != right_params) {
        return left_params ? 1 : -1;
    }
    if (left_static != right_static) {
        return left_static > right_static ? -1 : 1;
    }
    if (left_constrained != right_constrained) {
        return left_constrained > right_constrained ? -1 : 1;
    }
    if (left_segments != right_segments) {
        return left_segments > right_segments ? -1 : 1;
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
