/*
 * src/core/features.c
 *
 * Implements the runtime feature registry and Plan-driven activation decisions. The
 * registry is deterministic static data; Plan validation copies no runtime handles and
 * performs no provider/engine/transport initialization.
 */
#include "sloppy/features.h"

#include <stddef.h>

#define SL_FEATURE_BIT(id) (UINT32_C(1) << (uint32_t)(id))
#define SL_FEATURE_STR(text) {text, sizeof(text) - 1U}
#define SL_FEATURE_EMPTY {NULL, 0U}

#ifdef SLOPPY_ENABLE_V8_BRIDGE
#define SL_FEATURE_V8_AVAILABLE true
#else
#define SL_FEATURE_V8_AVAILABLE false
#endif

static SlStr sl_feature_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static SlRuntimeFeatureDescriptor
sl_feature_descriptor_make(SlRuntimeFeatureId id, SlRuntimeFeatureKind kind, SlStr stable_id,
                           SlStr diagnostics_name, SlStr stdlib_import,
                           SlStr v8_intrinsic_namespace, uint32_t dependencies, bool available,
                           bool requires_v8_intrinsics, bool package_include_hint)
{
    SlRuntimeFeatureDescriptor descriptor;

    descriptor.id = id;
    descriptor.kind = kind;
    descriptor.stable_id = stable_id;
    descriptor.diagnostics_name = diagnostics_name;
    descriptor.stdlib_import = stdlib_import;
    descriptor.v8_intrinsic_namespace = v8_intrinsic_namespace;
    descriptor.dependencies = dependencies;
    descriptor.available = available;
    descriptor.requires_v8_intrinsics = requires_v8_intrinsics;
    descriptor.package_include_hint = package_include_hint;
    return descriptor;
}

static SlRuntimeFeatureDescriptor sl_feature_time_descriptor(SlRuntimeFeatureId id)
{
    return sl_feature_descriptor_make(
        id, SL_RUNTIME_FEATURE_KIND_STDLIB,
        sl_feature_literal("stdlib.time", sizeof("stdlib.time") - 1U),
        sl_feature_literal("time stdlib", sizeof("time stdlib") - 1U),
        sl_feature_literal("sloppy/time", sizeof("sloppy/time") - 1U),
        sl_feature_literal("__sloppy.time", sizeof("__sloppy.time") - 1U),
        SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE) | SL_FEATURE_BIT(SL_RUNTIME_FEATURE_V8), true, true,
        true);
}

static SlRuntimeFeatureDescriptor sl_feature_crypto_descriptor(SlRuntimeFeatureId id,
                                                               bool available)
{
    return sl_feature_descriptor_make(
        id, SL_RUNTIME_FEATURE_KIND_STDLIB,
        sl_feature_literal("stdlib.crypto", sizeof("stdlib.crypto") - 1U),
        sl_feature_literal("crypto stdlib", sizeof("crypto stdlib") - 1U),
        sl_feature_literal("sloppy/crypto", sizeof("sloppy/crypto") - 1U),
        sl_feature_literal("__sloppy.crypto", sizeof("__sloppy.crypto") - 1U),
        SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE) | SL_FEATURE_BIT(SL_RUNTIME_FEATURE_V8), available,
        true, true);
}

static SlRuntimeFeatureDescriptor sl_feature_codec_descriptor(SlRuntimeFeatureId id, bool available)
{
    return sl_feature_descriptor_make(
        id, SL_RUNTIME_FEATURE_KIND_STDLIB,
        sl_feature_literal("stdlib.codec", sizeof("stdlib.codec") - 1U),
        sl_feature_literal("codec stdlib", sizeof("codec stdlib") - 1U),
        sl_feature_literal("sloppy/codec", sizeof("sloppy/codec") - 1U),
        sl_feature_literal("__sloppy.codec", sizeof("__sloppy.codec") - 1U),
        SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE) | SL_FEATURE_BIT(SL_RUNTIME_FEATURE_V8), available,
        true, true);
}

static SlRuntimeFeatureDescriptor sl_feature_fs_descriptor(SlRuntimeFeatureId id)
{
    return sl_feature_descriptor_make(
        id, SL_RUNTIME_FEATURE_KIND_STDLIB,
        sl_feature_literal("stdlib.fs", sizeof("stdlib.fs") - 1U),
        sl_feature_literal("filesystem stdlib", sizeof("filesystem stdlib") - 1U),
        sl_feature_literal("sloppy/fs", sizeof("sloppy/fs") - 1U),
        sl_feature_literal("__sloppy.fs", sizeof("__sloppy.fs") - 1U),
        SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE) | SL_FEATURE_BIT(SL_RUNTIME_FEATURE_V8) |
            SL_FEATURE_BIT(SL_RUNTIME_FEATURE_STDLIB_TIME),
        true, true, true);
}

