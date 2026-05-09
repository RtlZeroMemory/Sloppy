#include "sloppy/plan.h"

#include <stdbool.h>
#include <stdio.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int fixture_exists(const char* path)
{
    FILE* file = NULL;

#ifdef _MSC_VER
    if (fopen_s(&file, path, "rb") != 0) {
        return 0;
    }
#else
    file = fopen(path, "rb");
#endif
    if (file == NULL) {
        return 0;
    }

    if (fclose(file) != 0) {
        return 0;
    }

    return 1;
}

static int test_version_helpers(void)
{
    if (expect_true(SL_PLAN_CURRENT_VERSION == SL_PLAN_VERSION_1) != 0) {
        return 1;
    }

    if (expect_true(sl_plan_version_supported(SL_PLAN_VERSION_1)) != 0) {
        return 2;
    }

    if (expect_true(!sl_plan_version_supported(0U)) != 0 ||
        expect_true(!sl_plan_version_supported(2U)) != 0)
    {
        return 3;
    }

    return 0;
}

static int test_handler_id_helpers(void)
{
    if (expect_true(!sl_handler_id_valid(SL_HANDLER_ID_INVALID)) != 0) {
        return 10;
    }

    if (expect_true(sl_handler_id_valid(1U)) != 0) {
        return 11;
    }

    return 0;
}

static int test_route_method_helpers(void)
{
    if (expect_true(sl_plan_route_method_supported(sl_str_from_cstr("GET"))) != 0 ||
        expect_true(sl_plan_route_method_runnable(sl_str_from_cstr("GET"))) != 0 ||
        expect_true(sl_plan_route_method_supported(sl_str_from_cstr("POST"))) != 0 ||
        expect_true(sl_plan_route_method_runnable(sl_str_from_cstr("POST"))) != 0 ||
        expect_true(sl_plan_route_method_supported(sl_str_from_cstr("PUT"))) != 0 ||
        expect_true(sl_plan_route_method_runnable(sl_str_from_cstr("PUT"))) != 0 ||
        expect_true(sl_plan_route_method_supported(sl_str_from_cstr("PATCH"))) != 0 ||
        expect_true(sl_plan_route_method_runnable(sl_str_from_cstr("PATCH"))) != 0 ||
        expect_true(sl_plan_route_method_supported(sl_str_from_cstr("DELETE"))) != 0 ||
        expect_true(sl_plan_route_method_runnable(sl_str_from_cstr("DELETE"))) != 0 ||
        expect_true(sl_plan_route_method_supported(sl_str_from_cstr("OPTIONS"))) != 0 ||
        expect_true(sl_plan_route_method_runnable(sl_str_from_cstr("OPTIONS"))) != 0)
    {
        return 15;
    }

    if (expect_true(!sl_plan_route_method_supported(sl_str_from_cstr("HEAD"))) != 0 ||
        expect_true(!sl_plan_route_method_runnable(sl_str_from_cstr("HEAD"))) != 0)
    {
        return 16;
    }

    return 0;
}

static int test_handler_lookup(void)
{
    SlPlanHandler handlers[2] = {
        {1U, sl_str_from_cstr("__sloppy_handler_1"), sl_str_from_cstr("Home")},
        {2U, sl_str_from_cstr("__sloppy_handler_2"), sl_str_from_cstr("Health")}};
    SlPlan plan = {0};
    const SlPlanHandler* found = NULL;

    plan.version = SL_PLAN_VERSION_1;
    plan.handlers = handlers;
    plan.handler_count = 2U;

    if (expect_status(sl_plan_find_handler_by_id(&plan, 2U, &found), SL_STATUS_OK) != 0) {
        return 20;
    }

    if (found != &handlers[1] || !sl_str_equal(found->export_name, handlers[1].export_name)) {
        return 21;
    }

    if (expect_status(sl_plan_find_handler_by_id(&plan, 3U, &found), SL_STATUS_OUT_OF_RANGE) != 0 ||
        found != NULL)
    {
        return 22;
    }

    return 0;
}

