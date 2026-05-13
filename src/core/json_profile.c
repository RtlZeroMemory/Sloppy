#include "sloppy/json_profile.h"

#include "sloppy/alloc.h"
#include "sloppy/platform_thread.h"
#include "sloppy/platform_time.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct SlJsonProfileState
{
    bool initialized;
    bool enabled;
    char scenario[SL_JSON_PROFILE_SCENARIO_CAPACITY];
    uint64_t iterations;
    SlJsonProfilePhaseStats phases[SL_JSON_PROFILE_PHASE_COUNT];
    uint64_t counters[SL_JSON_PROFILE_COUNTER_COUNT];
} SlJsonProfileState;

static SlJsonProfileState g_sl_json_profile;

static void sl_json_profile_copy_scenario(char* out, size_t out_capacity, const char* scenario)
{
    size_t index = 0U;

    if (out == NULL || out_capacity == 0U) {
        return;
    }
    if (scenario == NULL) {
        out[0] = '\0';
        return;
    }
    while (index + 1U < out_capacity && scenario[index] != '\0') {
        out[index] = scenario[index];
        index += 1U;
    }
    out[index] = '\0';
}

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

static void sl_json_profile_init_once_locked(void)
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
    bool enabled = false;

    sl_platform_global_mutex_lock();
    sl_json_profile_init_once_locked();
    enabled = g_sl_json_profile.enabled;
    sl_platform_global_mutex_unlock();
    return enabled;
}

void sl_json_profile_reset(const char* scenario, uint64_t iterations)
{
    bool enabled = false;

    sl_platform_global_mutex_lock();
    sl_json_profile_init_once_locked();
    enabled = g_sl_json_profile.enabled;
    g_sl_json_profile = (SlJsonProfileState){0};
    g_sl_json_profile.initialized = true;
    g_sl_json_profile.enabled = enabled;
    sl_json_profile_copy_scenario(g_sl_json_profile.scenario, sizeof(g_sl_json_profile.scenario),
                                  scenario);
    g_sl_json_profile.iterations = iterations;
    sl_platform_global_mutex_unlock();
}

uint64_t sl_json_profile_phase_begin(SlJsonProfilePhase phase)
{
    uint64_t now_ns = 0U;
    bool enabled = false;

    (void)phase;
    sl_platform_global_mutex_lock();
    sl_json_profile_init_once_locked();
    enabled = g_sl_json_profile.enabled;
    sl_platform_global_mutex_unlock();
    if (!enabled) {
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

    if (start_ns == 0U || (unsigned int)phase >= SL_JSON_PROFILE_PHASE_COUNT ||
        !sl_status_is_ok(sl_platform_monotonic_time_ns(&end_ns)))
    {
        return;
    }
    sl_platform_global_mutex_lock();
    sl_json_profile_init_once_locked();
    if (!g_sl_json_profile.enabled) {
        sl_platform_global_mutex_unlock();
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
    sl_platform_global_mutex_unlock();
}

void sl_json_profile_counter_add(SlJsonProfileCounter counter, uint64_t amount)
{
    sl_platform_global_mutex_lock();
    sl_json_profile_init_once_locked();
    if (!g_sl_json_profile.enabled || (unsigned int)counter >= SL_JSON_PROFILE_COUNTER_COUNT) {
        sl_platform_global_mutex_unlock();
        return;
    }
    if (UINT64_MAX - g_sl_json_profile.counters[counter] < amount) {
        g_sl_json_profile.counters[counter] = UINT64_MAX;
        sl_platform_global_mutex_unlock();
        return;
    }
    g_sl_json_profile.counters[counter] += amount;
    sl_platform_global_mutex_unlock();
}

void sl_json_profile_snapshot(SlJsonProfileSnapshot* out_snapshot)
{
    if (out_snapshot == NULL) {
        return;
    }
    *out_snapshot = (SlJsonProfileSnapshot){0};
    sl_platform_global_mutex_lock();
    sl_json_profile_init_once_locked();
    if (!g_sl_json_profile.enabled) {
        sl_platform_global_mutex_unlock();
        return;
    }
    out_snapshot->enabled = true;
    sl_json_profile_copy_scenario(out_snapshot->scenario_storage,
                                  sizeof(out_snapshot->scenario_storage),
                                  g_sl_json_profile.scenario);
    out_snapshot->scenario = out_snapshot->scenario_storage;
    out_snapshot->iterations = g_sl_json_profile.iterations;
    for (size_t index = 0U; index < SL_JSON_PROFILE_PHASE_COUNT; index += 1U) {
        out_snapshot->phases[index] = g_sl_json_profile.phases[index];
    }
    for (size_t index = 0U; index < SL_JSON_PROFILE_COUNTER_COUNT; index += 1U) {
        out_snapshot->counters[index] = g_sl_json_profile.counters[index];
    }
    sl_platform_global_mutex_unlock();
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

static void sl_json_profile_fprint_string(FILE* out, const char* text)
{
    fputc('"', out);
    if (text != NULL) {
        for (size_t index = 0U; text[index] != '\0'; index += 1U) {
            unsigned char ch = (unsigned char)text[index];

            switch (ch) {
            case '"':
                fputs("\\\"", out);
                break;
            case '\\':
                fputs("\\\\", out);
                break;
            case '\b':
                fputs("\\b", out);
                break;
            case '\f':
                fputs("\\f", out);
                break;
            case '\n':
                fputs("\\n", out);
                break;
            case '\r':
                fputs("\\r", out);
                break;
            case '\t':
                fputs("\\t", out);
                break;
            default:
                if (ch < 0x20U) {
                    fprintf(out, "\\u%04x", (unsigned int)ch);
                }
                else {
                    fputc((int)ch, out);
                }
                break;
            }
        }
    }
    fputc('"', out);
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
    fputs("\"scenario\": ", out);
    sl_json_profile_fprint_string(out, snapshot->scenario);
    fputs(",\n", out);
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
