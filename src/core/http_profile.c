#include "sloppy/http_profile.h"

#include "sloppy/json_writer.h"
#include "sloppy/platform_time.h"
#include "sloppy/string.h"

#include "env.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct SlHttpProfilePhaseStats
{
    uint64_t total_ns;
    uint64_t count;
    uint64_t min_ns;
    uint64_t max_ns;
} SlHttpProfilePhaseStats;

typedef struct SlHttpProfileState
{
    SlHttpProfilePhaseStats phases[SL_HTTP_PROFILE_PHASE_COUNT];
    uint64_t counters[SL_HTTP_PROFILE_COUNTER_COUNT];
} SlHttpProfileState;

static SlHttpProfileState sl_http_profile_global;
static bool sl_http_profile_enabled_cached;
static bool sl_http_profile_enabled_initialized;

static bool sl_http_profile_env_copy(const char* name, char* buffer, size_t capacity)
{
    if (buffer == NULL || capacity == 0U) {
        return false;
    }
    buffer[0] = '\0';
#if defined(_WIN32)
    {
        size_t required = 0U;
        errno_t result = getenv_s(&required, buffer, capacity, name);
        if (result != 0 || required == 0U || required > capacity) {
            buffer[0] = '\0';
            return false;
        }
        return true;
    }
#else
    {
        const char* value = getenv(name);
        size_t index = 0U;
        if (value == NULL || value[0] == '\0') {
            return false;
        }
        while (index + 1U < capacity && value[index] != '\0') {
            buffer[index] = value[index];
            index += 1U;
        }
        buffer[index] = '\0';
        return true;
    }
#endif
}

static const char* sl_http_profile_phase_name(SlHttpProfilePhase phase)
{
    static const char* names[SL_HTTP_PROFILE_PHASE_COUNT] = {
        "socket_read",
        "http_parse",
        "header_classification",
        "media_type_content_length",
        "body_policy",
        "route_dispatch",
        "json_validation",
        "body_materialization",
        "v8_handler_execution",
        "v8_context_construction",
        "v8_handler_call",
        "v8_result_conversion",
        "v8_result_construction",
        "v8_json_stringify_generic_serialization",
        "native_response_selection",
        "response_serialization_header_writing",
        "socket_write_scheduling",
        "write_completion",
        "connection_keep_alive_reuse_close",
        "diagnostics_logging_breadcrumb_overhead",
        "v8_handler_lookup",
        "v8_context_base_object_creation",
        "v8_request_facade_creation",
        "v8_route_params_materialization",
        "v8_query_materialization",
        "v8_headers_materialization",
        "v8_body_facade_materialization",
        "v8_body_json_materialization",
        "v8_services_materialization",
        "v8_promise_await_microtask",
        "v8_exception_mapping"};
    if ((unsigned int)phase >= SL_HTTP_PROFILE_PHASE_COUNT) {
        return "unknown";
    }
    return names[phase];
}

static const char* sl_http_profile_counter_name(SlHttpProfileCounter counter)
{
    static const char* names[SL_HTTP_PROFILE_COUNTER_COUNT] = {"requestsTotal",
                                                               "keepAliveReused",
                                                               "connectionsOpened",
                                                               "connectionsClosed",
                                                               "parserBytes",
                                                               "headerCount",
                                                               "bodyBytes",
                                                               "nativeRouteHits",
                                                               "classicRouteHits",
                                                               "nativeJsonHits",
                                                               "genericJsonHits",
                                                               "jsonRejects",
                                                               "materializations",
                                                               "duplicateValidationSkipped",
                                                               "nativeResponseHits",
                                                               "v8HandlerCalls",
                                                               "responseBytesWritten",
                                                               "uvWriteCalls",
                                                               "uvTryWriteCalls",
                                                               "writeBufferAllocations",
                                                               "responseBufferCopies",
                                                               "arenaResets",
                                                               "diagnosticsRendered",
                                                               "breadcrumbsRecorded",
                                                               "nativeResponseEligible",
                                                               "v8HandlerCacheHits",
                                                               "v8HandlerCacheMisses",
                                                               "ctxCreated",
                                                               "routeParamsMaterialized",
                                                               "queryMaterialized",
                                                               "headersMaterialized",
                                                               "bodyFacadeMaterialized",
                                                               "bodyJsonMaterialized",
                                                               "servicesMaterialized",
                                                               "syncReturns",
                                                               "promiseReturns",
                                                               "resultDescriptorConversions",
                                                               "plainValueConversions",
                                                               "resultsJsonConversions",
                                                               "resultsTextConversions",
                                                               "resultsStatusConversions",
                                                               "resultsProblemConversions",
                                                               "jsonStringifyCalls",
                                                               "exceptionCount",
                                                               "noJsResponsePlanHits",
                                                               "noJsResponsePlanMisses",
                                                               "nativeStaticResponseHits",
                                                               "genericFallbacks"};
    if ((unsigned int)counter >= SL_HTTP_PROFILE_COUNTER_COUNT) {
        return "unknown";
    }
    return names[counter];
}

