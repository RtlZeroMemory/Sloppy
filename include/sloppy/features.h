#ifndef SLOPPY_FEATURES_H
#define SLOPPY_FEATURES_H

#include "sloppy/arena.h"
#include "sloppy/diagnostics.h"
#include "sloppy/plan.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SlRuntimeFeatureId
{
    SL_RUNTIME_FEATURE_CORE = 0,
    SL_RUNTIME_FEATURE_V8 = 1,
    SL_RUNTIME_FEATURE_HTTP = 2,
    SL_RUNTIME_FEATURE_TRANSPORT_LIBUV = 3,
    SL_RUNTIME_FEATURE_STDLIB_APP = 4,
    SL_RUNTIME_FEATURE_STDLIB_RESULTS = 5,
    SL_RUNTIME_FEATURE_STDLIB_SCHEMA = 6,
    SL_RUNTIME_FEATURE_STDLIB_CONFIG = 7,
    SL_RUNTIME_FEATURE_STDLIB_DATA = 8,
    SL_RUNTIME_FEATURE_STDLIB_FS = 9,
    SL_RUNTIME_FEATURE_PROVIDER_SQLITE = 10,
    SL_RUNTIME_FEATURE_PROVIDER_POSTGRES = 11,
    SL_RUNTIME_FEATURE_PROVIDER_SQLSERVER = 12,
    SL_RUNTIME_FEATURE_STDLIB_TIME = 13,
    SL_RUNTIME_FEATURE_STDLIB_CRYPTO = 14,
    SL_RUNTIME_FEATURE_STDLIB_CODEC = 15,
    SL_RUNTIME_FEATURE_STDLIB_NET = 16,
    SL_RUNTIME_FEATURE_STDLIB_OS = 17,
    SL_RUNTIME_FEATURE_STDLIB_HTTP_CLIENT = 18,
    SL_RUNTIME_FEATURE_STDLIB_WORKERS = 19,
    SL_RUNTIME_FEATURE_NODE_COMPAT_PATH = 20,
    SL_RUNTIME_FEATURE_NODE_COMPAT_EVENTS = 21,
    SL_RUNTIME_FEATURE_NODE_COMPAT_URL = 22,
    SL_RUNTIME_FEATURE_NODE_COMPAT_QUERYSTRING = 23,
    SL_RUNTIME_FEATURE_NODE_COMPAT_BUFFER = 24,
    SL_RUNTIME_FEATURE_NODE_COMPAT_UTIL = 25,
    SL_RUNTIME_FEATURE_NODE_COMPAT_TIMERS = 26,
    SL_RUNTIME_FEATURE_NODE_COMPAT_FS = 27,
    SL_RUNTIME_FEATURE_NODE_COMPAT_FS_PROMISES = 28,
    SL_RUNTIME_FEATURE_NODE_COMPAT_OS = 29,
    SL_RUNTIME_FEATURE_NODE_COMPAT_PROCESS = 30,
    SL_RUNTIME_FEATURE_NODE_COMPAT_CRYPTO = 31,
    SL_RUNTIME_FEATURE_STDLIB_FFI = 32,
    SL_RUNTIME_FEATURE_NODE_COMPAT_ASSERT = 33,
    SL_RUNTIME_FEATURE_NODE_COMPAT_STREAM = 34,
    SL_RUNTIME_FEATURE_COUNT = 35
} SlRuntimeFeatureId;

#ifdef __cplusplus
static_assert(SL_RUNTIME_FEATURE_COUNT <= (sizeof(uint64_t) * 8U),
              "SlRuntimeFeatureSet.active_mask must cover all runtime features");
#else
_Static_assert(SL_RUNTIME_FEATURE_COUNT <= (sizeof(uint64_t) * 8U),
               "SlRuntimeFeatureSet.active_mask must cover all runtime features");
#endif

typedef enum SlRuntimeFeatureKind
{
    SL_RUNTIME_FEATURE_KIND_CORE = 0,
    SL_RUNTIME_FEATURE_KIND_ENGINE = 1,
    SL_RUNTIME_FEATURE_KIND_HTTP = 2,
    SL_RUNTIME_FEATURE_KIND_TRANSPORT = 3,
    SL_RUNTIME_FEATURE_KIND_STDLIB = 4,
    SL_RUNTIME_FEATURE_KIND_PROVIDER = 5
} SlRuntimeFeatureKind;

typedef enum SlRuntimeFeatureActivationReason
{
    SL_RUNTIME_FEATURE_REASON_NONE = 0,
    SL_RUNTIME_FEATURE_REASON_CORE = 1,
    SL_RUNTIME_FEATURE_REASON_PLAN_TARGET = 2,
    SL_RUNTIME_FEATURE_REASON_PLAN_ROUTE = 3,
    SL_RUNTIME_FEATURE_REASON_PLAN_PROVIDER = 4,
    SL_RUNTIME_FEATURE_REASON_PLAN_REQUIRED_FEATURE = 5,
    SL_RUNTIME_FEATURE_REASON_DEPENDENCY = 6
} SlRuntimeFeatureActivationReason;

typedef struct SlRuntimeFeatureDescriptor
{
    SlRuntimeFeatureId id;
    SlRuntimeFeatureKind kind;
    SlStr stable_id;
    SlStr diagnostics_name;
    SlStr stdlib_import;
    SlStr v8_intrinsic_namespace;
    uint64_t dependencies;
    bool available;
    bool requires_v8_intrinsics;
    bool package_include_hint;
} SlRuntimeFeatureDescriptor;

typedef struct SlRuntimeFeatureActivation
{
    SlRuntimeFeatureId id;
    SlRuntimeFeatureActivationReason reason;
    SlStr requested_by;
} SlRuntimeFeatureActivation;

typedef struct SlRuntimeFeatureSet
{
    uint64_t active_mask;
    SlRuntimeFeatureActivation activations[SL_RUNTIME_FEATURE_COUNT];
    size_t activation_count;
} SlRuntimeFeatureSet;

typedef struct SlRuntimeFeatureAvailability
{
    bool v8;
    bool http;
    bool transport_libuv;
    bool provider_sqlite;
    bool provider_postgres;
    bool provider_sqlserver;
    bool stdlib_crypto;
    bool stdlib_codec;
    bool stdlib_net;
    bool stdlib_os;
    bool stdlib_http_client;
    bool stdlib_workers;
    bool stdlib_ffi;
} SlRuntimeFeatureAvailability;

SlRuntimeFeatureAvailability sl_runtime_feature_default_availability(void);
const SlRuntimeFeatureDescriptor* sl_runtime_feature_descriptor(SlRuntimeFeatureId id);
SlStr sl_runtime_feature_id_name(SlRuntimeFeatureId id);
SlStr sl_runtime_feature_kind_name(SlRuntimeFeatureKind kind);
SlStr sl_runtime_feature_activation_reason_name(SlRuntimeFeatureActivationReason reason);
bool sl_runtime_feature_set_contains(const SlRuntimeFeatureSet* set, SlRuntimeFeatureId id);
SlStatus sl_runtime_feature_id_from_str(SlStr text, SlRuntimeFeatureId* out);
SlStatus sl_runtime_feature_activate_plan(const SlPlan* plan,
                                          const SlRuntimeFeatureAvailability* availability,
                                          SlArena* diag_arena, SlRuntimeFeatureSet* out_set,
                                          SlDiag* out_diag);

#ifdef __cplusplus
}
#endif

#endif
