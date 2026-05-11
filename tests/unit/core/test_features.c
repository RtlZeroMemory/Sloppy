#include "sloppy/features.h"

#include <stdio.h>

typedef int (*FeatureTestFn)(void);

static SlPlanDataProvider empty_data_provider(void)
{
    SlPlanDataProvider provider;

    provider.token = sl_str_empty();
    provider.provider = sl_str_empty();
    provider.capability = sl_str_empty();
    provider.service = sl_str_empty();
    provider.database = sl_str_empty();
    return provider;
}

static SlPlanRequiredFeature empty_required_feature(void)
{
    SlPlanRequiredFeature feature;

    feature.id = sl_str_empty();
    return feature;
}

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int expect_str_equal(SlStr actual, SlStr expected)
{
    return expect_true(sl_str_equal(actual, expected));
}

static int expect_snapshot(SlStr actual, const char* path)
{
    char expected[2048];
    FILE* file = NULL;
    size_t length = 0U;

#ifdef _MSC_VER
    if (fopen_s(&file, path, "rb") != 0) {
        return 1;
    }
#else
    file = fopen(path, "rb");
#endif

    if (file == NULL) {
        return 1;
    }

    length = fread(expected, 1U, sizeof(expected), file);
    if (ferror(file) != 0) {
        fclose(file);
        return 2;
    }
    if (length == sizeof(expected)) {
        unsigned char extra = 0U;
        const size_t extra_read = fread(&extra, 1U, 1U, file);
        if (extra_read == 1U) {
            fclose(file);
            return 4;
        }
        if (ferror(file) != 0) {
            fclose(file);
            return 2;
        }
    }
    if (fclose(file) != 0) {
        return 3;
    }

    return expect_str_equal(actual, sl_str_from_parts(expected, length));
}

static SlPlan base_plan(SlPlanHandler* handlers, SlPlanRoute* routes)
{
    SlPlan plan = {0};

    handlers[0].id = 1U;
    handlers[0].export_name = sl_str_from_cstr("__sloppy_handler_1");
    handlers[0].display_name = sl_str_from_cstr("Home");

    routes[0].method = sl_str_from_cstr("GET");
    routes[0].pattern = sl_str_from_cstr("/");
    routes[0].handler_id = 1U;
    routes[0].name = sl_str_from_cstr("Home");

    plan.version = SL_PLAN_CURRENT_VERSION;
    plan.target.platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    plan.target.engine = sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8);
    plan.handlers = handlers;
    plan.handler_count = 1U;
    plan.routes = routes;
    plan.route_count = 1U;
    return plan;
}

static SlRuntimeFeatureAvailability all_available(void)
{
    SlRuntimeFeatureAvailability availability = {0};

    availability.v8 = true;
    availability.http = true;
    availability.transport_libuv = true;
    availability.provider_sqlite = true;
    availability.provider_postgres = true;
    availability.provider_sqlserver = true;
    availability.stdlib_crypto = true;
    availability.stdlib_codec = true;
    availability.stdlib_net = true;
    availability.stdlib_os = true;
    availability.stdlib_http_client = true;
    availability.stdlib_workers = true;
    availability.stdlib_ffi = true;
    return availability;
}

static SlPlan target_only_plan(void)
{
    SlPlan plan = {0};

    plan.version = SL_PLAN_CURRENT_VERSION;
    plan.target.platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    plan.target.engine = sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8);
    return plan;
}

static int expect_activation_diagnostic_snapshot(const SlPlan* plan,
                                                 const SlRuntimeFeatureAvailability* availability,
                                                 SlStatusCode expected_status,
                                                 SlDiagCode expected_diag_code,
                                                 const char* snapshot_path)
{
    unsigned char diag_storage[4096];
    unsigned char render_storage[4096];
    SlArena diag_arena = {0};
    SlArena render_arena = {0};
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};
    SlStr rendered = {0};

    if (expect_status(sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&render_arena, render_storage, sizeof(render_storage)),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (expect_status(
            sl_runtime_feature_activate_plan(plan, availability, &diag_arena, &set, &diag),
            expected_status) != 0)
    {
        return 2;
    }
    if (diag.code != expected_diag_code) {
        return 3;
    }
    if (expect_status(sl_diag_render_json(&render_arena, &diag, &rendered), SL_STATUS_OK) != 0) {
        return 4;
    }
    if (expect_snapshot(rendered, snapshot_path) != 0) {
        return 5;
    }

    return 0;
}