bool sl_http_profile_enabled(void)
{
    char value[16];
    if (!sl_http_profile_enabled_initialized) {
        sl_http_profile_enabled_cached =
            (sl_http_profile_env_copy("SLOPPY_HTTP_PROFILE", value, sizeof(value)) &&
             sl_env_value_is_truthy(value)) ||
            (sl_http_profile_env_copy("SLOPPY_V8_PROFILE", value, sizeof(value)) &&
             sl_env_value_is_truthy(value));
        sl_http_profile_enabled_initialized = true;
    }
    return sl_http_profile_enabled_cached;
}

uint64_t sl_http_profile_now_ns(void)
{
    uint64_t now = 0U;
    if (!sl_http_profile_enabled()) {
        return 0U;
    }
    if (!sl_status_is_ok(sl_platform_monotonic_time_ns(&now))) {
        return 0U;
    }
    return now;
}

void sl_http_profile_reset(void)
{
    sl_http_profile_global = (SlHttpProfileState){0};
    sl_http_profile_enabled_cached = false;
    sl_http_profile_enabled_initialized = false;
}

void sl_http_profile_record_phase(SlHttpProfilePhase phase, uint64_t elapsed_ns)
{
    SlHttpProfilePhaseStats* stats = NULL;

    if (!sl_http_profile_enabled() || (unsigned int)phase >= SL_HTTP_PROFILE_PHASE_COUNT) {
        return;
    }
    stats = &sl_http_profile_global.phases[phase];
    stats->total_ns += elapsed_ns;
    stats->count += 1U;
    if (stats->count == 1U || elapsed_ns < stats->min_ns) {
        stats->min_ns = elapsed_ns;
    }
    if (elapsed_ns > stats->max_ns) {
        stats->max_ns = elapsed_ns;
    }
}

void sl_http_profile_count(SlHttpProfileCounter counter, uint64_t amount)
{
    if (!sl_http_profile_enabled() || (unsigned int)counter >= SL_HTTP_PROFILE_COUNTER_COUNT) {
        return;
    }
    sl_http_profile_global.counters[counter] += amount;
}

static SlStatus sl_http_profile_append_json_string(SlByteBuilder* builder, const char* text)
{
    return sl_json_writer_append_escaped_string_bytes(
        builder, text == NULL ? sl_str_empty() : sl_str_from_cstr(text));
}

static SlStatus sl_http_profile_append_cstr(SlByteBuilder* builder, const char* text)
{
    SlStr str = sl_str_from_cstr(text);
    return sl_byte_builder_append_bytes(
        builder, sl_bytes_from_parts((const unsigned char*)str.ptr, str.length));
}

