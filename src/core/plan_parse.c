/*
 * src/core/plan_parse.c
 *
 * Parses and validates Sloppy's Plan v1 JSON contract. This module turns caller-provided
 * bytes into arena-owned SlPlan data and reports shape diagnostics for the minimal handler
 * contract plus the alpha route/provider/capability metadata sections. It does not perform
 * file I/O, hash verification, source-map parsing, runtime compatibility checks, engine
 * loading, HTTP route dispatch, or compiler work.
 *
 * Safety invariants:
 * - yyjson document storage never escapes this file;
 * - all returned strings and handler arrays are copied into the caller-provided arena;
 * - parser failures clear the output plan where practical and report bounded diagnostics;
 * - no platform APIs, OS headers, V8 types, global parser state, or raw heap allocation are
 *   introduced by Sloppy code.
 *
 * Tests: tests/unit/core/test_plan_parse.c.
 */
#include "sloppy/plan.h"

#include "sloppy/container.h"
#include "sloppy/route.h"

#include <stdint.h>
#include <string.h>
#include <yyjson.h>

typedef struct SlPlanParseContext
{
    SlArena* arena;
    const SlPlanParseOptions* options;
    SlDiag* out_diag;
    SlDiagCode diag_code;
    SlStr diag_message;
    SlStr diag_hint;
} SlPlanParseContext;

static SlStr sl_plan_parse_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static bool sl_plan_parse_str_valid(SlStr str)
{
    return str.length == 0U || str.ptr != NULL;
}

static SlStatus sl_plan_parse_set_diag(SlPlanParseContext* ctx, SlDiagCode code, SlStr message,
                                       SlStr hint)
{
    if (ctx == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    ctx->diag_code = code;
    ctx->diag_message = message;
    ctx->diag_hint = hint;

    return sl_status_from_code(code == SL_DIAG_INVALID_PLAN_VERSION ? SL_STATUS_UNSUPPORTED
                                                                    : SL_STATUS_INVALID_ARGUMENT);
}

static SlStatus sl_plan_parse_finish_diag(SlPlanParseContext* ctx)
{
    SlDiagBuilder builder;
    SlStatus status;

    if (ctx == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (ctx->out_diag == NULL || ctx->diag_code == SL_DIAG_NONE) {
        return sl_status_ok();
    }

    *ctx->out_diag = (SlDiag){0};

    status = sl_diag_builder_init(&builder, ctx->arena, SL_DIAG_SEVERITY_ERROR, ctx->diag_code,
                                  ctx->diag_message);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (ctx->options != NULL && !sl_str_is_empty(ctx->options->source_name)) {
        status = sl_diag_builder_set_primary_span(
            &builder, sl_source_span_make(ctx->options->source_name, 0U, 0U, 0U));
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    if (!sl_str_is_empty(ctx->diag_hint)) {
        status = sl_diag_builder_add_hint(&builder, ctx->diag_hint);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    return sl_diag_builder_finish(&builder, ctx->out_diag);
}

static SlStatus sl_plan_parse_field_diag(SlPlanParseContext* ctx, SlStr message, SlStr hint)
{
    return sl_plan_parse_set_diag(ctx, SL_DIAG_INVALID_PLAN_FIELD, message, hint);
}

static SlStatus sl_plan_parse_copy_str(SlArena* arena, SlStr src, SlStr* out)
{
    if (arena == NULL || out == NULL || !sl_plan_parse_str_valid(src)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_str_copy_view_to_arena(arena, src, out);
}

static SlStatus sl_plan_parse_alloc_array(SlPlanParseContext* ctx, size_t count, size_t elem_size,
                                          size_t alignment, void** out)
{
    SlSlice slice = {0};
    SlStatus status;

    if (ctx == NULL || out == NULL || elem_size == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (count == 0U) {
        *out = NULL;
        return sl_status_ok();
    }
    status = sl_arena_array_alloc(ctx->arena, count, elem_size, alignment, &slice);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out = slice.ptr;
    return sl_status_ok();
}

static SlStatus sl_plan_parse_require_string(SlPlanParseContext* ctx, yyjson_val* object,
                                             const char* field_name, bool require_non_empty,
                                             SlStr* out)
{
    yyjson_val* value = NULL;
    SlStr str = {0};

    if (ctx == NULL || object == NULL || field_name == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    value = yyjson_obj_get(object, field_name);
    if (value == NULL) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("missing required app plan field",
                                  sizeof("missing required app plan field") - 1U),
            sl_plan_parse_literal("regenerate the plan with the required minimal v1 field",
                                  sizeof("regenerate the plan with the required minimal v1 field") -
                                      1U));
    }

    if (!yyjson_is_str(value)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan field type",
                                  sizeof("invalid app plan field type") - 1U),
            sl_plan_parse_literal("expected a JSON string for this app plan field",
                                  sizeof("expected a JSON string for this app plan field") - 1U));
    }

    str = sl_str_from_parts(yyjson_get_str(value), yyjson_get_len(value));
    if (require_non_empty && sl_str_is_empty(str)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("missing required app plan field",
                                  sizeof("missing required app plan field") - 1U),
            sl_plan_parse_literal(
                "expected a non-empty JSON string for this app plan field",
                sizeof("expected a non-empty JSON string for this app plan field") - 1U));
    }

    return sl_plan_parse_copy_str(ctx->arena, str, out);
}

static SlStatus sl_plan_parse_optional_string(SlPlanParseContext* ctx, yyjson_val* object,
                                              const char* field_name, bool require_non_empty,
                                              SlStr* out)
{
    yyjson_val* value = NULL;
    SlStr str = {0};

    if (ctx == NULL || object == NULL || field_name == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out = sl_str_empty();
    value = yyjson_obj_get(object, field_name);
    if (value == NULL || yyjson_is_null(value)) {
        return sl_status_ok();
    }
    if (!yyjson_is_str(value)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan field type",
                                  sizeof("invalid app plan field type") - 1U),
            sl_plan_parse_literal(
                "expected a JSON string for this optional app plan field",
                sizeof("expected a JSON string for this optional app plan field") - 1U));
    }

    str = sl_str_from_parts(yyjson_get_str(value), yyjson_get_len(value));
    if (require_non_empty && sl_str_is_empty(str)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("missing required app plan field",
                                  sizeof("missing required app plan field") - 1U),
            sl_plan_parse_literal(
                "expected a non-empty JSON string for this app plan field",
                sizeof("expected a non-empty JSON string for this app plan field") - 1U));
    }

    return sl_plan_parse_copy_str(ctx->arena, str, out);
}

static bool sl_plan_parse_token_syntax_valid(SlStr token)
{
    size_t index = 0U;

    if (sl_str_is_empty(token)) {
        return false;
    }

    for (index = 0U; index < token.length; index += 1U) {
        char ch = token.ptr[index];
        bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                  (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-';
        if (!ok) {
            return false;
        }
    }

    return true;
}

static SlStatus sl_plan_parse_require_object(SlPlanParseContext* ctx, yyjson_val* object,
                                             const char* field_name, yyjson_val** out)
{
    yyjson_val* value = NULL;

    if (ctx == NULL || object == NULL || field_name == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    value = yyjson_obj_get(object, field_name);
    if (value == NULL) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("missing required app plan field",
                                  sizeof("missing required app plan field") - 1U),
            sl_plan_parse_literal(
                "regenerate the plan with the required minimal v1 section",
                sizeof("regenerate the plan with the required minimal v1 section") - 1U));
    }

    if (!yyjson_is_obj(value)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan field type",
                                  sizeof("invalid app plan field type") - 1U),
            sl_plan_parse_literal("expected a JSON object for this app plan section",
                                  sizeof("expected a JSON object for this app plan section") - 1U));
    }

    *out = value;
    return sl_status_ok();
}

