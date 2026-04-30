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

#include "sloppy/checked_math.h"
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
    SlOwnedStr copied = {0};
    SlStatus status;

    if (arena == NULL || out == NULL || !sl_plan_parse_str_valid(src)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_str_copy_to_arena(arena, src, &copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out = sl_owned_str_as_view(copied);
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
    void* ptr = NULL;
    size_t handler_count = 0U;
    size_t alloc_size = 0U;
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

    status = sl_checked_mul_size(handler_count, sizeof(SlPlanHandler), &alloc_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_arena_alloc(ctx->arena, alloc_size, _Alignof(SlPlanHandler), &ptr);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    parsed_handlers = (SlPlanHandler*)ptr;
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

static SlStatus sl_plan_parse_one_route(SlPlanParseContext* ctx, const SlPlan* plan,
                                        yyjson_val* value, SlPlanRoute* out)
{
    const SlPlanHandler* handler = NULL;
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
                "Plan v1 route metadata supports GET, POST, PUT, PATCH, and DELETE",
                sizeof("Plan v1 route metadata supports GET, POST, PUT, "
                       "PATCH, and DELETE") -
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

    return sl_plan_parse_optional_string(ctx, value, "name", true, &out->name);
}

static SlStatus sl_plan_parse_routes(SlPlanParseContext* ctx, yyjson_val* root, SlPlan* out)
{
    yyjson_val* routes = yyjson_obj_get(root, "routes");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;
    SlPlanRoute* parsed_routes = NULL;
    void* ptr = NULL;
    size_t route_count = 0U;
    size_t alloc_size = 0U;
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

    status = sl_checked_mul_size(route_count, sizeof(SlPlanRoute), &alloc_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(ctx->arena, alloc_size, _Alignof(SlPlanRoute), &ptr);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    parsed_routes = (SlPlanRoute*)ptr;
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
    void* ptr = NULL;
    size_t count = 0U;
    size_t alloc_size = 0U;
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
    status = sl_checked_mul_size(count, sizeof(SlPlanDataProvider), &alloc_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(ctx->arena, alloc_size, _Alignof(SlPlanDataProvider), &ptr);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    parsed = (SlPlanDataProvider*)ptr;
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
                "supported capability kinds are database, filesystem, and network",
                sizeof("supported capability kinds are database, filesystem, and "
                       "network") -
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
    void* ptr = NULL;
    size_t count = 0U;
    size_t alloc_size = 0U;
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
    status = sl_checked_mul_size(count, sizeof(SlPlanCapability), &alloc_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(ctx->arena, alloc_size, _Alignof(SlPlanCapability), &ptr);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    parsed = (SlPlanCapability*)ptr;
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
