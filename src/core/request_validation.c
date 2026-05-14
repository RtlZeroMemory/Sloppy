/*
 * src/core/request_validation.c
 *
 * Consumes compiler-emitted framework Plan metadata at the native request boundary.
 * The validator intentionally stays small: route/query/header primitive binding checks,
 * JSON body schema validation for the supported Plan schema subset, safe problem response
 * materialization, and deterministic diagnostics. It does not execute handlers, providers,
 * queues, services, controllers, or TypeScript reflection.
 */
#include "sloppy/request_validation.h"

#include "sloppy/builder.h"
#include "sloppy/checked_math.h"
#include "sloppy/container.h"
#include "sloppy/json_profile.h"
#include "sloppy/json_writer.h"

#include <yyjson.h>

#define SL_REQUEST_VALIDATION_MAX_ISSUES 8U
#define SL_REQUEST_VALIDATION_MAX_JSON_DEPTH 50U
#define SL_REQUEST_VALIDATION_MAX_PATH_FRAMES 64U
#define SL_REQUEST_VALIDATION_PROBLEM_INITIAL 256U
#define SL_REQUEST_VALIDATION_PROBLEM_MAX 4096U

typedef enum SlRequestValidationPathFrameKind
{
    SL_REQUEST_VALIDATION_PATH_FIELD = 0,
    SL_REQUEST_VALIDATION_PATH_INDEX = 1
} SlRequestValidationPathFrameKind;

typedef struct SlRequestValidationPathFrame
{
    SlStr field;
    size_t index;
    SlRequestValidationPathFrameKind kind;
} SlRequestValidationPathFrame;

typedef struct SlRequestValidationIssue
{
    SlStr path;
    SlStr code;
    SlStr message;
} SlRequestValidationIssue;

typedef struct SlRequestValidationState
{
    SlArena* arena;
    SlRequestValidationIssue issue_storage[SL_REQUEST_VALIDATION_MAX_ISSUES];
    SlFixedVec issues;
    SlPlanJsonUnknownFieldPolicy unknown_fields;
    size_t max_depth;
    size_t max_string_bytes;
    size_t max_array_length;
    SlRequestValidationPathFrame path_frames[SL_REQUEST_VALIDATION_MAX_PATH_FRAMES];
    size_t path_frame_count;
    size_t path_suppressed_count;
    bool profile_enabled;
} SlRequestValidationState;

static uint64_t sl_request_validation_profile_begin(const SlRequestValidationState* state,
                                                    SlJsonProfilePhase phase)
{
    return state != NULL && state->profile_enabled ? sl_json_profile_phase_begin(phase) : 0U;
}

static void sl_request_validation_profile_end(const SlRequestValidationState* state,
                                              SlJsonProfilePhase phase, uint64_t start_ns)
{
    if (state != NULL && state->profile_enabled) {
        sl_json_profile_phase_end(phase, start_ns);
    }
}

static void sl_request_validation_profile_counter_add(const SlRequestValidationState* state,
                                                      SlJsonProfileCounter counter, uint64_t amount)
{
    if (state != NULL && state->profile_enabled) {
        sl_json_profile_counter_add(counter, amount);
    }
}

