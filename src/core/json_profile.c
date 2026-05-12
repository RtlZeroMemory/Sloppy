#include "sloppy/json_profile.h"

#include "sloppy/alloc.h"
#include "sloppy/platform_time.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

typedef struct SlJsonProfileState
{
    bool initialized;
    bool enabled;
    const char* scenario;
    uint64_t iterations;
    SlJsonProfilePhaseStats phases[SL_JSON_PROFILE_PHASE_COUNT];
    uint64_t counters[SL_JSON_PROFILE_COUNTER_COUNT];
} SlJsonProfileState;

static SlJsonProfileState g_sl_json_profile;

static const char* sl_json_profile_phase_name(SlJsonProfilePhase phase)
{
    static const char* const names[SL_JSON_PROFILE_PHASE_COUNT] = {"bodySizeCheck",
                                                                   "yyjsonParse",
                                                                   "rootSchemaLookup",
                                                                   "shapeLookup",
                                                                   "objectFieldIteration",
                                                                   "fieldLookup",
                                                                   "requiredFieldTracking",
                                                                   "scalarValidation",
                                                                   "stringValidation",
                                                                   "numberIntValidation",
                                                                   "arrayValidation",
                                                                   "unknownFieldPolicy",
                                                                   "pathConstruction",
                                                                   "issueRecording",
                                                                   "problemDetailsConstruction",
                                                                   "materializeOnceHandoffMarker",
                                                                   "genericFallbackPath",
                                                                   "writePlanLookup",
                                                                   "outputSizeEstimation",
                                                                   "responseFieldIteration",
                                                                   "fieldLookupValueAccess",
                                                                   "stringEscapeScan",
                                                                   "stringEscapeWrite",
                                                                   "scalarFormatting",
                                                                   "literalFragmentWrite",
                                                                   "builderBufferGrowCopy",
                                                                   "httpResponseHeaderWriting",
                                                                   "nativeFallbackPath",
                                                                   "capacityFailurePath"};

    return (unsigned int)phase < SL_JSON_PROFILE_PHASE_COUNT ? names[phase] : "unknown";
}

static const char* sl_json_profile_counter_name(SlJsonProfileCounter counter)
{
    static const char* const names[SL_JSON_PROFILE_COUNTER_COUNT] = {
        "requestsTotal",           "jsonBytesParsed",       "jsonValuesSeen",
        "objectFieldsSeen",        "schemaFieldLookups",    "schemaFieldLookupLinear",
        "schemaFieldLookupBinary", "schemaFieldLookupHash", "requiredBitmapChecks",
        "unknownFieldsSeen",       "issuesEmitted",         "pathsRendered",
        "problemDetailsBuilt",     "materializations",      "duplicateValidationSkipped",
        "responseFieldsWritten",   "stringsEscaped",        "stringsFastPathNoEscape",
        "scalarFormatCalls",       "builderGrows",          "bufferCopies",
        "nativeResponseHits",      "genericFallbacks",      "fallbackReasonCounts"};

    return (unsigned int)counter < SL_JSON_PROFILE_COUNTER_COUNT ? names[counter] : "unknown";
}

static void sl_json_profile_init_once(void)
{
    const char* value = NULL;
#if defined(_WIN32)
    char* allocated_value = NULL;
    size_t value_length = 0U;
#endif

    if (g_sl_json_profile.initialized) {
        return;
    }
#if defined(_WIN32)
    if (_dupenv_s(&allocated_value, &value_length, "SLOPPY_JSON_PROFILE") == 0) {
        (void)value_length;
        value = allocated_value;
    }
#else
    value = getenv("SLOPPY_JSON_PROFILE");
#endif
    g_sl_json_profile.enabled = value != NULL && value[0] != '\0' && strcmp(value, "0") != 0 &&
                                strcmp(value, "false") != 0 && strcmp(value, "FALSE") != 0;
#if defined(_WIN32)
    sl_alloc_release(allocated_value);
#endif
    g_sl_json_profile.initialized = true;
}

bool sl_json_profile_enabled(void)
{
    sl_json_profile_init_once();
    return g_sl_json_profile.enabled;
}

void sl_json_profile_reset(const char* scenario, uint64_t iterations)
{
    bool enabled = sl_json_profile_enabled();

    g_sl_json_profile = (SlJsonProfileState){0};
    g_sl_json_profile.initialized = true;
    g_sl_json_profile.enabled = enabled;
    g_sl_json_profile.scenario = scenario;
    g_sl_json_profile.iterations = iterations;
}

uint64_t sl_json_profile_phase_begin(SlJsonProfilePhase phase)
{
    uint64_t now_ns = 0U;

    (void)phase;
    if (!g_sl_json_profile.initialized) {
        sl_json_profile_init_once();
    }
    if (!g_sl_json_profile.enabled) {
        return 0U;
    }
    if (!sl_status_is_ok(sl_platform_monotonic_time_ns(&now_ns))) {
        return 0U;
    }
    return now_ns;
}

