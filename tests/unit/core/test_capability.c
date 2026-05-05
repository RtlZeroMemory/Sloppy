#include "sloppy/capability.h"

#include <stdbool.h>

#define TEST_ARENA_SIZE 32768U

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static bool diag_has_hint(const SlDiag* diag, const char* expected)
{
    size_t index = 0U;
    SlStr hint = sl_str_from_cstr(expected);

    if (diag == NULL) {
        return false;
    }
    for (index = 0U; index < diag->hint_count; index += 1U) {
        if (sl_str_equal(diag->hints[index], hint)) {
            return true;
        }
    }
    return false;
}

static bool str_contains_cstr(SlStr haystack, const char* needle)
{
    SlStr expected = sl_str_from_cstr(needle);
    size_t index = 0U;
    size_t needle_index = 0U;

    if (haystack.ptr == NULL || needle == NULL || expected.length == 0U ||
        expected.length > haystack.length)
    {
        return false;
    }
    for (index = 0U; index <= haystack.length - expected.length; index += 1U) {
        bool matches = true;
        for (needle_index = 0U; needle_index < expected.length; needle_index += 1U) {
            if (haystack.ptr[index + needle_index] != expected.ptr[needle_index]) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return true;
        }
    }
    return false;
}

static bool diag_mentions_secret(const SlDiag* diag)
{
    size_t index = 0U;

    if (diag == NULL) {
        return false;
    }
    if (str_contains_cstr(diag->message, "secret") || str_contains_cstr(diag->message, "password"))
    {
        return true;
    }
    for (index = 0U; index < diag->hint_count; index += 1U) {
        if (str_contains_cstr(diag->hints[index], "secret") ||
            str_contains_cstr(diag->hints[index], "password"))
        {
            return true;
        }
    }
    return false;
}

static SlPlanCapability capability(const char* token, const char* kind, const char* access,
                                   const char* provider)
{
    SlPlanCapability cap;

    cap.token = sl_str_from_cstr(token);
    cap.kind = sl_str_from_cstr(kind);
    cap.access = sl_str_from_cstr(access);
    cap.provider = provider == NULL ? sl_str_empty() : sl_str_from_cstr(provider);
    return cap;
}

static SlPlanDataProvider provider(const char* token, const char* provider_kind,
                                   const char* capability_token, const char* service)
{
    SlPlanDataProvider data_provider;

    data_provider.token = sl_str_from_cstr(token);
    data_provider.provider = sl_str_from_cstr(provider_kind);
    data_provider.capability =
        capability_token == NULL ? sl_str_empty() : sl_str_from_cstr(capability_token);
    data_provider.service = service == NULL ? sl_str_empty() : sl_str_from_cstr(service);
    return data_provider;
}

static int test_registry_lookup(void)
{
    SlPlanDataProvider providers[] = {
        provider("data.main", "sqlite", NULL, "data.main"),
    };
    SlPlanCapability caps[] = {
        capability("data.read", "database", "read", "data.main"),
        capability("files.assets", "filesystem", "readwrite", NULL),
    };
    SlPlan plan = {.data_providers = providers,
                   .data_provider_count = 1U,
                   .capabilities = caps,
                   .capability_count = 2U};
    SlCapabilityRegistry registry = {0};
    const SlPlanCapability* found = NULL;
    SlStatus status = sl_capability_registry_init_from_plan(&plan, &registry);

    if (expect_status(status, SL_STATUS_OK) != 0 || registry.data_provider_count != 1U ||
        registry.capability_count != 2U)
    {
        return 1;
    }
    status = sl_capability_registry_find(&registry, sl_str_from_cstr("data.read"), &found);
    if (expect_status(status, SL_STATUS_OK) != 0 || found != &caps[0]) {
        return 2;
    }
    status = sl_capability_registry_find(&registry, sl_str_from_cstr("missing"), &found);
    if (expect_status(status, SL_STATUS_OUT_OF_RANGE) != 0 || found != NULL) {
        return 3;
    }
    return 0;
}

