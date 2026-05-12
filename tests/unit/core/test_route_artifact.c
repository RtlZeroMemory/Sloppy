#include "sloppy/route_artifact.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_ARENA_SIZE 4096U
#define TEST_ARTIFACT_SIZE 128U
#define TEST_CHECKSUM_OFFSET 40U
#define TEST_FNV_OFFSET_BASIS UINT64_C(0xcbf29ce484222325)
#define TEST_FNV_PRIME UINT64_C(0x00000100000001b3)

static int expect_status(SlStatus status, SlStatusCode code)
{
    return sl_status_code(status) == code ? 0 : 1;
}

static void put_u32(unsigned char* bytes, size_t offset, uint32_t value)
{
    bytes[offset] = (unsigned char)(value & 0xffU);
    bytes[offset + 1U] = (unsigned char)((value >> 8U) & 0xffU);
    bytes[offset + 2U] = (unsigned char)((value >> 16U) & 0xffU);
    bytes[offset + 3U] = (unsigned char)((value >> 24U) & 0xffU);
}

static void put_u64(unsigned char* bytes, size_t offset, uint64_t value)
{
    size_t index = 0U;
    for (index = 0U; index < 8U; index += 1U) {
        bytes[offset + index] = (unsigned char)((value >> (index * 8U)) & 0xffU);
    }
}

static void put_bytes(unsigned char* bytes, size_t offset, SlBytes source)
{
    size_t index = 0U;

    for (index = 0U; index < source.length; index += 1U) {
        bytes[offset + index] = source.ptr[index];
    }
}

static uint64_t checksum(const unsigned char* bytes, size_t length)
{
    uint64_t hash = TEST_FNV_OFFSET_BASIS;
    size_t index = 0U;
    for (index = 0U; index < length; index += 1U) {
        unsigned char byte = bytes[index];
        if (index >= TEST_CHECKSUM_OFFSET && index < TEST_CHECKSUM_OFFSET + 8U) {
            byte = 0U;
        }
        hash ^= (uint64_t)byte;
        hash *= TEST_FNV_PRIME;
    }
    return hash;
}

static SlPlan one_route_plan(SlPlanHandler* handler, SlPlanRoute* route)
{
    SlPlan plan = {0};

    handler->id = 1U;
    handler->export_name = sl_str_from_cstr("__sloppy_handler_1");
    handler->display_name = sl_str_from_cstr("Health");
    route->method = sl_str_from_cstr("GET");
    route->pattern = sl_str_from_cstr("/health");
    route->handler_id = 1U;
    route->name = sl_str_from_cstr("health");
    plan.version = SL_PLAN_CURRENT_VERSION;
    plan.handlers = handler;
    plan.handler_count = 1U;
    plan.routes = route;
    plan.route_count = 1U;
    return plan;
}

static SlBytes make_artifact(unsigned char* bytes)
{
    static const unsigned char magic[4] = {'S', 'L', 'R', 'T'};
    const char* pattern = "/health";
    const char* name = "health";
    size_t string_offset = 112U;

    memset(bytes, 0, TEST_ARTIFACT_SIZE);
    put_bytes(bytes, 0U, sl_bytes_from_parts(magic, sizeof(magic)));
    put_u32(bytes, 4U, SL_ROUTE_ARTIFACT_VERSION_1);
    put_u32(bytes, 8U, SL_ROUTE_ARTIFACT_ENDIAN_MARKER);
    put_u32(bytes, 12U, SL_ROUTE_ARTIFACT_HEADER_SIZE);
    put_u32(bytes, 16U, 1U);
    put_u32(bytes, 20U, 1U);
    put_u32(bytes, 24U, SL_ROUTE_ARTIFACT_HEADER_SIZE);
    put_u32(bytes, 28U, SL_ROUTE_ARTIFACT_ENTRY_SIZE);
    put_u32(bytes, 32U, (uint32_t)string_offset);
    put_u32(bytes, 36U, 13U);
    put_u32(bytes, 56U, (uint32_t)string_offset);
    put_u32(bytes, 60U, 13U);
    put_u32(bytes, 64U, 1U);
    put_u32(bytes, 68U, 1U);
    put_u32(bytes, 72U, 0U);
    put_u32(bytes, 76U, 7U);
    put_u32(bytes, 80U, 7U);
    put_u32(bytes, 84U, 6U);
    put_u32(bytes, 88U, 1U);
    put_u32(bytes, 92U, 1U);
    put_bytes(bytes, string_offset, sl_bytes_from_parts((const unsigned char*)pattern, 7U));
    put_bytes(bytes, string_offset + 7U, sl_bytes_from_parts((const unsigned char*)name, 6U));
    put_u64(bytes, TEST_CHECKSUM_OFFSET, checksum(bytes, TEST_ARTIFACT_SIZE));
    return sl_bytes_from_parts(bytes, TEST_ARTIFACT_SIZE);
}

static void refresh_checksum(unsigned char* bytes)
{
    put_u64(bytes, TEST_CHECKSUM_OFFSET, 0U);
    put_u64(bytes, TEST_CHECKSUM_OFFSET, checksum(bytes, TEST_ARTIFACT_SIZE));
}