static int test_descriptors_publish_import_and_intrinsic_metadata(void)
{
    const SlRuntimeFeatureDescriptor* sqlite =
        sl_runtime_feature_descriptor(SL_RUNTIME_FEATURE_PROVIDER_SQLITE);
    const SlRuntimeFeatureDescriptor* postgres =
        sl_runtime_feature_descriptor(SL_RUNTIME_FEATURE_PROVIDER_POSTGRES);
    const SlRuntimeFeatureDescriptor* sqlserver =
        sl_runtime_feature_descriptor(SL_RUNTIME_FEATURE_PROVIDER_SQLSERVER);
    const SlRuntimeFeatureDescriptor* data =
        sl_runtime_feature_descriptor(SL_RUNTIME_FEATURE_STDLIB_DATA);
    const SlRuntimeFeatureDescriptor* time =
        sl_runtime_feature_descriptor(SL_RUNTIME_FEATURE_STDLIB_TIME);
    const SlRuntimeFeatureDescriptor* crypto =
        sl_runtime_feature_descriptor(SL_RUNTIME_FEATURE_STDLIB_CRYPTO);
    const SlRuntimeFeatureDescriptor* codec =
        sl_runtime_feature_descriptor(SL_RUNTIME_FEATURE_STDLIB_CODEC);
    const SlRuntimeFeatureDescriptor* net =
        sl_runtime_feature_descriptor(SL_RUNTIME_FEATURE_STDLIB_NET);
    const SlRuntimeFeatureDescriptor* os =
        sl_runtime_feature_descriptor(SL_RUNTIME_FEATURE_STDLIB_OS);
    const SlRuntimeFeatureDescriptor* http_client =
        sl_runtime_feature_descriptor(SL_RUNTIME_FEATURE_STDLIB_HTTP_CLIENT);
    const SlRuntimeFeatureDescriptor* fs =
        sl_runtime_feature_descriptor(SL_RUNTIME_FEATURE_STDLIB_FS);
    const SlRuntimeFeatureDescriptor* config =
        sl_runtime_feature_descriptor(SL_RUNTIME_FEATURE_STDLIB_CONFIG);
    const SlRuntimeFeatureDescriptor* node_path =
        sl_runtime_feature_descriptor(SL_RUNTIME_FEATURE_NODE_COMPAT_PATH);
    const SlRuntimeFeatureDescriptor* node_fs_promises =
        sl_runtime_feature_descriptor(SL_RUNTIME_FEATURE_NODE_COMPAT_FS_PROMISES);
    const SlRuntimeFeatureDescriptor* node_assert =
        sl_runtime_feature_descriptor(SL_RUNTIME_FEATURE_NODE_COMPAT_ASSERT);
    const SlRuntimeFeatureDescriptor* node_stream =
        sl_runtime_feature_descriptor(SL_RUNTIME_FEATURE_NODE_COMPAT_STREAM);
    const SlRuntimeFeatureDescriptor* ffi =
        sl_runtime_feature_descriptor(SL_RUNTIME_FEATURE_STDLIB_FFI);

    if (SL_RUNTIME_FEATURE_COUNT != 45) {
        return 60;
    }
    if (sqlite == NULL || postgres == NULL || sqlserver == NULL || data == NULL || time == NULL ||
        crypto == NULL || codec == NULL || net == NULL || os == NULL || http_client == NULL ||
        fs == NULL || config == NULL || node_path == NULL || node_fs_promises == NULL ||
        node_assert == NULL || node_stream == NULL || ffi == NULL)
    {
        return 61;
    }
    if (!sl_str_equal(sqlite->stdlib_import, sl_str_from_cstr("sloppy/providers/sqlite")) ||
        !sl_str_equal(sqlite->v8_intrinsic_namespace, sl_str_from_cstr("__sloppy.data.sqlite")) ||
        !sqlite->requires_v8_intrinsics)
    {
        return 62;
    }
    if (!sl_str_equal(data->stable_id, sl_str_from_cstr("stdlib.data")) ||
        !sl_str_equal(data->stdlib_import, sl_str_from_cstr("sloppy/data")) ||
        !sl_str_is_empty(data->v8_intrinsic_namespace) || data->requires_v8_intrinsics)
    {
        return 63;
    }
    if (!sl_str_equal(time->stable_id, sl_str_from_cstr("stdlib.time")) ||
        !sl_str_equal(time->stdlib_import, sl_str_from_cstr("sloppy/time")) ||
        !sl_str_equal(time->v8_intrinsic_namespace, sl_str_from_cstr("__sloppy.time")) ||
        !time->requires_v8_intrinsics)
    {
        return 67;
    }
    if (!sl_str_equal(crypto->stable_id, sl_str_from_cstr("stdlib.crypto")) ||
        !sl_str_equal(crypto->stdlib_import, sl_str_from_cstr("sloppy/crypto")) ||
        !sl_str_equal(crypto->v8_intrinsic_namespace, sl_str_from_cstr("__sloppy.crypto")) ||
        !crypto->requires_v8_intrinsics || !crypto->available ||
        (crypto->dependencies & (1U << (uint32_t)SL_RUNTIME_FEATURE_STDLIB_CODEC)) == 0U)
    {
        return 68;
    }
    if (!sl_str_equal(codec->stable_id, sl_str_from_cstr("stdlib.codec")) ||
        !sl_str_equal(codec->stdlib_import, sl_str_from_cstr("sloppy/codec")) ||
        !sl_str_equal(codec->v8_intrinsic_namespace, sl_str_from_cstr("__sloppy.codec")) ||
        !codec->requires_v8_intrinsics || !codec->available)
    {
        return 69;
    }
    if (!sl_str_equal(net->stable_id, sl_str_from_cstr("stdlib.net")) ||
        !sl_str_equal(net->stdlib_import, sl_str_from_cstr("sloppy/net")) ||
        !sl_str_equal(net->v8_intrinsic_namespace, sl_str_from_cstr("__sloppy.net")) ||
        !net->requires_v8_intrinsics || !net->available ||
        (net->dependencies & (1U << (uint32_t)SL_RUNTIME_FEATURE_TRANSPORT_LIBUV)) == 0U ||
        (net->dependencies & (1U << (uint32_t)SL_RUNTIME_FEATURE_STDLIB_TIME)) == 0U ||
        (net->dependencies & (1U << (uint32_t)SL_RUNTIME_FEATURE_STDLIB_CODEC)) == 0U)
    {
        return 70;
    }
    if (!sl_str_equal(os->stable_id, sl_str_from_cstr("stdlib.os")) ||
        !sl_str_equal(os->stdlib_import, sl_str_from_cstr("sloppy/os")) ||
        !sl_str_equal(os->v8_intrinsic_namespace, sl_str_from_cstr("__sloppy.os")) ||
        !os->requires_v8_intrinsics || os->available ||
        (os->dependencies & (1U << (uint32_t)SL_RUNTIME_FEATURE_TRANSPORT_LIBUV)) == 0U ||
        (os->dependencies & (1U << (uint32_t)SL_RUNTIME_FEATURE_STDLIB_TIME)) == 0U ||
        (os->dependencies & (1U << (uint32_t)SL_RUNTIME_FEATURE_STDLIB_CODEC)) == 0U)
    {
        return 71;
    }
    if (!sl_str_equal(http_client->stable_id, sl_str_from_cstr("stdlib.httpclient")) ||
        !sl_str_equal(http_client->stdlib_import, sl_str_from_cstr("sloppy/net")) ||
        !sl_str_equal(http_client->v8_intrinsic_namespace, sl_str_from_cstr("__sloppy.net")) ||
        !http_client->requires_v8_intrinsics || !http_client->available ||
        (http_client->dependencies & (1U << (uint32_t)SL_RUNTIME_FEATURE_STDLIB_NET)) == 0U ||
        (http_client->dependencies & (1U << (uint32_t)SL_RUNTIME_FEATURE_STDLIB_TIME)) == 0U ||
        (http_client->dependencies & (1U << (uint32_t)SL_RUNTIME_FEATURE_STDLIB_CODEC)) == 0U ||
        (http_client->dependencies & (1U << (uint32_t)SL_RUNTIME_FEATURE_STDLIB_CRYPTO)) == 0U)
    {
        return 72;
    }
    if (!sl_str_equal(fs->stable_id, sl_str_from_cstr("stdlib.fs")) ||
        !sl_str_equal(fs->stdlib_import, sl_str_from_cstr("sloppy/fs")) ||
        !sl_str_equal(fs->v8_intrinsic_namespace, sl_str_from_cstr("__sloppy.fs")) ||
        !fs->requires_v8_intrinsics ||
        (fs->dependencies & (1U << (uint32_t)SL_RUNTIME_FEATURE_STDLIB_TIME)) == 0U ||
        (fs->dependencies & (1U << (uint32_t)SL_RUNTIME_FEATURE_STDLIB_CODEC)) == 0U)
    {
        return 66;
    }
    if (!sl_str_equal(config->stable_id, sl_str_from_cstr("stdlib.config")) ||
        !sl_str_equal(config->stdlib_import, sl_str_from_cstr("sloppy/config")) ||
        !sl_str_is_empty(config->v8_intrinsic_namespace))
    {
        return 64;
    }
    if (!sl_str_equal(ffi->stable_id, sl_str_from_cstr("stdlib.ffi")) ||
        !sl_str_equal(ffi->stdlib_import, sl_str_from_cstr("sloppy/ffi")) ||
        !sl_str_equal(ffi->v8_intrinsic_namespace, sl_str_from_cstr("__sloppy.ffi")) ||
        !ffi->requires_v8_intrinsics || !ffi->available)
    {
        return 76;
    }
    if (!postgres->available || !postgres->requires_v8_intrinsics ||
        !sl_str_equal(postgres->stdlib_import, sl_str_from_cstr("sloppy/providers/postgres")) ||
        !sl_str_equal(postgres->v8_intrinsic_namespace,
                      sl_str_from_cstr("__sloppy.data.postgres")) ||
        (postgres->dependencies & (1U << (uint32_t)SL_RUNTIME_FEATURE_STDLIB_DATA)) == 0U)
    {
        return 65;
    }
    if (!sqlserver->requires_v8_intrinsics ||
        !sl_str_equal(sqlserver->stdlib_import, sl_str_from_cstr("sloppy/providers/sqlserver")) ||
        !sl_str_equal(sqlserver->v8_intrinsic_namespace,
                      sl_str_from_cstr("__sloppy.data.sqlserver")) ||
        (sqlserver->dependencies & (1U << (uint32_t)SL_RUNTIME_FEATURE_STDLIB_DATA)) == 0U)
    {
        return 73;
    }
#ifdef SLOPPY_ENABLE_SQLSERVER_PROVIDER
    if (!sqlserver->available) {
        return 74;
    }
#else
    if (sqlserver->available) {
        return 75;
    }
#endif
    if (!sl_str_equal(node_path->stable_id, sl_str_from_cstr("node.compat.path")) ||
        !sl_str_equal(node_path->diagnostics_name,
                      sl_str_from_cstr("node:path compatibility shim")) ||
        !sl_str_equal(node_path->stdlib_import, sl_str_from_cstr("sloppy/node/path")) ||
        !sl_str_is_empty(node_path->v8_intrinsic_namespace) || node_path->requires_v8_intrinsics ||
        !node_path->available ||
        (node_path->dependencies & (1U << (uint32_t)SL_RUNTIME_FEATURE_V8)) == 0U)
    {
        return 76;
    }
    if (!sl_str_equal(node_fs_promises->stable_id, sl_str_from_cstr("node.compat.fs.promises")) ||
        !sl_str_equal(node_fs_promises->stdlib_import,
                      sl_str_from_cstr("sloppy/node/fs/promises")) ||
        !sl_str_is_empty(node_fs_promises->v8_intrinsic_namespace) ||
        node_fs_promises->requires_v8_intrinsics ||
        (node_fs_promises->dependencies & (1U << (uint32_t)SL_RUNTIME_FEATURE_V8)) == 0U)
    {
        return 77;
    }
    if (!sl_str_equal(node_assert->stable_id, sl_str_from_cstr("node.compat.assert")) ||
        !sl_str_equal(node_assert->stdlib_import, sl_str_from_cstr("sloppy/node/assert")) ||
        !sl_str_is_empty(node_assert->v8_intrinsic_namespace) ||
        node_assert->requires_v8_intrinsics ||
        (node_assert->dependencies & (1U << (uint32_t)SL_RUNTIME_FEATURE_V8)) == 0U)
    {
        return 78;
    }
    if (!sl_str_equal(node_stream->stable_id, sl_str_from_cstr("node.compat.stream")) ||
        !sl_str_equal(node_stream->stdlib_import, sl_str_from_cstr("sloppy/node/stream")) ||
        !sl_str_is_empty(node_stream->v8_intrinsic_namespace) ||
        node_stream->requires_v8_intrinsics ||
        (node_stream->dependencies & (1U << (uint32_t)SL_RUNTIME_FEATURE_V8)) == 0U)
    {
        return 79;
    }
    return 0;
}

