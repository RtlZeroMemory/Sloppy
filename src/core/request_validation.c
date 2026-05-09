/*
 * src/core/request_validation.c
 *
 * Consumes compiler-emitted Framework v2 Plan metadata at the native request boundary.
 * The validator intentionally stays small: route/query/header primitive binding checks,
 * JSON body schema validation for the supported Plan schema subset, safe problem response
 * materialization, and deterministic diagnostics. It does not execute handlers, providers,
 * queues, services, controllers, or TypeScript reflection.
 */
#include "sloppy/request_validation.h"

#include "sloppy/builder.h"
#include "sloppy/checked_math.h"
#include "sloppy/container.h"

#include <yyjson.h>

#define SL_REQUEST_VALIDATION_MAX_ISSUES 8U
#define SL_REQUEST_VALIDATION_MAX_JSON_DEPTH 50U
#define SL_REQUEST_VALIDATION_PROBLEM_INITIAL 256U
#define SL_REQUEST_VALIDATION_PROBLEM_MAX 4096U

typedef struct SlRequestValidationIssue
{
    SlStr path;
    SlStr message;
} SlRequestValidationIssue;

typedef struct SlRequestValidationState
{
    SlArena* arena;
    SlRequestValidationIssue issue_storage[SL_REQUEST_VALIDATION_MAX_ISSUES];
    SlFixedVec issues;
} SlRequestValidationState;

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