static int test_valid_artifact(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    unsigned char artifact_storage[TEST_ARTIFACT_SIZE];
    SlArena arena = {0};
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlan plan = one_route_plan(&handler, &route);
    SlRouteArtifactSummary summary = {0};
    SlDiag diag = {0};

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 1;
    }
    if (expect_status(sl_route_artifact_validate(&arena, make_artifact(artifact_storage),
                                                 sl_str_empty(), &plan, &summary, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 2;
    }
    return summary.route_count == 1U && summary.string_table_size == 13U ? 0 : 3;
}

static int expect_invalid_after_mutation(size_t offset, unsigned char value)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    unsigned char artifact_storage[TEST_ARTIFACT_SIZE];
    SlArena arena = {0};
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlan plan = one_route_plan(&handler, &route);
    SlDiag diag = {0};
    SlBytes artifact = make_artifact(artifact_storage);

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 10;
    }
    artifact_storage[offset] = value;
    return expect_status(
        sl_route_artifact_validate(&arena, artifact, sl_str_empty(), &plan, NULL, &diag),
        SL_STATUS_INVALID_ARGUMENT);
}

static int test_malformed_artifacts_fail_cleanly(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    unsigned char artifact_storage[TEST_ARTIFACT_SIZE];
    SlArena arena = {0};
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlan plan = one_route_plan(&handler, &route);
    SlDiag diag = {0};
    SlBytes artifact = make_artifact(artifact_storage);

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 20;
    }
    if (expect_status(sl_route_artifact_validate(&arena, sl_bytes_from_parts(artifact.ptr, 8U),
                                                 sl_str_empty(), &plan, NULL, &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_invalid_after_mutation(0U, 'X') != 0 ||
        expect_invalid_after_mutation(4U, 99U) != 0 ||
        expect_invalid_after_mutation(40U, 0U) != 0 ||
        expect_invalid_after_mutation(68U, 99U) != 0 ||
        expect_invalid_after_mutation(76U, 120U) != 0)
    {
        return 21;
    }
    return expect_status(sl_route_artifact_validate(
                             &arena, artifact, sl_str_from_cstr("sha256:0000"), &plan, NULL, &diag),
                         SL_STATUS_INVALID_ARGUMENT);
}

static int test_extreme_route_count_fails_cleanly(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    unsigned char artifact_storage[TEST_ARTIFACT_SIZE];
    SlArena arena = {0};
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlan plan = one_route_plan(&handler, &route);
    SlDiag diag = {0};
    SlBytes artifact = make_artifact(artifact_storage);

    plan.route_count = (size_t)UINT32_MAX;
    put_u32(artifact_storage, 16U, UINT32_MAX);
    put_u32(artifact_storage, 20U, UINT32_MAX);
    put_u32(artifact_storage, 28U, UINT32_MAX);
    refresh_checksum(artifact_storage);

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 30;
    }
    return expect_status(
        sl_route_artifact_validate(&arena, artifact, sl_str_empty(), &plan, NULL, &diag),
        SL_STATUS_INVALID_ARGUMENT);
}

static int test_extreme_section_offsets_fail_cleanly(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    unsigned char artifact_storage[TEST_ARTIFACT_SIZE];
    SlArena arena = {0};
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlan plan = one_route_plan(&handler, &route);
    SlDiag diag = {0};
    SlBytes artifact = make_artifact(artifact_storage);

    put_u32(artifact_storage, 24U, UINT32_MAX - 8U);
    put_u32(artifact_storage, 32U, UINT32_MAX);
    refresh_checksum(artifact_storage);

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 40;
    }
    return expect_status(
        sl_route_artifact_validate(&arena, artifact, sl_str_empty(), &plan, NULL, &diag),
        SL_STATUS_INVALID_ARGUMENT);
}

static int test_extreme_entry_string_offset_fails_cleanly(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    unsigned char artifact_storage[TEST_ARTIFACT_SIZE];
    SlArena arena = {0};
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlan plan = one_route_plan(&handler, &route);
    SlDiag diag = {0};
    SlBytes artifact = make_artifact(artifact_storage);

    put_u32(artifact_storage, 72U, UINT32_MAX);
    refresh_checksum(artifact_storage);

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 50;
    }
    return expect_status(
        sl_route_artifact_validate(&arena, artifact, sl_str_empty(), &plan, NULL, &diag),
        SL_STATUS_INVALID_ARGUMENT);
}

int main(void)
{
    if (test_valid_artifact() != 0) {
        fprintf(stderr, "test_valid_artifact failed\n");
        return 1;
    }
    if (test_malformed_artifacts_fail_cleanly() != 0) {
        fprintf(stderr, "test_malformed_artifacts_fail_cleanly failed\n");
        return 2;
    }
    if (test_extreme_route_count_fails_cleanly() != 0) {
        fprintf(stderr, "test_extreme_route_count_fails_cleanly failed\n");
        return 3;
    }
    if (test_extreme_section_offsets_fail_cleanly() != 0) {
        fprintf(stderr, "test_extreme_section_offsets_fail_cleanly failed\n");
        return 4;
    }
    if (test_extreme_entry_string_offset_fails_cleanly() != 0) {
        fprintf(stderr, "test_extreme_entry_string_offset_fails_cleanly failed\n");
        return 5;
    }
    return 0;
}
