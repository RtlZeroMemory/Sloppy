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
    SL_RUNTIME_FEATURE_COUNT = 14
} SlRuntimeFeatureId;

#ifdef __cplusplus
static_assert(SL_RUNTIME_FEATURE_COUNT <= (sizeof(uint32_t) * 8U),
              "SlRuntimeFeatureSet.active_mask must cover all runtime features");
#else
_Static_assert(SL_RUNTIME_FEATURE_COUNT <= (sizeof(uint32_t) * 8U),
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
    uint32_t dependencies;
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
    uint32_t active_mask;
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