static SlStatus sl_plan_parse_reject_secret_fields(SlPlanParseContext* ctx, yyjson_val* object)
{
    static const char* secret_fields[] = {"connectionstring", "password", "pwd",
                                          "secret",           "apikey",   "accesstoken"};
    yyjson_obj_iter iter;
    yyjson_val* key = NULL;
    char normalized[128];
    size_t index = 0U;

    if (ctx == NULL || object == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    iter = yyjson_obj_iter_with(object);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL) {
        SlStr raw = sl_str_from_parts(yyjson_get_str(key), yyjson_get_len(key));
        size_t normalized_len = 0U;
        size_t char_index = 0U;

        for (char_index = 0U; char_index < raw.length && normalized_len < sizeof(normalized) - 1U;
             char_index += 1U)
        {
            char ch = raw.ptr[char_index];
            if (ch >= 'A' && ch <= 'Z') {
                normalized[normalized_len] = (char)(ch - 'A' + 'a');
                normalized_len += 1U;
            }
            else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
                normalized[normalized_len] = ch;
                normalized_len += 1U;
            }
        }
        normalized[normalized_len] = '\0';

        for (index = 0U; index < sizeof(secret_fields) / sizeof(secret_fields[0]); index += 1U) {
            if (strcmp(normalized, secret_fields[index]) == 0) {
                return sl_plan_parse_field_diag(
                    ctx,
                    sl_plan_parse_literal("app plan contains secret-bearing field",
                                          sizeof("app plan contains secret-bearing field") - 1U),
                    sl_plan_parse_literal(
                        "store secret values outside app.plan.json and reference config keys "
                        "instead",
                        sizeof(
                            "store secret values outside app.plan.json and reference config keys "
                            "instead") -
                            1U));
            }
        }
    }
    return sl_status_ok();
}

static SlStatus sl_plan_parse_schema_version(SlPlanParseContext* ctx, yyjson_val* root,
                                             uint32_t* out)
{
    yyjson_val* value = NULL;
    uint64_t version = 0U;

    value = yyjson_obj_get(root, "schemaVersion");
    if (value == NULL) {
        return sl_plan_parse_set_diag(
            ctx, SL_DIAG_INVALID_PLAN_VERSION,
            sl_plan_parse_literal("invalid app plan version",
                                  sizeof("invalid app plan version") - 1U),
            sl_plan_parse_literal("schemaVersion must be present and equal to 1",
                                  sizeof("schemaVersion must be present and equal to 1") - 1U));
    }

    if (!yyjson_is_uint(value)) {
        return sl_plan_parse_set_diag(
            ctx, SL_DIAG_INVALID_PLAN_VERSION,
            sl_plan_parse_literal("invalid app plan version",
                                  sizeof("invalid app plan version") - 1U),
            sl_plan_parse_literal("schemaVersion must be an unsigned integer",
                                  sizeof("schemaVersion must be an unsigned integer") - 1U));
    }

    version = yyjson_get_uint(value);
    if (version > UINT32_MAX || !sl_plan_version_supported((uint32_t)version)) {
        return sl_plan_parse_set_diag(
            ctx, SL_DIAG_INVALID_PLAN_VERSION,
            sl_plan_parse_literal("invalid app plan version",
                                  sizeof("invalid app plan version") - 1U),
            sl_plan_parse_literal("only Sloppy Plan schemaVersion 1 is supported",
                                  sizeof("only Sloppy Plan schemaVersion 1 is supported") - 1U));
    }

    *out = (uint32_t)version;
    return sl_status_ok();
}

static SlStatus sl_plan_parse_required_features(SlPlanParseContext* ctx, yyjson_val* root,
                                                SlPlan* out)
{
    yyjson_val* features = yyjson_obj_get(root, "requiredFeatures");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;
    SlPlanRequiredFeature* parsed = NULL;
    size_t count = 0U;
    size_t index = 0U;
    SlStatus status;

    if (features == NULL) {
        return sl_status_ok();
    }
    if (!yyjson_is_arr(features)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan field type",
                                  sizeof("invalid app plan field type") - 1U),
            sl_plan_parse_literal("requiredFeatures must be a JSON array",
                                  sizeof("requiredFeatures must be a JSON array") - 1U));
    }
    count = yyjson_arr_size(features);
    if (count == 0U) {
        return sl_status_ok();
    }

    status = sl_plan_parse_alloc_array(ctx, count, sizeof(SlPlanRequiredFeature),
                                       _Alignof(SlPlanRequiredFeature), (void**)&parsed);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    yyjson_arr_iter_init(features, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        SlStr feature = {0};
        if (!yyjson_is_str(value)) {
            return sl_plan_parse_field_diag(
                ctx,
                sl_plan_parse_literal("invalid app plan field type",
                                      sizeof("invalid app plan field type") - 1U),
                sl_plan_parse_literal("requiredFeatures entries must be JSON strings",
                                      sizeof("requiredFeatures entries must be JSON strings") -
                                          1U));
        }
        feature = sl_str_from_parts(yyjson_get_str(value), yyjson_get_len(value));
        if (sl_str_is_empty(feature)) {
            return sl_plan_parse_field_diag(
                ctx,
                sl_plan_parse_literal("missing required app plan feature",
                                      sizeof("missing required app plan feature") - 1U),
                sl_plan_parse_literal("requiredFeatures entries must be non-empty strings",
                                      sizeof("requiredFeatures entries must be non-empty "
                                             "strings") -
                                          1U));
        }
        status = sl_plan_parse_copy_str(ctx->arena, feature, &parsed[index].id);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        index += 1U;
    }

    out->required_features = parsed;
    out->required_feature_count = count;
    return sl_status_ok();
}

static SlStatus sl_plan_parse_target(SlPlanParseContext* ctx, yyjson_val* root, SlPlanTarget* out)
{
    yyjson_val* target = NULL;
    SlStatus status;

    status = sl_plan_parse_require_object(ctx, root, "target", &target);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_plan_parse_require_string(ctx, target, "platform", true, &out->platform);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_plan_parse_require_string(ctx, target, "engine", true, &out->engine);
}

static SlStatus sl_plan_parse_bundle(SlPlanParseContext* ctx, yyjson_val* root, SlPlanBundle* out)
{
    yyjson_val* bundle = NULL;
    SlStatus status;

    status = sl_plan_parse_require_object(ctx, root, "bundle", &bundle);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_plan_parse_require_string(ctx, bundle, "path", true, &out->path);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_plan_parse_require_string(ctx, bundle, "id", true, &out->id);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_plan_parse_require_string(ctx, bundle, "hash", true, &out->hash);
}

static SlStatus sl_plan_parse_source_map(SlPlanParseContext* ctx, yyjson_val* root,
                                         SlPlanSourceMap* out)
{
    yyjson_val* source_map = NULL;
    SlStatus status;

    status = sl_plan_parse_require_object(ctx, root, "sourceMap", &source_map);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_plan_parse_require_string(ctx, source_map, "path", true, &out->path);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_plan_parse_require_string(ctx, source_map, "id", true, &out->id);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_plan_parse_require_string(ctx, source_map, "hash", true, &out->hash);
}