static int test_lookup_invalid_arguments(void)
{
    SlPlan plan = {0};
    const SlPlanHandler* found = (const SlPlanHandler*)1;

    if (expect_status(sl_plan_find_handler_by_id(NULL, 1U, &found), SL_STATUS_INVALID_ARGUMENT) !=
            0 ||
        found != NULL)
    {
        return 30;
    }

    if (expect_status(sl_plan_find_handler_by_id(&plan, SL_HANDLER_ID_INVALID, &found),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        found != NULL)
    {
        return 31;
    }

    if (expect_status(sl_plan_find_handler_by_id(&plan, 1U, NULL), SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 32;
    }

    plan.handler_count = 1U;
    plan.handlers = NULL;
    if (expect_status(sl_plan_find_handler_by_id(&plan, 1U, &found), SL_STATUS_INVALID_ARGUMENT) !=
            0 ||
        found != NULL)
    {
        return 33;
    }

    return 0;
}

static int test_empty_handler_table(void)
{
    SlPlan plan = {0};
    const SlPlanHandler* found = NULL;

    if (expect_status(sl_plan_find_handler_by_id(&plan, 1U, &found), SL_STATUS_OUT_OF_RANGE) != 0 ||
        found != NULL)
    {
        return 40;
    }

    if (sl_plan_has_duplicate_handler_ids(&plan)) {
        return 41;
    }

    return 0;
}

static int test_duplicate_handler_ids(void)
{
    SlPlanHandler unique_handlers[2] = {
        {1U, sl_str_from_cstr("__sloppy_handler_1"), sl_str_from_cstr("Home")},
        {2U, sl_str_from_cstr("__sloppy_handler_2"), sl_str_from_cstr("Health")}};
    SlPlanHandler duplicate_handlers[3] = {
        {1U, sl_str_from_cstr("__sloppy_handler_1"), sl_str_from_cstr("Home")},
        {2U, sl_str_from_cstr("__sloppy_handler_2"), sl_str_from_cstr("Health")},
        {1U, sl_str_from_cstr("__sloppy_handler_1_duplicate"), sl_str_from_cstr("Home duplicate")}};
    SlPlan plan = {0};

    plan.handlers = unique_handlers;
    plan.handler_count = 2U;
    if (sl_plan_has_duplicate_handler_ids(&plan)) {
        return 50;
    }

    plan.handlers = duplicate_handlers;
    plan.handler_count = 3U;
    if (!sl_plan_has_duplicate_handler_ids(&plan)) {
        return 51;
    }

    if (sl_plan_has_duplicate_handler_ids(NULL)) {
        return 52;
    }

    plan.handlers = NULL;
    plan.handler_count = 2U;
    if (sl_plan_has_duplicate_handler_ids(&plan)) {
        return 53;
    }

    return 0;
}

static void init_interning_test_plan(SlPlan* plan, SlPlanHandler* handlers, SlPlanRoute* routes,
                                     SlPlanDataProvider* providers, SlPlanCapability* capabilities,
                                     SlPlanRequiredFeature* required_features)
{
    *plan = (SlPlan){0};
    plan->version = SL_PLAN_VERSION_1;
    plan->compiler_version = sl_str_from_cstr("sloppyc-test");
    plan->runtime_min_version = sl_str_from_cstr(SL_PLAN_RUNTIME_MIN_VERSION_0_1_0);
    plan->stdlib_version = sl_str_from_cstr(SL_PLAN_STDLIB_VERSION_0_1_0);
    plan->target.platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    plan->target.engine = sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8);
    plan->bundle.path = sl_str_from_cstr("app.js");
    plan->bundle.id = sl_str_from_cstr("same-artifact-id");
    plan->bundle.hash = sl_str_from_cstr("sha256:not-interned");
    plan->source_map.path = sl_str_from_cstr("app.js.map");
    plan->source_map.id = sl_str_from_cstr("same-artifact-id");
    plan->source_map.hash = sl_str_from_cstr("sha256:not-interned");
    plan->handlers = handlers;
    plan->handler_count = 1U;
    plan->routes = routes;
    plan->route_count = 1U;
    plan->data_providers = providers;
    plan->data_provider_count = 1U;
    plan->capabilities = capabilities;
    plan->capability_count = 1U;
    plan->required_features = required_features;
    plan->required_feature_count = 1U;
}

static int test_plan_metadata_interns_stable_strings(void)
{
    unsigned char arena_storage[8192];
    SlArena arena = {0};
    SlPlanHandler handlers[1] = {
        {1U, sl_str_from_cstr("__sloppy_handler_1"), sl_str_from_cstr("home")}};
    SlPlanRequestBinding bindings[1] = {
        {SL_PLAN_REQUEST_BINDING_BODY_JSON, sl_str_from_cstr("input"), sl_str_from_cstr("input"),
         sl_str_from_cstr("UserCreate"), sl_str_from_cstr("Body<UserCreate>"), false}};
    SlPlanRoute routes[1] = {{
        .bindings = bindings,
        .binding_count = 1U,
        .method = sl_str_from_cstr("GET"),
        .pattern = sl_str_from_cstr("/"),
        .handler_id = 1U,
        .name = sl_str_from_cstr("home"),
    }};
    SlPlanSchemaNode property_node = {.kind = SL_PLAN_SCHEMA_STRING,
                                      .validation = sl_str_from_cstr("email")};
    SlPlanSchemaProperty properties[1] = {{sl_str_from_cstr("email"), &property_node}};
    SlPlanSchema schemas[1] = {
        {sl_str_from_cstr("UserCreate"),
         {.kind = SL_PLAN_SCHEMA_OBJECT, .properties = properties, .property_count = 1U}}};
    SlPlanDataProvider providers[1] = {
        {sl_str_from_cstr("main.db"), sl_str_from_cstr("sqlite"), sl_str_from_cstr("main.db.read"),
         sl_str_from_cstr("db.service"), sl_str_from_cstr("sensitive-ish-database-name")}};
    SlPlanCapability capabilities[1] = {{sl_str_from_cstr("main.db.read"),
                                         sl_str_from_cstr("database"), sl_str_from_cstr("read"),
                                         sl_str_from_cstr("main.db")}};
    SlPlanRequiredFeature required_features[1] = {{sl_str_from_cstr("provider.sqlite")}};
    SlPlan plan = {0};
    SlPlan interned_plan = {0};
    SlInternTable table = {0};
    SlStatus status;

    init_interning_test_plan(&plan, handlers, routes, providers, capabilities, required_features);
    plan.schemas = schemas;
    plan.schema_count = 1U;

    status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
    if (!sl_status_is_ok(status)) {
        return 70;
    }

    status = sl_plan_intern_metadata(&arena, &plan, 64U, 32U, &interned_plan, &table);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 71;
    }

    if (sl_intern_table_count(&table) == 0U) {
        return 72;
    }
    if (interned_plan.source_map.id.ptr != interned_plan.bundle.id.ptr) {
        return 73;
    }
    if (interned_plan.routes[0].name.ptr != interned_plan.handlers[0].display_name.ptr) {
        return 74;
    }
    if (interned_plan.capabilities[0].provider.ptr != interned_plan.data_providers[0].token.ptr) {
        return 75;
    }
    if (interned_plan.capabilities[0].token.ptr != interned_plan.data_providers[0].capability.ptr) {
        return 76;
    }
    if (interned_plan.data_providers[0].database.ptr != plan.data_providers[0].database.ptr) {
        return 77;
    }
    if (interned_plan.required_features[0].id.ptr == plan.required_features[0].id.ptr) {
        return 78;
    }
    if (interned_plan.routes[0].bindings == plan.routes[0].bindings ||
        interned_plan.schemas == plan.schemas ||
        interned_plan.schemas[0].definition.properties == plan.schemas[0].definition.properties)
    {
        return 80;
    }
    if (interned_plan.routes[0].bindings[0].schema.ptr != interned_plan.schemas[0].name.ptr) {
        return 81;
    }
    if (interned_plan.schemas[0].definition.properties[0].schema == &property_node ||
        interned_plan.schemas[0].definition.properties[0].schema->validation.ptr ==
            property_node.validation.ptr)
    {
        return 82;
    }
    if (interned_plan.bundle.path.ptr != plan.bundle.path.ptr ||
        interned_plan.bundle.hash.ptr != plan.bundle.hash.ptr ||
        interned_plan.source_map.path.ptr != plan.source_map.path.ptr ||
        interned_plan.source_map.hash.ptr != plan.source_map.hash.ptr)
    {
        return 79;
    }

    return 0;
}