static SlRuntimeFeatureDescriptor sl_feature_net_descriptor(SlRuntimeFeatureId id, bool available)
{
    /* The TCP stdlib is Plan-visible before it is executable: sloppy/net remains known even when
       unavailable, and depends on V8, libuv transport, and stdlib.time deadline semantics. */
    return sl_feature_descriptor_make(
        id, SL_RUNTIME_FEATURE_KIND_STDLIB,
        sl_feature_literal("stdlib.net", sizeof("stdlib.net") - 1U),
        sl_feature_literal("network stdlib", sizeof("network stdlib") - 1U),
        sl_feature_literal("sloppy/net", sizeof("sloppy/net") - 1U),
        sl_feature_literal("__sloppy.net", sizeof("__sloppy.net") - 1U),
        SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE) | SL_FEATURE_BIT(SL_RUNTIME_FEATURE_V8) |
            SL_FEATURE_BIT(SL_RUNTIME_FEATURE_TRANSPORT_LIBUV) |
            SL_FEATURE_BIT(SL_RUNTIME_FEATURE_STDLIB_TIME),
        available, true, true);
}

static SlRuntimeFeatureDescriptor
sl_feature_descriptor_with_availability(SlRuntimeFeatureId id,
                                        const SlRuntimeFeatureAvailability* availability)
{
    const bool v8 = availability == NULL ? false : availability->v8;
    const bool http = availability == NULL ? false : availability->http;
    const bool libuv = availability == NULL ? false : availability->transport_libuv;
    const bool sqlite = availability == NULL ? false : availability->provider_sqlite;
    const bool postgres = availability == NULL ? false : availability->provider_postgres;
    const bool sqlserver = availability == NULL ? false : availability->provider_sqlserver;
    const bool crypto = availability == NULL ? false : availability->stdlib_crypto;
    const bool net = availability == NULL ? false : availability->stdlib_net;
    const bool codec = availability == NULL ? false : availability->stdlib_codec;

    switch (id) {
    case SL_RUNTIME_FEATURE_CORE:
        return sl_feature_descriptor_make(
            id, SL_RUNTIME_FEATURE_KIND_CORE, sl_feature_literal("core", sizeof("core") - 1U),
            sl_feature_literal("core runtime", sizeof("core runtime") - 1U), sl_str_empty(),
            sl_str_empty(), 0U, true, false, true);
    case SL_RUNTIME_FEATURE_V8:
        return sl_feature_descriptor_make(
            id, SL_RUNTIME_FEATURE_KIND_ENGINE, sl_feature_literal("v8", sizeof("v8") - 1U),
            sl_feature_literal("V8 engine", sizeof("V8 engine") - 1U), sl_str_empty(),
            sl_str_empty(), SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE), v8, true, true);
    case SL_RUNTIME_FEATURE_HTTP:
        return sl_feature_descriptor_make(
            id, SL_RUNTIME_FEATURE_KIND_HTTP, sl_feature_literal("http", sizeof("http") - 1U),
            sl_feature_literal("HTTP runtime", sizeof("HTTP runtime") - 1U), sl_str_empty(),
            sl_str_empty(),
            SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE) | SL_FEATURE_BIT(SL_RUNTIME_FEATURE_V8) |
                SL_FEATURE_BIT(SL_RUNTIME_FEATURE_TRANSPORT_LIBUV),
            http, false, true);
    case SL_RUNTIME_FEATURE_TRANSPORT_LIBUV:
        return sl_feature_descriptor_make(
            id, SL_RUNTIME_FEATURE_KIND_TRANSPORT,
            sl_feature_literal("transport.libuv", sizeof("transport.libuv") - 1U),
            sl_feature_literal("libuv transport", sizeof("libuv transport") - 1U), sl_str_empty(),
            sl_str_empty(), SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE), libuv, false, true);
    case SL_RUNTIME_FEATURE_STDLIB_APP:
        return sl_feature_descriptor_make(
            id, SL_RUNTIME_FEATURE_KIND_STDLIB,
            sl_feature_literal("stdlib.framework/app", sizeof("stdlib.framework/app") - 1U),
            sl_feature_literal("framework app stdlib", sizeof("framework app stdlib") - 1U),
            sl_feature_literal("sloppy/app", sizeof("sloppy/app") - 1U),
            sl_feature_literal("__sloppy_register_handler",
                               sizeof("__sloppy_register_handler") - 1U),
            SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE) | SL_FEATURE_BIT(SL_RUNTIME_FEATURE_V8), true,
            true, true);
    case SL_RUNTIME_FEATURE_STDLIB_RESULTS:
        return sl_feature_descriptor_make(
            id, SL_RUNTIME_FEATURE_KIND_STDLIB,
            sl_feature_literal("stdlib.results", sizeof("stdlib.results") - 1U),
            sl_feature_literal("results stdlib", sizeof("results stdlib") - 1U),
            sl_feature_literal("sloppy/results", sizeof("sloppy/results") - 1U), sl_str_empty(),
            SL_FEATURE_BIT(SL_RUNTIME_FEATURE_STDLIB_APP), true, false, true);
    case SL_RUNTIME_FEATURE_STDLIB_SCHEMA:
        return sl_feature_descriptor_make(
            id, SL_RUNTIME_FEATURE_KIND_STDLIB,
            sl_feature_literal("stdlib.schema", sizeof("stdlib.schema") - 1U),
            sl_feature_literal("schema stdlib", sizeof("schema stdlib") - 1U),
            sl_feature_literal("sloppy/schema", sizeof("sloppy/schema") - 1U), sl_str_empty(),
            SL_FEATURE_BIT(SL_RUNTIME_FEATURE_STDLIB_APP), true, false, true);
    case SL_RUNTIME_FEATURE_STDLIB_CONFIG:
        return sl_feature_descriptor_make(
            id, SL_RUNTIME_FEATURE_KIND_STDLIB,
            sl_feature_literal("stdlib.config", sizeof("stdlib.config") - 1U),
            sl_feature_literal("config stdlib", sizeof("config stdlib") - 1U),
            sl_feature_literal("sloppy/config", sizeof("sloppy/config") - 1U), sl_str_empty(),
            SL_FEATURE_BIT(SL_RUNTIME_FEATURE_STDLIB_APP), true, false, true);
    case SL_RUNTIME_FEATURE_STDLIB_DATA:
        return sl_feature_descriptor_make(
            id, SL_RUNTIME_FEATURE_KIND_STDLIB,
            sl_feature_literal("stdlib.data", sizeof("stdlib.data") - 1U),
            sl_feature_literal("data stdlib", sizeof("data stdlib") - 1U),
            sl_feature_literal("sloppy/data", sizeof("sloppy/data") - 1U), sl_str_empty(),
            SL_FEATURE_BIT(SL_RUNTIME_FEATURE_STDLIB_APP), true, false, true);
    case SL_RUNTIME_FEATURE_STDLIB_TIME:
        return sl_feature_time_descriptor(id);
    case SL_RUNTIME_FEATURE_STDLIB_CRYPTO:
        return sl_feature_crypto_descriptor(id, crypto);
    case SL_RUNTIME_FEATURE_STDLIB_CODEC:
        return sl_feature_codec_descriptor(id, codec);
    case SL_RUNTIME_FEATURE_STDLIB_NET:
        return sl_feature_net_descriptor(id, net);
    case SL_RUNTIME_FEATURE_STDLIB_FS:
        return sl_feature_fs_descriptor(id);
    case SL_RUNTIME_FEATURE_PROVIDER_SQLITE:
        return sl_feature_descriptor_make(
            id, SL_RUNTIME_FEATURE_KIND_PROVIDER,
            sl_feature_literal("provider.sqlite", sizeof("provider.sqlite") - 1U),
            sl_feature_literal("SQLite provider", sizeof("SQLite provider") - 1U),
            sl_feature_literal("sloppy/providers/sqlite", sizeof("sloppy/providers/sqlite") - 1U),
            sl_feature_literal("__sloppy.data.sqlite", sizeof("__sloppy.data.sqlite") - 1U),
            SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE) | SL_FEATURE_BIT(SL_RUNTIME_FEATURE_V8) |
                SL_FEATURE_BIT(SL_RUNTIME_FEATURE_STDLIB_DATA),
            sqlite, true, true);
    case SL_RUNTIME_FEATURE_PROVIDER_POSTGRES:
        return sl_feature_descriptor_make(
            id, SL_RUNTIME_FEATURE_KIND_PROVIDER,
            sl_feature_literal("provider.postgres", sizeof("provider.postgres") - 1U),
            sl_feature_literal("PostgreSQL provider", sizeof("PostgreSQL provider") - 1U),
            sl_feature_literal("sloppy/providers/postgres",
                               sizeof("sloppy/providers/postgres") - 1U),
            sl_str_empty(),
            SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE) | SL_FEATURE_BIT(SL_RUNTIME_FEATURE_V8),
            postgres, false, true);
    case SL_RUNTIME_FEATURE_PROVIDER_SQLSERVER:
        return sl_feature_descriptor_make(
            id, SL_RUNTIME_FEATURE_KIND_PROVIDER,
            sl_feature_literal("provider.sqlserver", sizeof("provider.sqlserver") - 1U),
            sl_feature_literal("SQL Server provider", sizeof("SQL Server provider") - 1U),
            sl_feature_literal("sloppy/providers/sqlserver",
                               sizeof("sloppy/providers/sqlserver") - 1U),
            sl_str_empty(),
            SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE) | SL_FEATURE_BIT(SL_RUNTIME_FEATURE_V8),
            sqlserver, false, true);
    default:
        return sl_feature_descriptor_make(id, SL_RUNTIME_FEATURE_KIND_CORE, sl_str_empty(),
                                          sl_str_empty(), sl_str_empty(), sl_str_empty(), 0U, false,
                                          false, false);
    }
}

