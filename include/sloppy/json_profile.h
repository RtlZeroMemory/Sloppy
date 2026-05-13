#ifndef SLOPPY_JSON_PROFILE_H
#define SLOPPY_JSON_PROFILE_H

#include "sloppy/status.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SlJsonProfilePhase
{
    SL_JSON_PROFILE_PHASE_BODY_SIZE_CHECK = 0,
    SL_JSON_PROFILE_PHASE_YYJSON_PARSE = 1,
    SL_JSON_PROFILE_PHASE_ROOT_SCHEMA_LOOKUP = 2,
    SL_JSON_PROFILE_PHASE_SHAPE_LOOKUP = 3,
    SL_JSON_PROFILE_PHASE_OBJECT_FIELD_ITERATION = 4,
    SL_JSON_PROFILE_PHASE_FIELD_LOOKUP = 5,
    SL_JSON_PROFILE_PHASE_REQUIRED_FIELD_TRACKING = 6,
    SL_JSON_PROFILE_PHASE_SCALAR_VALIDATION = 7,
    SL_JSON_PROFILE_PHASE_STRING_VALIDATION = 8,
    SL_JSON_PROFILE_PHASE_NUMBER_INT_VALIDATION = 9,
    SL_JSON_PROFILE_PHASE_ARRAY_VALIDATION = 10,
    SL_JSON_PROFILE_PHASE_UNKNOWN_FIELD_POLICY = 11,
    SL_JSON_PROFILE_PHASE_PATH_CONSTRUCTION = 12,
    SL_JSON_PROFILE_PHASE_ISSUE_RECORDING = 13,
    SL_JSON_PROFILE_PHASE_PROBLEM_DETAILS_CONSTRUCTION = 14,
    SL_JSON_PROFILE_PHASE_MATERIALIZE_ONCE_HANDOFF = 15,
    SL_JSON_PROFILE_PHASE_GENERIC_FALLBACK = 16,
    SL_JSON_PROFILE_PHASE_WRITE_PLAN_LOOKUP = 17,
    SL_JSON_PROFILE_PHASE_OUTPUT_SIZE_ESTIMATION = 18,
    SL_JSON_PROFILE_PHASE_RESPONSE_FIELD_ITERATION = 19,
    SL_JSON_PROFILE_PHASE_RESPONSE_FIELD_LOOKUP_VALUE_ACCESS = 20,
    SL_JSON_PROFILE_PHASE_STRING_ESCAPE_SCAN = 21,
    SL_JSON_PROFILE_PHASE_STRING_ESCAPE_WRITE = 22,
    SL_JSON_PROFILE_PHASE_SCALAR_FORMAT = 23,
    SL_JSON_PROFILE_PHASE_LITERAL_FRAGMENT_WRITE = 24,
    SL_JSON_PROFILE_PHASE_BUILDER_BUFFER_GROW_COPY = 25,
    SL_JSON_PROFILE_PHASE_HTTP_RESPONSE_HEADER_WRITE = 26,
    SL_JSON_PROFILE_PHASE_NATIVE_FALLBACK = 27,
    SL_JSON_PROFILE_PHASE_CAPACITY_FAILURE = 28,
    SL_JSON_PROFILE_PHASE_COUNT = 29
} SlJsonProfilePhase;

typedef enum SlJsonProfileCounter
{
    SL_JSON_PROFILE_COUNTER_REQUESTS_TOTAL = 0,
    SL_JSON_PROFILE_COUNTER_JSON_BYTES_PARSED = 1,
    SL_JSON_PROFILE_COUNTER_JSON_VALUES_SEEN = 2,
    SL_JSON_PROFILE_COUNTER_OBJECT_FIELDS_SEEN = 3,
    SL_JSON_PROFILE_COUNTER_SCHEMA_FIELD_LOOKUPS = 4,
    SL_JSON_PROFILE_COUNTER_SCHEMA_FIELD_LOOKUP_LINEAR = 5,
    SL_JSON_PROFILE_COUNTER_SCHEMA_FIELD_LOOKUP_BINARY = 6,
    SL_JSON_PROFILE_COUNTER_SCHEMA_FIELD_LOOKUP_HASH = 7,
    SL_JSON_PROFILE_COUNTER_REQUIRED_BITMAP_CHECKS = 8,
    SL_JSON_PROFILE_COUNTER_UNKNOWN_FIELDS_SEEN = 9,
    SL_JSON_PROFILE_COUNTER_ISSUES_EMITTED = 10,
    SL_JSON_PROFILE_COUNTER_PATHS_RENDERED = 11,
    SL_JSON_PROFILE_COUNTER_PROBLEM_DETAILS_BUILT = 12,
    SL_JSON_PROFILE_COUNTER_MATERIALIZATIONS = 13,
    SL_JSON_PROFILE_COUNTER_DUPLICATE_VALIDATION_SKIPPED = 14,
    SL_JSON_PROFILE_COUNTER_RESPONSE_FIELDS_WRITTEN = 15,
    SL_JSON_PROFILE_COUNTER_STRINGS_ESCAPED = 16,
    SL_JSON_PROFILE_COUNTER_STRINGS_FAST_PATH_NO_ESCAPE = 17,
    SL_JSON_PROFILE_COUNTER_SCALAR_FORMAT_CALLS = 18,
    SL_JSON_PROFILE_COUNTER_BUILDER_GROWS = 19,
    SL_JSON_PROFILE_COUNTER_BUFFER_COPIES = 20,
    SL_JSON_PROFILE_COUNTER_NATIVE_RESPONSE_HITS = 21,
    SL_JSON_PROFILE_COUNTER_GENERIC_FALLBACKS = 22,
    SL_JSON_PROFILE_COUNTER_FALLBACK_REASON_COUNTS = 23,
    SL_JSON_PROFILE_COUNTER_COUNT = 24
} SlJsonProfileCounter;

#define SL_JSON_PROFILE_SCENARIO_CAPACITY 256U

typedef struct SlJsonProfilePhaseStats
{
    uint64_t total_ns;
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t count;
} SlJsonProfilePhaseStats;

typedef struct SlJsonProfileSnapshot
{
    bool enabled;
    /*
     * `scenario` points at this snapshot's `scenario_storage`. NULL reset scenarios become an
     * empty string. Names longer than the storage capacity are truncated and remain valid JSON.
     */
    const char* scenario;
    char scenario_storage[SL_JSON_PROFILE_SCENARIO_CAPACITY];
    uint64_t iterations;
    SlJsonProfilePhaseStats phases[SL_JSON_PROFILE_PHASE_COUNT];
    uint64_t counters[SL_JSON_PROFILE_COUNTER_COUNT];
} SlJsonProfileSnapshot;

bool sl_json_profile_enabled(void);
/*
 * Resets the process-wide profiler for one measured scenario. `scenario` may be NULL; the profiler
 * copies a bounded display name, so caller-owned memory only needs to live for this call.
 */
void sl_json_profile_reset(const char* scenario, uint64_t iterations);
uint64_t sl_json_profile_phase_begin(SlJsonProfilePhase phase);
void sl_json_profile_phase_end(SlJsonProfilePhase phase, uint64_t start_ns);
void sl_json_profile_counter_add(SlJsonProfileCounter counter, uint64_t amount);
void sl_json_profile_snapshot(SlJsonProfileSnapshot* out_snapshot);
bool sl_json_profile_snapshot_has_data(const SlJsonProfileSnapshot* snapshot);
void sl_json_profile_fprint_json(FILE* out, const SlJsonProfileSnapshot* snapshot,
                                 unsigned int indent);

#ifdef __cplusplus
}
#endif

#endif