static SlStatus sl_plan_parse_handler_id(SlPlanParseContext* ctx, yyjson_val* object,
                                         SlHandlerId* out)
{
    yyjson_val* value = NULL;
    uint64_t id = 0U;

    value = yyjson_obj_get(object, "id");
    if (value == NULL || !yyjson_is_uint(value)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan handler id",
                                  sizeof("invalid app plan handler id") - 1U),
            sl_plan_parse_literal("handler id must be a nonzero unsigned integer",
                                  sizeof("handler id must be a nonzero unsigned integer") - 1U));
    }

    id = yyjson_get_uint(value);
    if (id > UINT32_MAX || !sl_handler_id_valid((SlHandlerId)id)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan handler id",
                                  sizeof("invalid app plan handler id") - 1U),
            sl_plan_parse_literal(
                "handler id 0 is reserved and handler ids must fit uint32",
                sizeof("handler id 0 is reserved and handler ids must fit uint32") - 1U));
    }

    *out = (SlHandlerId)id;
    return sl_status_ok();
}

static SlStatus sl_plan_parse_one_handler(SlPlanParseContext* ctx, yyjson_val* value,
                                          SlPlanHandler* out)
{
    SlStatus status;

    if (!yyjson_is_obj(value)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan field type",
                                  sizeof("invalid app plan field type") - 1U),
            sl_plan_parse_literal("each handlers entry must be a JSON object",
                                  sizeof("each handlers entry must be a JSON object") - 1U));
    }

    status = sl_plan_parse_handler_id(ctx, value, &out->id);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_plan_parse_require_string(ctx, value, "exportName", true, &out->export_name);
    if (!sl_status_is_ok(status)) {
        return sl_plan_parse_set_diag(
            ctx, SL_DIAG_INVALID_PLAN_FIELD,
            sl_plan_parse_literal("invalid handler export name",
                                  sizeof("invalid handler export name") - 1U),
            sl_plan_parse_literal("handler exportName must be a non-empty string",
                                  sizeof("handler exportName must be a non-empty string") - 1U));
    }

    return sl_plan_parse_require_string(ctx, value, "displayName", true, &out->display_name);
}

static SlStatus sl_plan_parse_duplicate_handler_diag(SlPlanParseContext* ctx)
{
    return sl_plan_parse_set_diag(
        ctx, SL_DIAG_DUPLICATE_HANDLER_ID,
        sl_plan_parse_literal("duplicate handler id", sizeof("duplicate handler id") - 1U),
        sl_plan_parse_literal("handler ids must be unique within an app plan",
                              sizeof("handler ids must be unique within an app plan") - 1U));
}

static SlStatus sl_plan_parse_handlers(SlPlanParseContext* ctx, yyjson_val* root, SlPlan* out)
{
    yyjson_val* handlers = NULL;
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;
    SlPlanHandler* parsed_handlers = NULL;
    size_t handler_count = 0U;
    size_t index = 0U;
    SlStatus status;

    handlers = yyjson_obj_get(root, "handlers");
    if (handlers == NULL) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("missing required app plan field",
                                  sizeof("missing required app plan field") - 1U),
            sl_plan_parse_literal("handlers must be present in minimal Plan v1",
                                  sizeof("handlers must be present in minimal Plan v1") - 1U));
    }

    if (!yyjson_is_arr(handlers)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan field type",
                                  sizeof("invalid app plan field type") - 1U),
            sl_plan_parse_literal("handlers must be a JSON array",
                                  sizeof("handlers must be a JSON array") - 1U));
    }

    handler_count = yyjson_arr_size(handlers);
    if (handler_count == 0U) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("missing required app plan field",
                                  sizeof("missing required app plan field") - 1U),
            sl_plan_parse_literal("minimal Plan v1 requires at least one handler",
                                  sizeof("minimal Plan v1 requires at least one handler") - 1U));
    }

    status = sl_plan_parse_alloc_array(ctx, handler_count, sizeof(SlPlanHandler),
                                       _Alignof(SlPlanHandler), (void**)&parsed_handlers);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    yyjson_arr_iter_init(handlers, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        parsed_handlers[index] = (SlPlanHandler){0};
        status = sl_plan_parse_one_handler(ctx, value, &parsed_handlers[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        index += 1U;
    }

    out->handlers = parsed_handlers;
    out->handler_count = handler_count;

    if (sl_plan_has_duplicate_handler_ids(out)) {
        return sl_plan_parse_duplicate_handler_diag(ctx);
    }

    return sl_status_ok();
}

static SlStatus sl_plan_parse_route_id(SlPlanParseContext* ctx, yyjson_val* object,
                                       SlHandlerId* out)
{
    yyjson_val* value = yyjson_obj_get(object, "handlerId");
    uint64_t id = 0U;

    if (value == NULL || !yyjson_is_uint(value)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan route handler id",
                                  sizeof("invalid app plan route handler id") - 1U),
            sl_plan_parse_literal("route handlerId must reference a declared nonzero handler id",
                                  sizeof("route handlerId must reference a declared nonzero "
                                         "handler id") -
                                      1U));
    }

    id = yyjson_get_uint(value);
    if (id > UINT32_MAX || !sl_handler_id_valid((SlHandlerId)id)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan route handler id",
                                  sizeof("invalid app plan route handler id") - 1U),
            sl_plan_parse_literal("route handlerId must fit uint32 and must not be 0",
                                  sizeof("route handlerId must fit uint32 and must not be 0") -
                                      1U));
    }

    *out = (SlHandlerId)id;
    return sl_status_ok();
}

static SlStatus sl_plan_parse_validate_route_pattern(SlPlanParseContext* ctx, SlStr pattern)
{
    SlArenaMark mark = sl_arena_mark(ctx->arena);
    SlRoutePattern parsed = {0};
    SlStatus status = sl_route_pattern_parse(ctx->arena, pattern, &parsed, NULL);
    SlStatus reset_status = sl_arena_reset_to(ctx->arena, mark);

    if (!sl_status_is_ok(reset_status)) {
        return reset_status;
    }
    if (!sl_status_is_ok(status)) {
        return sl_plan_parse_set_diag(
            ctx, SL_DIAG_INVALID_ROUTE_PATTERN,
            sl_plan_parse_literal("invalid app plan route pattern",
                                  sizeof("invalid app plan route pattern") - 1U),
            sl_plan_parse_literal("routes[].pattern must use the supported alpha route syntax",
                                  sizeof("routes[].pattern must use the supported alpha route "
                                         "syntax") -
                                      1U));
    }
    return sl_status_ok();
}

static SlPlanRequestBindingKind sl_plan_parse_binding_kind(SlStr kind)
{
    if (sl_str_equal(kind, sl_str_from_cstr("route"))) {
        return SL_PLAN_REQUEST_BINDING_ROUTE;
    }
    if (sl_str_equal(kind, sl_str_from_cstr("query"))) {
        return SL_PLAN_REQUEST_BINDING_QUERY;
    }
    if (sl_str_equal(kind, sl_str_from_cstr("body.json"))) {
        return SL_PLAN_REQUEST_BINDING_BODY_JSON;
    }
    if (sl_str_equal(kind, sl_str_from_cstr("header"))) {
        return SL_PLAN_REQUEST_BINDING_HEADER;
    }
    if (sl_str_equal(kind, sl_str_from_cstr("context"))) {
        return SL_PLAN_REQUEST_BINDING_CONTEXT;
    }
    if (sl_str_equal(kind, sl_str_from_cstr("injection"))) {
        return SL_PLAN_REQUEST_BINDING_INJECTION;
    }
    return SL_PLAN_REQUEST_BINDING_UNKNOWN;
}

static SlStatus sl_plan_parse_bool_field(SlPlanParseContext* ctx, yyjson_val* object,
                                         const char* field_name, bool default_value, bool* out)
{
    yyjson_val* value = NULL;

    if (ctx == NULL || object == NULL || field_name == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out = default_value;
    value = yyjson_obj_get(object, field_name);
    if (value == NULL || yyjson_is_null(value)) {
        return sl_status_ok();
    }
    if (!yyjson_is_bool(value)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan field type",
                                  sizeof("invalid app plan field type") - 1U),
            sl_plan_parse_literal("expected a JSON boolean for this metadata field",
                                  sizeof("expected a JSON boolean for this metadata field") - 1U));
    }

    *out = yyjson_get_bool(value);
    return sl_status_ok();
}