static int test_plan_metadata_interns_recursive_schema_graph(void)
{
    unsigned char arena_storage[8192];
    SlArena arena = {0};
    SlPlanSchemaProperty properties[1] = {0};
    SlPlanSchema schemas[1] = {0};
    SlPlan plan = {0};
    SlPlan interned_plan = {0};
    SlInternTable table = {0};
    SlStatus status;

    schemas[0].name = sl_str_from_cstr("Node");
    schemas[0].definition.kind = SL_PLAN_SCHEMA_OBJECT;
    schemas[0].definition.properties = properties;
    schemas[0].definition.property_count = 1U;
    properties[0].name = sl_str_from_cstr("next");
    properties[0].schema = &schemas[0].definition;
    plan.version = SL_PLAN_VERSION_1;
    plan.compiler_version = sl_str_from_cstr("sloppyc-test");
    plan.runtime_min_version = sl_str_from_cstr(SL_PLAN_RUNTIME_MIN_VERSION_0_1_0);
    plan.stdlib_version = sl_str_from_cstr(SL_PLAN_STDLIB_VERSION_0_1_0);
    plan.target.platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    plan.target.engine = sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8);
    plan.bundle.id = sl_str_from_cstr("app-js");
    plan.source_map.id = sl_str_from_cstr("app-js-map");
    plan.schemas = schemas;
    plan.schema_count = 1U;

    status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
    if (!sl_status_is_ok(status)) {
        return 83;
    }

    status = sl_plan_intern_metadata(&arena, &plan, 64U, 32U, &interned_plan, &table);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 84;
    }
    if (interned_plan.schemas == schemas ||
        interned_plan.schemas[0].definition.properties == properties)
    {
        return 85;
    }
    if (interned_plan.schemas[0].definition.properties[0].schema !=
        &interned_plan.schemas[0].definition)
    {
        return 86;
    }

    return 0;
}