static SlStr sl_request_validation_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static SlStatus sl_request_validation_diag(SlArena* arena, SlDiag* out_diag, SlDiagCode code,
                                           SlStr message, SlStr hint)
{
    SlDiagBuilder builder;
    SlStatus status;

    if (out_diag == NULL) {
        return sl_status_ok();
    }
    if (arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_diag = (SlDiag){0};
    status = sl_diag_builder_init(&builder, arena, SL_DIAG_SEVERITY_ERROR, code, message);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!sl_str_is_empty(hint)) {
        status = sl_diag_builder_add_hint(&builder, hint);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_diag_builder_finish(&builder, out_diag);
}

static SlStatus sl_request_validation_copy_str(SlArena* arena, SlStr src, SlStr* out)
{
    if (arena == NULL || out == NULL || (src.length != 0U && src.ptr == NULL)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_str_copy_view_to_arena(arena, src, out);
}

static SlStatus sl_request_validation_path_push_field(SlRequestValidationState* state, SlStr field)
{
    SlRequestValidationPathFrame* frame = NULL;

    if (state == NULL || (field.length != 0U && field.ptr == NULL)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (state->path_frame_count >= SL_REQUEST_VALIDATION_MAX_PATH_FRAMES) {
        state->path_suppressed_count += 1U;
        return sl_status_ok();
    }
    frame = &state->path_frames[state->path_frame_count];
    *frame =
        (SlRequestValidationPathFrame){.field = field, .kind = SL_REQUEST_VALIDATION_PATH_FIELD};
    state->path_frame_count += 1U;
    return sl_status_ok();
}

static SlStatus sl_request_validation_path_push_index(SlRequestValidationState* state, size_t index)
{
    SlRequestValidationPathFrame* frame = NULL;

    if (state == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (state->path_frame_count >= SL_REQUEST_VALIDATION_MAX_PATH_FRAMES) {
        state->path_suppressed_count += 1U;
        return sl_status_ok();
    }
    frame = &state->path_frames[state->path_frame_count];
    *frame =
        (SlRequestValidationPathFrame){.index = index, .kind = SL_REQUEST_VALIDATION_PATH_INDEX};
    state->path_frame_count += 1U;
    return sl_status_ok();
}

static void sl_request_validation_path_pop(SlRequestValidationState* state)
{
    if (state == NULL) {
        return;
    }
    if (state->path_suppressed_count != 0U) {
        state->path_suppressed_count -= 1U;
        return;
    }
    if (state->path_frame_count != 0U) {
        state->path_frame_count -= 1U;
    }
}

static SlStatus sl_request_validation_render_fallback_path(SlRequestValidationState* state,
                                                           SlStr* out)
{
    SlStr fallback = sl_str_from_cstr("$");

    if (state == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (state->path_frame_count != 0U &&
        state->path_frames[0U].kind == SL_REQUEST_VALIDATION_PATH_FIELD &&
        sl_str_equal(state->path_frames[0U].field, sl_str_from_cstr("body")))
    {
        fallback = sl_str_from_cstr("body.<truncated>");
    }
    return sl_request_validation_copy_str(state->arena, fallback, out);
}

static SlStatus sl_request_validation_render_current_path(SlRequestValidationState* state,
                                                          SlStr* out)
{
    SlStringBuilder builder = {0};
    uint64_t profile_start = 0U;
    size_t index = 0U;
    SlStatus status;

    if (state == NULL || state->arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    profile_start =
        sl_request_validation_profile_begin(state, SL_JSON_PROFILE_PHASE_PATH_CONSTRUCTION);
    status = sl_string_builder_init_arena(&builder, state->arena, 64U,
                                          SL_REQUEST_VALIDATION_PROBLEM_MAX);
    if (!sl_status_is_ok(status)) {
        sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_PATH_CONSTRUCTION,
                                          profile_start);
        return status;
    }
    if (state->path_frame_count != 0U &&
        state->path_frames[0U].kind == SL_REQUEST_VALIDATION_PATH_FIELD &&
        sl_str_equal(state->path_frames[0U].field, sl_str_from_cstr("body")))
    {
        status = sl_string_builder_append_str(&builder, state->path_frames[0U].field);
        index = 1U;
    }
    else {
        status = sl_string_builder_append_char(&builder, '$');
    }
    if (!sl_status_is_ok(status)) {
        sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_PATH_CONSTRUCTION,
                                          profile_start);
        return status;
    }
    for (; index < state->path_frame_count; index += 1U) {
        const SlRequestValidationPathFrame* frame = &state->path_frames[index];

        if (frame->kind == SL_REQUEST_VALIDATION_PATH_FIELD) {
            status = sl_string_builder_append_char(&builder, '.');
            if (sl_status_is_ok(status)) {
                status = sl_string_builder_append_str(&builder, frame->field);
            }
        }
        else {
            status = sl_string_builder_append_char(&builder, '[');
            if (sl_status_is_ok(status)) {
                status = sl_string_builder_append_size(&builder, frame->index);
            }
            if (sl_status_is_ok(status)) {
                status = sl_string_builder_append_char(&builder, ']');
            }
        }
        if (!sl_status_is_ok(status)) {
            status = sl_request_validation_render_fallback_path(state, out);
            if (sl_status_is_ok(status)) {
                sl_request_validation_profile_counter_add(
                    state, SL_JSON_PROFILE_COUNTER_PATHS_RENDERED, 1U);
            }
            sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_PATH_CONSTRUCTION,
                                              profile_start);
            return status;
        }
    }
    if (state->path_suppressed_count != 0U) {
        status = sl_string_builder_append_cstr(&builder, ".<truncated>");
        if (!sl_status_is_ok(status)) {
            status = sl_request_validation_render_fallback_path(state, out);
            if (sl_status_is_ok(status)) {
                sl_request_validation_profile_counter_add(
                    state, SL_JSON_PROFILE_COUNTER_PATHS_RENDERED, 1U);
            }
            sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_PATH_CONSTRUCTION,
                                              profile_start);
            return status;
        }
    }
    *out = sl_string_builder_view(&builder);
    sl_request_validation_profile_counter_add(state, SL_JSON_PROFILE_COUNTER_PATHS_RENDERED, 1U);
    sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_PATH_CONSTRUCTION,
                                      profile_start);
    return sl_status_ok();
}

static SlStatus sl_request_validation_add_issue_code(SlRequestValidationState* state, SlStr path,
                                                     SlStr code, SlStr message)
{
    SlRequestValidationIssue* issue = NULL;
    uint64_t profile_start = 0U;
    SlStatus status;

    if (state == NULL || state->arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (sl_fixed_vec_count(&state->issues) >= SL_REQUEST_VALIDATION_MAX_ISSUES) {
        return sl_status_ok();
    }

    profile_start =
        sl_request_validation_profile_begin(state, SL_JSON_PROFILE_PHASE_ISSUE_RECORDING);
    status = sl_fixed_vec_push_zero(&state->issues, (void**)&issue);
    if (!sl_status_is_ok(status)) {
        sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_ISSUE_RECORDING,
                                          profile_start);
        return status;
    }
    if (sl_str_is_empty(path)) {
        status = sl_request_validation_render_current_path(state, &path);
        if (!sl_status_is_ok(status)) {
            sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_ISSUE_RECORDING,
                                              profile_start);
            return status;
        }
    }
    status = sl_request_validation_copy_str(state->arena, path, &issue->path);
    if (!sl_status_is_ok(status)) {
        sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_ISSUE_RECORDING,
                                          profile_start);
        return status;
    }
    status = sl_request_validation_copy_str(state->arena, code, &issue->code);
    if (!sl_status_is_ok(status)) {
        sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_ISSUE_RECORDING,
                                          profile_start);
        return status;
    }
    status = sl_request_validation_copy_str(state->arena, message, &issue->message);
    if (sl_status_is_ok(status)) {
        sl_request_validation_profile_counter_add(state, SL_JSON_PROFILE_COUNTER_ISSUES_EMITTED,
                                                  1U);
    }
    sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_ISSUE_RECORDING, profile_start);
    return status;
}

static SlStatus sl_request_validation_add_issue(SlRequestValidationState* state, SlStr path,
                                                SlStr message)
{
    return sl_request_validation_add_issue_code(
        state, path, sl_request_validation_literal("validation", sizeof("validation") - 1U),
        message);
}