static SlStatus sl_plan_parse_one_binding(SlPlanParseContext* ctx, yyjson_val* value,
                                          SlPlanRequestBinding* out)
{
    SlStr kind = {0};
    SlStatus status;

    if (!yyjson_is_obj(value)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan field type",
                                  sizeof("invalid app plan field type") - 1U),
            sl_plan_parse_literal("each route binding entry must be a JSON object",
                                  sizeof("each route binding entry must be a JSON object") - 1U));
    }

    status = sl_plan_parse_require_string(ctx, value, "kind", true, &kind);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    out->kind = sl_plan_parse_binding_kind(kind);
    if (out->kind == SL_PLAN_REQUEST_BINDING_UNKNOWN) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("unsupported route binding kind",
                                  sizeof("unsupported route binding kind") - 1U),
            sl_plan_parse_literal("route bindings must use supported framework metadata kinds",
                                  sizeof("route bindings must use supported framework metadata "
                                         "kinds") -
                                      1U));
    }

    status = sl_plan_parse_optional_string(ctx, value, "parameter", false, &out->parameter);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_parse_optional_string(ctx, value, "name", false, &out->name);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_parse_optional_string(ctx, value, "schema", false, &out->schema);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_parse_optional_string(ctx, value, "type", false, &out->type);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_plan_parse_bool_field(ctx, value, "redacted", false, &out->redacted);
}

static SlStatus sl_plan_parse_route_bindings(SlPlanParseContext* ctx, yyjson_val* route,
                                             SlPlanRoute* out)
{
    yyjson_val* bindings = yyjson_obj_get(route, "bindings");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;
    SlPlanRequestBinding* parsed = NULL;
    size_t count = 0U;
    size_t index = 0U;
    SlStatus status;

    if (bindings == NULL || yyjson_is_null(bindings)) {
        return sl_status_ok();
    }
    if (!yyjson_is_arr(bindings)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan field type",
                                  sizeof("invalid app plan field type") - 1U),
            sl_plan_parse_literal("route bindings must be a JSON array",
                                  sizeof("route bindings must be a JSON array") - 1U));
    }

    count = yyjson_arr_size(bindings);
    if (count == 0U) {
        sl_plan_route_mark_bindings_empty(out);
        return sl_status_ok();
    }

    status = sl_plan_parse_alloc_array(ctx, count, sizeof(SlPlanRequestBinding),
                                       _Alignof(SlPlanRequestBinding), (void**)&parsed);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    yyjson_arr_iter_init(bindings, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        parsed[index] = (SlPlanRequestBinding){0};
        status = sl_plan_parse_one_binding(ctx, value, &parsed[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        index += 1U;
    }

    out->bindings = parsed;
    out->binding_count = count;
    return sl_status_ok();
}

static SlStatus sl_plan_parse_route_middleware(SlPlanParseContext* ctx, yyjson_val* route,
                                               bool* out_has_middleware)
{
    yyjson_val* middleware = yyjson_obj_get(route, "middleware");

    if (out_has_middleware == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_has_middleware = false;
    if (middleware == NULL || yyjson_is_null(middleware)) {
        return sl_status_ok();
    }
    if (!yyjson_is_arr(middleware)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan field type",
                                  sizeof("invalid app plan field type") - 1U),
            sl_plan_parse_literal("route middleware must be a JSON array",
                                  sizeof("route middleware must be a JSON array") - 1U));
    }

    *out_has_middleware = yyjson_arr_size(middleware) != 0U;
    return sl_status_ok();
}

static SlStatus sl_plan_route_add_context_binding(SlPlanParseContext* ctx, SlPlanRoute* route)
{
    SlPlanRequestBinding* parsed = NULL;
    size_t existing_count = 0U;
    size_t index = 0U;
    SlStatus status;

    if (ctx == NULL || route == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    for (index = 0U; index < route->binding_count; index += 1U) {
        if (route->bindings[index].kind == SL_PLAN_REQUEST_BINDING_CONTEXT) {
            return sl_status_ok();
        }
    }

    existing_count = route->bindings == SL_PLAN_ROUTE_EMPTY_BINDINGS ? 0U : route->binding_count;
    status = sl_plan_parse_alloc_array(ctx, existing_count + 1U, sizeof(SlPlanRequestBinding),
                                       _Alignof(SlPlanRequestBinding), (void**)&parsed);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (index = 0U; index < existing_count; index += 1U) {
        parsed[index] = route->bindings[index];
    }
    parsed[existing_count] = (SlPlanRequestBinding){
        .kind = SL_PLAN_REQUEST_BINDING_CONTEXT,
        .name = sl_str_from_cstr("RequestContext"),
        .type = sl_str_from_cstr("RequestContext"),
    };
    route->bindings = parsed;
    route->binding_count = existing_count + 1U;
    return sl_status_ok();
}

static SlStatus sl_plan_parse_one_route(SlPlanParseContext* ctx, const SlPlan* plan,
                                        yyjson_val* value, SlPlanRoute* out)
{
    const SlPlanHandler* handler = NULL;
    bool has_middleware = false;
    SlStatus status;

    if (!yyjson_is_obj(value)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan field type",
                                  sizeof("invalid app plan field type") - 1U),
            sl_plan_parse_literal("each routes entry must be a JSON object",
                                  sizeof("each routes entry must be a JSON object") - 1U));
    }

    status = sl_plan_parse_require_string(ctx, value, "method", true, &out->method);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!sl_plan_route_method_supported(out->method)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("unsupported app plan route method",
                                  sizeof("unsupported app plan route method") - 1U),
            sl_plan_parse_literal(
                "Plan v1 route metadata supports GET, POST, PUT, PATCH, DELETE, and OPTIONS",
                sizeof("Plan v1 route metadata supports GET, POST, PUT, "
                       "PATCH, DELETE, and OPTIONS") -
                    1U));
    }

    status = sl_plan_parse_require_string(ctx, value, "pattern", true, &out->pattern);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_parse_validate_route_pattern(ctx, out->pattern);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_plan_parse_route_id(ctx, value, &out->handler_id);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_find_handler_by_id(plan, out->handler_id, &handler);
    if (!sl_status_is_ok(status)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("app plan route references missing handler",
                                  sizeof("app plan route references missing handler") - 1U),
            sl_plan_parse_literal("routes[].handlerId must reference handlers[].id",
                                  sizeof("routes[].handlerId must reference handlers[].id") - 1U));
    }

    status = sl_plan_parse_optional_string(ctx, value, "name", true, &out->name);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_plan_parse_route_middleware(ctx, value, &has_middleware);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_plan_parse_route_bindings(ctx, value, out);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (has_middleware) {
        return sl_plan_route_add_context_binding(ctx, out);
    }
    return sl_status_ok();
}