void sl_json_profile_phase_end(SlJsonProfilePhase phase, uint64_t start_ns)
{
    uint64_t end_ns = 0U;
    uint64_t elapsed_ns = 0U;
    SlJsonProfilePhaseStats* stats = NULL;

    if (!g_sl_json_profile.initialized) {
        sl_json_profile_init_once();
    }
    if (!g_sl_json_profile.enabled || start_ns == 0U ||
        (unsigned int)phase >= SL_JSON_PROFILE_PHASE_COUNT ||
        !sl_status_is_ok(sl_platform_monotonic_time_ns(&end_ns)))
    {
        return;
    }
    elapsed_ns = end_ns >= start_ns ? end_ns - start_ns : 0U;
    stats = &g_sl_json_profile.phases[phase];
    stats->total_ns += elapsed_ns;
    stats->count += 1U;
    if (stats->count == 1U || elapsed_ns < stats->min_ns) {
        stats->min_ns = elapsed_ns;
    }
    if (elapsed_ns > stats->max_ns) {
        stats->max_ns = elapsed_ns;
    }
}

void sl_json_profile_counter_add(SlJsonProfileCounter counter, uint64_t amount)
{
    if (!g_sl_json_profile.initialized) {
        sl_json_profile_init_once();
    }
    if (!g_sl_json_profile.enabled || (unsigned int)counter >= SL_JSON_PROFILE_COUNTER_COUNT) {
        return;
    }
    if (UINT64_MAX - g_sl_json_profile.counters[counter] < amount) {
        g_sl_json_profile.counters[counter] = UINT64_MAX;
        return;
    }
    g_sl_json_profile.counters[counter] += amount;
}

void sl_json_profile_snapshot(SlJsonProfileSnapshot* out_snapshot)
{
    if (out_snapshot == NULL) {
        return;
    }
    *out_snapshot = (SlJsonProfileSnapshot){0};
    if (!g_sl_json_profile.initialized) {
        sl_json_profile_init_once();
    }
    if (!g_sl_json_profile.enabled) {
        return;
    }
    out_snapshot->enabled = true;
    out_snapshot->scenario = g_sl_json_profile.scenario;
    out_snapshot->iterations = g_sl_json_profile.iterations;
    for (size_t index = 0U; index < SL_JSON_PROFILE_PHASE_COUNT; index += 1U) {
        out_snapshot->phases[index] = g_sl_json_profile.phases[index];
    }
    for (size_t index = 0U; index < SL_JSON_PROFILE_COUNTER_COUNT; index += 1U) {
        out_snapshot->counters[index] = g_sl_json_profile.counters[index];
    }
}

bool sl_json_profile_snapshot_has_data(const SlJsonProfileSnapshot* snapshot)
{
    return snapshot != NULL && snapshot->enabled;
}

static void sl_json_profile_indent(FILE* out, unsigned int indent)
{
    while (indent > 0U) {
        fputc(' ', out);
        indent -= 1U;
    }
}

void sl_json_profile_fprint_json(FILE* out, const SlJsonProfileSnapshot* snapshot,
                                 unsigned int indent)
{
    size_t index = 0U;

    if (out == NULL || !sl_json_profile_snapshot_has_data(snapshot)) {
        return;
    }

    sl_json_profile_indent(out, indent);
    fputs("{\n", out);
    sl_json_profile_indent(out, indent + 2U);
    fprintf(out, "\"scenario\": \"%s\",\n", snapshot->scenario == NULL ? "" : snapshot->scenario);
    sl_json_profile_indent(out, indent + 2U);
    fprintf(out, "\"iterations\": %" PRIu64 ",\n", snapshot->iterations);
    sl_json_profile_indent(out, indent + 2U);
    fputs("\"phases\": {\n", out);
    for (index = 0U; index < SL_JSON_PROFILE_PHASE_COUNT; index += 1U) {
        const SlJsonProfilePhaseStats* stats = &snapshot->phases[index];
        double avg_ns = stats->count == 0U ? 0.0 : (double)stats->total_ns / (double)stats->count;

        sl_json_profile_indent(out, indent + 4U);
        fprintf(out,
                "\"%s\": { \"totalNs\": %" PRIu64 ", \"avgNs\": %.2f, \"minNs\": %" PRIu64
                ", \"maxNs\": %" PRIu64 ", \"count\": %" PRIu64 " }%s\n",
                sl_json_profile_phase_name((SlJsonProfilePhase)index), stats->total_ns, avg_ns,
                stats->min_ns, stats->max_ns, stats->count,
                index + 1U == SL_JSON_PROFILE_PHASE_COUNT ? "" : ",");
    }
    sl_json_profile_indent(out, indent + 2U);
    fputs("},\n", out);
    sl_json_profile_indent(out, indent + 2U);
    fputs("\"counters\": {\n", out);
    for (index = 0U; index < SL_JSON_PROFILE_COUNTER_COUNT; index += 1U) {
        sl_json_profile_indent(out, indent + 4U);
        fprintf(out, "\"%s\": %" PRIu64 "%s\n",
                sl_json_profile_counter_name((SlJsonProfileCounter)index),
                snapshot->counters[index], index + 1U == SL_JSON_PROFILE_COUNTER_COUNT ? "" : ",");
    }
    sl_json_profile_indent(out, indent + 2U);
    fputs("}\n", out);
    sl_json_profile_indent(out, indent);
    fputs("}", out);
}