static int test_plan_metadata_rebuild_advances_generation(void)
{
    unsigned char arena_storage[2048];
    SlArena arena = {0};
    SlPlan plan = {0};
    SlPlan interned_plan = {0};
    SlPlan reinterned_plan = {0};
    SlInternTable table = {0};
    SlInternedString interned_id = {0};
    SlInternedString stale_lookup = {0};
    SlSymbol old_id_symbol = {0};
    unsigned int first_generation = 0U;
    SlStatus status;

    plan.version = SL_PLAN_VERSION_1;
    plan.compiler_version = sl_str_from_cstr("sloppyc-test");
    plan.runtime_min_version = sl_str_from_cstr(SL_PLAN_RUNTIME_MIN_VERSION_0_1_0);
    plan.stdlib_version = sl_str_from_cstr(SL_PLAN_STDLIB_VERSION_0_1_0);
    plan.target.platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    plan.target.engine = sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8);
    plan.bundle.id = sl_str_from_cstr("app-js");
    plan.source_map.id = sl_str_from_cstr("app-js-map");

    status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
    if (!sl_status_is_ok(status)) {
        return 90;
    }
    status = sl_plan_intern_metadata(&arena, &plan, 8U, 8U, &interned_plan, &table);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 91;
    }
    status = sl_intern_table_find(&table, sl_str_from_cstr("app-js"), &interned_id);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 92;
    }
    old_id_symbol = interned_id.symbol;
    first_generation = table.generation;

    status = sl_plan_intern_metadata(&arena, &interned_plan, 8U, 8U, &reinterned_plan, &table);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 93;
    }
    if (table.generation == first_generation) {
        return 94;
    }
    if (expect_status(sl_intern_table_get(&table, old_id_symbol, &stale_lookup),
                      SL_STATUS_STALE_RESOURCE) != 0)
    {
        return 95;
    }
    if (!sl_str_equal(reinterned_plan.bundle.id, sl_str_from_cstr("app-js"))) {
        return 96;
    }

    return 0;
}