static SlStatus sl_plan_parse_routes(SlPlanParseContext* ctx, yyjson_val* root, SlPlan* out)
{
    yyjson_val* routes = yyjson_obj_get(root, "routes");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;
    SlPlanRoute* parsed_routes = NULL;
    size_t route_count = 0U;
    size_t index = 0U;
    SlStatus status;

    if (routes == NULL) {
        return sl_status_ok();
    }
    if (!yyjson_is_arr(routes)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan field type",
                                  sizeof("invalid app plan field type") - 1U),
            sl_plan_parse_literal("routes must be a JSON array",
                                  sizeof("routes must be a JSON array") - 1U));
    }

    route_count = yyjson_arr_size(routes);
    if (route_count == 0U) {
        out->routes = NULL;
        out->route_count = 0U;
        return sl_status_ok();
    }

    status = sl_plan_parse_alloc_array(ctx, route_count, sizeof(SlPlanRoute), _Alignof(SlPlanRoute),
                                       (void**)&parsed_routes);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    yyjson_arr_iter_init(routes, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        parsed_routes[index] = (SlPlanRoute){0};
        status = sl_plan_parse_one_route(ctx, out, value, &parsed_routes[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        index += 1U;
    }

    out->routes = parsed_routes;
    out->route_count = route_count;

    if (sl_plan_has_duplicate_routes(out)) {
        return sl_plan_parse_set_diag(
            ctx, SL_DIAG_INVALID_PLAN_FIELD,
            sl_plan_parse_literal("duplicate app plan route",
                                  sizeof("duplicate app plan route") - 1U),
            sl_plan_parse_literal("route method and pattern pairs must be unique",
                                  sizeof("route method and pattern pairs must be unique") - 1U));
    }
    if (sl_plan_has_duplicate_route_names(out)) {
        return sl_plan_parse_set_diag(
            ctx, SL_DIAG_INVALID_PLAN_FIELD,
            sl_plan_parse_literal("duplicate app plan route name",
                                  sizeof("duplicate app plan route name") - 1U),
            sl_plan_parse_literal("non-empty route names must be unique",
                                  sizeof("non-empty route names must be unique") - 1U));
    }

    return sl_status_ok();
}

static SlPlanSchemaKind sl_plan_parse_schema_kind(SlStr kind)
{
    if (sl_str_equal(kind, sl_str_from_cstr("object"))) {
        return SL_PLAN_SCHEMA_OBJECT;
    }
    if (sl_str_equal(kind, sl_str_from_cstr("string"))) {
        return SL_PLAN_SCHEMA_STRING;
    }
    if (sl_str_equal(kind, sl_str_from_cstr("number"))) {
        return SL_PLAN_SCHEMA_NUMBER;
    }
    if (sl_str_equal(kind, sl_str_from_cstr("boolean"))) {
        return SL_PLAN_SCHEMA_BOOLEAN;
    }
    if (sl_str_equal(kind, sl_str_from_cstr("int"))) {
        return SL_PLAN_SCHEMA_INT;
    }
    if (sl_str_equal(kind, sl_str_from_cstr("array"))) {
        return SL_PLAN_SCHEMA_ARRAY;
    }
    if (sl_str_equal(kind, sl_str_from_cstr("literalUnion"))) {
        return SL_PLAN_SCHEMA_LITERAL_UNION;
    }
    if (sl_str_equal(kind, sl_str_from_cstr("literal"))) {
        return SL_PLAN_SCHEMA_LITERAL;
    }
    if (sl_str_equal(kind, sl_str_from_cstr("null"))) {
        return SL_PLAN_SCHEMA_NULL;
    }
    return SL_PLAN_SCHEMA_UNKNOWN;
}

static SlStatus sl_plan_parse_i64_field(SlPlanParseContext* ctx, yyjson_val* object,
                                        const char* field_name, bool* out_present, int64_t* out)
{
    yyjson_val* value = NULL;

    if (ctx == NULL || object == NULL || field_name == NULL || out_present == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_present = false;
    *out = 0;
    value = yyjson_obj_get(object, field_name);
    if (value == NULL || yyjson_is_null(value)) {
        return sl_status_ok();
    }
    if (yyjson_is_int(value)) {
        *out_present = true;
        *out = yyjson_get_sint(value);
        return sl_status_ok();
    }
    if (yyjson_is_num(value)) {
        double number = yyjson_get_num(value);
        int64_t converted = 0;

        if (number >= (double)INT64_MIN && number <= (double)INT64_MAX) {
            converted = (int64_t)number;
            if ((double)converted == number) {
                *out_present = true;
                *out = converted;
                return sl_status_ok();
            }
        }
    }
    {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan field type",
                                  sizeof("invalid app plan field type") - 1U),
            sl_plan_parse_literal("expected an integer for this schema constraint",
                                  sizeof("expected an integer for this schema constraint") - 1U));
    }
}

static SlStatus sl_plan_parse_schema_node(SlPlanParseContext* ctx, yyjson_val* value,
                                          SlPlanSchemaNode* out);

static SlStatus sl_plan_parse_schema_node_alloc(SlPlanParseContext* ctx, yyjson_val* value,
                                                SlPlanSchemaNode** out)
{
    SlSlice slice = {0};
    SlPlanSchemaNode* parsed = NULL;
    SlStatus status;

    if (ctx == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out = NULL;
    status = sl_arena_array_alloc(ctx->arena, 1U, sizeof(SlPlanSchemaNode),
                                  _Alignof(SlPlanSchemaNode), &slice);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (slice.ptr == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    parsed = (SlPlanSchemaNode*)slice.ptr;
    *parsed = (SlPlanSchemaNode){0};
    status = sl_plan_parse_schema_node(ctx, value, parsed);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out = parsed;
    return sl_status_ok();
}

static SlStatus sl_plan_parse_schema_properties(SlPlanParseContext* ctx, yyjson_val* object,
                                                SlPlanSchemaNode* out)
{
    yyjson_val* properties = yyjson_obj_get(object, "properties");
    yyjson_val* key = NULL;
    yyjson_val* value = NULL;
    yyjson_obj_iter iter;
    SlPlanSchemaProperty* parsed = NULL;
    size_t count = 0U;
    size_t index = 0U;
    SlStatus status;

    if (properties == NULL || yyjson_is_null(properties)) {
        return sl_status_ok();
    }
    if (!yyjson_is_obj(properties)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan field type",
                                  sizeof("invalid app plan field type") - 1U),
            sl_plan_parse_literal("object schema properties must be a JSON object",
                                  sizeof("object schema properties must be a JSON object") - 1U));
    }

    count = yyjson_obj_size(properties);
    if (count == 0U) {
        return sl_status_ok();
    }

    status = sl_plan_parse_alloc_array(ctx, count, sizeof(SlPlanSchemaProperty),
                                       _Alignof(SlPlanSchemaProperty), (void**)&parsed);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    yyjson_obj_iter_init(properties, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL) {
        SlPlanSchemaNode* child = NULL;

        value = yyjson_obj_iter_get_val(key);
        parsed[index] = (SlPlanSchemaProperty){0};
        status = sl_plan_parse_copy_str(ctx->arena,
                                        sl_str_from_parts(yyjson_get_str(key), yyjson_get_len(key)),
                                        &parsed[index].name);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_parse_schema_node_alloc(ctx, value, &child);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        parsed[index].schema = child;
        index += 1U;
    }

    out->properties = parsed;
    out->property_count = count;
    return sl_status_ok();
}

static SlStatus sl_plan_parse_schema_array_items(SlPlanParseContext* ctx, yyjson_val* object,
                                                 SlPlanSchemaNode* out)
{
    yyjson_val* items = yyjson_obj_get(object, "items");
    SlPlanSchemaNode* parsed = NULL;
    SlStatus status;

    if (items == NULL || yyjson_is_null(items)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("missing schema array item metadata",
                                  sizeof("missing schema array item metadata") - 1U),
            sl_plan_parse_literal("array schemas must include an items schema",
                                  sizeof("array schemas must include an items schema") - 1U));
    }

    status = sl_plan_parse_schema_node_alloc(ctx, items, &parsed);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    out->items = parsed;
    return sl_status_ok();
}