static int test_workers_descriptor_metadata(void)
{
    const SlRuntimeFeatureDescriptor* workers =
        sl_runtime_feature_descriptor(SL_RUNTIME_FEATURE_STDLIB_WORKERS);

    if (workers == NULL) {
        return 1;
    }
    if (!sl_str_equal(workers->stable_id, sl_str_from_cstr("stdlib.workers")) ||
        !sl_str_equal(workers->stdlib_import, sl_str_from_cstr("sloppy/workers")) ||
        !sl_str_equal(workers->v8_intrinsic_namespace, sl_str_from_cstr("__sloppy.workers")) ||
        !workers->requires_v8_intrinsics || !workers->available ||
        (workers->dependencies & (1U << (uint32_t)SL_RUNTIME_FEATURE_TRANSPORT_LIBUV)) == 0U ||
        (workers->dependencies & (1U << (uint32_t)SL_RUNTIME_FEATURE_STDLIB_TIME)) == 0U ||
        (workers->dependencies & (1U << (uint32_t)SL_RUNTIME_FEATURE_STDLIB_CODEC)) == 0U)
    {
        return 2;
    }

    return 0;
}

static int test_workers_required_feature_activates_runtime_dependencies(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("stdlib.workers")}};
    SlPlan plan = target_only_plan();
    SlRuntimeFeatureAvailability availability = sl_runtime_feature_default_availability();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    availability.v8 = true;
    plan.required_features = required;
    plan.required_feature_count = 1U;
    sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (!sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_WORKERS) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_TRANSPORT_LIBUV) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_TIME) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_CODEC))
    {
        return 2;
    }
    if (diag.code != SL_DIAG_NONE) {
        return 3;
    }
    return 0;
}

