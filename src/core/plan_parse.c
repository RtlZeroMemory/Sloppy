/*
 * src/core/plan_parse.c
 *
 * Parses and validates Sloppy's minimal handwritten Plan v1 JSON contract. This module is
 * intentionally narrow: it turns caller-provided bytes into arena-owned SlPlan data and
 * reports shape diagnostics. It does not perform file I/O, hash verification, source-map
 * parsing, runtime compatibility checks, engine loading, route handling, or compiler work.
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

#include <stdint.h>
#include <yyjson.h>

typedef struct SlPlanParseContext
{
    SlArena* arena;
    const SlPlanParseOptions* options;
    SlDiag* out_diag;
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
    SlDiagBuilder builder;
    SlStatus status;

    if (ctx == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (ctx->out_diag == NULL) {
        return sl_status_from_code(code == SL_DIAG_INVALID_PLAN_VERSION
                                       ? SL_STATUS_UNSUPPORTED
                                       : SL_STATUS_INVALID_ARGUMENT);
    }

    *ctx->out_diag = (SlDiag){0};

    status = sl_diag_builder_init(&builder, ctx->arena, SL_DIAG_SEVERITY_ERROR, code, message);
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

    if (!sl_str_is_empty(hint)) {
        status = sl_diag_builder_add_hint(&builder, hint);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    status = sl_diag_builder_finish(&builder, ctx->out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_status_from_code(code == SL_DIAG_INVALID_PLAN_VERSION ? SL_STATUS_UNSUPPORTED
                                                                    : SL_STATUS_INVALID_ARGUMENT);
}

static SlStatus sl_plan_parse_field_diag(SlPlanParseContext* ctx, SlStr message, SlStr hint)
{
    return sl_plan_parse_set_diag(ctx, SL_DIAG_INVALID_PLAN_FIELD, message, hint);
}

static SlStatus sl_plan_parse_copy_str(SlArena* arena, SlStr src, SlStr* out)
{
    void* ptr = NULL;
    char* dst = NULL;
    size_t index = 0U;
    SlStatus status;

    if (arena == NULL || out == NULL || !sl_plan_parse_str_valid(src)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (src.length == 0U) {
        *out = sl_str_empty();
        return sl_status_ok();
    }

    status = sl_arena_alloc(arena, src.length, 1U, &ptr);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    dst = (char*)ptr;
    for (index = 0U; index < src.length; index += 1U) {
        dst[index] = src.ptr[index];
    }

    *out = sl_str_from_parts(dst, src.length);
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

    return sl_plan_parse_handlers(ctx, root, out);
}

SlStatus sl_plan_parse_json(SlArena* arena, SlBytes json, const SlPlanParseOptions* options,
                            SlPlan* out_plan, SlDiag* out_diag)
{
    SlPlanParseContext ctx = {0};
    yyjson_read_err error = {0};
    yyjson_doc* doc = NULL;
    yyjson_val* root = NULL;
    SlPlan parsed = {0};
    SlStatus status;

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

    doc = yyjson_read_opts((char*)json.ptr, json.length, 0U, NULL, &error);
    if (doc == NULL) {
        return sl_plan_parse_set_diag(
            &ctx, SL_DIAG_MALFORMED_JSON,
            sl_plan_parse_literal("malformed app plan JSON",
                                  sizeof("malformed app plan JSON") - 1U),
            sl_plan_parse_literal("provide strict JSON bytes for app.plan.json",
                                  sizeof("provide strict JSON bytes for app.plan.json") - 1U));
    }

    root = yyjson_doc_get_root(doc);
    status = sl_plan_parse_document(&ctx, root, &parsed);

    yyjson_doc_free(doc);

    if (!sl_status_is_ok(status)) {
        *out_plan = (SlPlan){0};
        return status;
    }

    *out_plan = parsed;
    return sl_status_ok();
}