static SlStatus sl_plan_parse_schema_variants(SlPlanParseContext* ctx, yyjson_val* object,
                                              SlPlanSchemaNode* out)
{
    yyjson_val* variants = yyjson_obj_get(object, "variants");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;
    SlPlanSchemaNode* parsed = NULL;
    size_t count = 0U;
    size_t index = 0U;
    SlStatus status;

    if (variants == NULL || !yyjson_is_arr(variants)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan field type",
                                  sizeof("invalid app plan field type") - 1U),
            sl_plan_parse_literal("literalUnion schemas must include a variants array",
                                  sizeof("literalUnion schemas must include a variants array") -
                                      1U));
    }

    count = yyjson_arr_size(variants);
    if (count == 0U) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("missing literal union variants",
                                  sizeof("missing literal union variants") - 1U),
            sl_plan_parse_literal("literalUnion schemas must include at least one variant",
                                  sizeof("literalUnion schemas must include at least one variant") -
                                      1U));
    }

    status = sl_plan_parse_alloc_array(ctx, count, sizeof(SlPlanSchemaNode),
                                       _Alignof(SlPlanSchemaNode), (void**)&parsed);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    yyjson_arr_iter_init(variants, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        parsed[index] = (SlPlanSchemaNode){0};
        status = sl_plan_parse_schema_node(ctx, value, &parsed[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        index += 1U;
    }

    out->variants = parsed;
    out->variant_count = count;
    return sl_status_ok();
}

static SlStatus sl_plan_parse_schema_literal(SlPlanParseContext* ctx, yyjson_val* object,
                                             SlPlanSchemaNode* out)
{
    yyjson_val* value = yyjson_obj_get(object, "value");

    if (value == NULL) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("missing literal schema value",
                                  sizeof("missing literal schema value") - 1U),
            sl_plan_parse_literal("literal schemas must include a value",
                                  sizeof("literal schemas must include a value") - 1U));
    }

    if (yyjson_is_str(value)) {
        out->literal_kind = SL_PLAN_SCHEMA_LITERAL_STRING;
        return sl_plan_parse_copy_str(
            ctx->arena, sl_str_from_parts(yyjson_get_str(value), yyjson_get_len(value)),
            &out->literal_string);
    }
    if (yyjson_is_num(value)) {
        out->literal_kind = SL_PLAN_SCHEMA_LITERAL_NUMBER;
        out->literal_number = yyjson_get_num(value);
        return sl_status_ok();
    }
    if (yyjson_is_bool(value)) {
        out->literal_kind = SL_PLAN_SCHEMA_LITERAL_BOOLEAN;
        out->literal_boolean = yyjson_get_bool(value);
        return sl_status_ok();
    }

    return sl_plan_parse_field_diag(
        ctx,
        sl_plan_parse_literal("unsupported literal schema value",
                              sizeof("unsupported literal schema value") - 1U),
        sl_plan_parse_literal("literal schemas support string, number, and boolean values",
                              sizeof("literal schemas support string, number, and boolean values") -
                                  1U));
}

static SlStatus sl_plan_parse_schema_node(SlPlanParseContext* ctx, yyjson_val* value,
                                          SlPlanSchemaNode* out)
{
    SlStr kind = {0};
    SlStatus status;

    if (!yyjson_is_obj(value)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan field type",
                                  sizeof("invalid app plan field type") - 1U),
            sl_plan_parse_literal("schema definitions must be JSON objects",
                                  sizeof("schema definitions must be JSON objects") - 1U));
    }

    status = sl_plan_parse_require_string(ctx, value, "kind", true, &kind);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    out->kind = sl_plan_parse_schema_kind(kind);
    if (out->kind == SL_PLAN_SCHEMA_UNKNOWN) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("unsupported app plan schema kind",
                                  sizeof("unsupported app plan schema kind") - 1U),
            sl_plan_parse_literal("rebuild the app with schema metadata supported by this runtime",
                                  sizeof("rebuild the app with schema metadata supported by this "
                                         "runtime") -
                                      1U));
    }

    status = sl_plan_parse_bool_field(ctx, value, "optional", false, &out->optional);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_parse_bool_field(ctx, value, "nullable", false, &out->nullable);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_parse_bool_field(ctx, value, "secret", false, &out->secret);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_parse_optional_string(ctx, value, "semantic", false, &out->semantic);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_parse_optional_string(ctx, value, "validation", false, &out->validation);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_parse_i64_field(ctx, value, "min", &out->has_min, &out->min_value);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (out->kind == SL_PLAN_SCHEMA_OBJECT) {
        return sl_plan_parse_schema_properties(ctx, value, out);
    }
    if (out->kind == SL_PLAN_SCHEMA_ARRAY) {
        return sl_plan_parse_schema_array_items(ctx, value, out);
    }
    if (out->kind == SL_PLAN_SCHEMA_LITERAL_UNION) {
        return sl_plan_parse_schema_variants(ctx, value, out);
    }
    if (out->kind == SL_PLAN_SCHEMA_LITERAL) {
        return sl_plan_parse_schema_literal(ctx, value, out);
    }

    return sl_status_ok();
}

static SlStatus sl_plan_parse_one_schema(SlPlanParseContext* ctx, yyjson_val* value,
                                         SlPlanSchema* out)
{
    yyjson_val* definition = NULL;
    SlStatus status;

    if (!yyjson_is_obj(value)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan field type",
                                  sizeof("invalid app plan field type") - 1U),
            sl_plan_parse_literal("each schemas entry must be a JSON object",
                                  sizeof("each schemas entry must be a JSON object") - 1U));
    }

    status = sl_plan_parse_require_string(ctx, value, "name", true, &out->name);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    definition = yyjson_obj_get(value, "definition");
    if (definition == NULL || !yyjson_is_obj(definition)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan schema definition",
                                  sizeof("invalid app plan schema definition") - 1U),
            sl_plan_parse_literal("schemas[].definition must be a JSON object",
                                  sizeof("schemas[].definition must be a JSON object") - 1U));
    }

    return sl_plan_parse_schema_node(ctx, definition, &out->definition);
}