SlRuntimeFeatureAvailability sl_runtime_feature_default_availability(void)
{
    SlRuntimeFeatureAvailability availability;

#ifdef SLOPPY_ENABLE_V8_BRIDGE
    availability.v8 = true;
#else
    availability.v8 = false;
#endif
    availability.http = true;
    availability.transport_libuv = true;
    availability.provider_sqlite = true;
    availability.provider_postgres = false;
    availability.provider_sqlserver = false;
    availability.stdlib_crypto = true;
    availability.stdlib_codec = false;
    /* Known-but-unavailable by default keeps sloppy/net import diagnostics deterministic until the
       V8/libuv TCP backend is explicitly wired by the implementation PRs. */
    availability.stdlib_net = false;
    return availability;
}

const SlRuntimeFeatureDescriptor* sl_runtime_feature_descriptor(SlRuntimeFeatureId id)
{
    static const SlRuntimeFeatureDescriptor descriptors[SL_RUNTIME_FEATURE_COUNT] = {
        {SL_RUNTIME_FEATURE_CORE, SL_RUNTIME_FEATURE_KIND_CORE, SL_FEATURE_STR("core"),
         SL_FEATURE_STR("core runtime"), SL_FEATURE_EMPTY, SL_FEATURE_EMPTY, 0U, true, false, true},
        {SL_RUNTIME_FEATURE_V8, SL_RUNTIME_FEATURE_KIND_ENGINE, SL_FEATURE_STR("v8"),
         SL_FEATURE_STR("V8 engine"), SL_FEATURE_EMPTY, SL_FEATURE_EMPTY,
         SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE), SL_FEATURE_V8_AVAILABLE, true, true},
        {SL_RUNTIME_FEATURE_HTTP, SL_RUNTIME_FEATURE_KIND_HTTP, SL_FEATURE_STR("http"),
         SL_FEATURE_STR("HTTP runtime"), SL_FEATURE_EMPTY, SL_FEATURE_EMPTY,
         SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE) | SL_FEATURE_BIT(SL_RUNTIME_FEATURE_V8) |
             SL_FEATURE_BIT(SL_RUNTIME_FEATURE_TRANSPORT_LIBUV),
         true, false, true},
        {SL_RUNTIME_FEATURE_TRANSPORT_LIBUV, SL_RUNTIME_FEATURE_KIND_TRANSPORT,
         SL_FEATURE_STR("transport.libuv"), SL_FEATURE_STR("libuv transport"), SL_FEATURE_EMPTY,
         SL_FEATURE_EMPTY, SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE), true, false, true},
        {SL_RUNTIME_FEATURE_STDLIB_APP, SL_RUNTIME_FEATURE_KIND_STDLIB,
         SL_FEATURE_STR("stdlib.framework/app"), SL_FEATURE_STR("framework app stdlib"),
         SL_FEATURE_STR("sloppy/app"), SL_FEATURE_STR("__sloppy_register_handler"),
         SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE) | SL_FEATURE_BIT(SL_RUNTIME_FEATURE_V8), true,
         true, true},
        {SL_RUNTIME_FEATURE_STDLIB_RESULTS, SL_RUNTIME_FEATURE_KIND_STDLIB,
         SL_FEATURE_STR("stdlib.results"), SL_FEATURE_STR("results stdlib"),
         SL_FEATURE_STR("sloppy/results"), SL_FEATURE_EMPTY,
         SL_FEATURE_BIT(SL_RUNTIME_FEATURE_STDLIB_APP), true, false, true},
        {SL_RUNTIME_FEATURE_STDLIB_SCHEMA, SL_RUNTIME_FEATURE_KIND_STDLIB,
         SL_FEATURE_STR("stdlib.schema"), SL_FEATURE_STR("schema stdlib"),
         SL_FEATURE_STR("sloppy/schema"), SL_FEATURE_EMPTY,
         SL_FEATURE_BIT(SL_RUNTIME_FEATURE_STDLIB_APP), true, false, true},
        {SL_RUNTIME_FEATURE_STDLIB_CONFIG, SL_RUNTIME_FEATURE_KIND_STDLIB,
         SL_FEATURE_STR("stdlib.config"), SL_FEATURE_STR("config stdlib"),
         SL_FEATURE_STR("sloppy/config"), SL_FEATURE_EMPTY,
         SL_FEATURE_BIT(SL_RUNTIME_FEATURE_STDLIB_APP), true, false, true},
        {SL_RUNTIME_FEATURE_STDLIB_DATA, SL_RUNTIME_FEATURE_KIND_STDLIB,
         SL_FEATURE_STR("stdlib.data"), SL_FEATURE_STR("data stdlib"),
         SL_FEATURE_STR("sloppy/data"), SL_FEATURE_EMPTY,
         SL_FEATURE_BIT(SL_RUNTIME_FEATURE_STDLIB_APP), true, false, true},
        {SL_RUNTIME_FEATURE_STDLIB_FS, SL_RUNTIME_FEATURE_KIND_STDLIB, SL_FEATURE_STR("stdlib.fs"),
         SL_FEATURE_STR("filesystem stdlib"), SL_FEATURE_STR("sloppy/fs"),
         SL_FEATURE_STR("__sloppy.fs"),
         SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE) | SL_FEATURE_BIT(SL_RUNTIME_FEATURE_V8) |
             SL_FEATURE_BIT(SL_RUNTIME_FEATURE_STDLIB_TIME),
         true, true, true},
        {SL_RUNTIME_FEATURE_PROVIDER_SQLITE, SL_RUNTIME_FEATURE_KIND_PROVIDER,
         SL_FEATURE_STR("provider.sqlite"), SL_FEATURE_STR("SQLite provider"),
         SL_FEATURE_STR("sloppy/providers/sqlite"), SL_FEATURE_STR("__sloppy.data.sqlite"),
         SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE) | SL_FEATURE_BIT(SL_RUNTIME_FEATURE_V8) |
             SL_FEATURE_BIT(SL_RUNTIME_FEATURE_STDLIB_DATA),
         true, true, true},
        {SL_RUNTIME_FEATURE_PROVIDER_POSTGRES, SL_RUNTIME_FEATURE_KIND_PROVIDER,
         SL_FEATURE_STR("provider.postgres"), SL_FEATURE_STR("PostgreSQL provider"),
         SL_FEATURE_STR("sloppy/providers/postgres"), SL_FEATURE_EMPTY,
         SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE) | SL_FEATURE_BIT(SL_RUNTIME_FEATURE_V8), false,
         false, true},
        {SL_RUNTIME_FEATURE_PROVIDER_SQLSERVER, SL_RUNTIME_FEATURE_KIND_PROVIDER,
         SL_FEATURE_STR("provider.sqlserver"), SL_FEATURE_STR("SQL Server provider"),
         SL_FEATURE_STR("sloppy/providers/sqlserver"), SL_FEATURE_EMPTY,
         SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE) | SL_FEATURE_BIT(SL_RUNTIME_FEATURE_V8), false,
         false, true},
        {SL_RUNTIME_FEATURE_STDLIB_TIME, SL_RUNTIME_FEATURE_KIND_STDLIB,
         SL_FEATURE_STR("stdlib.time"), SL_FEATURE_STR("time stdlib"),
         SL_FEATURE_STR("sloppy/time"), SL_FEATURE_STR("__sloppy.time"),
         SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE) | SL_FEATURE_BIT(SL_RUNTIME_FEATURE_V8), true,
         true, true},
        {SL_RUNTIME_FEATURE_STDLIB_CRYPTO, SL_RUNTIME_FEATURE_KIND_STDLIB,
         SL_FEATURE_STR("stdlib.crypto"), SL_FEATURE_STR("crypto stdlib"),
         SL_FEATURE_STR("sloppy/crypto"), SL_FEATURE_STR("__sloppy.crypto"),
         SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE) | SL_FEATURE_BIT(SL_RUNTIME_FEATURE_V8), true,
         true, true},
        {SL_RUNTIME_FEATURE_STDLIB_CODEC, SL_RUNTIME_FEATURE_KIND_STDLIB,
         SL_FEATURE_STR("stdlib.codec"), SL_FEATURE_STR("codec stdlib"),
         SL_FEATURE_STR("sloppy/codec"), SL_FEATURE_STR("__sloppy.codec"),
         SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE) | SL_FEATURE_BIT(SL_RUNTIME_FEATURE_V8), false,
         true, true},
        {SL_RUNTIME_FEATURE_STDLIB_NET, SL_RUNTIME_FEATURE_KIND_STDLIB,
         SL_FEATURE_STR("stdlib.net"), SL_FEATURE_STR("network stdlib"),
         SL_FEATURE_STR("sloppy/net"), SL_FEATURE_STR("__sloppy.net"),
         SL_FEATURE_BIT(SL_RUNTIME_FEATURE_CORE) | SL_FEATURE_BIT(SL_RUNTIME_FEATURE_V8) |
             SL_FEATURE_BIT(SL_RUNTIME_FEATURE_TRANSPORT_LIBUV) |
             SL_FEATURE_BIT(SL_RUNTIME_FEATURE_STDLIB_TIME),
         false, true, true}};

    if ((uint32_t)id >= (uint32_t)SL_RUNTIME_FEATURE_COUNT) {
        return NULL;
    }
    return &descriptors[(size_t)id];
}