static SlStatus sl_request_validation_problem(SlArena* arena, const SlRequestValidationState* state,
                                              SlEngineResult* out_result)
{
    SlStringBuilder builder = {0};
    SlStr body = {0};
    size_t index = 0U;
    uint64_t profile_start = 0U;
    SlStatus status;

    if (arena == NULL || state == NULL || out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_result = (SlEngineResult){0};
    profile_start = sl_request_validation_profile_begin(
        state, SL_JSON_PROFILE_PHASE_PROBLEM_DETAILS_CONSTRUCTION);
    status = sl_string_builder_init_arena(&builder, arena, SL_REQUEST_VALIDATION_PROBLEM_INITIAL,
                                          SL_REQUEST_VALIDATION_PROBLEM_MAX);
    if (!sl_status_is_ok(status)) {
        sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_PROBLEM_DETAILS_CONSTRUCTION,
                                          profile_start);
        return status;
    }

    status = sl_string_builder_append_cstr(
        &builder, "{\"type\":\"https://sloppy.dev/problems/validation\",\"title\":\"Validation "
                  "failed\",\"status\":400,\"code\":\"SLOPPY_E_VALIDATION_FAILED\",\"errors\":[");
    if (!sl_status_is_ok(status)) {
        sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_PROBLEM_DETAILS_CONSTRUCTION,
                                          profile_start);
        return status;
    }

    for (index = 0U; index < sl_fixed_vec_count(&state->issues); index += 1U) {
        const SlRequestValidationIssue* issue =
            (const SlRequestValidationIssue*)sl_fixed_vec_at_const(&state->issues, index);

        if (issue == NULL) {
            sl_request_validation_profile_end(
                state, SL_JSON_PROFILE_PHASE_PROBLEM_DETAILS_CONSTRUCTION, profile_start);
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        if (index != 0U) {
            status = sl_string_builder_append_char(&builder, ',');
            if (!sl_status_is_ok(status)) {
                sl_request_validation_profile_end(
                    state, SL_JSON_PROFILE_PHASE_PROBLEM_DETAILS_CONSTRUCTION, profile_start);
                return status;
            }
        }
        status = sl_string_builder_append_cstr(&builder, "{\"path\":");
        if (!sl_status_is_ok(status)) {
            sl_request_validation_profile_end(
                state, SL_JSON_PROFILE_PHASE_PROBLEM_DETAILS_CONSTRUCTION, profile_start);
            return status;
        }
        status = sl_json_writer_append_escaped_string(&builder, issue->path);
        if (!sl_status_is_ok(status)) {
            sl_request_validation_profile_end(
                state, SL_JSON_PROFILE_PHASE_PROBLEM_DETAILS_CONSTRUCTION, profile_start);
            return status;
        }
        status = sl_string_builder_append_cstr(&builder, ",\"code\":");
        if (!sl_status_is_ok(status)) {
            sl_request_validation_profile_end(
                state, SL_JSON_PROFILE_PHASE_PROBLEM_DETAILS_CONSTRUCTION, profile_start);
            return status;
        }
        status = sl_json_writer_append_escaped_string(&builder, issue->code);
        if (!sl_status_is_ok(status)) {
            sl_request_validation_profile_end(
                state, SL_JSON_PROFILE_PHASE_PROBLEM_DETAILS_CONSTRUCTION, profile_start);
            return status;
        }
        status = sl_string_builder_append_cstr(&builder, ",\"message\":");
        if (!sl_status_is_ok(status)) {
            sl_request_validation_profile_end(
                state, SL_JSON_PROFILE_PHASE_PROBLEM_DETAILS_CONSTRUCTION, profile_start);
            return status;
        }
        status = sl_json_writer_append_escaped_string(&builder, issue->message);
        if (!sl_status_is_ok(status)) {
            sl_request_validation_profile_end(
                state, SL_JSON_PROFILE_PHASE_PROBLEM_DETAILS_CONSTRUCTION, profile_start);
            return status;
        }
        status = sl_string_builder_append_char(&builder, '}');
        if (!sl_status_is_ok(status)) {
            sl_request_validation_profile_end(
                state, SL_JSON_PROFILE_PHASE_PROBLEM_DETAILS_CONSTRUCTION, profile_start);
            return status;
        }
    }

    status = sl_string_builder_append_cstr(&builder, "]}\n");
    if (!sl_status_is_ok(status)) {
        sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_PROBLEM_DETAILS_CONSTRUCTION,
                                          profile_start);
        return status;
    }

    body = sl_string_builder_view(&builder);
    out_result->kind = SL_ENGINE_RESULT_ERROR;
    out_result->payload_kind = SL_ENGINE_RESULT_PAYLOAD_RESPONSE;
    out_result->response = sl_http_response_problem(
        400U, sl_bytes_from_parts((const unsigned char*)body.ptr, body.length));
    sl_request_validation_profile_counter_add(state, SL_JSON_PROFILE_COUNTER_PROBLEM_DETAILS_BUILT,
                                              1U);
    sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_PROBLEM_DETAILS_CONSTRUCTION,
                                      profile_start);
    return sl_status_ok();
}

static const SlPlanSchema* sl_request_validation_find_schema(const SlRequestValidationState* state,
                                                             const SlPlan* plan, SlStr name)
{
    size_t index = 0U;
    uint64_t profile_start = 0U;

    if (plan == NULL || sl_str_is_empty(name) ||
        (plan->schema_count != 0U && plan->schemas == NULL))
    {
        return NULL;
    }

    profile_start =
        sl_request_validation_profile_begin(state, SL_JSON_PROFILE_PHASE_ROOT_SCHEMA_LOOKUP);
    for (index = 0U; index < plan->schema_count; index += 1U) {
        if (sl_str_equal(plan->schemas[index].name, name)) {
            sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_ROOT_SCHEMA_LOOKUP,
                                              profile_start);
            return &plan->schemas[index];
        }
    }
    sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_ROOT_SCHEMA_LOOKUP,
                                      profile_start);
    return NULL;
}

static bool sl_request_validation_email_like(SlStr value)
{
    size_t index = 0U;
    size_t at = SIZE_MAX;

    for (index = 0U; index < value.length; index += 1U) {
        if (value.ptr[index] == '@') {
            at = index;
            break;
        }
    }
    if (at == SIZE_MAX || at == 0U || at + 1U >= value.length) {
        return false;
    }
    for (index = at + 1U; index < value.length; index += 1U) {
        if (value.ptr[index] == '.' && index > at + 1U && index + 1U < value.length) {
            return true;
        }
    }
    return false;
}

static bool sl_request_validation_uuid_like(SlStr value)
{
    static const size_t hyphens[] = {8U, 13U, 18U, 23U};
    size_t index = 0U;
    size_t hyphen_index = 0U;

    if (value.length != 36U) {
        return false;
    }
    for (index = 0U; index < value.length; index += 1U) {
        char ch = value.ptr[index];
        bool is_hyphen =
            hyphen_index < sizeof(hyphens) / sizeof(hyphens[0]) && index == hyphens[hyphen_index];
        if (is_hyphen) {
            if (ch != '-') {
                return false;
            }
            hyphen_index += 1U;
            continue;
        }
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))) {
            return false;
        }
    }
    return true;
}

static bool sl_request_validation_parse_number(SlStr value, double* out)
{
    double parsed = 0.0;
    double fraction = 0.1;
    size_t index = 0U;
    bool negative = false;
    bool saw_digit = false;
    bool after_dot = false;

    if (out == NULL || value.length == 0U || value.ptr == NULL) {
        return false;
    }
    if (value.ptr[index] == '-') {
        negative = true;
        index += 1U;
    }
    for (; index < value.length; index += 1U) {
        char ch = value.ptr[index];
        if (ch == '.' && !after_dot) {
            after_dot = true;
            continue;
        }
        if (ch < '0' || ch > '9') {
            return false;
        }
        saw_digit = true;
        if (after_dot) {
            parsed += ((double)(ch - '0')) * fraction;
            fraction /= 10.0;
        }
        else {
            parsed = (parsed * 10.0) + (double)(ch - '0');
        }
    }
    if (!saw_digit) {
        return false;
    }
    *out = negative ? -parsed : parsed;
    return true;
}

static bool sl_request_validation_parse_int_text(SlStr value)
{
    size_t index = 0U;
    bool saw_digit = false;

    if (value.length == 0U || value.ptr == NULL) {
        return false;
    }
    if (value.ptr[index] == '-') {
        index += 1U;
    }
    for (; index < value.length; index += 1U) {
        if (value.ptr[index] < '0' || value.ptr[index] > '9') {
            return false;
        }
        saw_digit = true;
    }
    return saw_digit;
}

static bool sl_request_validation_parse_bool(SlStr value, bool* out)
{
    if (out == NULL) {
        return false;
    }
    if (sl_str_equal_ci_ascii(value, sl_str_from_cstr("true"))) {
        *out = true;
        return true;
    }
    if (sl_str_equal_ci_ascii(value, sl_str_from_cstr("false"))) {
        *out = false;
        return true;
    }
    return false;
}

static SlStatus sl_request_validation_validate_json_value(SlRequestValidationState* state,
                                                          const SlPlanSchemaNode* schema,
                                                          yyjson_val* value, SlStr path,
                                                          size_t depth);