static int test_workers_feature_diagnostic_golden(void)
{
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("stdlib.workers")}};
    SlRuntimeFeatureAvailability availability = all_available();
    SlPlan plan = target_only_plan();

    availability.stdlib_workers = false;
    plan.required_features = required;
    plan.required_feature_count = 1U;
    if (expect_activation_diagnostic_snapshot(
            &plan, &availability, SL_STATUS_UNSUPPORTED, SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE,
            "tests/golden/diagnostics/runtime_feature_unavailable_workers.json") != 0)
    {
        return 82;
    }

    return 0;
}

static int test_node_compat_required_features_activate_v8_dependency(void)
{
    static const struct
    {
        const char* required_feature;
        SlRuntimeFeatureId feature_id;
    } cases[] = {
        {"node.compat.path", SL_RUNTIME_FEATURE_NODE_COMPAT_PATH},
        {"node.compat.events", SL_RUNTIME_FEATURE_NODE_COMPAT_EVENTS},
        {"node.compat.url", SL_RUNTIME_FEATURE_NODE_COMPAT_URL},
        {"node.compat.querystring", SL_RUNTIME_FEATURE_NODE_COMPAT_QUERYSTRING},
        {"node.compat.buffer", SL_RUNTIME_FEATURE_NODE_COMPAT_BUFFER},
        {"node.compat.util", SL_RUNTIME_FEATURE_NODE_COMPAT_UTIL},
        {"node.compat.timers", SL_RUNTIME_FEATURE_NODE_COMPAT_TIMERS},
        {"node.compat.fs", SL_RUNTIME_FEATURE_NODE_COMPAT_FS},
        {"node.compat.fs.promises", SL_RUNTIME_FEATURE_NODE_COMPAT_FS_PROMISES},
        {"node.compat.os", SL_RUNTIME_FEATURE_NODE_COMPAT_OS},
        {"node.compat.process", SL_RUNTIME_FEATURE_NODE_COMPAT_PROCESS},
        {"node.compat.crypto", SL_RUNTIME_FEATURE_NODE_COMPAT_CRYPTO},
        {"node.compat.assert", SL_RUNTIME_FEATURE_NODE_COMPAT_ASSERT},
        {"node.compat.stream", SL_RUNTIME_FEATURE_NODE_COMPAT_STREAM},
        {"node.compat.console", SL_RUNTIME_FEATURE_NODE_COMPAT_CONSOLE},
        {"node.compat.constants", SL_RUNTIME_FEATURE_NODE_COMPAT_CONSTANTS},
        {"node.compat.diagnostics_channel", SL_RUNTIME_FEATURE_NODE_COMPAT_DIAGNOSTICS_CHANNEL},
        {"node.compat.http", SL_RUNTIME_FEATURE_NODE_COMPAT_HTTP},
        {"node.compat.https", SL_RUNTIME_FEATURE_NODE_COMPAT_HTTPS},
        {"node.compat.module", SL_RUNTIME_FEATURE_NODE_COMPAT_MODULE},
        {"node.compat.perf_hooks", SL_RUNTIME_FEATURE_NODE_COMPAT_PERF_HOOKS},
        {"node.compat.string_decoder", SL_RUNTIME_FEATURE_NODE_COMPAT_STRING_DECODER},
        {"node.compat.tty", SL_RUNTIME_FEATURE_NODE_COMPAT_TTY},
        {"node.compat.zlib", SL_RUNTIME_FEATURE_NODE_COMPAT_ZLIB},
    };
    size_t index = 0U;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index += 1U) {
        unsigned char diag_storage[2048];
        SlArena diag_arena = {0};
        SlPlanRequiredFeature required[1] = {{sl_str_from_cstr(cases[index].required_feature)}};
        SlPlan plan = target_only_plan();
        SlRuntimeFeatureAvailability availability = all_available();
        SlRuntimeFeatureSet set = {0};
        SlDiag diag = {0};

        plan.target.engine = sl_str_empty();
        plan.required_features = required;
        plan.required_feature_count = 1U;
        sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

        if (expect_status(
                sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
                SL_STATUS_OK) != 0)
        {
            return (int)(1U + index);
        }
        if (!sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_CORE) ||
            !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_V8) ||
            !sl_runtime_feature_set_contains(&set, cases[index].feature_id) ||
            set.activation_count != 3U || diag.code != SL_DIAG_NONE)
        {
            return (int)(20U + index);
        }
    }

    return 0;
}