SlStr sl_runtime_feature_id_name(SlRuntimeFeatureId id)
{
    const SlRuntimeFeatureDescriptor* descriptor = sl_runtime_feature_descriptor(id);
    return descriptor == NULL ? sl_str_empty() : descriptor->stable_id;
}

SlStr sl_runtime_feature_kind_name(SlRuntimeFeatureKind kind)
{
    switch (kind) {
    case SL_RUNTIME_FEATURE_KIND_CORE:
        return sl_feature_literal("core", sizeof("core") - 1U);
    case SL_RUNTIME_FEATURE_KIND_ENGINE:
        return sl_feature_literal("engine", sizeof("engine") - 1U);
    case SL_RUNTIME_FEATURE_KIND_HTTP:
        return sl_feature_literal("http", sizeof("http") - 1U);
    case SL_RUNTIME_FEATURE_KIND_TRANSPORT:
        return sl_feature_literal("transport", sizeof("transport") - 1U);
    case SL_RUNTIME_FEATURE_KIND_STDLIB:
        return sl_feature_literal("stdlib", sizeof("stdlib") - 1U);
    case SL_RUNTIME_FEATURE_KIND_PROVIDER:
        return sl_feature_literal("provider", sizeof("provider") - 1U);
    default:
        return sl_str_empty();
    }
}