static SlStatus sl_http_profile_append_u64(SlByteBuilder* builder, uint64_t value)
{
    char digits[32];
    size_t count = 0U;
    size_t index = 0U;

    if (value == 0U) {
        return sl_byte_builder_append_byte(builder, (unsigned char)'0');
    }
    while (value != 0U && count < sizeof(digits)) {
        digits[count] = (char)('0' + (value % 10U));
        value /= 10U;
        count += 1U;
    }
    for (index = count; index > 0U; index -= 1U) {
        SlStatus status = sl_byte_builder_append_byte(builder, (unsigned char)digits[index - 1U]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_status_ok();
}

static SlStatus sl_http_profile_append_phase(SlByteBuilder* builder, SlHttpProfilePhase phase,
                                             bool first)
{
    const SlHttpProfilePhaseStats* stats = &sl_http_profile_global.phases[phase];
    uint64_t avg = stats->count == 0U ? 0U : stats->total_ns / stats->count;
    SlStatus status = sl_http_profile_append_cstr(builder, first ? "" : ",\n");

    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_profile_append_cstr(builder, "    ");
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_profile_append_json_string(builder, sl_http_profile_phase_name(phase));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_profile_append_cstr(builder, ": { \"totalNs\": ");
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_profile_append_u64(builder, stats->total_ns);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_profile_append_cstr(builder, ", \"count\": ");
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_profile_append_u64(builder, stats->count);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_profile_append_cstr(builder, ", \"avgNs\": ");
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_profile_append_u64(builder, avg);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_profile_append_cstr(builder, ", \"minNs\": ");
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_profile_append_u64(builder, stats->count == 0U ? 0U : stats->min_ns);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_profile_append_cstr(builder, ", \"maxNs\": ");
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_profile_append_u64(builder, stats->max_ns);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_http_profile_append_cstr(builder, " }");
}

SlStatus sl_http_profile_write_json(SlByteBuilder* builder)
{
    char scenario[256];
    SlStatus status;

    if (builder == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_http_profile_append_cstr(builder, "{\n  \"scenario\": ");
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!sl_http_profile_env_copy("SLOPPY_HTTP_PROFILE_SCENARIO", scenario, sizeof(scenario))) {
        scenario[0] = 'u';
        scenario[1] = 'n';
        scenario[2] = 'k';
        scenario[3] = 'n';
        scenario[4] = 'o';
        scenario[5] = 'w';
        scenario[6] = 'n';
        scenario[7] = '\0';
    }
    status = sl_http_profile_append_json_string(builder, scenario);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_profile_append_cstr(builder, ",\n  \"requests\": ");
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_profile_append_u64(
        builder, sl_http_profile_global.counters[SL_HTTP_PROFILE_COUNTER_REQUESTS_TOTAL]);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_profile_append_cstr(builder, ",\n  \"phases\": {\n");
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (unsigned int phase = 0U; phase < SL_HTTP_PROFILE_PHASE_COUNT; phase += 1U) {
        status = sl_http_profile_append_phase(builder, (SlHttpProfilePhase)phase, phase == 0U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    status = sl_http_profile_append_cstr(builder, "\n  },\n  \"counters\": {\n");
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (unsigned int counter = 0U; counter < SL_HTTP_PROFILE_COUNTER_COUNT; counter += 1U) {
        status = sl_http_profile_append_cstr(builder, counter == 0U ? "    " : ",\n    ");
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_http_profile_append_json_string(
            builder, sl_http_profile_counter_name((SlHttpProfileCounter)counter));
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_http_profile_append_cstr(builder, ": ");
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_http_profile_append_u64(builder, sl_http_profile_global.counters[counter]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_http_profile_append_cstr(builder, "\n  }\n}\n");
}

SlStatus sl_http_profile_flush_if_requested(void)
{
    char path[1024];
    unsigned char storage[131072];
    SlByteBuilder builder = {0};
    SlBytes json = {0};
    FILE* file = NULL;
    SlStatus status;

    if (!sl_http_profile_enabled() ||
        !sl_http_profile_env_copy("SLOPPY_HTTP_PROFILE_OUT", path, sizeof(path)))
    {
        return sl_status_ok();
    }
    status = sl_byte_builder_init_fixed(&builder, storage, sizeof(storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_profile_write_json(&builder);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    json = sl_byte_builder_view(&builder);
#if defined(_WIN32)
    if (fopen_s(&file, path, "wb") != 0) {
        file = NULL;
    }
#else
    file = fopen(path, "wb");
#endif
    if (file == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    if (json.length != 0U && fwrite(json.ptr, 1U, json.length, file) != json.length) {
        fclose(file);
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    if (fclose(file) != 0) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    return sl_status_ok();
}