static SlStatus sl_request_validation_validate_string(SlRequestValidationState* state,
                                                      const SlPlanSchemaNode* schema, SlStr value,
                                                      SlStr path)
{
    size_t max_string_bytes = 0U;
    uint64_t profile_start =
        sl_request_validation_profile_begin(state, SL_JSON_PROFILE_PHASE_STRING_VALIDATION);
    SlStatus status;

    if (schema->has_min && schema->min_value >= 0 && value.length < (size_t)schema->min_value) {
        status = sl_request_validation_add_issue_code(
            state, path, sl_request_validation_literal("string.min", sizeof("string.min") - 1U),
            sl_request_validation_literal("Expected a longer string.",
                                          sizeof("Expected a longer string.") - 1U));
        sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_STRING_VALIDATION,
                                          profile_start);
        return status;
    }
    if (schema->has_max && schema->max_value >= 0) {
        max_string_bytes = (size_t)schema->max_value;
    }
    else {
        max_string_bytes = state->max_string_bytes;
    }
    if (max_string_bytes != 0U && value.length > max_string_bytes) {
        status = sl_request_validation_add_issue_code(
            state, path, sl_request_validation_literal("string.max", sizeof("string.max") - 1U),
            sl_request_validation_literal("Expected a shorter string.",
                                          sizeof("Expected a shorter string.") - 1U));
        sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_STRING_VALIDATION,
                                          profile_start);
        return status;
    }
    if (sl_str_equal(schema->validation, sl_str_from_cstr("email")) &&
        !sl_request_validation_email_like(value))
    {
        status = sl_request_validation_add_issue_code(
            state, path, sl_request_validation_literal("string.email", sizeof("string.email") - 1U),
            sl_request_validation_literal("Expected an email address.",
                                          sizeof("Expected an email address.") - 1U));
        sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_STRING_VALIDATION,
                                          profile_start);
        return status;
    }
    if (sl_str_equal(schema->validation, sl_str_from_cstr("uuid")) &&
        !sl_request_validation_uuid_like(value))
    {
        status = sl_request_validation_add_issue_code(
            state, path, sl_request_validation_literal("string.uuid", sizeof("string.uuid") - 1U),
            sl_request_validation_literal("Expected a UUID string.",
                                          sizeof("Expected a UUID string.") - 1U));
        sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_STRING_VALIDATION,
                                          profile_start);
        return status;
    }
    sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_STRING_VALIDATION,
                                      profile_start);
    return sl_status_ok();
}

static const SlPlanSchemaProperty*
sl_request_validation_find_property(const SlRequestValidationState* state,
                                    const SlPlanSchemaNode* schema, SlStr name, size_t* out_index)
{
    size_t index = 0U;

    if (out_index != NULL) {
        *out_index = SIZE_MAX;
    }
    if (schema == NULL || (schema->property_count != 0U && schema->properties == NULL)) {
        return NULL;
    }
    for (index = 0U; index < schema->property_count; index += 1U) {
        sl_request_validation_profile_counter_add(state,
                                                  SL_JSON_PROFILE_COUNTER_SCHEMA_FIELD_LOOKUPS, 1U);
        sl_request_validation_profile_counter_add(
            state, SL_JSON_PROFILE_COUNTER_SCHEMA_FIELD_LOOKUP_LINEAR, 1U);
        if (sl_str_equal(schema->properties[index].name, name)) {
            if (out_index != NULL) {
                *out_index = index;
            }
            return &schema->properties[index];
        }
    }
    return NULL;
}

static SlStatus sl_request_validation_validate_object(SlRequestValidationState* state,
                                                      const SlPlanSchemaNode* schema,
                                                      yyjson_val* value, SlStr path, size_t depth)
{
    yyjson_obj_iter iter;
    yyjson_val* key = NULL;
    size_t index = 0U;
    uint64_t seen_mask = 0U;
    bool use_seen_mask = false;
    SlStatus status;

    if (!yyjson_is_obj(value)) {
        return sl_request_validation_add_issue_code(
            state, path, sl_request_validation_literal("type", sizeof("type") - 1U),
            sl_request_validation_literal("Expected an object.",
                                          sizeof("Expected an object.") - 1U));
    }

    sl_request_validation_profile_counter_add(state, SL_JSON_PROFILE_COUNTER_OBJECT_FIELDS_SEEN,
                                              (uint64_t)yyjson_obj_size(value));
    use_seen_mask = schema->property_count <= 64U;

    yyjson_obj_iter_init(value, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL) {
        yyjson_val* child = yyjson_obj_iter_get_val(key);
        SlStr name = sl_str_from_parts(yyjson_get_str(key), yyjson_get_len(key));
        const SlPlanSchemaProperty* property = NULL;
        size_t property_index = SIZE_MAX;
        uint64_t iteration_start = sl_request_validation_profile_begin(
            state, SL_JSON_PROFILE_PHASE_OBJECT_FIELD_ITERATION);
        uint64_t lookup_start =
            sl_request_validation_profile_begin(state, SL_JSON_PROFILE_PHASE_FIELD_LOOKUP);

        property = sl_request_validation_find_property(state, schema, name, &property_index);
        sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_FIELD_LOOKUP, lookup_start);

        if (property == NULL) {
            if (state->unknown_fields == SL_PLAN_JSON_UNKNOWN_FIELDS_REJECT) {
                uint64_t unknown_start = sl_request_validation_profile_begin(
                    state, SL_JSON_PROFILE_PHASE_UNKNOWN_FIELD_POLICY);

                sl_request_validation_profile_counter_add(
                    state, SL_JSON_PROFILE_COUNTER_UNKNOWN_FIELDS_SEEN, 1U);
                status = sl_request_validation_path_push_field(state, name);
                if (sl_status_is_ok(status)) {
                    status = sl_request_validation_add_issue_code(
                        state, sl_str_empty(),
                        sl_request_validation_literal("unknown", sizeof("unknown") - 1U),
                        sl_request_validation_literal("Unknown field is not allowed.",
                                                      sizeof("Unknown field is not allowed.") -
                                                          1U));
                    sl_request_validation_path_pop(state);
                }
                sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_UNKNOWN_FIELD_POLICY,
                                                  unknown_start);
                if (!sl_status_is_ok(status)) {
                    sl_request_validation_profile_end(
                        state, SL_JSON_PROFILE_PHASE_OBJECT_FIELD_ITERATION, iteration_start);
                    return status;
                }
            }
            sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_OBJECT_FIELD_ITERATION,
                                              iteration_start);
            continue;
        }
        if (use_seen_mask && property_index < 64U) {
            seen_mask |= UINT64_C(1) << property_index;
        }
        if (property->schema == NULL) {
            sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_OBJECT_FIELD_ITERATION,
                                              iteration_start);
            continue;
        }
        status = sl_request_validation_path_push_field(state, property->name);
        if (!sl_status_is_ok(status)) {
            sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_OBJECT_FIELD_ITERATION,
                                              iteration_start);
            return status;
        }
        if (yyjson_is_null(child)) {
            if (!property->schema->nullable) {
                status = sl_request_validation_add_issue_code(
                    state, sl_str_empty(),
                    sl_request_validation_literal("required", sizeof("required") - 1U),
                    sl_request_validation_literal("Field is required.",
                                                  sizeof("Field is required.") - 1U));
                if (!sl_status_is_ok(status)) {
                    sl_request_validation_path_pop(state);
                    sl_request_validation_profile_end(
                        state, SL_JSON_PROFILE_PHASE_OBJECT_FIELD_ITERATION, iteration_start);
                    return status;
                }
            }
            sl_request_validation_path_pop(state);
            sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_OBJECT_FIELD_ITERATION,
                                              iteration_start);
            continue;
        }
        status = sl_request_validation_validate_json_value(state, property->schema, child,
                                                           sl_str_empty(), depth + 1U);
        sl_request_validation_path_pop(state);
        if (!sl_status_is_ok(status)) {
            sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_OBJECT_FIELD_ITERATION,
                                              iteration_start);
            return status;
        }
        sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_OBJECT_FIELD_ITERATION,
                                          iteration_start);
    }

    for (index = 0U; index < schema->property_count; index += 1U) {
        const SlPlanSchemaProperty* property = &schema->properties[index];
        bool seen = false;
        uint64_t required_start = sl_request_validation_profile_begin(
            state, SL_JSON_PROFILE_PHASE_REQUIRED_FIELD_TRACKING);

        if (property->schema == NULL || property->schema->optional) {
            sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_REQUIRED_FIELD_TRACKING,
                                              required_start);
            continue;
        }
        if (use_seen_mask && index < 64U) {
            sl_request_validation_profile_counter_add(
                state, SL_JSON_PROFILE_COUNTER_REQUIRED_BITMAP_CHECKS, 1U);
            seen = (seen_mask & (UINT64_C(1) << index)) != 0U;
        }
        else {
            yyjson_val* child = yyjson_obj_getn(value, property->name.ptr, property->name.length);
            seen = child != NULL;
        }
        if (!seen) {
            status = sl_request_validation_path_push_field(state, property->name);
            if (sl_status_is_ok(status)) {
                status = sl_request_validation_add_issue_code(
                    state, sl_str_empty(),
                    sl_request_validation_literal("required", sizeof("required") - 1U),
                    sl_request_validation_literal("Field is required.",
                                                  sizeof("Field is required.") - 1U));
                sl_request_validation_path_pop(state);
            }
            sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_REQUIRED_FIELD_TRACKING,
                                              required_start);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            continue;
        }
        sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_REQUIRED_FIELD_TRACKING,
                                          required_start);
    }

    return sl_status_ok();
}