SlStr sl_runtime_feature_activation_reason_name(SlRuntimeFeatureActivationReason reason)
{
    switch (reason) {
    case SL_RUNTIME_FEATURE_REASON_CORE:
        return sl_feature_literal("core", sizeof("core") - 1U);
    case SL_RUNTIME_FEATURE_REASON_PLAN_TARGET:
        return sl_feature_literal("plan target", sizeof("plan target") - 1U);
    case SL_RUNTIME_FEATURE_REASON_PLAN_ROUTE:
        return sl_feature_literal("plan route", sizeof("plan route") - 1U);
    case SL_RUNTIME_FEATURE_REASON_PLAN_PROVIDER:
        return sl_feature_literal("plan provider", sizeof("plan provider") - 1U);
    case SL_RUNTIME_FEATURE_REASON_PLAN_REQUIRED_FEATURE:
        return sl_feature_literal("plan required feature", sizeof("plan required feature") - 1U);
    case SL_RUNTIME_FEATURE_REASON_DEPENDENCY:
        return sl_feature_literal("feature dependency", sizeof("feature dependency") - 1U);
    default:
        return sl_str_empty();
    }
}

bool sl_runtime_feature_set_contains(const SlRuntimeFeatureSet* set, SlRuntimeFeatureId id)
{
    if (set == NULL || (uint32_t)id >= (uint32_t)SL_RUNTIME_FEATURE_COUNT) {
        return false;
    }
    return (set->active_mask & SL_FEATURE_BIT(id)) != 0U;
}