static SlStatus sl_plan_parse_schemas(SlPlanParseContext* ctx, yyjson_val* root, SlPlan* out)
{
    yyjson_val* schemas = yyjson_obj_get(root, "schemas");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;
    SlPlanSchema* parsed = NULL;
    size_t count = 0U;
    size_t index = 0U;
    SlStatus status;

    if (schemas == NULL || yyjson_is_null(schemas)) {
        return sl_status_ok();
    }
    if (!yyjson_is_arr(schemas)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan field type",
                                  sizeof("invalid app plan field type") - 1U),
            sl_plan_parse_literal("schemas must be a JSON array",
                                  sizeof("schemas must be a JSON array") - 1U));
    }

    count = yyjson_arr_size(schemas);
    if (count == 0U) {
        return sl_status_ok();
    }

    status = sl_plan_parse_alloc_array(ctx, count, sizeof(SlPlanSchema), _Alignof(SlPlanSchema),
                                       (void**)&parsed);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    yyjson_arr_iter_init(schemas, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        parsed[index] = (SlPlanSchema){0};
        status = sl_plan_parse_one_schema(ctx, value, &parsed[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        index += 1U;
    }

    out->schemas = parsed;
    out->schema_count = count;
    return sl_status_ok();
}

static bool sl_plan_parse_has_schema_named(const SlPlan* plan, SlStr name)
{
    size_t index = 0U;

    if (plan == NULL || sl_str_is_empty(name) ||
        (plan->schema_count != 0U && plan->schemas == NULL))
    {
        return false;
    }
    for (index = 0U; index < plan->schema_count; index += 1U) {
        if (sl_str_equal(plan->schemas[index].name, name)) {
            return true;
        }
    }
    return false;
}

static SlStatus sl_plan_parse_validate_schema_names(SlPlanParseContext* ctx, const SlPlan* plan)
{
    size_t left = 0U;
    size_t right = 0U;

    if (plan == NULL || plan->schema_count == 0U) {
        return sl_status_ok();
    }
    if (plan->schemas == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    for (left = 0U; left < plan->schema_count; left += 1U) {
        for (right = left + 1U; right < plan->schema_count; right += 1U) {
            if (sl_str_equal(plan->schemas[left].name, plan->schemas[right].name)) {
                return sl_plan_parse_field_diag(
                    ctx,
                    sl_plan_parse_literal("duplicate app plan schema name",
                                          sizeof("duplicate app plan schema name") - 1U),
                    sl_plan_parse_literal("schemas[].name values must be unique",
                                          sizeof("schemas[].name values must be unique") - 1U));
            }
        }
    }
    return sl_status_ok();
}

static SlStatus sl_plan_parse_validate_schema_references(SlPlanParseContext* ctx,
                                                         const SlPlan* plan)
{
    size_t route_index = 0U;

    if (plan == NULL || plan->route_count == 0U || plan->schema_count == 0U) {
        return sl_status_ok();
    }
    if (plan->routes == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    for (route_index = 0U; route_index < plan->route_count; route_index += 1U) {
        const SlPlanRoute* route = &plan->routes[route_index];
        size_t binding_index = 0U;

        if (route->binding_count != 0U && route->bindings == NULL) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        for (binding_index = 0U; binding_index < route->binding_count; binding_index += 1U) {
            const SlPlanRequestBinding* binding = &route->bindings[binding_index];

            if (binding->kind != SL_PLAN_REQUEST_BINDING_BODY_JSON) {
                continue;
            }
            if (sl_str_is_empty(binding->schema)) {
                continue;
            }
            if (!sl_plan_parse_has_schema_named(plan, binding->schema)) {
                return sl_plan_parse_field_diag(
                    ctx,
                    sl_plan_parse_literal("app plan route binding references missing schema",
                                          sizeof("app plan route binding references missing "
                                                 "schema") -
                                              1U),
                    sl_plan_parse_literal("body.json bindings must reference schemas[].name",
                                          sizeof("body.json bindings must reference "
                                                 "schemas[].name") -
                                              1U));
            }
        }
    }
    return sl_status_ok();
}

static SlStatus sl_plan_parse_one_provider(SlPlanParseContext* ctx, yyjson_val* value,
                                           SlPlanDataProvider* out)
{
    SlStatus status;

    if (!yyjson_is_obj(value)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan field type",
                                  sizeof("invalid app plan field type") - 1U),
            sl_plan_parse_literal("each dataProviders entry must be a JSON object",
                                  sizeof("each dataProviders entry must be a JSON object") - 1U));
    }

    status = sl_plan_parse_reject_secret_fields(ctx, value);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_plan_parse_require_string(ctx, value, "token", true, &out->token);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!sl_plan_parse_token_syntax_valid(out->token)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan provider token",
                                  sizeof("invalid app plan provider token") - 1U),
            sl_plan_parse_literal("provider tokens may contain letters, digits, '.', '_', and '-'",
                                  sizeof("provider tokens may contain letters, digits, '.', '_', "
                                         "and '-'") -
                                      1U));
    }

    status = sl_plan_parse_require_string(ctx, value, "provider", true, &out->provider);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!sl_plan_provider_supported(out->provider)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("unsupported app plan provider",
                                  sizeof("unsupported app plan provider") - 1U),
            sl_plan_parse_literal("supported provider values are sqlite, postgres, and sqlserver",
                                  sizeof("supported provider values are sqlite, postgres, and "
                                         "sqlserver") -
                                      1U));
    }

    status = sl_plan_parse_optional_string(ctx, value, "capability", true, &out->capability);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_parse_optional_string(ctx, value, "service", true, &out->service);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_plan_parse_optional_string(ctx, value, "database", true, &out->database);
}

static SlStatus sl_plan_parse_data_providers(SlPlanParseContext* ctx, yyjson_val* root, SlPlan* out)
{
    yyjson_val* providers = yyjson_obj_get(root, "dataProviders");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;
    SlPlanDataProvider* parsed = NULL;
    size_t count = 0U;
    size_t index = 0U;
    SlStatus status;

    if (providers == NULL) {
        return sl_status_ok();
    }
    if (!yyjson_is_arr(providers)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan field type",
                                  sizeof("invalid app plan field type") - 1U),
            sl_plan_parse_literal("dataProviders must be a JSON array",
                                  sizeof("dataProviders must be a JSON array") - 1U));
    }

    count = yyjson_arr_size(providers);
    if (count == 0U) {
        return sl_status_ok();
    }
    status = sl_plan_parse_alloc_array(ctx, count, sizeof(SlPlanDataProvider),
                                       _Alignof(SlPlanDataProvider), (void**)&parsed);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    yyjson_arr_iter_init(providers, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        parsed[index] = (SlPlanDataProvider){0};
        status = sl_plan_parse_one_provider(ctx, value, &parsed[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        index += 1U;
    }

    out->data_providers = parsed;
    out->data_provider_count = count;
    if (sl_plan_has_duplicate_data_provider_tokens(out)) {
        return sl_plan_parse_set_diag(
            ctx, SL_DIAG_INVALID_PLAN_FIELD,
            sl_plan_parse_literal("duplicate app plan provider token",
                                  sizeof("duplicate app plan provider token") - 1U),
            sl_plan_parse_literal("data provider tokens must be unique",
                                  sizeof("data provider tokens must be unique") - 1U));
    }

    return sl_status_ok();
}

static bool sl_plan_parse_provider_token_exists(const SlPlan* plan, SlStr token)
{
    size_t index = 0U;

    if (plan == NULL || sl_str_is_empty(token)) {
        return true;
    }
    for (index = 0U; index < plan->data_provider_count; index += 1U) {
        if (sl_str_equal(plan->data_providers[index].token, token)) {
            return true;
        }
    }
    return false;
}

static SlStatus sl_plan_parse_one_capability(SlPlanParseContext* ctx, const SlPlan* plan,
                                             yyjson_val* value, SlPlanCapability* out)
{
    SlStatus status;

    if (!yyjson_is_obj(value)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan field type",
                                  sizeof("invalid app plan field type") - 1U),
            sl_plan_parse_literal("each capabilities entry must be a JSON object",
                                  sizeof("each capabilities entry must be a JSON object") - 1U));
    }

    status = sl_plan_parse_reject_secret_fields(ctx, value);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_plan_parse_require_string(ctx, value, "token", true, &out->token);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!sl_plan_parse_token_syntax_valid(out->token)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan capability token",
                                  sizeof("invalid app plan capability token") - 1U),
            sl_plan_parse_literal(
                "capability tokens may contain letters, digits, '.', '_', and '-'",
                sizeof("capability tokens may contain letters, digits, '.', '_', and '-'") - 1U));
    }

    status = sl_plan_parse_require_string(ctx, value, "kind", true, &out->kind);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!sl_plan_capability_kind_supported(out->kind)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("unsupported app plan capability kind",
                                  sizeof("unsupported app plan capability kind") - 1U),
            sl_plan_parse_literal(
                "supported capability kinds are database, filesystem, network, queue, os, env, "
                "process, and signals",
                sizeof("supported capability kinds are database, filesystem, network, queue, os, "
                       "env, process, and signals") -
                    1U));
    }

    status = sl_plan_parse_require_string(ctx, value, "access", true, &out->access);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!sl_plan_capability_access_supported(out->kind, out->access)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("unsupported app plan capability access",
                                  sizeof("unsupported app plan capability access") - 1U),
            sl_plan_parse_literal("capability access must match its capability kind",
                                  sizeof("capability access must match its capability kind") - 1U));
    }

    status = sl_plan_parse_optional_string(ctx, value, "provider", true, &out->provider);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (sl_str_equal(out->kind, sl_str_from_cstr("database")) && sl_str_is_empty(out->provider)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("database capability is missing provider",
                                  sizeof("database capability is missing provider") - 1U),
            sl_plan_parse_literal("database capabilities require capabilities[].provider",
                                  sizeof("database capabilities require capabilities[].provider") -
                                      1U));
    }
    if (!sl_str_equal(out->kind, sl_str_from_cstr("database")) && !sl_str_is_empty(out->provider)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("non-database capability has provider",
                                  sizeof("non-database capability has provider") - 1U),
            sl_plan_parse_literal("only database capabilities may reference dataProviders",
                                  sizeof("only database capabilities may reference dataProviders") -
                                      1U));
    }
    if (!sl_plan_parse_provider_token_exists(plan, out->provider)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("app plan capability references missing provider",
                                  sizeof("app plan capability references missing provider") - 1U),
            sl_plan_parse_literal("capabilities[].provider must reference dataProviders[].token",
                                  sizeof("capabilities[].provider must reference "
                                         "dataProviders[].token") -
                                      1U));
    }

    return sl_status_ok();
}

