#ifndef SLOPPY_ROUTE_ARTIFACT_H
#define SLOPPY_ROUTE_ARTIFACT_H

#include "sloppy/bytes.h"
#include "sloppy/diagnostics.h"
#include "sloppy/plan.h"
#include "sloppy/status.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_ROUTE_ARTIFACT_VERSION_1 UINT32_C(1)
#define SL_ROUTE_ARTIFACT_HEADER_SIZE UINT32_C(64)
#define SL_ROUTE_ARTIFACT_ENTRY_SIZE UINT32_C(48)
#define SL_ROUTE_ARTIFACT_ENDIAN_MARKER UINT32_C(0x01020304)

typedef struct SlRouteArtifactSummary
{
    uint32_t version;
    uint32_t route_count;
    uint32_t endpoint_count;
    uint32_t route_table_offset;
    uint32_t route_table_size;
    uint32_t string_table_offset;
    uint32_t string_table_size;
    uint64_t checksum;
} SlRouteArtifactSummary;

SlStatus sl_route_artifact_validate(SlArena* arena, SlBytes artifact, SlStr expected_hash,
                                    const SlPlan* plan, SlRouteArtifactSummary* out_summary,
                                    SlDiag* out_diag);

#ifdef __cplusplus
}
#endif

#endif
