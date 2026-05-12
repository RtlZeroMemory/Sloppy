#include "sloppy/route_artifact.h"

#include "sloppy/crypto.h"
#include "sloppy/http.h"
#include "sloppy/string.h"

#include <stdint.h>
#define SLRT_CHECKSUM_OFFSET 40U
#define SLRT_FNV_OFFSET_BASIS UINT64_C(0xcbf29ce484222325)
#define SLRT_FNV_PRIME UINT64_C(0x00000100000001b3)

static SlStr slrt_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static SlStatus slrt_diag(SlArena* arena, SlDiag* out_diag, SlStr message, SlStr hint)
{
    SlDiagBuilder builder = {0};
    SlStatus status;

    if (out_diag == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_diag = (SlDiag){0};
    if (arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_diag_builder_init(&builder, arena, SL_DIAG_SEVERITY_ERROR,
                                  SL_DIAG_INVALID_PLAN_FIELD, message);
    if (!sl_status_is_ok(status)) {
        return status;
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
    return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
}

static SlStatus slrt_fail(SlArena* arena, SlDiag* out_diag, const char* message, size_t message_len,
                          const char* hint, size_t hint_len)
{
    if (out_diag != NULL) {
        return slrt_diag(arena, out_diag, slrt_literal(message, message_len),
                         slrt_literal(hint, hint_len));
    }
    (void)arena;
    return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
}

static bool slrt_range_valid(size_t length, uint32_t offset, uint32_t size)
{
    return (size_t)offset <= length && (size_t)size <= length - (size_t)offset;
}

static uint32_t slrt_u32_at(SlBytes bytes, size_t offset)
{
    return (uint32_t)bytes.ptr[offset] | ((uint32_t)bytes.ptr[offset + 1U] << 8U) |
           ((uint32_t)bytes.ptr[offset + 2U] << 16U) | ((uint32_t)bytes.ptr[offset + 3U] << 24U);
}

static uint64_t slrt_u64_at(SlBytes bytes, size_t offset)
{
    uint64_t value = 0U;
    size_t index = 0U;

    for (index = 0U; index < 8U; index += 1U) {
        value |= ((uint64_t)bytes.ptr[offset + index]) << (index * 8U);
    }
    return value;
}

static uint64_t slrt_checksum(SlBytes bytes)
{
    uint64_t hash = SLRT_FNV_OFFSET_BASIS;
    size_t index = 0U;

    for (index = 0U; index < bytes.length; index += 1U) {
        unsigned char byte = bytes.ptr[index];
        if (index >= SLRT_CHECKSUM_OFFSET && index < SLRT_CHECKSUM_OFFSET + 8U) {
            byte = 0U;
        }
        hash ^= (uint64_t)byte;
        hash *= SLRT_FNV_PRIME;
    }
    return hash;
}

static SlStatus slrt_validate_hash(SlBytes artifact, SlStr expected_hash)
{
    unsigned char digest[SL_CRYPTO_SHA256_SIZE] = {0};
    char hex[sizeof("sha256:") - 1U + ((size_t)SL_CRYPTO_SHA256_SIZE * 2U)] = {0};
    SlStatus status;
    size_t prefix_index = 0U;

    if (sl_str_is_empty(expected_hash)) {
        return sl_status_ok();
    }
    if (expected_hash.length != sizeof(hex)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    for (prefix_index = 0U; prefix_index < sizeof("sha256:") - 1U; prefix_index += 1U) {
        hex[prefix_index] = "sha256:"[prefix_index];
    }
    status =
        sl_crypto_hash(SL_CRYPTO_HASH_SHA256, artifact, (SlOwnedBytes){digest, sizeof(digest)});
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status =
        sl_crypto_hex_encode(sl_bytes_from_parts(digest, sizeof(digest)),
                             hex + sizeof("sha256:") - 1U, sizeof(hex) - (sizeof("sha256:") - 1U));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_str_equal(expected_hash, sl_str_from_parts(hex, sizeof(hex)))
               ? sl_status_ok()
               : sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
}

static uint32_t slrt_method_code(SlStr method)
{
    SlHttpMethod parsed = SL_HTTP_METHOD_UNKNOWN;
    if (!sl_status_is_ok(sl_http_method_from_str(method, &parsed))) {
        return 0U;
    }
    return (uint32_t)parsed;
}

static SlStatus slrt_validate_entry(SlArena* arena, SlDiag* out_diag, SlBytes artifact,
                                    const SlRouteArtifactSummary* summary, const SlPlanRoute* route,
                                    size_t route_index)
{
    size_t entry_offset =
        (size_t)summary->route_table_offset + (route_index * SL_ROUTE_ARTIFACT_ENTRY_SIZE);
    uint32_t method = slrt_u32_at(artifact, entry_offset);
    uint32_t handler_id = slrt_u32_at(artifact, entry_offset + 4U);
    uint32_t pattern_offset = slrt_u32_at(artifact, entry_offset + 8U);
    uint32_t pattern_len = slrt_u32_at(artifact, entry_offset + 12U);
    uint32_t name_offset = slrt_u32_at(artifact, entry_offset + 16U);
    uint32_t name_len = slrt_u32_at(artifact, entry_offset + 20U);
    uint32_t execution_kind = slrt_u32_at(artifact, entry_offset + 28U);
    uint32_t pattern_abs = summary->string_table_offset + pattern_offset;
    uint32_t name_abs = summary->string_table_offset + name_offset;

    if (method != slrt_method_code(route->method) || handler_id != route->handler_id) {
        return slrt_fail(arena, out_diag, "invalid SLRT route endpoint",
                         sizeof("invalid SLRT route endpoint") - 1U,
                         "route artifact endpoint entries must match Plan routes by index",
                         sizeof("route artifact endpoint entries must match Plan routes by "
                                "index") -
                             1U);
    }
    if (execution_kind < 1U || execution_kind > 3U) {
        return slrt_fail(arena, out_diag, "invalid SLRT execution kind",
                         sizeof("invalid SLRT execution kind") - 1U,
                         "route artifact execution kinds must be known by this runtime",
                         sizeof("route artifact execution kinds must be known by this runtime") -
                             1U);
    }
    if (!slrt_range_valid(artifact.length, pattern_abs, pattern_len) ||
        !slrt_range_valid(artifact.length, name_abs, name_len) ||
        pattern_offset > summary->string_table_size ||
        pattern_len > summary->string_table_size - pattern_offset ||
        name_offset > summary->string_table_size ||
        name_len > summary->string_table_size - name_offset)
    {
        return slrt_fail(arena, out_diag, "invalid SLRT string range",
                         sizeof("invalid SLRT string range") - 1U,
                         "route artifact string offsets must stay within the string table",
                         sizeof("route artifact string offsets must stay within the string "
                                "table") -
                             1U);
    }
    if (!sl_str_equal(route->pattern,
                      sl_str_from_parts((const char*)artifact.ptr + pattern_abs, pattern_len)) ||
        !sl_str_equal(route->name,
                      sl_str_from_parts((const char*)artifact.ptr + name_abs, name_len)))
    {
        return slrt_fail(arena, out_diag, "SLRT route metadata mismatch",
                         sizeof("SLRT route metadata mismatch") - 1U,
                         "route artifact pattern and name strings must match app.plan.json",
                         sizeof("route artifact pattern and name strings must match "
                                "app.plan.json") -
                             1U);
    }
    return sl_status_ok();
}

SlStatus sl_route_artifact_validate(SlArena* arena, SlBytes artifact, SlStr expected_hash,
                                    const SlPlan* plan, SlRouteArtifactSummary* out_summary,
                                    SlDiag* out_diag)
{
    SlRouteArtifactSummary summary = {0};
    static const unsigned char slrt_magic[4] = {'S', 'L', 'R', 'T'};
    uint64_t checksum = 0U;
    size_t index = 0U;
    SlStatus status;

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (artifact.ptr == NULL || plan == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (artifact.length < SL_ROUTE_ARTIFACT_HEADER_SIZE) {
        return slrt_fail(arena, out_diag, "truncated SLRT route artifact",
                         sizeof("truncated SLRT route artifact") - 1U,
                         "routes.slrt must contain a complete header",
                         sizeof("routes.slrt must contain a complete header") - 1U);
    }
    if (!sl_bytes_equal(sl_bytes_from_parts(artifact.ptr, 4U),
                        sl_bytes_from_parts(slrt_magic, sizeof(slrt_magic))))
    {
        return slrt_fail(arena, out_diag, "invalid SLRT route artifact magic",
                         sizeof("invalid SLRT route artifact magic") - 1U,
                         "routes.slrt must start with the SLRT magic",
                         sizeof("routes.slrt must start with the SLRT magic") - 1U);
    }

    summary.version = slrt_u32_at(artifact, 4U);
    if (summary.version != SL_ROUTE_ARTIFACT_VERSION_1) {
        return slrt_fail(arena, out_diag, "unsupported SLRT route artifact version",
                         sizeof("unsupported SLRT route artifact version") - 1U,
                         "regenerate the artifacts with a compatible sloppyc",
                         sizeof("regenerate the artifacts with a compatible sloppyc") - 1U);
    }
    if (slrt_u32_at(artifact, 8U) != SL_ROUTE_ARTIFACT_ENDIAN_MARKER ||
        slrt_u32_at(artifact, 12U) != SL_ROUTE_ARTIFACT_HEADER_SIZE)
    {
        return slrt_fail(arena, out_diag, "invalid SLRT route artifact header",
                         sizeof("invalid SLRT route artifact header") - 1U,
                         "routes.slrt header size and endian marker must match runtime",
                         sizeof("routes.slrt header size and endian marker must match runtime") -
                             1U);
    }

    summary.route_count = slrt_u32_at(artifact, 16U);
    summary.endpoint_count = slrt_u32_at(artifact, 20U);
    summary.route_table_offset = slrt_u32_at(artifact, 24U);
    summary.route_table_size = slrt_u32_at(artifact, 28U);
    summary.string_table_offset = slrt_u32_at(artifact, 32U);
    summary.string_table_size = slrt_u32_at(artifact, 36U);
    summary.checksum = slrt_u64_at(artifact, SLRT_CHECKSUM_OFFSET);

    if (summary.route_count != plan->route_count || summary.endpoint_count != plan->route_count ||
        summary.route_table_size != summary.route_count * SL_ROUTE_ARTIFACT_ENTRY_SIZE)
    {
        return slrt_fail(arena, out_diag, "SLRT route count mismatch",
                         sizeof("SLRT route count mismatch") - 1U,
                         "route artifact counts must match app.plan.json routes",
                         sizeof("route artifact counts must match app.plan.json routes") - 1U);
    }
    if (!slrt_range_valid(artifact.length, summary.route_table_offset, summary.route_table_size) ||
        !slrt_range_valid(artifact.length, summary.string_table_offset, summary.string_table_size))
    {
        return slrt_fail(arena, out_diag, "invalid SLRT section range",
                         sizeof("invalid SLRT section range") - 1U,
                         "route artifact sections must be inside the file",
                         sizeof("route artifact sections must be inside the file") - 1U);
    }
    if ((size_t)summary.route_table_offset + summary.route_table_size >
        (size_t)summary.string_table_offset)
    {
        return slrt_fail(arena, out_diag, "overlapping SLRT sections",
                         sizeof("overlapping SLRT sections") - 1U,
                         "route table and string table sections must not overlap",
                         sizeof("route table and string table sections must not overlap") - 1U);
    }

    checksum = slrt_checksum(artifact);
    if (checksum != summary.checksum) {
        return slrt_fail(
            arena, out_diag, "SLRT checksum mismatch", sizeof("SLRT checksum mismatch") - 1U,
            "routes.slrt is corrupted or was not emitted with app.plan.json",
            sizeof("routes.slrt is corrupted or was not emitted with app.plan.json") - 1U);
    }
    status = slrt_validate_hash(artifact, expected_hash);
    if (!sl_status_is_ok(status)) {
        return slrt_fail(arena, out_diag, "SLRT hash mismatch", sizeof("SLRT hash mismatch") - 1U,
                         "routeDispatch.artifact.hash must match routes.slrt bytes",
                         sizeof("routeDispatch.artifact.hash must match routes.slrt bytes") - 1U);
    }

    for (index = 0U; index < plan->route_count; index += 1U) {
        status =
            slrt_validate_entry(arena, out_diag, artifact, &summary, &plan->routes[index], index);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    if (out_summary != NULL) {
        *out_summary = summary;
    }
    return sl_status_ok();
}