static int test_node_compat_required_feature_fails_without_v8(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("node.compat.path")}};
    SlPlan plan = target_only_plan();
    SlRuntimeFeatureAvailability availability = all_available();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    availability.v8 = false;
    plan.target.engine = sl_str_empty();
    plan.required_features = required;
    plan.required_feature_count = 1U;
    sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_UNSUPPORTED) != 0)
    {
        return 1;
    }
    if (diag.code != SL_DIAG_RUNTIME_FEATURE_DEPENDENCY_MISSING ||
        !sl_str_equal(diag.related[0].message, sl_str_from_cstr("v8")) ||
        !sl_str_equal(diag.related[1].message, sl_str_from_cstr("node.compat.path")))
    {
        return 2;
    }
    return 0;
}

static int test_ffi_required_feature_activates_runtime_dependencies(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("stdlib.ffi")}};
    SlPlan plan = target_only_plan();
    SlRuntimeFeatureAvailability availability = sl_runtime_feature_default_availability();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    availability.v8 = true;
    availability.stdlib_ffi = true;
    plan.required_features = required;
    plan.required_feature_count = 1U;
    sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (!sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_FFI) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_CORE) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_V8))
    {
        return 2;
    }
    if (diag.code != SL_DIAG_NONE) {
        return 3;
    }
    return 0;
}

static int test_ffi_plan_metadata_activates_runtime_feature(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanFfiLibrary libraries[1] = {0};
    SlPlan plan = target_only_plan();
    SlRuntimeFeatureAvailability availability = all_available();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    libraries[0].name = sl_str_from_cstr("ffi-test");
    plan.ffi_libraries = libraries;
    plan.ffi_library_count = 1U;
    sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (!sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_FFI) ||
        diag.code != SL_DIAG_NONE)
    {
        return 2;
    }
    return 0;
}

static int test_ffi_required_feature_fails_when_runtime_unavailable(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("stdlib.ffi")}};
    SlPlan plan = target_only_plan();
    SlRuntimeFeatureAvailability availability = all_available();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    availability.stdlib_ffi = false;
    plan.required_features = required;
    plan.required_feature_count = 1U;
    sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_UNSUPPORTED) != 0)
    {
        return 1;
    }
    if (diag.code != SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE ||
        !sl_str_equal(diag.related[0].message, sl_str_from_cstr("stdlib.ffi")))
    {
        return 2;
    }
    return 0;
}

static int test_explicit_time_required_feature_activates_stdlib_time(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("stdlib.time")}};
    SlPlan plan = target_only_plan();
    SlRuntimeFeatureAvailability availability = all_available();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    plan.required_features = required;
    plan.required_feature_count = 1U;
    sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (!sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_CORE) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_V8) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_TIME))
    {
        return 2;
    }
    if (diag.code != SL_DIAG_NONE) {
        return 3;
    }
    return 0;
}

static int test_explicit_fs_required_feature_activates_stdlib_fs(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("stdlib.fs")}};
    SlPlan plan = target_only_plan();
    SlRuntimeFeatureAvailability availability = all_available();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    plan.required_features = required;
    plan.required_feature_count = 1U;
    sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (!sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_CORE) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_V8) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_TIME) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_CODEC) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_FS))
    {
        return 2;
    }
    if (diag.code != SL_DIAG_NONE) {
        return 3;
    }
    return 0;
}

static int test_explicit_crypto_required_feature_activates_when_available(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("stdlib.crypto")}};
    SlPlan plan = target_only_plan();
    SlRuntimeFeatureAvailability availability = all_available();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    plan.required_features = required;
    plan.required_feature_count = 1U;
    sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (!sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_CORE) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_V8) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_CODEC) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_CRYPTO))
    {
        return 2;
    }
    if (diag.code != SL_DIAG_NONE) {
        return 3;
    }
    return 0;
}

static int test_crypto_required_feature_fails_when_backend_unavailable(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("stdlib.crypto")}};
    SlPlan plan = target_only_plan();
    SlRuntimeFeatureAvailability availability = sl_runtime_feature_default_availability();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    availability.v8 = true;
    availability.stdlib_crypto = false;
    plan.required_features = required;
    plan.required_feature_count = 1U;
    sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_UNSUPPORTED) != 0)
    {
        return 1;
    }
    if (diag.code != SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE ||
        !sl_str_equal(diag.related[0].message, sl_str_from_cstr("stdlib.crypto")))
    {
        return 2;
    }
    return 0;
}

static int test_explicit_codec_required_feature_activates_when_available(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("stdlib.codec")}};
    SlPlan plan = target_only_plan();
    SlRuntimeFeatureAvailability availability = all_available();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    plan.required_features = required;
    plan.required_feature_count = 1U;
    sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (!sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_CORE) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_V8) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_CODEC))
    {
        return 2;
    }
    if (diag.code != SL_DIAG_NONE) {
        return 3;
    }
    return 0;
}

static int test_codec_required_feature_fails_when_runtime_unavailable(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("stdlib.codec")}};
    SlPlan plan = target_only_plan();
    SlRuntimeFeatureAvailability availability = sl_runtime_feature_default_availability();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    availability.v8 = true;
    availability.stdlib_codec = false;
    plan.required_features = required;
    plan.required_feature_count = 1U;
    sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_UNSUPPORTED) != 0)
    {
        return 1;
    }
    if (diag.code != SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE ||
        !sl_str_equal(diag.related[0].message, sl_str_from_cstr("stdlib.codec")))
    {
        return 2;
    }
    return 0;
}

