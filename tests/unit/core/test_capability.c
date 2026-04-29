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

static bool diag_mentions_secret(const SlDiag* diag)
{
    size_t index = 0U;
    SlStr secret = sl_str_from_cstr("secret");
    SlStr password = sl_str_from_cstr("password");

    if (diag == NULL) {
        return false;
    }
    if (sl_str_equal(diag->message, secret) || sl_str_equal(diag->message, password)) {
        return true;
    }
    for (index = 0U; index < diag->hint_count; index += 1U) {
        if (sl_str_equal(diag->hints[index], secret) || sl_str_equal(diag->hints[index], password))
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

static int test_registry_lookup(void)
{
    SlPlanCapability caps[] = {
        capability("data.read", "database", "read", "data.main"),
        capability("files.assets", "filesystem", "readwrite", NULL),
    };
    SlPlan plan = {.capabilities = caps, .capability_count = 2U};
    SlCapabilityRegistry registry = {0};
    const SlPlanCapability* found = NULL;
    SlStatus status = sl_capability_registry_init_from_plan(&plan, &registry);

    if (expect_status(status, SL_STATUS_OK) != 0 || registry.capability_count != 2U) {
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
    SlPlanCapability caps[] = {
        capability("data.read", "database", "read", "data.main"),
        capability("data.write", "database", "write", "data.main"),
        capability("data.rw", "database", "readwrite", "data.main"),
    };
    SlPlan plan = {.capabilities = caps, .capability_count = 3U};
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
    return 0;
}

static int test_database_provider_and_missing_denials(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPlanCapability caps[] = {capability("data.main", "database", "readwrite", "data.main")};
    SlPlan plan = {.capabilities = caps, .capability_count = 1U};
    SlCapabilityRegistry registry = {0};
    SlDiag diag = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 20;
    }
    if (expect_status(sl_capability_registry_init_from_plan(&plan, &registry), SL_STATUS_OK) != 0) {
        return 21;
    }
    status = sl_capability_check_database(&registry, &arena, sl_str_from_cstr("data.main"),
                                          SL_CAPABILITY_OPERATION_READ,
                                          sl_str_from_cstr("data.audit"), &diag);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_PERMISSION_DENIED || !diag_has_hint(&diag, "provider: data.audit") ||
        diag_mentions_secret(&diag))
    {
        return 22;
    }
    status = sl_capability_check_database(&registry, &arena, sl_str_from_cstr("data.missing"),
                                          SL_CAPABILITY_OPERATION_READ,
                                          sl_str_from_cstr("data.main"), &diag);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_PERMISSION_DENIED || !diag_has_hint(&diag, "token: data.missing") ||
        diag_mentions_secret(&diag))
    {
        return 23;
    }
    return 0;
}

static int test_wrong_kind_and_skeleton_checks(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPlanCapability caps[] = {
        capability("files.assets", "filesystem", "readwrite", NULL),
        capability("net.admin", "network", "connect-listen", NULL),
    };
    SlPlan plan = {.capabilities = caps, .capability_count = 2U};
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
    return 0;
}

static int test_denied_check_precedes_provider_work(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPlanCapability caps[] = {capability("data.read", "database", "read", "data.main")};
    SlPlan plan = {.capabilities = caps, .capability_count = 1U};
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
    return test_denied_check_precedes_provider_work();
}