static int test_plan_metadata_intern_failure_preserves_outputs(void)
{
    unsigned char arena_storage[2048];
    SlArena arena = {0};
    SlPlanHandler handlers[1] = {
        {1U, sl_str_from_cstr("__sloppy_handler_1"), sl_str_from_cstr("home")}};
    SlPlan plan = {0};
    SlPlan sentinel_plan = {0};
    SlInternTable sentinel_table = {0};
    size_t used_before = 0U;
    SlStatus status;

    plan.version = SL_PLAN_VERSION_1;
    plan.compiler_version = sl_str_from_cstr("sloppyc-test");
    plan.runtime_min_version = sl_str_from_cstr(SL_PLAN_RUNTIME_MIN_VERSION_0_1_0);
    plan.stdlib_version = sl_str_from_cstr(SL_PLAN_STDLIB_VERSION_0_1_0);
    plan.target.platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    plan.target.engine = sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8);
    plan.bundle.id = sl_str_from_cstr("bundle-id");
    plan.source_map.id = sl_str_from_cstr("source-map-id");
    plan.handlers = handlers;
    plan.handler_count = 1U;

    sentinel_plan.version = 999U;
    sentinel_table.generation = 42U;

    status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
    if (!sl_status_is_ok(status)) {
        return 80;
    }
    used_before = sl_arena_used(&arena);

    status = sl_plan_intern_metadata(&arena, &plan, 1U, 1U, &sentinel_plan, &sentinel_table);
    if (expect_status(status, SL_STATUS_CAPACITY_EXCEEDED) != 0) {
        return 81;
    }
    if (sl_arena_used(&arena) != used_before) {
        return 82;
    }
    if (sentinel_plan.version != 999U || sentinel_table.generation != 42U ||
        sentinel_table.initialized)
    {
        return 83;
    }

    return 0;
}

static int test_fixture_files_exist(void)
{
    static const char* fixtures[] = {"tests/golden/plan/README.md",
                                     "tests/golden/plan/valid-minimal.plan.json",
                                     "tests/golden/plan/valid-multiple-handlers.plan.json",
                                     "tests/golden/plan/unknown-future-field.plan.json",
                                     "tests/golden/plan/malformed-json.plan.json",
                                     "tests/golden/plan/invalid-version.plan.json",
                                     "tests/golden/plan/missing-runtime-minimum-version.plan.json",
                                     "tests/golden/plan/missing-bundle.plan.json",
                                     "tests/golden/plan/missing-bundle-path.plan.json",
                                     "tests/golden/plan/missing-source-map.plan.json",
                                     "tests/golden/plan/missing-handlers.plan.json",
                                     "tests/golden/plan/empty-handlers.plan.json",
                                     "tests/golden/plan/invalid-handler-id.plan.json",
                                     "tests/golden/plan/duplicate-handler-id.plan.json",
                                     "tests/golden/plan/missing-handler-export.plan.json",
                                     "tests/golden/plan/empty-handler-export.plan.json",
                                     "tests/golden/plan/wrong-field-type.plan.json"};
    size_t index = 0U;

    for (index = 0U; index < sizeof(fixtures) / sizeof(fixtures[0]); index += 1U) {
        if (!fixture_exists(fixtures[index])) {
            return 60 + (int)index;
        }
    }

    return 0;
}

int main(void)
{
    int result = 0;

    result = test_version_helpers();
    if (result != 0) {
        return result;
    }

    result = test_handler_id_helpers();
    if (result != 0) {
        return result;
    }

    result = test_route_method_helpers();
    if (result != 0) {
        return result;
    }

    result = test_handler_lookup();
    if (result != 0) {
        return result;
    }

    result = test_lookup_invalid_arguments();
    if (result != 0) {
        return result;
    }

    result = test_empty_handler_table();
    if (result != 0) {
        return result;
    }

    result = test_duplicate_handler_ids();
    if (result != 0) {
        return result;
    }

    result = test_plan_metadata_interns_stable_strings();
    if (result != 0) {
        return result;
    }

    result = test_plan_metadata_interns_recursive_schema_graph();
    if (result != 0) {
        return result;
    }

    result = test_plan_metadata_rebuild_advances_generation();
    if (result != 0) {
        return result;
    }

    result = test_plan_metadata_intern_failure_preserves_outputs();
    if (result != 0) {
        return result;
    }

    return test_fixture_files_exist();
}