static int test_codec_dependents_fail_closed_when_codec_unavailable(void)
{
    static const struct
    {
        const char* required_feature;
        SlRuntimeFeatureId feature_id;
    } cases[] = {
        {"stdlib.fs", SL_RUNTIME_FEATURE_STDLIB_FS},
        {"stdlib.crypto", SL_RUNTIME_FEATURE_STDLIB_CRYPTO},
        {"stdlib.net", SL_RUNTIME_FEATURE_STDLIB_NET},
        {"stdlib.os", SL_RUNTIME_FEATURE_STDLIB_OS},
        {"stdlib.httpclient", SL_RUNTIME_FEATURE_STDLIB_HTTP_CLIENT},
        {"stdlib.workers", SL_RUNTIME_FEATURE_STDLIB_WORKERS},
    };
    size_t index = 0U;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index += 1U) {
        unsigned char diag_storage[2048];
        SlArena diag_arena = {0};
        SlPlanRequiredFeature required[1] = {{sl_str_from_cstr(cases[index].required_feature)}};
        SlPlan plan = target_only_plan();
        SlRuntimeFeatureAvailability availability = all_available();
        SlRuntimeFeatureSet set = {0};
        SlDiag diag = {0};

        availability.stdlib_codec = false;
        plan.required_features = required;
        plan.required_feature_count = 1U;
        sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

        if (expect_status(
                sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
                SL_STATUS_UNSUPPORTED) != 0)
        {
            return (int)(1U + index);
        }
        if (diag.code != SL_DIAG_RUNTIME_FEATURE_DEPENDENCY_MISSING ||
            !sl_str_equal(diag.related[0].message, sl_str_from_cstr("stdlib.codec")))
        {
            return (int)(10U + index);
        }
        if (sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_CODEC) ||
            sl_runtime_feature_set_contains(&set, cases[index].feature_id))
        {
            return (int)(20U + index);
        }
    }

    return 0;
}

static int test_explicit_net_required_feature_activates_when_available(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("stdlib.net")}};
    SlPlan plan = target_only_plan();
    SlRuntimeFeatureAvailability availability = all_available();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    plan.required_features = required;
    plan.required_feature_count = 1U;
    sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (!sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_CORE) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_V8) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_TRANSPORT_LIBUV) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_TIME) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_CODEC) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_NET))
    {
        return 2;
    }
    if (diag.code != SL_DIAG_NONE) {
        return 3;
    }
    return 0;
}

static int test_net_required_feature_activates_by_default_after_tcp_client_backend(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("stdlib.net")}};
    SlPlan plan = target_only_plan();
    SlRuntimeFeatureAvailability availability = sl_runtime_feature_default_availability();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    availability.v8 = true;
    plan.required_features = required;
    plan.required_feature_count = 1U;
    sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (!sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_NET) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_TRANSPORT_LIBUV) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_TIME) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_CODEC) ||
        diag.code != SL_DIAG_NONE)
    {
        return 2;
    }
    return 0;
}

static int test_explicit_os_required_feature_activates_when_available(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("stdlib.os")}};
    SlPlan plan = target_only_plan();
    SlRuntimeFeatureAvailability availability = all_available();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    plan.required_features = required;
    plan.required_feature_count = 1U;
    sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (!sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_CORE) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_V8) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_TRANSPORT_LIBUV) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_TIME) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_CODEC) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_OS))
    {
        return 2;
    }
    if (diag.code != SL_DIAG_NONE) {
        return 3;
    }
    return 0;
}

static int test_os_required_feature_activates_with_default_runtime_surface(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("stdlib.os")}};
    SlPlan plan = target_only_plan();
    SlRuntimeFeatureAvailability availability = sl_runtime_feature_default_availability();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    availability.v8 = true;
    plan.required_features = required;
    plan.required_feature_count = 1U;
    sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (!sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_OS) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_CODEC) ||
        diag.code != SL_DIAG_NONE)
    {
        return 2;
    }
    return 0;
}

static int test_minimal_route_activates_expected_features(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanHandler handlers[1];
    SlPlanRoute routes[1];
    SlPlan plan = base_plan(handlers, routes);
    SlRuntimeFeatureAvailability availability = all_available();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (!sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_CORE) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_V8) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_HTTP) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_TRANSPORT_LIBUV) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_APP) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_RESULTS) ||
        sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_PROVIDER_SQLITE))
    {
        return 2;
    }
    if (set.activation_count != 6U || set.activations[0].id != SL_RUNTIME_FEATURE_CORE ||
        set.activations[1].id != SL_RUNTIME_FEATURE_V8 ||
        set.activations[2].id != SL_RUNTIME_FEATURE_STDLIB_APP ||
        set.activations[3].id != SL_RUNTIME_FEATURE_STDLIB_RESULTS ||
        set.activations[4].id != SL_RUNTIME_FEATURE_TRANSPORT_LIBUV ||
        set.activations[5].id != SL_RUNTIME_FEATURE_HTTP)
    {
        return 3;
    }
    if (diag.code != SL_DIAG_NONE) {
        return 4;
    }
    return 0;
}

static int test_sqlite_provider_metadata_activates_sqlite(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanHandler handlers[1];
    SlPlanRoute routes[1];
    SlPlanDataProvider providers[1] = {empty_data_provider()};
    SlPlan plan = base_plan(handlers, routes);
    SlRuntimeFeatureAvailability availability = all_available();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    providers[0] = (SlPlanDataProvider){0};
    providers[0].token = sl_str_from_cstr("data.main");
    providers[0].provider = sl_str_from_cstr("sqlite");
    plan.data_providers = providers;
    plan.data_provider_count = 1U;
    sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_OK) != 0)
    {
        return 10;
    }
    if (!sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_PROVIDER_SQLITE) ||
        sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_PROVIDER_POSTGRES))
    {
        return 11;
    }
    if (!sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_DATA)) {
        return 12;
    }
    if (set.activation_count != 8U || set.activations[6].id != SL_RUNTIME_FEATURE_STDLIB_DATA ||
        set.activations[6].reason != SL_RUNTIME_FEATURE_REASON_DEPENDENCY ||
        !sl_str_equal(set.activations[6].requested_by, sl_str_from_cstr("provider.sqlite")) ||
        set.activations[7].id != SL_RUNTIME_FEATURE_PROVIDER_SQLITE ||
        set.activations[7].reason != SL_RUNTIME_FEATURE_REASON_PLAN_PROVIDER ||
        !sl_str_equal(set.activations[7].requested_by, sl_str_from_cstr("data.main")))
    {
        return 13;
    }
    return 0;
}