static SlStatus sl_request_validation_validate_array(SlRequestValidationState* state,
                                                     const SlPlanSchemaNode* schema,
                                                     yyjson_val* value, SlStr path, size_t depth)
{
    yyjson_val* item = NULL;
    yyjson_arr_iter iter;
    size_t item_index = 0U;
    size_t max_array_length = 0U;
    SlStatus status;

    if (!yyjson_is_arr(value)) {
        return sl_request_validation_add_issue_code(
            state, path, sl_request_validation_literal("type", sizeof("type") - 1U),
            sl_request_validation_literal("Expected an array.", sizeof("Expected an array.") - 1U));
    }
    if (schema->items == NULL) {
        return sl_request_validation_add_issue(
            state, path,
            sl_request_validation_literal("Schema is not supported by this runtime.",
                                          sizeof("Schema is not supported by this runtime.") - 1U));
    }
    if (schema->has_max && schema->max_value >= 0) {
        max_array_length = (size_t)schema->max_value;
    }
    else {
        max_array_length = state->max_array_length;
    }
    if (max_array_length != 0U && yyjson_arr_size(value) > max_array_length) {
        return sl_request_validation_add_issue_code(
            state, path, sl_request_validation_literal("array.max", sizeof("array.max") - 1U),
            sl_request_validation_literal("Expected a shorter array.",
                                          sizeof("Expected a shorter array.") - 1U));
    }

    yyjson_arr_iter_init(value, &iter);
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        uint64_t array_start =
            sl_request_validation_profile_begin(state, SL_JSON_PROFILE_PHASE_ARRAY_VALIDATION);

        status = sl_request_validation_path_push_index(state, item_index);
        if (!sl_status_is_ok(status)) {
            sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_ARRAY_VALIDATION,
                                              array_start);
            return status;
        }
        status = sl_request_validation_validate_json_value(state, schema->items, item,
                                                           sl_str_empty(), depth + 1U);
        sl_request_validation_path_pop(state);
        if (!sl_status_is_ok(status)) {
            sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_ARRAY_VALIDATION,
                                              array_start);
            return status;
        }
        item_index += 1U;
        sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_ARRAY_VALIDATION,
                                          array_start);
    }
    return sl_status_ok();
}

static bool sl_request_validation_literal_matches(const SlPlanSchemaNode* literal,
                                                  yyjson_val* value)
{
    if (literal->literal_kind == SL_PLAN_SCHEMA_LITERAL_STRING && yyjson_is_str(value)) {
        return sl_str_equal(sl_str_from_parts(yyjson_get_str(value), yyjson_get_len(value)),
                            literal->literal_string);
    }
    if (literal->literal_kind == SL_PLAN_SCHEMA_LITERAL_NUMBER && yyjson_is_num(value)) {
        return yyjson_get_num(value) == literal->literal_number;
    }
    if (literal->literal_kind == SL_PLAN_SCHEMA_LITERAL_BOOLEAN && yyjson_is_bool(value)) {
        return yyjson_get_bool(value) == literal->literal_boolean;
    }
    return false;
}

static SlStatus sl_request_validation_validate_literal_union(SlRequestValidationState* state,
                                                             const SlPlanSchemaNode* schema,
                                                             yyjson_val* value, SlStr path)
{
    size_t index = 0U;

    for (index = 0U; index < schema->variant_count; index += 1U) {
        if (schema->variants[index].kind == SL_PLAN_SCHEMA_LITERAL &&
            sl_request_validation_literal_matches(&schema->variants[index], value))
        {
            return sl_status_ok();
        }
    }

    return sl_request_validation_add_issue_code(
        state, path, sl_request_validation_literal("enum", sizeof("enum") - 1U),
        sl_request_validation_literal("Expected one of the declared literal values.",
                                      sizeof("Expected one of the declared literal values.") - 1U));
}

