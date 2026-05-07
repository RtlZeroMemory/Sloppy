#ifndef SLOPPY_PLAN_H
#define SLOPPY_PLAN_H

#include "sloppy/arena.h"
#include "sloppy/bytes.h"
#include "sloppy/diagnostics.h"
#include "sloppy/intern.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_PLAN_VERSION_1 UINT32_C(1)
#define SL_PLAN_CURRENT_VERSION SL_PLAN_VERSION_1

#define SL_PLAN_TARGET_PLATFORM_WINDOWS_X64 "windows-x64"
#define SL_PLAN_TARGET_PLATFORM_LINUX_X64 "linux-x64"
#define SL_PLAN_TARGET_PLATFORM_MACOS_X64 "macos-x64"
#define SL_PLAN_TARGET_ENGINE_V8 "v8"
#define SL_PLAN_RUNTIME_MIN_VERSION_0_1_0 "0.1.0"
#define SL_PLAN_STDLIB_VERSION_0_1_0 "0.1.0"

typedef uint32_t SlHandlerId;

#define SL_HANDLER_ID_INVALID UINT32_C(0)

/*
 * Sloppy Plan v1 is the minimal native representation of `app.plan.json`.
 *
 * This header defines the borrowed, allocation-free runtime contract shape only.
 * sl_plan_parse_json owns JSON parsing, copied storage, and validation diagnostics.
 * Artifact file I/O and hash verification stay with the loader path that has artifact
 * bytes available.
 *
 * All SlStr fields below are borrowed views. They do not require NUL termination and remain
 * valid only for the caller-documented plan lifetime. The handler, route, route-binding,
 * schema, provider, capability, and SlPlanRequiredFeature arrays exposed through SlPlan are
 * borrowed and caller-owned; SlPlan never allocates, copies, or frees them.
 */
typedef struct SlPlanTarget
{
    SlStr platform;
    SlStr engine;
} SlPlanTarget;

typedef struct SlPlanBundle
{
    SlStr path;
    SlStr id;
    SlStr hash;
} SlPlanBundle;

typedef struct SlPlanSourceMap
{
    SlStr path;
    SlStr id;
    SlStr hash;
} SlPlanSourceMap;

typedef struct SlPlanHandler
{
    SlHandlerId id;
    SlStr export_name;
    SlStr display_name;
} SlPlanHandler;

typedef struct SlPlanRoute
{
    SlStr method;
    SlStr pattern;
    SlHandlerId handler_id;
    SlStr name;
    const struct SlPlanRequestBinding* bindings;
    size_t binding_count;
} SlPlanRoute;

typedef enum SlPlanRequestBindingKind
{
    SL_PLAN_REQUEST_BINDING_UNKNOWN = 0,
    SL_PLAN_REQUEST_BINDING_ROUTE = 1,
    SL_PLAN_REQUEST_BINDING_QUERY = 2,
    SL_PLAN_REQUEST_BINDING_BODY_JSON = 3,
    SL_PLAN_REQUEST_BINDING_HEADER = 4,
    SL_PLAN_REQUEST_BINDING_CONTEXT = 5,
    SL_PLAN_REQUEST_BINDING_INJECTION = 6
} SlPlanRequestBindingKind;

typedef struct SlPlanRequestBinding
{
    SlPlanRequestBindingKind kind;
    SlStr parameter;
    SlStr name;
    SlStr schema;
    SlStr type;
    bool redacted;
} SlPlanRequestBinding;

typedef enum SlPlanSchemaKind
{
    SL_PLAN_SCHEMA_UNKNOWN = 0,
    SL_PLAN_SCHEMA_OBJECT = 1,
    SL_PLAN_SCHEMA_STRING = 2,
    SL_PLAN_SCHEMA_NUMBER = 3,
    SL_PLAN_SCHEMA_BOOLEAN = 4,
    SL_PLAN_SCHEMA_INT = 5,
    SL_PLAN_SCHEMA_ARRAY = 6,
    SL_PLAN_SCHEMA_LITERAL_UNION = 7,
    SL_PLAN_SCHEMA_LITERAL = 8,
    SL_PLAN_SCHEMA_NULL = 9
} SlPlanSchemaKind;

typedef enum SlPlanSchemaLiteralKind
{
    SL_PLAN_SCHEMA_LITERAL_NONE = 0,
    SL_PLAN_SCHEMA_LITERAL_STRING = 1,
    SL_PLAN_SCHEMA_LITERAL_NUMBER = 2,
    SL_PLAN_SCHEMA_LITERAL_BOOLEAN = 3
} SlPlanSchemaLiteralKind;

typedef struct SlPlanSchemaNode SlPlanSchemaNode;

typedef struct SlPlanSchemaProperty
{
    SlStr name;
    SlPlanSchemaNode* schema;
} SlPlanSchemaProperty;

typedef struct SlPlanSchemaNode
{
    SlPlanSchemaKind kind;
    bool optional;
    bool nullable;
    bool secret;
    bool has_min;
    int64_t min_value;
    SlStr semantic;
    SlStr validation;
    const SlPlanSchemaProperty* properties;
    size_t property_count;
    const SlPlanSchemaNode* items;
    const SlPlanSchemaNode* variants;
    size_t variant_count;
    SlPlanSchemaLiteralKind literal_kind;
    SlStr literal_string;
    double literal_number;
    bool literal_boolean;
} SlPlanSchemaNode;

typedef struct SlPlanSchema
{
    SlStr name;
    SlPlanSchemaNode definition;
} SlPlanSchema;