static int test_database_read_write_policy(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPlanDataProvider providers[] = {
        provider("data.main", "sqlite", NULL, "data.main"),
    };
    SlPlanCapability caps[] = {
        capability("data.read", "database", "read", "data.main"),
        capability("data.write", "database", "write", "data.main"),
        capability("data.rw", "database", "readwrite", "data.main"),
    };
    SlPlan plan = {.data_providers = providers,
                   .data_provider_count = 1U,
                   .capabilities = caps,
                   .capability_count = 3U};
    SlCapabilityRegistry registry = {0};
    SlDiag diag = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 10;
    }
    if (expect_status(sl_capability_registry_init_from_plan(&plan, &registry), SL_STATUS_OK) != 0) {
        return 11;
    }
    if (expect_status(sl_capability_check_database(&registry, &arena, sl_str_from_cstr("data.read"),
                                                   SL_CAPABILITY_OPERATION_READ,
                                                   sl_str_from_cstr("data.main"), &diag),
                      SL_STATUS_OK) != 0)
    {
        return 12;
    }
    if (expect_status(sl_capability_check_database(&registry, &arena, sl_str_from_cstr("data.rw"),
                                                   SL_CAPABILITY_OPERATION_WRITE,
                                                   sl_str_from_cstr("data.main"), &diag),
                      SL_STATUS_OK) != 0)
    {
        return 13;
    }
    status = sl_capability_check_database(&registry, &arena, sl_str_from_cstr("data.read"),
                                          SL_CAPABILITY_OPERATION_WRITE,
                                          sl_str_from_cstr("data.main"), &diag);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_PERMISSION_DENIED || !diag_has_hint(&diag, "actual access: read") ||
        !diag_has_hint(&diag, "operation: write"))
    {
        return 14;
    }
    if (expect_status(sl_capability_check_database(
                          &registry, &arena, sl_str_from_cstr("data.write"),
                          SL_CAPABILITY_OPERATION_WRITE, sl_str_from_cstr("data.main"), &diag),
                      SL_STATUS_OK) != 0)
    {
        return 15;
    }
    status = sl_capability_check_database(&registry, &arena, sl_str_from_cstr("data.read"),
                                          SL_CAPABILITY_OPERATION_READWRITE,
                                          sl_str_from_cstr("data.main"), &diag);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_PERMISSION_DENIED || !diag_has_hint(&diag, "operation: readwrite"))
    {
        return 16;
    }
    if (expect_status(sl_capability_check_database(&registry, &arena, sl_str_from_cstr("data.rw"),
                                                   SL_CAPABILITY_OPERATION_READWRITE,
                                                   sl_str_from_cstr("data.main"), &diag),
                      SL_STATUS_OK) != 0)
    {
        return 17;
    }
    return 0;
}

static int test_database_provider_and_missing_denials(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPlanDataProvider providers[] = {
        provider("data.main", "sqlite", NULL, "data.main"),
    };
    SlPlanCapability caps[] = {capability("data.main", "database", "readwrite", "data.main")};
    SlPlan plan = {.data_providers = providers,
                   .data_provider_count = 1U,
                   .capabilities = caps,
                   .capability_count = 1U};
    SlCapabilityRegistry registry = {0};
    SlDiag diag = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 20;
    }
    if (expect_status(sl_capability_registry_init_from_plan(&plan, &registry), SL_STATUS_OK) != 0) {
        return 21;
    }
    if (expect_status(sl_capability_check_database_provider(
                          &registry, &arena, sl_str_from_cstr("data.main"),
                          SL_CAPABILITY_OPERATION_READ, sl_str_from_cstr("sqlite"), &diag),
                      SL_STATUS_OK) != 0)
    {
        return 22;
    }
    status = sl_capability_check_database(&registry, &arena, sl_str_from_cstr("data.main"),
                                          SL_CAPABILITY_OPERATION_READ,
                                          sl_str_from_cstr("data.audit"), &diag);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_PERMISSION_DENIED || !diag_has_hint(&diag, "provider: data.audit") ||
        diag_mentions_secret(&diag))
    {
        return 23;
    }
    status = sl_capability_check_database_provider(&registry, &arena, sl_str_from_cstr("data.main"),
                                                   SL_CAPABILITY_OPERATION_READ,
                                                   sl_str_from_cstr("postgres"), &diag);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_PERMISSION_DENIED || !diag_has_hint(&diag, "provider: postgres") ||
        diag_mentions_secret(&diag))
    {
        return 24;
    }
    status = sl_capability_check_database(&registry, &arena, sl_str_from_cstr("data.missing"),
                                          SL_CAPABILITY_OPERATION_READ,
                                          sl_str_from_cstr("data.main"), &diag);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_PERMISSION_DENIED || !diag_has_hint(&diag, "token: data.missing") ||
        diag_mentions_secret(&diag))
    {
        return 25;
    }
    return 0;
}