static SlStatus sl_plan_parse_capabilities(SlPlanParseContext* ctx, yyjson_val* root, SlPlan* out)
{
    yyjson_val* capabilities = yyjson_obj_get(root, "capabilities");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;
    SlPlanCapability* parsed = NULL;
    size_t count = 0U;
    size_t index = 0U;
    SlStatus status;

    if (capabilities == NULL) {
        return sl_status_ok();
    }
    if (!yyjson_is_arr(capabilities)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan field type",
                                  sizeof("invalid app plan field type") - 1U),
            sl_plan_parse_literal("capabilities must be a JSON array",
                                  sizeof("capabilities must be a JSON array") - 1U));
    }

    count = yyjson_arr_size(capabilities);
    if (count == 0U) {
        return sl_status_ok();
    }
    status = sl_plan_parse_alloc_array(ctx, count, sizeof(SlPlanCapability),
                                       _Alignof(SlPlanCapability), (void**)&parsed);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    yyjson_arr_iter_init(capabilities, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        parsed[index] = (SlPlanCapability){0};
        status = sl_plan_parse_one_capability(ctx, out, value, &parsed[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        index += 1U;
    }

    out->capabilities = parsed;
    out->capability_count = count;
    if (sl_plan_has_duplicate_capability_tokens(out)) {
        return sl_plan_parse_set_diag(
            ctx, SL_DIAG_INVALID_PLAN_FIELD,
            sl_plan_parse_literal("duplicate app plan capability token",
                                  sizeof("duplicate app plan capability token") - 1U),
            sl_plan_parse_literal("capability tokens must be unique",
                                  sizeof("capability tokens must be unique") - 1U));
    }

    return sl_status_ok();
}

static bool sl_plan_parse_capability_token_exists(const SlPlan* plan, SlStr token)
{
    size_t index = 0U;

    if (plan == NULL || sl_str_is_empty(token)) {
        return true;
    }
    for (index = 0U; index < plan->capability_count; index += 1U) {
        if (sl_str_equal(plan->capabilities[index].token, token)) {
            return true;
        }
    }
    return false;
}

static SlStatus sl_plan_parse_validate_provider_capabilities(SlPlanParseContext* ctx,
                                                             const SlPlan* plan)
{
    size_t index = 0U;

    for (index = 0U; index < plan->data_provider_count; index += 1U) {
        if (!sl_plan_parse_capability_token_exists(plan, plan->data_providers[index].capability)) {
            return sl_plan_parse_field_diag(
                ctx,
                sl_plan_parse_literal("app plan provider references missing capability",
                                      sizeof("app plan provider references missing capability") -
                                          1U),
                sl_plan_parse_literal(
                    "dataProviders[].capability must reference capabilities[].token",
                    sizeof("dataProviders[].capability must reference "
                           "capabilities[].token") -
                        1U));
        }
    }

    return sl_status_ok();
}

static SlStatus sl_plan_parse_document(SlPlanParseContext* ctx, yyjson_val* root, SlPlan* out)
{
    SlStatus status;

    if (!yyjson_is_obj(root)) {
        return sl_plan_parse_field_diag(
            ctx,
            sl_plan_parse_literal("invalid app plan field type",
                                  sizeof("invalid app plan field type") - 1U),
            sl_plan_parse_literal("app plan root must be a JSON object",
                                  sizeof("app plan root must be a JSON object") - 1U));
    }

    status = sl_plan_parse_schema_version(ctx, root, &out->version);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_plan_parse_required_features(ctx, root, out);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status =
        sl_plan_parse_require_string(ctx, root, "compilerVersion", true, &out->compiler_version);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_plan_parse_require_string(ctx, root, "runtimeMinimumVersion", true,
                                          &out->runtime_min_version);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_plan_parse_require_string(ctx, root, "stdlibVersion", true, &out->stdlib_version);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_plan_parse_target(ctx, root, &out->target);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_plan_parse_bundle(ctx, root, &out->bundle);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_plan_parse_source_map(ctx, root, &out->source_map);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_plan_parse_handlers(ctx, root, out);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_plan_parse_routes(ctx, root, out);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_plan_parse_schemas(ctx, root, out);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_parse_validate_schema_names(ctx, out);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_parse_validate_schema_references(ctx, out);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_plan_parse_data_providers(ctx, root, out);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_plan_parse_capabilities(ctx, root, out);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_plan_parse_validate_provider_capabilities(ctx, out);
}

SlStatus sl_plan_parse_json(SlArena* arena, SlBytes json, const SlPlanParseOptions* options,
                            SlPlan* out_plan, SlDiag* out_diag)
{
    SlPlanParseContext ctx = {0};
    SlArenaMark mark = {0};
    yyjson_read_err error = {0};
    yyjson_doc* doc = NULL;
    yyjson_val* root = NULL;
    SlPlan parsed = {0};
    SlStatus status;
    SlStatus reset_status;
    SlStatus diag_status;

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }

    if (out_plan == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_plan = (SlPlan){0};

    if (arena == NULL || json.ptr == NULL || json.length == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    ctx.arena = arena;
    ctx.options = options;
    ctx.out_diag = out_diag;
    ctx.diag_code = SL_DIAG_NONE;
    mark = sl_arena_mark(arena);

    doc = yyjson_read_opts((char*)json.ptr, json.length, 0U, NULL, &error);
    if (doc == NULL) {
        status = sl_plan_parse_set_diag(
            &ctx, SL_DIAG_MALFORMED_JSON,
            sl_plan_parse_literal("malformed app plan JSON",
                                  sizeof("malformed app plan JSON") - 1U),
            sl_plan_parse_literal("provide strict JSON bytes for app.plan.json",
                                  sizeof("provide strict JSON bytes for app.plan.json") - 1U));
        goto failure;
    }

    root = yyjson_doc_get_root(doc);
    status = sl_plan_parse_document(&ctx, root, &parsed);

    yyjson_doc_free(doc);

    if (!sl_status_is_ok(status)) {
        goto failure;
    }

    *out_plan = parsed;
    return sl_status_ok();

failure:
    *out_plan = (SlPlan){0};

    reset_status = sl_arena_reset_to(arena, mark);
    if (!sl_status_is_ok(reset_status)) {
        return reset_status;
    }

    diag_status = sl_plan_parse_finish_diag(&ctx);
    if (!sl_status_is_ok(diag_status)) {
        return diag_status;
    }

    return status;
}
