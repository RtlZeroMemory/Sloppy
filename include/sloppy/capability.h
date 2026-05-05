#ifndef SLOPPY_CAPABILITY_H
#define SLOPPY_CAPABILITY_H

#include "sloppy/arena.h"
#include "sloppy/diagnostics.h"
#include "sloppy/plan.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SlCapabilityAccess
{
    SL_CAPABILITY_ACCESS_UNKNOWN = 0,
    SL_CAPABILITY_ACCESS_READ = 1,
    SL_CAPABILITY_ACCESS_WRITE = 2,
    SL_CAPABILITY_ACCESS_READWRITE = 3,
    SL_CAPABILITY_ACCESS_CONNECT = 4,
    SL_CAPABILITY_ACCESS_LISTEN = 5,
    SL_CAPABILITY_ACCESS_CONNECT_LISTEN = 6,
    SL_CAPABILITY_ACCESS_APPEND = 7,
    SL_CAPABILITY_ACCESS_DELETE = 8,
    SL_CAPABILITY_ACCESS_LIST = 9,
    SL_CAPABILITY_ACCESS_METADATA = 10,
    SL_CAPABILITY_ACCESS_WATCH = 11,
    SL_CAPABILITY_ACCESS_LOCK = 12
} SlCapabilityAccess;

typedef enum SlCapabilityOperation
{
    SL_CAPABILITY_OPERATION_READ = 1,
    SL_CAPABILITY_OPERATION_WRITE = 2,
    SL_CAPABILITY_OPERATION_CONNECT = 3,
    SL_CAPABILITY_OPERATION_LISTEN = 4,
    /* Provider operations that require both read and write authority. */
    SL_CAPABILITY_OPERATION_READWRITE = 5,
    SL_CAPABILITY_OPERATION_APPEND = 6,
    SL_CAPABILITY_OPERATION_DELETE = 7,
    SL_CAPABILITY_OPERATION_LIST = 8,
    SL_CAPABILITY_OPERATION_METADATA = 9,
    SL_CAPABILITY_OPERATION_WATCH = 10,
    SL_CAPABILITY_OPERATION_LOCK = 11
} SlCapabilityOperation;

/*
 * Immutable runtime view of Plan capability metadata.
 *
 * The registry borrows the parsed SlPlan provider and capability arrays. Callers must keep
 * the parsed plan arena alive for at least as long as the registry. No global mutable
 * registry is created; app hosts and JS-native bridge entrypoints pass the registry
 * explicitly.
 */
typedef struct SlCapabilityRegistry
{
    const SlPlanDataProvider* data_providers;
    size_t data_provider_count;
    const SlPlanCapability* capabilities;
    size_t capability_count;
} SlCapabilityRegistry;

SlStatus sl_capability_registry_init_from_plan(const SlPlan* plan, SlCapabilityRegistry* out);
SlStatus sl_capability_registry_find(const SlCapabilityRegistry* registry, SlStr token,
                                     const SlPlanCapability** out);

SlStatus sl_capability_check_database(const SlCapabilityRegistry* registry, SlArena* diag_arena,
                                      SlStr token, SlCapabilityOperation operation, SlStr provider,
                                      SlDiag* out_diag);
SlStatus sl_capability_check_database_provider(const SlCapabilityRegistry* registry,
                                               SlArena* diag_arena, SlStr token,
                                               SlCapabilityOperation operation, SlStr provider_kind,
                                               SlDiag* out_diag);
SlStatus sl_capability_check_filesystem(const SlCapabilityRegistry* registry, SlArena* diag_arena,
                                        SlStr token, SlCapabilityOperation operation,
                                        SlDiag* out_diag);
SlStatus sl_capability_check_network(const SlCapabilityRegistry* registry, SlArena* diag_arena,
                                     SlStr token, SlCapabilityOperation operation,
                                     SlDiag* out_diag);

#ifdef __cplusplus
}
#endif

#endif