static SlStatus sl_request_validation_validate_json_value(SlRequestValidationState* state,
                                                          const SlPlanSchemaNode* schema,
                                                          yyjson_val* value, SlStr path,
                                                          size_t depth)
{
    SlStatus status;
    uint64_t scalar_start = 0U;

    if (state == NULL || schema == NULL || value == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    sl_request_validation_profile_counter_add(state, SL_JSON_PROFILE_COUNTER_JSON_VALUES_SEEN, 1U);
    if (state->max_depth != 0U && depth > state->max_depth) {
        return sl_request_validation_add_issue(
            state, path,
            sl_request_validation_literal("JSON body exceeds maximum validation depth.",
                                          sizeof("JSON body exceeds maximum validation depth.") -
                                              1U));
    }

    if (yyjson_is_null(value)) {
        if (schema->nullable || schema->kind == SL_PLAN_SCHEMA_NULL) {
            return sl_status_ok();
        }
        return sl_request_validation_add_issue(
            state, path,
            sl_request_validation_literal("Expected a non-null value.",
                                          sizeof("Expected a non-null value.") - 1U));
    }

    switch (schema->kind) {
    case SL_PLAN_SCHEMA_OBJECT:
        return sl_request_validation_validate_object(state, schema, value, path, depth);
    case SL_PLAN_SCHEMA_STRING:
        scalar_start =
            sl_request_validation_profile_begin(state, SL_JSON_PROFILE_PHASE_SCALAR_VALIDATION);
        if (!yyjson_is_str(value)) {
            sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_SCALAR_VALIDATION,
                                              scalar_start);
            return sl_request_validation_add_issue_code(
                state, path, sl_request_validation_literal("type", sizeof("type") - 1U),
                sl_request_validation_literal("Expected a string.",
                                              sizeof("Expected a string.") - 1U));
        }
        sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_SCALAR_VALIDATION,
                                          scalar_start);
        return sl_request_validation_validate_string(
            state, schema, sl_str_from_parts(yyjson_get_str(value), yyjson_get_len(value)), path);
    case SL_PLAN_SCHEMA_NUMBER:
        scalar_start =
            sl_request_validation_profile_begin(state, SL_JSON_PROFILE_PHASE_NUMBER_INT_VALIDATION);
        if (!yyjson_is_num(value)) {
            sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_NUMBER_INT_VALIDATION,
                                              scalar_start);
            return sl_request_validation_add_issue_code(
                state, path, sl_request_validation_literal("type", sizeof("type") - 1U),
                sl_request_validation_literal("Expected a finite number.",
                                              sizeof("Expected a finite number.") - 1U));
        }
        {
            double number = yyjson_get_num(value);
            if (schema->has_min && number < (double)schema->min_value) {
                sl_request_validation_profile_end(
                    state, SL_JSON_PROFILE_PHASE_NUMBER_INT_VALIDATION, scalar_start);
                return sl_request_validation_add_issue_code(
                    state, path,
                    sl_request_validation_literal("number.min", sizeof("number.min") - 1U),
                    sl_request_validation_literal("Expected a larger number.",
                                                  sizeof("Expected a larger number.") - 1U));
            }
            if (schema->has_max && number > (double)schema->max_value) {
                sl_request_validation_profile_end(
                    state, SL_JSON_PROFILE_PHASE_NUMBER_INT_VALIDATION, scalar_start);
                return sl_request_validation_add_issue_code(
                    state, path,
                    sl_request_validation_literal("number.max", sizeof("number.max") - 1U),
                    sl_request_validation_literal("Expected a smaller number.",
                                                  sizeof("Expected a smaller number.") - 1U));
            }
        }
        sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_NUMBER_INT_VALIDATION,
                                          scalar_start);
        return sl_status_ok();
    case SL_PLAN_SCHEMA_INT:
        scalar_start =
            sl_request_validation_profile_begin(state, SL_JSON_PROFILE_PHASE_NUMBER_INT_VALIDATION);
        if (!yyjson_is_int(value)) {
            sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_NUMBER_INT_VALIDATION,
                                              scalar_start);
            return sl_request_validation_add_issue_code(
                state, path, sl_request_validation_literal("type", sizeof("type") - 1U),
                sl_request_validation_literal("Expected an integer.",
                                              sizeof("Expected an integer.") - 1U));
        }
        {
            int64_t integer = yyjson_get_sint(value);
            if (schema->has_min && integer < schema->min_value) {
                sl_request_validation_profile_end(
                    state, SL_JSON_PROFILE_PHASE_NUMBER_INT_VALIDATION, scalar_start);
                return sl_request_validation_add_issue_code(
                    state, path,
                    sl_request_validation_literal("number.min", sizeof("number.min") - 1U),
                    sl_request_validation_literal("Expected a larger integer.",
                                                  sizeof("Expected a larger integer.") - 1U));
            }
            if (schema->has_max && integer > schema->max_value) {
                sl_request_validation_profile_end(
                    state, SL_JSON_PROFILE_PHASE_NUMBER_INT_VALIDATION, scalar_start);
                return sl_request_validation_add_issue_code(
                    state, path,
                    sl_request_validation_literal("number.max", sizeof("number.max") - 1U),
                    sl_request_validation_literal("Expected a smaller integer.",
                                                  sizeof("Expected a smaller integer.") - 1U));
            }
        }
        sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_NUMBER_INT_VALIDATION,
                                          scalar_start);
        return sl_status_ok();
    case SL_PLAN_SCHEMA_BOOLEAN:
        scalar_start =
            sl_request_validation_profile_begin(state, SL_JSON_PROFILE_PHASE_SCALAR_VALIDATION);
        if (!yyjson_is_bool(value)) {
            sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_SCALAR_VALIDATION,
                                              scalar_start);
            return sl_request_validation_add_issue_code(
                state, path, sl_request_validation_literal("type", sizeof("type") - 1U),
                sl_request_validation_literal("Expected a boolean.",
                                              sizeof("Expected a boolean.") - 1U));
        }
        sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_SCALAR_VALIDATION,
                                          scalar_start);
        return sl_status_ok();
    case SL_PLAN_SCHEMA_ARRAY:
        return sl_request_validation_validate_array(state, schema, value, path, depth);
    case SL_PLAN_SCHEMA_LITERAL_UNION:
        return sl_request_validation_validate_literal_union(state, schema, value, path);
    case SL_PLAN_SCHEMA_LITERAL:
        if (!sl_request_validation_literal_matches(schema, value)) {
            return sl_request_validation_add_issue_code(
                state, path, sl_request_validation_literal("literal", sizeof("literal") - 1U),
                sl_request_validation_literal("Expected the declared literal value.",
                                              sizeof("Expected the declared literal value.") - 1U));
        }
        return sl_status_ok();
    case SL_PLAN_SCHEMA_NULL:
        return sl_request_validation_add_issue(
            state, path,
            sl_request_validation_literal("Expected null.", sizeof("Expected null.") - 1U));
    case SL_PLAN_SCHEMA_UNKNOWN:
    default:
        status = sl_request_validation_add_issue(
            state, path,
            sl_request_validation_literal("Schema is not supported by this runtime.",
                                          sizeof("Schema is not supported by this runtime.") - 1U));
        return status;
    }
}