static int test_unavailable_postgres_required_feature_fails(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanHandler handlers[1];
    SlPlanRoute routes[1];
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("provider.postgres")}};
    SlPlan plan = base_plan(handlers, routes);
    SlRuntimeFeatureAvailability availability = all_available();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    availability.provider_postgres = false;
    plan.required_features = required;
    plan.required_feature_count = 1U;
    sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_UNSUPPORTED) != 0)
    {
        return 20;
    }
    if (diag.code != SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE ||
        !sl_str_equal(diag.related[0].message, sl_str_from_cstr("provider.postgres")))
    {
        return 21;
    }
    return 0;
}

static int test_unknown_required_feature_fails_deterministically(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanHandler handlers[1];
    SlPlanRoute routes[1];
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("future.magic")}};
    SlPlan plan = base_plan(handlers, routes);
    SlRuntimeFeatureAvailability availability = all_available();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    plan.required_features = required;
    plan.required_feature_count = 1U;
    sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 30;
    }
    if (diag.code != SL_DIAG_UNKNOWN_RUNTIME_FEATURE ||
        !sl_str_equal(diag.message, sl_str_from_cstr("unknown runtime feature")) ||
        !sl_str_equal(diag.related[0].message, sl_str_from_cstr("future.magic")))
    {
        return 31;
    }
    return 0;
}

static int test_missing_dependency_fails_deterministically(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanHandler handlers[1];
    SlPlanRoute routes[1];
    SlPlan plan = base_plan(handlers, routes);
    SlRuntimeFeatureAvailability availability = all_available();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    availability.transport_libuv = false;
    sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_UNSUPPORTED) != 0)
    {
        return 40;
    }
    if (diag.code != SL_DIAG_RUNTIME_FEATURE_DEPENDENCY_MISSING ||
        !sl_str_equal(diag.related[0].message, sl_str_from_cstr("transport.libuv")) ||
        !sl_str_equal(diag.related[1].message, sl_str_from_cstr("http")))
    {
        return 41;
    }
    return 0;
}

static int test_v8_disabled_fails_honestly(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanHandler handlers[1];
    SlPlanRoute routes[1];
    SlPlan plan = base_plan(handlers, routes);
    SlRuntimeFeatureAvailability availability = all_available();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    availability.v8 = false;
    sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_UNSUPPORTED) != 0)
    {
        return 50;
    }
    if (diag.code != SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE ||
        !sl_str_equal(diag.related[0].message, sl_str_from_cstr("v8")))
    {
        return 51;
    }
    return 0;
}

static int test_missing_feature_diagnostic_goldens(void)
{
    SlPlanHandler handlers[1];
    SlPlanRoute routes[1];
    SlPlanDataProvider providers[1] = {empty_data_provider()};
    SlPlanRequiredFeature required[1] = {empty_required_feature()};
    SlRuntimeFeatureAvailability availability = all_available();
    SlPlan plan = base_plan(handlers, routes);

    providers[0] = (SlPlanDataProvider){0};
    required[0] = (SlPlanRequiredFeature){0};
    required[0].id = sl_str_from_cstr("future.magic");
    plan.required_features = required;
    plan.required_feature_count = 1U;
    if (expect_activation_diagnostic_snapshot(
            &plan, &availability, SL_STATUS_INVALID_ARGUMENT, SL_DIAG_UNKNOWN_RUNTIME_FEATURE,
            "tests/golden/diagnostics/runtime_feature_unknown.json") != 0)
    {
        return 70;
    }

    availability.provider_postgres = false;
    required[0].id = sl_str_from_cstr("provider.postgres");
    if (expect_activation_diagnostic_snapshot(
            &plan, &availability, SL_STATUS_UNSUPPORTED, SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE,
            "tests/golden/diagnostics/runtime_feature_unavailable_postgres.json") != 0)
    {
        return 71;
    }

    availability.provider_postgres = true;
    availability.provider_sqlserver = false;
    required[0].id = sl_str_from_cstr("provider.sqlserver");
    if (expect_activation_diagnostic_snapshot(
            &plan, &availability, SL_STATUS_UNSUPPORTED, SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE,
            "tests/golden/diagnostics/runtime_feature_unavailable_sqlserver.json") != 0)
    {
        return 72;
    }

    plan.required_features = NULL;
    plan.required_feature_count = 0U;
    providers[0].token = sl_str_from_cstr("data.main");
    providers[0].provider = sl_str_from_cstr("sqlite");
    plan.data_providers = providers;
    plan.data_provider_count = 1U;
    availability.provider_sqlite = false;
    if (expect_activation_diagnostic_snapshot(
            &plan, &availability, SL_STATUS_UNSUPPORTED, SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE,
            "tests/golden/diagnostics/runtime_feature_unavailable_sqlite.json") != 0)
    {
        return 73;
    }

    availability = all_available();
    availability.v8 = false;
    plan.data_providers = NULL;
    plan.data_provider_count = 0U;
    if (expect_activation_diagnostic_snapshot(
            &plan, &availability, SL_STATUS_UNSUPPORTED, SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE,
            "tests/golden/diagnostics/runtime_feature_v8_disabled.json") != 0)
    {
        return 74;
    }

    availability = all_available();
    availability.transport_libuv = false;
    if (expect_activation_diagnostic_snapshot(
            &plan, &availability, SL_STATUS_UNSUPPORTED, SL_DIAG_RUNTIME_FEATURE_DEPENDENCY_MISSING,
            "tests/golden/diagnostics/runtime_feature_missing_transport_dependency.json") != 0)
    {
        return 75;
    }

    availability = all_available();
    availability.transport_libuv = false;
    plan = target_only_plan();
    required[0].id = sl_str_from_cstr("transport.libuv");
    plan.required_features = required;
    plan.required_feature_count = 1U;
    if (expect_activation_diagnostic_snapshot(
            &plan, &availability, SL_STATUS_UNSUPPORTED, SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE,
            "tests/golden/diagnostics/runtime_feature_unavailable_transport.json") != 0)
    {
        return 76;
    }

    return 0;
}

