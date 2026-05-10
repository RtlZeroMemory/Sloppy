/*
 * src/engine/v8/intrinsics.cc
 *
 * Aggregates provider-specific V8 intrinsic modules. engine_v8.cc calls this
 * file once while constructing the private __sloppy namespace; new provider
 * bridges should add sibling intrinsics_<provider>.cc files and register them
 * here instead of growing engine_v8.cc.
 */
#include "engine_v8_internal.h"

bool sl_v8_runtime_feature_active(const SlV8Engine* backend, SlRuntimeFeatureId id)
{
    return backend != nullptr && backend->has_runtime_features &&
           sl_runtime_feature_set_contains(&backend->runtime_features, id);
}

bool sl_v8_install_provider_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context,
                                       v8::Local<v8::Object> data)
{
    if (backend == nullptr) {
        return false;
    }
    if (!backend->has_runtime_features) {
        return sl_v8_install_sqlite_intrinsics(backend->isolate, context, data);
    }
    if (sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_PROVIDER_SQLITE)) {
        if (!sl_v8_install_sqlite_intrinsics(backend->isolate, context, data)) {
            return false;
        }
    }
    if (sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_PROVIDER_POSTGRES)) {
        if (!sl_v8_install_postgres_intrinsics(backend->isolate, context, data)) {
            return false;
        }
    }
    if (sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_PROVIDER_SQLSERVER)) {
        if (!sl_v8_install_sqlserver_intrinsics(backend->isolate, context, data)) {
            return false;
        }
    }
    return true;
}

void sl_v8_append_provider_external_references(std::vector<intptr_t>* refs)
{
    sl_v8_append_sqlite_external_references(refs);
    sl_v8_append_postgres_external_references(refs);
    sl_v8_append_sqlserver_external_references(refs);
}