static SlStatus sl_request_validation_validate_scalar(SlRequestValidationState* state, SlStr type,
                                                      SlStr value, SlStr path)
{
    double parsed_number = 0.0;
    bool parsed_bool = false;

    if (sl_str_equal(type, sl_str_from_cstr("Route<number>")) ||
        sl_str_equal(type, sl_str_from_cstr("Query<number>")))
    {
        type = sl_str_from_cstr("number");
    }
    if (sl_str_equal(type, sl_str_from_cstr("Route<boolean>")) ||
        sl_str_equal(type, sl_str_from_cstr("Query<boolean>")) ||
        sl_str_equal(type, sl_str_from_cstr("bool")))
    {
        type = sl_str_from_cstr("boolean");
    }

    if (sl_str_equal(type, sl_str_from_cstr("number")) ||
        sl_str_equal(type, sl_str_from_cstr("int")))
    {
        if (!sl_request_validation_parse_number(value, &parsed_number) ||
            (sl_str_equal(type, sl_str_from_cstr("int")) &&
             !sl_request_validation_parse_int_text(value)))
        {
            return sl_request_validation_add_issue(
                state, path,
                sl_request_validation_literal("Expected a numeric value.",
                                              sizeof("Expected a numeric value.") - 1U));
        }
        return sl_status_ok();
    }
    if (sl_str_equal(type, sl_str_from_cstr("boolean"))) {
        if (!sl_request_validation_parse_bool(value, &parsed_bool)) {
            return sl_request_validation_add_issue(
                state, path,
                sl_request_validation_literal("Expected a boolean value.",
                                              sizeof("Expected a boolean value.") - 1U));
        }
        return sl_status_ok();
    }
    return sl_status_ok();
}

static bool sl_request_validation_find_route_param(const SlHttpRequestContext* context, SlStr name,
                                                   SlStr* out)
{
    size_t index = 0U;

    if (out != NULL) {
        *out = sl_str_empty();
    }
    if (context == NULL || out == NULL ||
        (context->route_param_count != 0U && context->route_params == NULL))
    {
        return false;
    }

    for (index = 0U; index < context->route_param_count; index += 1U) {
        if (sl_str_equal(context->route_params[index].name, name)) {
            *out = context->route_params[index].value;
            return true;
        }
    }
    return false;
}

static bool sl_request_validation_find_query_param(const SlHttpRequestContext* context, SlStr name,
                                                   SlStr* out)
{
    size_t index = 0U;

    if (out != NULL) {
        *out = sl_str_empty();
    }
    if (context == NULL || out == NULL ||
        (context->query_param_count != 0U && context->query_params == NULL))
    {
        return false;
    }

    for (index = 0U; index < context->query_param_count; index += 1U) {
        if (sl_str_equal(context->query_params[index].name, name)) {
            *out = context->query_params[index].value;
            return true;
        }
    }
    return false;
}

static bool sl_request_validation_find_header(const SlHttpRequestContext* context, SlStr name,
                                              SlStr* out)
{
    size_t index = 0U;

    if (out != NULL) {
        *out = sl_str_empty();
    }
    if (context == NULL || context->request == NULL || out == NULL ||
        (context->request->header_count != 0U && context->request->headers == NULL))
    {
        return false;
    }

    for (index = 0U; index < context->request->header_count; index += 1U) {
        if (sl_str_equal_ci_ascii(context->request->headers[index].name, name)) {
            *out = context->request->headers[index].value;
            return true;
        }
    }
    return false;
}

static bool sl_request_validation_binding_is_string_like(const SlPlanRequestBinding* binding)
{
    SlStr type = sl_str_empty();

    if (binding == NULL) {
        return false;
    }
    type = !sl_str_is_empty(binding->schema) ? binding->schema : binding->type;
    return sl_str_is_empty(type) || sl_str_equal(type, sl_str_from_cstr("string"));
}

static bool sl_request_validation_bindings_are_trivially_satisfied(
    const SlPlanRoute* route, const SlHttpRequestContext* context)
{
    size_t index = 0U;

    if (route == NULL || context == NULL) {
        return false;
    }
    if (route->binding_count == 0U) {
        return true;
    }
    if (route->bindings == NULL) {
        return false;
    }

    for (index = 0U; index < route->binding_count; index += 1U) {
        const SlPlanRequestBinding* binding = &route->bindings[index];
        SlStr value = sl_str_empty();
        SlStr path = !sl_str_is_empty(binding->name) ? binding->name : binding->parameter;

        switch (binding->kind) {
        case SL_PLAN_REQUEST_BINDING_ROUTE:
            if (!sl_request_validation_binding_is_string_like(binding) ||
                !sl_request_validation_find_route_param(context, path, &value))
            {
                return false;
            }
            break;
        case SL_PLAN_REQUEST_BINDING_QUERY:
            if (!sl_request_validation_binding_is_string_like(binding) ||
                !sl_request_validation_find_query_param(context, path, &value))
            {
                return false;
            }
            break;
        case SL_PLAN_REQUEST_BINDING_HEADER:
            if (!sl_request_validation_binding_is_string_like(binding) ||
                !sl_request_validation_find_header(context, path, &value))
            {
                return false;
            }
            break;
        case SL_PLAN_REQUEST_BINDING_CONTEXT:
        case SL_PLAN_REQUEST_BINDING_INJECTION:
        case SL_PLAN_REQUEST_BINDING_UNKNOWN:
            break;
        default:
            return false;
        }
    }
    return true;
}

static SlStatus sl_request_validation_validate_named_scalar(SlRequestValidationState* state,
                                                            const SlPlanRequestBinding* binding,
                                                            SlStr value)
{
    SlStr path = !sl_str_is_empty(binding->name) ? binding->name : binding->parameter;
    SlStr type = !sl_str_is_empty(binding->schema) ? binding->schema : binding->type;
    return sl_request_validation_validate_scalar(state, type, value, path);
}