typedef struct SlPlanDataProvider
{
    SlStr token;
    SlStr provider;
    SlStr capability;
    SlStr service;
    SlStr database;
} SlPlanDataProvider;

typedef struct SlPlanCapability
{
    SlStr token;
    SlStr kind;
    SlStr access;
    SlStr provider;
} SlPlanCapability;

typedef struct SlPlanRequiredFeature
{
    SlStr id;
} SlPlanRequiredFeature;

typedef struct SlPlan
{
    uint32_t version;
    SlStr compiler_version;
    SlStr runtime_min_version;
    SlStr stdlib_version;
    SlPlanTarget target;
    SlPlanBundle bundle;
    SlPlanSourceMap source_map;
    const SlPlanHandler* handlers;
    size_t handler_count;
    const SlPlanRoute* routes;
    size_t route_count;
    const SlPlanSchema* schemas;
    size_t schema_count;
    const SlPlanDataProvider* data_providers;
    size_t data_provider_count;
    const SlPlanCapability* capabilities;
    size_t capability_count;
    const SlPlanRequiredFeature* required_features;
    size_t required_feature_count;
} SlPlan;

/*
 * Options for parsing caller-provided `app.plan.json` bytes.
 *
 * `source_name` is an optional borrowed name used in diagnostics. It is copied into
 * diagnostics when `out_diag` is provided and never affects parser ownership.
 */
typedef struct SlPlanParseOptions
{
    SlStr source_name;
} SlPlanParseOptions;

bool sl_plan_version_supported(uint32_t version);
bool sl_handler_id_valid(SlHandlerId id);
bool sl_plan_route_method_supported(SlStr method);
bool sl_plan_route_method_runnable(SlStr method);
bool sl_plan_provider_supported(SlStr provider);
bool sl_plan_capability_kind_supported(SlStr kind);
bool sl_plan_capability_access_supported(SlStr kind, SlStr access);

/*
 * Finds a handler by numeric runtime dispatch ID.
 *
 * `plan` and `out` are required. `id` must be valid; handler ID 0 is reserved/invalid. When
 * `handler_count` is nonzero, `plan->handlers` must point to at least that many caller-owned
 * entries. On success, `*out` is a borrowed pointer into `plan->handlers` and remains valid
 * for the handler array lifetime. On failure, `*out` is set to NULL.
 */
SlStatus sl_plan_find_handler_by_id(const SlPlan* plan, SlHandlerId id, const SlPlanHandler** out);

/*
 * Reports whether the borrowed handler table contains duplicate numeric dispatch IDs.
 *
 * Passing NULL, an empty table, or a nonzero count with a NULL handler array returns false
 * because there is no valid table to inspect. Future validation owns the separate malformed
 * table diagnostic.
 */
bool sl_plan_has_duplicate_handler_ids(const SlPlan* plan);
bool sl_plan_has_duplicate_routes(const SlPlan* plan);
bool sl_plan_has_duplicate_route_names(const SlPlan* plan);
bool sl_plan_has_duplicate_data_provider_tokens(const SlPlan* plan);
bool sl_plan_has_duplicate_capability_tokens(const SlPlan* plan);

/*
 * Builds an arena-owned copy of stable Plan metadata with selected strings interned.
 *
 * `arena`, `plan`, `out_plan`, and `out_table` are required. `capacity` and
 * `bucket_count` bound the app/static-lifetime intern table and must be nonzero. The
 * returned `out_plan` is a Plan-shaped view over arena-owned arrays and interned string
 * bytes. It remains valid until `arena` is reset or its caller-owned backing storage ends.
 *
 * The helper interns only stable metadata: version/target strings, artifact identifiers,
 * handler names, route methods/patterns/names, route binding names/types/schema references,
 * schema names/property names/semantic markers/literal values, provider/capability tokens,
 * provider names, service names, capability kind/access/provider metadata, and required
 * runtime feature identifiers. It intentionally does not intern artifact paths, hashes,
 * source-map paths, provider database names, request data, secrets, connection strings, or
 * transient diagnostics. Byte equality remains the correctness rule; intern symbols are only
 * a lookup/ownership aid.
 *
 * On failure, `out_plan` and `out_table` are left unchanged and allocations made by this
 * helper are rolled back to the entry arena mark. The input `plan` is never mutated unless
 * the caller also passes it as `out_plan`, in which case mutation happens only after all
 * interning work succeeds.
 */
SlStatus sl_plan_intern_metadata(SlArena* arena, const SlPlan* plan, size_t capacity,
                                 size_t bucket_count, SlPlan* out_plan, SlInternTable* out_table);

/*
 * Parses and validates minimal Sloppy Plan v1 JSON from caller-provided bytes.
 *
 * `arena`, `out_plan`, and non-empty `json` bytes are required. `options` and `out_diag`
 * may be NULL. The parser performs no file I/O, no platform calls, and no runtime
 * compatibility checks beyond the documented minimal shape.
 *
 * On success, parsed strings, arrays, and schema graphs stored in `*out_plan` are
 * arena-owned and remain valid until `arena` is reset or its caller-owned backing buffer
 * ends. On failure, `*out_plan` is cleared where practical. If `out_diag` is provided, it
 * receives an arena-owned diagnostic describing the failure.
 */
SlStatus sl_plan_parse_json(SlArena* arena, SlBytes json, const SlPlanParseOptions* options,
                            SlPlan* out_plan, SlDiag* out_diag);

#ifdef __cplusplus
}
#endif

#endif