static int test_crypto_feature_diagnostic_golden(void)
{
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("stdlib.crypto")}};
    SlRuntimeFeatureAvailability availability = all_available();
    SlPlan plan = target_only_plan();

    availability.stdlib_crypto = false;
    plan.required_features = required;
    plan.required_feature_count = 1U;
    if (expect_activation_diagnostic_snapshot(
            &plan, &availability, SL_STATUS_UNSUPPORTED, SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE,
            "tests/golden/diagnostics/runtime_feature_unavailable_crypto.json") != 0)
    {
        return 77;
    }

    return 0;
}

static int test_codec_feature_diagnostic_golden(void)
{
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("stdlib.codec")}};
    SlRuntimeFeatureAvailability availability = all_available();
    SlPlan plan = target_only_plan();

    availability.stdlib_codec = false;
    plan.required_features = required;
    plan.required_feature_count = 1U;
    if (expect_activation_diagnostic_snapshot(
            &plan, &availability, SL_STATUS_UNSUPPORTED, SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE,
            "tests/golden/diagnostics/runtime_feature_unavailable_codec.json") != 0)
    {
        return 78;
    }

    return 0;
}

static int test_net_feature_diagnostic_golden(void)
{
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("stdlib.net")}};
    SlRuntimeFeatureAvailability availability = all_available();
    SlPlan plan = target_only_plan();

    availability.stdlib_net = false;
    plan.required_features = required;
    plan.required_feature_count = 1U;
    if (expect_activation_diagnostic_snapshot(
            &plan, &availability, SL_STATUS_UNSUPPORTED, SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE,
            "tests/golden/diagnostics/runtime_feature_unavailable_net.json") != 0)
    {
        return 79;
    }

    return 0;
}

static int test_os_feature_diagnostic_golden(void)
{
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("stdlib.os")}};
    SlRuntimeFeatureAvailability availability = all_available();
    SlPlan plan = target_only_plan();

    availability.stdlib_os = false;
    plan.required_features = required;
    plan.required_feature_count = 1U;
    if (expect_activation_diagnostic_snapshot(
            &plan, &availability, SL_STATUS_UNSUPPORTED, SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE,
            "tests/golden/diagnostics/runtime_feature_unavailable_os.json") != 0)
    {
        return 80;
    }

    return 0;
}

static int test_http_client_feature_diagnostic_golden(void)
{
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("stdlib.httpclient")}};
    SlRuntimeFeatureAvailability availability = all_available();
    SlPlan plan = target_only_plan();

    availability.stdlib_http_client = false;
    plan.required_features = required;
    plan.required_feature_count = 1U;
    if (expect_activation_diagnostic_snapshot(
            &plan, &availability, SL_STATUS_UNSUPPORTED, SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE,
            "tests/golden/diagnostics/runtime_feature_unavailable_http_client.json") != 0)
    {
        return 81;
    }

    return 0;
}

static int test_http_client_required_feature_activates_tcp_dependency(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("stdlib.httpclient")}};
    SlPlan plan = target_only_plan();
    SlRuntimeFeatureAvailability availability = sl_runtime_feature_default_availability();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    availability.v8 = true;
    plan.required_features = required;
    plan.required_feature_count = 1U;
    sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (!sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_HTTP_CLIENT) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_NET) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_TIME) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_CODEC) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_CRYPTO))
    {
        return 2;
    }
    if (diag.code != SL_DIAG_NONE) {
        return 3;
    }
    return 0;
}

int main(void)
{
    static const FeatureTestFn tests[] = {
        test_descriptors_publish_import_and_intrinsic_metadata,
        test_workers_descriptor_metadata,
        test_explicit_time_required_feature_activates_stdlib_time,
        test_explicit_fs_required_feature_activates_stdlib_fs,
        test_explicit_crypto_required_feature_activates_when_available,
        test_crypto_required_feature_fails_when_backend_unavailable,
        test_explicit_codec_required_feature_activates_when_available,
        test_codec_required_feature_fails_when_runtime_unavailable,
        test_codec_dependents_fail_closed_when_codec_unavailable,
        test_explicit_net_required_feature_activates_when_available,
        test_net_required_feature_activates_by_default_after_tcp_client_backend,
        test_explicit_os_required_feature_activates_when_available,
        test_os_required_feature_activates_with_default_runtime_surface,
        test_minimal_route_activates_expected_features,
        test_sqlite_provider_metadata_activates_sqlite,
        test_unavailable_postgres_required_feature_fails,
        test_unknown_required_feature_fails_deterministically,
        test_missing_dependency_fails_deterministically,
        test_v8_disabled_fails_honestly,
        test_missing_feature_diagnostic_goldens,
        test_crypto_feature_diagnostic_golden,
        test_codec_feature_diagnostic_golden,
        test_net_feature_diagnostic_golden,
        test_os_feature_diagnostic_golden,
        test_http_client_feature_diagnostic_golden,
        test_http_client_required_feature_activates_tcp_dependency,
        test_workers_required_feature_activates_runtime_dependencies,
        test_workers_feature_diagnostic_golden,
        test_node_compat_required_features_activate_v8_dependency,
        test_node_compat_required_feature_fails_without_v8,
        test_ffi_required_feature_activates_runtime_dependencies,
        test_ffi_plan_metadata_activates_runtime_feature,
        test_ffi_required_feature_fails_when_runtime_unavailable,
    };
    size_t index = 0U;

    for (index = 0U; index < sizeof(tests) / sizeof(tests[0]); index += 1U) {
        if (tests[index]() != 0) {
            return (int)index + 1;
        }
    }
    return 0;
}