static SlStatus sl_request_validation_validate_body(SlRequestValidationState* state,
                                                    const SlPlan* plan,
                                                    const SlPlanRequestBinding* binding,
                                                    const SlHttpRequestContext* context)
{
    yyjson_read_err error = {0};
    yyjson_doc* doc = NULL;
    yyjson_val* root = NULL;
    const SlPlanSchema* schema = NULL;
    uint64_t body_check_start = 0U;
    uint64_t parse_start = 0U;
    uint64_t shape_start = 0U;
    SlStatus status;

    sl_request_validation_profile_counter_add(state, SL_JSON_PROFILE_COUNTER_REQUESTS_TOTAL, 1U);
    if (sl_str_is_empty(binding->schema)) {
        return sl_request_validation_add_issue_code(
            state, sl_str_from_cstr("body"),
            sl_request_validation_literal("schema.missing", sizeof("schema.missing") - 1U),
            sl_request_validation_literal("JSON body schema metadata is missing.",
                                          sizeof("JSON body schema metadata is missing.") - 1U));
    }

    if (context->request_schema != NULL && (plan->schema_count == 0U || plan->schemas != NULL)) {
        for (size_t schema_index = 0U; schema_index < plan->schema_count; schema_index += 1U) {
            if (&plan->schemas[schema_index] == context->request_schema &&
                sl_str_equal(plan->schemas[schema_index].name, binding->schema))
            {
                schema = &plan->schemas[schema_index];
                break;
            }
        }
    }
    if (schema == NULL) {
        schema = sl_request_validation_find_schema(state, plan, binding->schema);
    }
    if (schema == NULL) {
        return sl_request_validation_add_issue_code(
            state, sl_str_from_cstr("body"),
            sl_request_validation_literal("schema.unavailable", sizeof("schema.unavailable") - 1U),
            sl_request_validation_literal("JSON body schema is not available at runtime.",
                                          sizeof("JSON body schema is not available at runtime.") -
                                              1U));
    }

    body_check_start =
        sl_request_validation_profile_begin(state, SL_JSON_PROFILE_PHASE_BODY_SIZE_CHECK);
    if (context->request == NULL || context->request->body.ptr == NULL ||
        context->request->body.length == 0U || context->body_kind != SL_HTTP_REQUEST_BODY_JSON)
    {
        sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_BODY_SIZE_CHECK,
                                          body_check_start);
        return sl_request_validation_add_issue_code(
            state, sl_str_from_cstr("body"),
            sl_request_validation_literal("body.required", sizeof("body.required") - 1U),
            sl_request_validation_literal("Expected a JSON request body.",
                                          sizeof("Expected a JSON request body.") - 1U));
    }
    sl_request_validation_profile_counter_add(state, SL_JSON_PROFILE_COUNTER_JSON_BYTES_PARSED,
                                              (uint64_t)context->request->body.length);
    sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_BODY_SIZE_CHECK,
                                      body_check_start);

    parse_start = sl_request_validation_profile_begin(state, SL_JSON_PROFILE_PHASE_YYJSON_PARSE);
    doc = yyjson_read_opts((char*)context->request->body.ptr, context->request->body.length, 0U,
                           NULL, &error);
    sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_YYJSON_PARSE, parse_start);
    if (doc == NULL) {
        return sl_request_validation_add_issue_code(
            state, sl_str_from_cstr("body"),
            sl_request_validation_literal("json.malformed", sizeof("json.malformed") - 1U),
            sl_request_validation_literal("Expected a valid JSON request body.",
                                          sizeof("Expected a valid JSON request body.") - 1U));
    }

    root = yyjson_doc_get_root(doc);
    shape_start = sl_request_validation_profile_begin(state, SL_JSON_PROFILE_PHASE_SHAPE_LOOKUP);
    status = sl_request_validation_path_push_field(state, sl_str_from_cstr("body"));
    if (sl_status_is_ok(status)) {
        status = sl_request_validation_validate_json_value(state, &schema->definition, root,
                                                           sl_str_empty(), 0U);
        sl_request_validation_path_pop(state);
    }
    sl_request_validation_profile_end(state, SL_JSON_PROFILE_PHASE_SHAPE_LOOKUP, shape_start);
    yyjson_doc_free(doc);
    return status;
}

static SlStatus sl_request_validation_validate_binding(SlRequestValidationState* state,
                                                       const SlPlan* plan,
                                                       const SlPlanRequestBinding* binding,
                                                       const SlHttpRequestContext* context)
{
    SlStr value = {0};
    SlStr path = {0};

    switch (binding->kind) {
    case SL_PLAN_REQUEST_BINDING_ROUTE:
        path = !sl_str_is_empty(binding->name) ? binding->name : binding->parameter;
        if (!sl_request_validation_find_route_param(context, path, &value)) {
            return sl_request_validation_add_issue(
                state, path,
                sl_request_validation_literal("Route parameter is required.",
                                              sizeof("Route parameter is required.") - 1U));
        }
        return sl_request_validation_validate_named_scalar(state, binding, value);
    case SL_PLAN_REQUEST_BINDING_QUERY:
        path = !sl_str_is_empty(binding->name) ? binding->name : binding->parameter;
        if (!sl_request_validation_find_query_param(context, path, &value)) {
            return sl_request_validation_add_issue(
                state, path,
                sl_request_validation_literal("Query parameter is required.",
                                              sizeof("Query parameter is required.") - 1U));
        }
        return sl_request_validation_validate_named_scalar(state, binding, value);
    case SL_PLAN_REQUEST_BINDING_HEADER:
        path = !sl_str_is_empty(binding->name) ? binding->name : binding->parameter;
        if (!sl_request_validation_find_header(context, path, &value)) {
            return sl_request_validation_add_issue(
                state, path,
                sl_request_validation_literal("Header is required.",
                                              sizeof("Header is required.") - 1U));
        }
        return sl_request_validation_validate_named_scalar(state, binding, value);
    case SL_PLAN_REQUEST_BINDING_BODY_JSON:
        return sl_request_validation_validate_body(state, plan, binding, context);
    case SL_PLAN_REQUEST_BINDING_BODY_FORM:
    case SL_PLAN_REQUEST_BINDING_BODY_MULTIPART:
    case SL_PLAN_REQUEST_BINDING_COOKIE:
    case SL_PLAN_REQUEST_BINDING_CONTEXT:
    case SL_PLAN_REQUEST_BINDING_INJECTION:
    case SL_PLAN_REQUEST_BINDING_UNKNOWN:
    default:
        return sl_status_ok();
    }
}

SlStatus sl_request_validation_validate(SlArena* arena, const SlPlan* plan,
                                        const SlPlanRoute* route,
                                        const SlHttpRequestContext* request_context,
                                        SlEngineResult* out_result, SlDiag* out_diag)
{
    SlRequestValidationState state = {0};
    size_t index = 0U;
    SlStatus status;

    if (out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_result = (SlEngineResult){0};
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }

    if (arena == NULL || plan == NULL || route == NULL || request_context == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (route->binding_count == 0U) {
        return sl_status_ok();
    }
    if (route->bindings == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (sl_request_validation_bindings_are_trivially_satisfied(route, request_context)) {
        return sl_status_ok();
    }

    state.arena = arena;
    state.unknown_fields = route->json_request.unknown_fields;
    state.max_depth = route->json_request.max_depth == 0U ? SL_REQUEST_VALIDATION_MAX_JSON_DEPTH
                                                          : route->json_request.max_depth;
    state.max_string_bytes = route->json_request.max_string_bytes;
    state.max_array_length = route->json_request.max_array_length;
    state.profile_enabled = sl_json_profile_enabled();
    status = sl_fixed_vec_init(&state.issues, state.issue_storage, sizeof(SlRequestValidationIssue),
                               SL_REQUEST_VALIDATION_MAX_ISSUES);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (index = 0U; index < route->binding_count; index += 1U) {
        status = sl_request_validation_validate_binding(&state, plan, &route->bindings[index],
                                                        request_context);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    if (sl_fixed_vec_count(&state.issues) == 0U) {
        return sl_status_ok();
    }

    status = sl_request_validation_diag(
        arena, out_diag, SL_DIAG_REQUEST_VALIDATION_FAILED,
        sl_request_validation_literal("request validation failed",
                                      sizeof("request validation failed") - 1U),
        sl_request_validation_literal("the handler was not called because Plan-backed request "
                                      "metadata rejected the request",
                                      sizeof("the handler was not called because Plan-backed "
                                             "request metadata rejected the request") -
                                          1U));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_request_validation_problem(arena, &state, out_result);
}