static int test_wrong_kind_and_skeleton_checks(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPlanDataProvider providers[] = {
        provider("data.main", "sqlite", NULL, "data.main"),
    };
    SlPlanCapability caps[] = {
        capability("files.assets", "filesystem", "readwrite", NULL),
        capability("net.admin", "network", "connect-listen", NULL),
    };
    SlPlan plan = {.data_providers = providers,
                   .data_provider_count = 1U,
                   .capabilities = caps,
                   .capability_count = 2U};
    SlCapabilityRegistry registry = {0};
    SlDiag diag = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 30;
    }
    if (expect_status(sl_capability_registry_init_from_plan(&plan, &registry), SL_STATUS_OK) != 0) {
        return 31;
    }
    if (expect_status(sl_capability_check_filesystem(&registry, &arena,
                                                     sl_str_from_cstr("files.assets"),
                                                     SL_CAPABILITY_OPERATION_WRITE, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 32;
    }
    if (expect_status(sl_capability_check_filesystem(&registry, &arena,
                                                     sl_str_from_cstr("files.assets"),
                                                     SL_CAPABILITY_OPERATION_APPEND, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 37;
    }
    if (expect_status(sl_capability_check_filesystem(&registry, &arena,
                                                     sl_str_from_cstr("files.assets"),
                                                     SL_CAPABILITY_OPERATION_DELETE, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 38;
    }
    if (expect_status(sl_capability_check_filesystem(&registry, &arena,
                                                     sl_str_from_cstr("files.assets"),
                                                     SL_CAPABILITY_OPERATION_LIST, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 39;
    }
    if (expect_status(sl_capability_check_filesystem(&registry, &arena,
                                                     sl_str_from_cstr("files.assets"),
                                                     SL_CAPABILITY_OPERATION_METADATA, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 40;
    }
    if (expect_status(sl_capability_check_filesystem(&registry, &arena,
                                                     sl_str_from_cstr("files.assets"),
                                                     SL_CAPABILITY_OPERATION_WATCH, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 41;
    }
    if (expect_status(sl_capability_check_filesystem(&registry, &arena,
                                                     sl_str_from_cstr("files.assets"),
                                                     SL_CAPABILITY_OPERATION_LOCK, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 42;
    }
    if (expect_status(sl_capability_check_network(&registry, &arena, sl_str_from_cstr("net.admin"),
                                                  SL_CAPABILITY_OPERATION_LISTEN, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 33;
    }
    status = sl_capability_check_database(&registry, &arena, sl_str_from_cstr("files.assets"),
                                          SL_CAPABILITY_OPERATION_READ,
                                          sl_str_from_cstr("data.main"), &diag);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_PERMISSION_DENIED || !diag_has_hint(&diag, "kind: filesystem"))
    {
        return 34;
    }
    status = sl_capability_check_filesystem(&registry, &arena, sl_str_from_cstr("files.missing"),
                                            SL_CAPABILITY_OPERATION_READ, &diag);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_PERMISSION_DENIED)
    {
        return 35;
    }
    status = sl_capability_check_filesystem(&registry, &arena, sl_str_empty(),
                                            SL_CAPABILITY_OPERATION_READ, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 36;
    }
    return 0;
}

static int test_denied_check_precedes_provider_work(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPlanDataProvider providers[] = {
        provider("data.main", "sqlite", NULL, "data.main"),
    };
    SlPlanCapability caps[] = {capability("data.read", "database", "read", "data.main")};
    SlPlan plan = {.data_providers = providers,
                   .data_provider_count = 1U,
                   .capabilities = caps,
                   .capability_count = 1U};
    SlCapabilityRegistry registry = {0};
    SlDiag diag = {0};
    int fake_provider_calls = 0;
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 40;
    }
    if (expect_status(sl_capability_registry_init_from_plan(&plan, &registry), SL_STATUS_OK) != 0) {
        return 41;
    }
    status = sl_capability_check_database(&registry, &arena, sl_str_from_cstr("data.read"),
                                          SL_CAPABILITY_OPERATION_WRITE,
                                          sl_str_from_cstr("data.main"), &diag);
    if (sl_status_is_ok(status)) {
        fake_provider_calls += 1;
    }
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 || fake_provider_calls != 0 ||
        diag.code != SL_DIAG_PERMISSION_DENIED)
    {
        return 42;
    }
    return 0;
}

static int test_denied_hints_use_single_arena_owned_builder_output(void)
{
    unsigned char storage[128];
    SlArena arena = {0};
    SlPlan plan = {0};
    SlCapabilityRegistry registry = {0};
    SlDiag diag = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 45;
    }
    if (expect_status(sl_capability_registry_init_from_plan(&plan, &registry), SL_STATUS_OK) != 0) {
        return 46;
    }

    status = sl_capability_check_database(
        &registry, &arena, sl_str_from_cstr("data.missing-metadata-token"),
        SL_CAPABILITY_OPERATION_READ, sl_str_from_cstr("data.main"), &diag);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_PERMISSION_DENIED ||
        !diag_has_hint(&diag, "token: data.missing-metadata-token") ||
        !diag_has_hint(&diag, "kind: database") || !diag_has_hint(&diag, "operation: read"))
    {
        return 47;
    }

    return 0;
}

static int test_registry_rejects_invalid_shapes(void)
{
    SlCapabilityRegistry registry = {0};
    SlPlanDataProvider providers[] = {
        provider("data.main", "sqlite", NULL, "data.main"),
    };
    SlPlanDataProvider duplicate_providers[] = {
        provider("data.main", "sqlite", NULL, "data.main"),
        provider("data.main", "postgres", NULL, "data.audit"),
    };
    SlPlanDataProvider invalid_provider_kind[] = {
        provider("data.main", "unknown", NULL, "data.main"),
    };
    SlPlanCapability duplicate_caps[] = {
        capability("data.main", "database", "read", "data.main"),
        capability("data.main", "database", "write", "data.main"),
    };
    SlPlanCapability invalid_kind_caps[] = {
        capability("data.main", "unknown", "read", "data.main"),
    };
    SlPlanCapability missing_provider_caps[] = {
        capability("data.main", "database", "read", NULL),
    };
    SlPlanCapability forbidden_provider_caps[] = {
        capability("files.assets", "filesystem", "read", "data.main"),
    };
    SlPlanCapability missing_provider_ref_caps[] = {
        capability("data.main", "database", "read", "data.missing"),
    };
    SlPlan duplicate_plan = {.data_providers = providers,
                             .data_provider_count = 1U,
                             .capabilities = duplicate_caps,
                             .capability_count = 2U};
    SlPlan invalid_kind_plan = {.data_providers = providers,
                                .data_provider_count = 1U,
                                .capabilities = invalid_kind_caps,
                                .capability_count = 1U};
    SlPlan missing_provider_plan = {.capabilities = missing_provider_caps, .capability_count = 1U};
    SlPlan forbidden_provider_plan = {.data_providers = providers,
                                      .data_provider_count = 1U,
                                      .capabilities = forbidden_provider_caps,
                                      .capability_count = 1U};
    SlPlan missing_provider_ref_plan = {.data_providers = providers,
                                        .data_provider_count = 1U,
                                        .capabilities = missing_provider_ref_caps,
                                        .capability_count = 1U};
    SlPlan duplicate_provider_plan = {.data_providers = duplicate_providers,
                                      .data_provider_count = 2U};
    SlPlan invalid_provider_plan = {.data_providers = invalid_provider_kind,
                                    .data_provider_count = 1U};

    if (expect_status(sl_capability_registry_init_from_plan(&duplicate_plan, &registry),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 50;
    }
    if (expect_status(sl_capability_registry_init_from_plan(&invalid_kind_plan, &registry),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 51;
    }
    if (expect_status(sl_capability_registry_init_from_plan(&missing_provider_plan, &registry),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 52;
    }
    if (expect_status(sl_capability_registry_init_from_plan(&forbidden_provider_plan, &registry),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 53;
    }
    if (expect_status(sl_capability_registry_init_from_plan(&missing_provider_ref_plan, &registry),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 54;
    }
    if (expect_status(sl_capability_registry_init_from_plan(&duplicate_provider_plan, &registry),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 55;
    }
    if (expect_status(sl_capability_registry_init_from_plan(&invalid_provider_plan, &registry),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 56;
    }
    return 0;
}

int main(void)
{
    int result = test_registry_lookup();
    if (result != 0) {
        return result;
    }
    result = test_database_read_write_policy();
    if (result != 0) {
        return result;
    }
    result = test_database_provider_and_missing_denials();
    if (result != 0) {
        return result;
    }
    result = test_wrong_kind_and_skeleton_checks();
    if (result != 0) {
        return result;
    }
    result = test_denied_check_precedes_provider_work();
    if (result != 0) {
        return result;
    }
    result = test_denied_hints_use_single_arena_owned_builder_output();
    if (result != 0) {
        return result;
    }
    return test_registry_rejects_invalid_shapes();
}