SlStatus sl_runtime_feature_id_from_str(SlStr text, SlRuntimeFeatureId* out)
{
    size_t index = 0U;

    if (out == NULL || (text.length > 0U && text.ptr == NULL)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = SL_RUNTIME_FEATURE_COUNT;
    for (index = 0U; index < (size_t)SL_RUNTIME_FEATURE_COUNT; index += 1U) {
        const SlRuntimeFeatureDescriptor* descriptor =
            sl_runtime_feature_descriptor((SlRuntimeFeatureId)index);
        if (descriptor != NULL && sl_str_equal(text, descriptor->stable_id)) {
            *out = descriptor->id;
            return sl_status_ok();
        }
    }
    return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
}

static SlStatus sl_feature_build_diag(SlArena* arena, SlDiag* out_diag, SlDiagCode code,
                                      SlStr message, SlStr feature, SlStr requested_by, SlStr hint)
{
    SlDiagBuilder builder;
    SlStatus status;

    if (out_diag == NULL) {
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    }
    if (arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_diag_builder_init(&builder, arena, SL_DIAG_SEVERITY_ERROR, code, message);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!sl_str_is_empty(feature)) {
        status = sl_diag_builder_add_related(&builder, sl_source_span_unknown(), feature);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    if (!sl_str_is_empty(requested_by)) {
        status = sl_diag_builder_add_related(&builder, sl_source_span_unknown(), requested_by);
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
    status = sl_diag_builder_finish(&builder, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_status_from_code(code == SL_DIAG_UNKNOWN_RUNTIME_FEATURE ? SL_STATUS_INVALID_ARGUMENT
                                                                       : SL_STATUS_UNSUPPORTED);
}

static SlStatus sl_feature_activate_id(SlRuntimeFeatureSet* set, SlRuntimeFeatureId id,
                                       SlRuntimeFeatureActivationReason reason, SlStr requested_by)
{
    if (set == NULL || (uint32_t)id >= (uint32_t)SL_RUNTIME_FEATURE_COUNT) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if ((set->active_mask & SL_FEATURE_BIT(id)) == 0U) {
        SlRuntimeFeatureActivation* activation = NULL;
        if (set->activation_count >= (size_t)SL_RUNTIME_FEATURE_COUNT) {
            return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
        }
        activation = &set->activations[set->activation_count];
        activation->id = id;
        activation->reason = reason;
        activation->requested_by = requested_by;
        set->activation_count += 1U;
        set->active_mask |= SL_FEATURE_BIT(id);
    }
    return sl_status_ok();
}

static SlStatus sl_feature_require_id(SlRuntimeFeatureSet* set,
                                      const SlRuntimeFeatureAvailability* availability,
                                      SlArena* diag_arena, SlRuntimeFeatureId id,
                                      SlRuntimeFeatureActivationReason reason, SlStr requested_by,
                                      SlDiag* out_diag)
{
    SlRuntimeFeatureDescriptor descriptor =
        sl_feature_descriptor_with_availability(id, availability);
    SlStatus status;
    size_t index = 0U;

    if (!descriptor.available) {
        SlStr hint =
            id == SL_RUNTIME_FEATURE_V8
                ? sl_feature_literal("requires V8-enabled build; configure the V8 runtime lane or "
                                     "remove the Plan feature",
                                     sizeof("requires V8-enabled build; configure the V8 runtime "
                                            "lane or remove the Plan feature") -
                                         1U)
                : sl_feature_literal("configure the required runtime lane or remove the Plan "
                                     "feature",
                                     sizeof("configure the required runtime lane or remove the "
                                            "Plan feature") -
                                         1U);
        return sl_feature_build_diag(
            diag_arena, out_diag, SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE,
            sl_feature_literal("runtime feature is unavailable",
                               sizeof("runtime feature is unavailable") - 1U),
            descriptor.stable_id, requested_by, hint);
    }

    for (index = 0U; index < (size_t)SL_RUNTIME_FEATURE_COUNT; index += 1U) {
        if ((descriptor.dependencies & SL_FEATURE_BIT(index)) != 0U) {
            SlRuntimeFeatureDescriptor dependency =
                sl_feature_descriptor_with_availability((SlRuntimeFeatureId)index, availability);
            if (!dependency.available) {
                return sl_feature_build_diag(
                    diag_arena, out_diag, SL_DIAG_RUNTIME_FEATURE_DEPENDENCY_MISSING,
                    sl_feature_literal("runtime feature dependency is unavailable",
                                       sizeof("runtime feature dependency is unavailable") - 1U),
                    dependency.stable_id, descriptor.stable_id,
                    sl_feature_literal(
                        "enable the dependency runtime lane before activating this feature",
                        sizeof("enable the dependency runtime lane before activating this "
                               "feature") -
                            1U));
            }
            status = sl_feature_require_id(set, availability, diag_arena, (SlRuntimeFeatureId)index,
                                           SL_RUNTIME_FEATURE_REASON_DEPENDENCY,
                                           descriptor.stable_id, out_diag);
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
    }

    return sl_feature_activate_id(set, id, reason, requested_by);
}

static SlStatus sl_feature_require_text(SlRuntimeFeatureSet* set,
                                        const SlRuntimeFeatureAvailability* availability,
                                        SlArena* diag_arena, SlStr text,
                                        SlRuntimeFeatureActivationReason reason, SlStr requested_by,
                                        SlDiag* out_diag)
{
    SlRuntimeFeatureId id = SL_RUNTIME_FEATURE_COUNT;
    SlStatus status = sl_runtime_feature_id_from_str(text, &id);

    if (!sl_status_is_ok(status)) {
        return sl_feature_build_diag(
            diag_arena, out_diag, SL_DIAG_UNKNOWN_RUNTIME_FEATURE,
            sl_feature_literal("unknown runtime feature", sizeof("unknown runtime feature") - 1U),
            text, requested_by,
            sl_feature_literal("use a feature id known by this runtime",
                               sizeof("use a feature id known by this runtime") - 1U));
    }

    return sl_feature_require_id(set, availability, diag_arena, id, reason, requested_by, out_diag);
}

static SlRuntimeFeatureId sl_feature_provider_id(SlStr provider)
{
    if (sl_str_equal(provider, sl_str_from_cstr("sqlite"))) {
        return SL_RUNTIME_FEATURE_PROVIDER_SQLITE;
    }
    if (sl_str_equal(provider, sl_str_from_cstr("postgres"))) {
        return SL_RUNTIME_FEATURE_PROVIDER_POSTGRES;
    }
    if (sl_str_equal(provider, sl_str_from_cstr("sqlserver"))) {
        return SL_RUNTIME_FEATURE_PROVIDER_SQLSERVER;
    }
    return SL_RUNTIME_FEATURE_COUNT;
}

static SlStatus sl_feature_require_plan_target(const SlPlan* plan, SlRuntimeFeatureSet* set,
                                               const SlRuntimeFeatureAvailability* availability,
                                               SlArena* diag_arena, SlDiag* out_diag)
{
    if (!sl_str_equal(plan->target.engine, sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8))) {
        return sl_status_ok();
    }

    return sl_feature_require_id(
        set, availability, diag_arena, SL_RUNTIME_FEATURE_V8, SL_RUNTIME_FEATURE_REASON_PLAN_TARGET,
        sl_feature_literal("target.engine", sizeof("target.engine") - 1U), out_diag);
}

static SlStatus sl_feature_require_plan_routes(const SlPlan* plan, SlRuntimeFeatureSet* set,
                                               const SlRuntimeFeatureAvailability* availability,
                                               SlArena* diag_arena, SlDiag* out_diag)
{
    SlStatus status;

    if (plan->route_count == 0U) {
        return sl_status_ok();
    }

    status =
        sl_feature_require_id(set, availability, diag_arena, SL_RUNTIME_FEATURE_STDLIB_APP,
                              SL_RUNTIME_FEATURE_REASON_PLAN_ROUTE,
                              sl_feature_literal("routes[]", sizeof("routes[]") - 1U), out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status =
        sl_feature_require_id(set, availability, diag_arena, SL_RUNTIME_FEATURE_STDLIB_RESULTS,
                              SL_RUNTIME_FEATURE_REASON_PLAN_ROUTE,
                              sl_feature_literal("routes[]", sizeof("routes[]") - 1U), out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_feature_require_id(set, availability, diag_arena, SL_RUNTIME_FEATURE_HTTP,
                                 SL_RUNTIME_FEATURE_REASON_PLAN_ROUTE,
                                 sl_feature_literal("routes[]", sizeof("routes[]") - 1U), out_diag);
}

static SlStatus sl_feature_require_plan_providers(const SlPlan* plan, SlRuntimeFeatureSet* set,
                                                  const SlRuntimeFeatureAvailability* availability,
                                                  SlArena* diag_arena, SlDiag* out_diag)
{
    size_t index = 0U;

    for (index = 0U; index < plan->data_provider_count; index += 1U) {
        SlRuntimeFeatureId provider_id =
            sl_feature_provider_id(plan->data_providers[index].provider);
        if ((uint32_t)provider_id >= (uint32_t)SL_RUNTIME_FEATURE_COUNT) {
            continue;
        }
        SlStatus status = sl_feature_require_id(set, availability, diag_arena, provider_id,
                                                SL_RUNTIME_FEATURE_REASON_PLAN_PROVIDER,
                                                plan->data_providers[index].token, out_diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    return sl_status_ok();
}

static SlStatus sl_feature_require_plan_explicit(const SlPlan* plan, SlRuntimeFeatureSet* set,
                                                 const SlRuntimeFeatureAvailability* availability,
                                                 SlArena* diag_arena, SlDiag* out_diag)
{
    size_t index = 0U;

    for (index = 0U; index < plan->required_feature_count; index += 1U) {
        SlStatus status = sl_feature_require_text(
            set, availability, diag_arena, plan->required_features[index].id,
            SL_RUNTIME_FEATURE_REASON_PLAN_REQUIRED_FEATURE,
            sl_feature_literal("requiredFeatures[]", sizeof("requiredFeatures[]") - 1U), out_diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    return sl_status_ok();
}

SlStatus sl_runtime_feature_activate_plan(const SlPlan* plan,
                                          const SlRuntimeFeatureAvailability* availability,
                                          SlArena* diag_arena, SlRuntimeFeatureSet* out_set,
                                          SlDiag* out_diag)
{
    SlRuntimeFeatureAvailability default_availability;
    const SlRuntimeFeatureAvailability* resolved_availability = availability;
    SlRuntimeFeatureSet set = {0};
    SlStatus status;

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (plan == NULL || out_set == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (resolved_availability == NULL) {
        default_availability = sl_runtime_feature_default_availability();
        resolved_availability = &default_availability;
    }

    status = sl_feature_require_id(
        &set, resolved_availability, diag_arena, SL_RUNTIME_FEATURE_CORE,
        SL_RUNTIME_FEATURE_REASON_CORE,
        sl_feature_literal("runtime startup", sizeof("runtime startup") - 1U), out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status =
        sl_feature_require_plan_target(plan, &set, resolved_availability, diag_arena, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status =
        sl_feature_require_plan_routes(plan, &set, resolved_availability, diag_arena, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status =
        sl_feature_require_plan_providers(plan, &set, resolved_availability, diag_arena, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status =
        sl_feature_require_plan_explicit(plan, &set, resolved_availability, diag_arena, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out_set = set;
    return sl_status_ok();
}