static SlStatus sl_request_validation_add_issue(SlRequestValidationState* state, SlStr path,
                                                SlStr message)
{
    SlRequestValidationIssue* issue = NULL;
    SlStatus status;

    if (state == NULL || state->arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (sl_fixed_vec_count(&state->issues) >= SL_REQUEST_VALIDATION_MAX_ISSUES) {
        return sl_status_ok();
    }

    status = sl_fixed_vec_push_zero(&state->issues, (void**)&issue);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (sl_str_is_empty(path)) {
        path = sl_str_from_cstr("$");
    }
    status = sl_request_validation_copy_str(state->arena, path, &issue->path);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_request_validation_copy_str(state->arena, message, &issue->message);
}

static SlStatus sl_request_validation_append_json_escaped(SlStringBuilder* builder, SlStr text)
{
    size_t index = 0U;
    SlStatus status;

    status = sl_string_builder_append_char(builder, '"');
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < text.length; index += 1U) {
        unsigned char ch = (unsigned char)text.ptr[index];
        switch (ch) {
        case '"':
            status = sl_string_builder_append_cstr(builder, "\\\"");
            break;
        case '\\':
            status = sl_string_builder_append_cstr(builder, "\\\\");
            break;
        case '\b':
            status = sl_string_builder_append_cstr(builder, "\\b");
            break;
        case '\f':
            status = sl_string_builder_append_cstr(builder, "\\f");
            break;
        case '\n':
            status = sl_string_builder_append_cstr(builder, "\\n");
            break;
        case '\r':
            status = sl_string_builder_append_cstr(builder, "\\r");
            break;
        case '\t':
            status = sl_string_builder_append_cstr(builder, "\\t");
            break;
        default:
            if (ch < 0x20U) {
                static const char hex[] = "0123456789abcdef";
                char escaped[] = {'\\', 'u', '0', '0', '0', '0', '\0'};
                escaped[4] = hex[(ch >> 4U) & 0x0FU];
                escaped[5] = hex[ch & 0x0FU];
                status = sl_string_builder_append_cstr(builder, escaped);
            }
            else {
                status = sl_string_builder_append_char(builder, (char)ch);
            }
            break;
        }
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    return sl_string_builder_append_char(builder, '"');
}

static SlStatus sl_request_validation_problem(SlArena* arena, const SlRequestValidationState* state,
                                              SlEngineResult* out_result)
{
    SlStringBuilder builder = {0};
    SlStr body = {0};
    size_t index = 0U;
    SlStatus status;

    if (arena == NULL || state == NULL || out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_result = (SlEngineResult){0};
    status = sl_string_builder_init_arena(&builder, arena, SL_REQUEST_VALIDATION_PROBLEM_INITIAL,
                                          SL_REQUEST_VALIDATION_PROBLEM_MAX);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_string_builder_append_cstr(
        &builder, "{\"type\":\"https://sloppy.dev/problems/validation\",\"title\":\"Validation "
                  "failed\",\"status\":400,\"errors\":{");
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < sl_fixed_vec_count(&state->issues); index += 1U) {
        const SlRequestValidationIssue* issue =
            (const SlRequestValidationIssue*)sl_fixed_vec_at_const(&state->issues, index);

        if (issue == NULL) {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        if (index != 0U) {
            status = sl_string_builder_append_char(&builder, ',');
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
        status = sl_request_validation_append_json_escaped(&builder, issue->path);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_string_builder_append_cstr(&builder, ":[");
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_request_validation_append_json_escaped(&builder, issue->message);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_string_builder_append_char(&builder, ']');
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    status = sl_string_builder_append_cstr(&builder, "}}\n");
    if (!sl_status_is_ok(status)) {
        return status;
    }

    body = sl_string_builder_view(&builder);
    out_result->kind = SL_ENGINE_RESULT_ERROR;
    out_result->response = sl_http_response_problem(
        400U, sl_bytes_from_parts((const unsigned char*)body.ptr, body.length));
    return sl_status_ok();
}

static const SlPlanSchema* sl_request_validation_find_schema(const SlPlan* plan, SlStr name)
{
    size_t index = 0U;

    if (plan == NULL || sl_str_is_empty(name) ||
        (plan->schema_count != 0U && plan->schemas == NULL))
    {
        return NULL;
    }

    for (index = 0U; index < plan->schema_count; index += 1U) {
        if (sl_str_equal(plan->schemas[index].name, name)) {
            return &plan->schemas[index];
        }
    }
    return NULL;
}

static SlStatus sl_request_validation_child_path(SlArena* arena, SlStr parent, SlStr child,
                                                 SlStr* out)
{
    SlStringBuilder builder = {0};
    SlStatus status;

    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_string_builder_init_arena(&builder, arena, parent.length + child.length + 2U,
                                          parent.length + child.length + 2U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!sl_str_is_empty(parent) && !sl_str_equal(parent, sl_str_from_cstr("$"))) {
        status = sl_string_builder_append_str(&builder, parent);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_string_builder_append_char(&builder, '.');
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    status = sl_string_builder_append_str(&builder, child);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out = sl_string_builder_view(&builder);
    return sl_status_ok();
}

static SlStatus sl_request_validation_index_path(SlArena* arena, SlStr parent, size_t index,
                                                 SlStr* out)
{
    size_t capacity = 0U;
    SlStringBuilder builder = {0};
    SlStatus status;

    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = sl_str_empty();
    status = sl_checked_add_size(parent.length, 32U, &capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_init_arena(&builder, arena, capacity, capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_append_str(&builder, parent);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_append_char(&builder, '[');
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_append_size(&builder, index);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_append_char(&builder, ']');
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out = sl_string_builder_view(&builder);
    return sl_status_ok();
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
    if (schema->has_min && schema->min_value >= 0 && value.length < (size_t)schema->min_value) {
        return sl_request_validation_add_issue(
            state, path,
            sl_request_validation_literal("Expected a longer string.",
                                          sizeof("Expected a longer string.") - 1U));
    }
    if (sl_str_equal(schema->validation, sl_str_from_cstr("email")) &&
        !sl_request_validation_email_like(value))
    {
        return sl_request_validation_add_issue(
            state, path,
            sl_request_validation_literal("Expected an email address.",
                                          sizeof("Expected an email address.") - 1U));
    }
    if (sl_str_equal(schema->validation, sl_str_from_cstr("uuid")) &&
        !sl_request_validation_uuid_like(value))
    {
        return sl_request_validation_add_issue(
            state, path,
            sl_request_validation_literal("Expected a UUID string.",
                                          sizeof("Expected a UUID string.") - 1U));
    }
    return sl_status_ok();
}

static SlStatus sl_request_validation_validate_object(SlRequestValidationState* state,
                                                      const SlPlanSchemaNode* schema,
                                                      yyjson_val* value, SlStr path, size_t depth)
{
    size_t index = 0U;
    SlStatus status;

    if (!yyjson_is_obj(value)) {
        return sl_request_validation_add_issue(
            state, path,
            sl_request_validation_literal("Expected an object.",
                                          sizeof("Expected an object.") - 1U));
    }

    for (index = 0U; index < schema->property_count; index += 1U) {
        const SlPlanSchemaProperty* property = &schema->properties[index];
        SlStr child_path = {0};
        yyjson_val* child = yyjson_obj_getn(value, property->name.ptr, property->name.length);

        if (property->schema == NULL) {
            continue;
        }
        status = sl_request_validation_child_path(state->arena, path, property->name, &child_path);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (child == NULL) {
            if (!property->schema->optional) {
                status = sl_request_validation_add_issue(
                    state, child_path,
                    sl_request_validation_literal("Field is required.",
                                                  sizeof("Field is required.") - 1U));
                if (!sl_status_is_ok(status)) {
                    return status;
                }
            }
            continue;
        }
        if (yyjson_is_null(child)) {
            if (!property->schema->nullable) {
                status = sl_request_validation_add_issue(
                    state, child_path,
                    sl_request_validation_literal("Field is required.",
                                                  sizeof("Field is required.") - 1U));
                if (!sl_status_is_ok(status)) {
                    return status;
                }
            }
            continue;
        }
        status = sl_request_validation_validate_json_value(state, property->schema, child,
                                                           child_path, depth + 1U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
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
    SlStatus status;

    if (!yyjson_is_arr(value)) {
        return sl_request_validation_add_issue(
            state, path,
            sl_request_validation_literal("Expected an array.", sizeof("Expected an array.") - 1U));
    }
    if (schema->items == NULL) {
        return sl_request_validation_add_issue(
            state, path,
            sl_request_validation_literal("Schema is not supported by this runtime.",
                                          sizeof("Schema is not supported by this runtime.") - 1U));
    }

    yyjson_arr_iter_init(value, &iter);
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        SlStr item_path = {0};

        status = sl_request_validation_index_path(state->arena, path, item_index, &item_path);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_request_validation_validate_json_value(state, schema->items, item, item_path,
                                                           depth + 1U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        item_index += 1U;
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

    return sl_request_validation_add_issue(
        state, path,
        sl_request_validation_literal("Expected one of the declared literal values.",
                                      sizeof("Expected one of the declared literal values.") - 1U));
}

static SlStatus sl_request_validation_validate_json_value(SlRequestValidationState* state,
                                                          const SlPlanSchemaNode* schema,
                                                          yyjson_val* value, SlStr path,
                                                          size_t depth)
{
    SlStatus status;

    if (state == NULL || schema == NULL || value == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (depth > SL_REQUEST_VALIDATION_MAX_JSON_DEPTH) {
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
        if (!yyjson_is_str(value)) {
            return sl_request_validation_add_issue(
                state, path,
                sl_request_validation_literal("Expected a string.",
                                              sizeof("Expected a string.") - 1U));
        }
        return sl_request_validation_validate_string(
            state, schema, sl_str_from_parts(yyjson_get_str(value), yyjson_get_len(value)), path);
    case SL_PLAN_SCHEMA_NUMBER:
        if (!yyjson_is_num(value)) {
            return sl_request_validation_add_issue(
                state, path,
                sl_request_validation_literal("Expected a finite number.",
                                              sizeof("Expected a finite number.") - 1U));
        }
        return sl_status_ok();
    case SL_PLAN_SCHEMA_INT:
        if (!yyjson_is_int(value)) {
            return sl_request_validation_add_issue(
                state, path,
                sl_request_validation_literal("Expected an integer.",
                                              sizeof("Expected an integer.") - 1U));
        }
        if (schema->has_min && yyjson_get_sint(value) < schema->min_value) {
            return sl_request_validation_add_issue(
                state, path,
                sl_request_validation_literal("Expected a larger integer.",
                                              sizeof("Expected a larger integer.") - 1U));
        }
        return sl_status_ok();
    case SL_PLAN_SCHEMA_BOOLEAN:
        if (!yyjson_is_bool(value)) {
            return sl_request_validation_add_issue(
                state, path,
                sl_request_validation_literal("Expected a boolean.",
                                              sizeof("Expected a boolean.") - 1U));
        }
        return sl_status_ok();
    case SL_PLAN_SCHEMA_ARRAY:
        return sl_request_validation_validate_array(state, schema, value, path, depth);
    case SL_PLAN_SCHEMA_LITERAL_UNION:
        return sl_request_validation_validate_literal_union(state, schema, value, path);
    case SL_PLAN_SCHEMA_LITERAL:
        if (!sl_request_validation_literal_matches(schema, value)) {
            return sl_request_validation_add_issue(
                state, path,
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
    SlStatus status;

    if (sl_str_is_empty(binding->schema)) {
        return sl_request_validation_add_issue(
            state, sl_str_from_cstr("body"),
            sl_request_validation_literal("JSON body schema metadata is missing.",
                                          sizeof("JSON body schema metadata is missing.") - 1U));
    }

    schema = sl_request_validation_find_schema(plan, binding->schema);
    if (schema == NULL) {
        return sl_request_validation_add_issue(
            state, sl_str_from_cstr("body"),
            sl_request_validation_literal("JSON body schema is not available at runtime.",
                                          sizeof("JSON body schema is not available at runtime.") -
                                              1U));
    }

    if (context->request == NULL || context->request->body.ptr == NULL ||
        context->request->body.length == 0U || context->body_kind != SL_HTTP_REQUEST_BODY_JSON)
    {
        return sl_request_validation_add_issue(
            state, sl_str_from_cstr("body"),
            sl_request_validation_literal("Expected a JSON request body.",
                                          sizeof("Expected a JSON request body.") - 1U));
    }

    doc = yyjson_read_opts((char*)context->request->body.ptr, context->request->body.length, 0U,
                           NULL, &error);
    if (doc == NULL) {
        return sl_request_validation_add_issue(
            state, sl_str_from_cstr("body"),
            sl_request_validation_literal("Expected a valid JSON request body.",
                                          sizeof("Expected a valid JSON request body.") - 1U));
    }

    root = yyjson_doc_get_root(doc);
    status = sl_request_validation_validate_json_value(state, &schema->definition, root,
                                                       sl_str_from_cstr("body"), 0U);
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

    state.arena = arena;
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
