#include "sloppy/resource.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct CleanupRecord
{
    int values[8];
    int users[8];
    size_t count;
} CleanupRecord;

typedef struct CleanupPayload
{
    CleanupRecord* record;
    int value;
} CleanupPayload;

typedef struct CleanupUser
{
    int value;
} CleanupUser;

typedef struct ReentrantCloseUser
{
    SlResourceTable* table;
    SlResourceId target;
    SlStatus close_status;
    SlDiag diag;
    size_t live_count_before_reentrant_close;
    int value;
} ReentrantCloseUser;

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int expect_str_equal(SlStr left, SlStr right)
{
    return expect_true(sl_str_equal(left, right));
}

static void record_cleanup(void* ptr, void* user)
{
    CleanupPayload* payload = (CleanupPayload*)ptr;
    CleanupUser* cleanup_user = (CleanupUser*)user;
    CleanupRecord* record = payload == NULL ? NULL : payload->record;
    size_t index = 0U;

    if (record == NULL || record->count >= 8U) {
        return;
    }

    index = record->count;
    record->values[index] = payload->value;
    record->users[index] = cleanup_user == NULL ? -1 : cleanup_user->value;
    record->count += 1U;
}

static void reentrant_self_close_cleanup(void* ptr, void* user)
{
    CleanupPayload* payload = (CleanupPayload*)ptr;
    ReentrantCloseUser* cleanup_user = (ReentrantCloseUser*)user;
    CleanupRecord* record = payload == NULL ? NULL : payload->record;
    size_t index = 0U;

    if (record != NULL && record->count < 8U) {
        index = record->count;
        record->values[index] = payload->value;
        record->users[index] = cleanup_user == NULL ? -1 : cleanup_user->value;
        record->count += 1U;
    }

    if (cleanup_user != NULL && cleanup_user->table != NULL) {
        cleanup_user->live_count_before_reentrant_close =
            sl_resource_table_live_count(cleanup_user->table);
        cleanup_user->close_status =
            sl_resource_table_close(cleanup_user->table, cleanup_user->target, &cleanup_user->diag);
    }
}

static void reentrant_close_other_cleanup(void* ptr, void* user)
{
    CleanupPayload* payload = (CleanupPayload*)ptr;
    ReentrantCloseUser* cleanup_user = (ReentrantCloseUser*)user;
    CleanupRecord* record = payload == NULL ? NULL : payload->record;
    size_t index = 0U;

    if (record != NULL && record->count < 8U) {
        index = record->count;
        record->values[index] = payload->value;
        record->users[index] = cleanup_user == NULL ? -1 : cleanup_user->value;
        record->count += 1U;
    }

    if (cleanup_user != NULL && cleanup_user->table != NULL) {
        cleanup_user->live_count_before_reentrant_close =
            sl_resource_table_live_count(cleanup_user->table);
        cleanup_user->close_status =
            sl_resource_table_close(cleanup_user->table, cleanup_user->target, &cleanup_user->diag);
    }
}

static int test_id_model_and_init(void)
{
    SlResourceEntry storage[2];
    SlResourceTable table = {0};
    SlResourceTable zero = {0};
    SlResourceTable invalid_storage = {0};
    unsigned char arena_buffer[256];
    SlArena arena;
    SlResourceTable arena_table = {0};
#if SIZE_MAX > UINT32_MAX
    SlResourceTable too_large = {0};
    size_t impossible_capacity = ((size_t)UINT32_MAX) + 1U;
#endif

    if (sl_resource_id_is_valid(sl_resource_id_invalid())) {
        return 1;
    }

    if (expect_status(sl_resource_table_init(&table, storage, 2U), SL_STATUS_OK) != 0) {
        return 2;
    }

    if (sl_resource_table_capacity(&table) != 2U || sl_resource_table_live_count(&table) != 0U) {
        return 3;
    }

    if (expect_status(sl_resource_table_init(NULL, storage, 2U), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 4;
    }

    if (expect_status(sl_resource_table_init(&invalid_storage, NULL, 2U),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 5;
    }

    if (expect_status(sl_resource_table_init(&table, storage, 2U), SL_STATUS_INVALID_STATE) != 0) {
        return 6;
    }

    if (expect_status(sl_resource_table_init(&zero, NULL, 0U), SL_STATUS_OK) != 0 ||
        sl_resource_table_capacity(&zero) != 0U)
    {
        return 7;
    }

#if SIZE_MAX > UINT32_MAX
    if (expect_status(sl_resource_table_init(&too_large, storage, impossible_capacity),
                      SL_STATUS_OVERFLOW) != 0)
    {
        return 8;
    }
#endif

    if (expect_status(sl_arena_init(&arena, arena_buffer, sizeof(arena_buffer)), SL_STATUS_OK) != 0)
    {
        return 9;
    }

    if (expect_status(sl_resource_table_init_from_arena(&arena_table, &arena, 2U), SL_STATUS_OK) !=
            0 ||
        sl_resource_table_capacity(&arena_table) != 2U)
    {
        return 10;
    }

    if (expect_status(sl_resource_table_init_from_arena(NULL, &arena, 1U),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_resource_table_init_from_arena(&arena_table, NULL, 1U),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_resource_table_init_from_arena(&arena_table, &arena, 1U),
                      SL_STATUS_INVALID_STATE) != 0 ||
        expect_status(sl_resource_table_init_from_arena(&zero, NULL, 1U),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 11;
    }

#if SIZE_MAX > UINT32_MAX
    if (expect_status(sl_resource_table_init_from_arena(&too_large, &arena, impossible_capacity),
                      SL_STATUS_OVERFLOW) != 0)
    {
        return 12;
    }
#endif

    return 0;
}

static int test_insert_lookup_and_failure_atomicity(void)
{
    SlResourceEntry storage[1];
    SlResourceTable table = {0};
    SlResourceId id = sl_resource_id_invalid();
    SlResourceId sentinel = {99U, 99U};
    int value = 42;
    void* ptr = &sentinel;

    if (expect_status(sl_resource_table_init(&table, storage, 1U), SL_STATUS_OK) != 0) {
        return 10;
    }

    if (expect_status(sl_resource_table_insert(NULL, SL_RESOURCE_KIND_TEST_RESOURCE, &value, NULL,
                                               NULL, &id, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 11;
    }

    id = sentinel;
    if (expect_status(
            sl_resource_table_insert(&table, SL_RESOURCE_KIND_NONE, &value, NULL, NULL, &id, NULL),
            SL_STATUS_INVALID_ARGUMENT) != 0 ||
        id.slot != sentinel.slot || id.generation != sentinel.generation)
    {
        return 12;
    }

    if (expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_TEST_RESOURCE, NULL, NULL,
                                               NULL, &id, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 13;
    }

    if (expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_TEST_RESOURCE, &value, NULL,
                                               NULL, NULL, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 14;
    }

    if (expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_TEST_RESOURCE, &value, NULL,
                                               NULL, &id, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 15;
    }

    if (!sl_resource_id_is_valid(id) || id.slot != 1U || id.generation != 1U ||
        sl_resource_table_live_count(&table) != 1U)
    {
        return 16;
    }

    if (expect_status(sl_resource_table_get(&table, id, SL_RESOURCE_KIND_TEST_RESOURCE, &ptr, NULL),
                      SL_STATUS_OK) != 0 ||
        ptr != &value)
    {
        return 17;
    }

    ptr = &sentinel;
    if (expect_status(sl_resource_table_get(&table, id, SL_RESOURCE_KIND_NONE, &ptr, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        ptr != &sentinel)
    {
        return 18;
    }

    if (!sl_resource_table_contains(&table, id, SL_RESOURCE_KIND_TEST_RESOURCE) ||
        !sl_resource_table_is_alive(&table, id))
    {
        return 19;
    }

    return 0;
}

static int test_invalid_wrong_kind_and_output_atomicity(void)
{
    SlResourceEntry storage[1];
    SlResourceTable table = {0};
    SlResourceId id;
    SlResourceId missing = {2U, 1U};
    int value = 7;
    int sentinel_value = 99;
    void* ptr = &sentinel_value;
    SlDiag diag;

    if (expect_status(sl_resource_table_init(&table, storage, 1U), SL_STATUS_OK) != 0 ||
        expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_TEST_RESOURCE, &value, NULL,
                                               NULL, &id, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 20;
    }

    if (expect_status(sl_resource_table_get(&table, sl_resource_id_invalid(),
                                            SL_RESOURCE_KIND_TEST_RESOURCE, &ptr, &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        ptr != &sentinel_value || diag.code != SL_DIAG_RESOURCE_INVALID_ID)
    {
        return 21;
    }

    if (expect_status(
            sl_resource_table_get(&table, missing, SL_RESOURCE_KIND_TEST_RESOURCE, &ptr, &diag),
            SL_STATUS_OUT_OF_RANGE) != 0 ||
        ptr != &sentinel_value || diag.code != SL_DIAG_RESOURCE_INVALID_ID)
    {
        return 22;
    }

    if (expect_status(
            sl_resource_table_get(&table, id, SL_RESOURCE_KIND_SQLITE_CONNECTION, &ptr, &diag),
            SL_STATUS_WRONG_RESOURCE_KIND) != 0 ||
        ptr != &sentinel_value || diag.code != SL_DIAG_RESOURCE_WRONG_KIND ||
        diag.hint_count != 3U ||
        expect_str_equal(diag.hints[1], sl_str_from_cstr("sqlite.connection")) != 0 ||
        expect_str_equal(diag.hints[2], sl_str_from_cstr("test.resource")) != 0)
    {
        return 23;
    }

    if (expect_status(sl_resource_table_get(&table, id, SL_RESOURCE_KIND_TEST_RESOURCE, NULL, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 24;
    }

    return 0;
}

static int test_close_stale_reuse_and_double_close(void)
{
    SlResourceEntry storage[1];
    SlResourceTable table = {0};
    SlResourceId first;
    SlResourceId second;
    int first_value = 1;
    int second_value = 2;
    void* ptr = &first_value;
    SlDiag diag;

    if (expect_status(sl_resource_table_init(&table, storage, 1U), SL_STATUS_OK) != 0 ||
        expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_TEST_RESOURCE, &first_value,
                                               NULL, NULL, &first, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 30;
    }

    if (expect_status(sl_resource_table_close(&table, first, &diag), SL_STATUS_OK) != 0 ||
        sl_resource_table_live_count(&table) != 0U || sl_resource_table_is_alive(&table, first))
    {
        return 31;
    }

    if (expect_status(
            sl_resource_table_get(&table, first, SL_RESOURCE_KIND_TEST_RESOURCE, &ptr, &diag),
            SL_STATUS_STALE_RESOURCE) != 0 ||
        ptr != &first_value || diag.code != SL_DIAG_RESOURCE_STALE_ID)
    {
        return 32;
    }

    if (expect_status(sl_resource_table_close(&table, first, &diag), SL_STATUS_STALE_RESOURCE) !=
            0 ||
        diag.code != SL_DIAG_RESOURCE_STALE_ID)
    {
        return 33;
    }

    if (expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_TEST_RESOURCE,
                                               &second_value, NULL, NULL, &second, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 34;
    }

    if (second.slot != first.slot || second.generation != first.generation + 1U) {
        return 35;
    }

    if (expect_status(
            sl_resource_table_get(&table, first, SL_RESOURCE_KIND_TEST_RESOURCE, &ptr, &diag),
            SL_STATUS_STALE_RESOURCE) != 0 ||
        ptr != &first_value)
    {
        return 36;
    }

    if (expect_status(
            sl_resource_table_get(&table, second, SL_RESOURCE_KIND_TEST_RESOURCE, &ptr, NULL),
            SL_STATUS_OK) != 0 ||
        ptr != &second_value)
    {
        return 37;
    }

    return 0;
}

static int test_close_kind_rejects_wrong_kind_without_cleanup(void)
{
    SlResourceEntry storage[1];
    SlResourceTable table = {0};
    SlResourceId id = sl_resource_id_invalid();
    CleanupRecord record = {{0}, {0}, 0U};
    CleanupPayload payload = {&record, 55};
    SlDiag diag = {0};

    if (expect_status(sl_resource_table_init(&table, storage, 1U), SL_STATUS_OK) != 0 ||
        expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_TEST_RESOURCE, &payload,
                                               record_cleanup, NULL, &id, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 38;
    }

    if (expect_status(
            sl_resource_table_close_kind(&table, id, SL_RESOURCE_KIND_SQLITE_CONNECTION, &diag),
            SL_STATUS_WRONG_RESOURCE_KIND) != 0 ||
        diag.code != SL_DIAG_RESOURCE_WRONG_KIND || record.count != 0U ||
        !sl_resource_table_is_alive(&table, id))
    {
        return 39;
    }

    if (expect_status(
            sl_resource_table_close_kind(&table, id, SL_RESOURCE_KIND_TEST_RESOURCE, &diag),
            SL_STATUS_OK) != 0 ||
        record.count != 1U || sl_resource_table_live_count(&table) != 0U)
    {
        return 48;
    }

    return 0;
}

static int test_exhaustion_cleanup_and_dispose(void)
{
    SlResourceEntry storage[2];
    SlResourceTable table = {0};
    CleanupRecord record = {{0}, {0}, 0U};
    CleanupPayload first_payload = {&record, 10};
    CleanupPayload second_payload = {&record, 20};
    CleanupPayload third_payload = {&record, 30};
    CleanupUser user_a = {100};
    CleanupUser user_b = {200};
    SlResourceId first;
    SlResourceId second;
    SlResourceId untouched = {77U, 77U};
    SlDiag diag;

    if (expect_status(sl_resource_table_init(&table, storage, 2U), SL_STATUS_OK) != 0) {
        return 40;
    }

    if (expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_TEST_RESOURCE,
                                               &first_payload, record_cleanup, &user_a, &first,
                                               NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_SQLITE_CONNECTION,
                                               &second_payload, record_cleanup, &user_b, &second,
                                               NULL),
                      SL_STATUS_OK) != 0)
    {
        return 41;
    }

    if (expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_TEST_RESOURCE,
                                               &third_payload, record_cleanup, NULL, &untouched,
                                               &diag),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        untouched.slot != 77U || untouched.generation != 77U || record.count != 0U ||
        diag.code != SL_DIAG_RESOURCE_TABLE_EXHAUSTED)
    {
        return 42;
    }

    if (expect_status(sl_resource_table_init(&table, storage, 2U), SL_STATUS_INVALID_STATE) != 0 ||
        record.count != 0U || sl_resource_table_live_count(&table) != 2U)
    {
        return 43;
    }

    sl_resource_table_dispose(&table);
    if (record.count != 2U || record.values[0] != 10 || record.users[0] != 100 ||
        record.values[1] != 20 || record.users[1] != 200 ||
        sl_resource_table_capacity(&table) != 0U || sl_resource_table_live_count(&table) != 0U)
    {
        return 44;
    }

    if (expect_status(sl_resource_table_close(&table, first, NULL), SL_STATUS_OUT_OF_RANGE) != 0 ||
        record.count != 2U)
    {
        return 45;
    }

    if (expect_status(sl_resource_table_init(&table, storage, 2U), SL_STATUS_INVALID_STATE) != 0) {
        return 46;
    }

    sl_resource_table_dispose(&table);
    if (record.count != 2U) {
        return 47;
    }

    (void)second;
    return 0;
}

static int test_invalid_close_and_dispose_are_cleanup_safe(void)
{
    SlResourceEntry storage[2];
    SlResourceTable table = {0};
    SlResourceTable uninitialized = {0};
    CleanupRecord record = {{0}, {0}, 0U};
    CleanupPayload payload = {&record, 77};
    CleanupUser user = {700};
    SlResourceId id = sl_resource_id_invalid();
    SlResourceId missing = {3U, 1U};
    SlDiag diag = {0};

    if (expect_status(sl_resource_table_init(&table, storage, 2U), SL_STATUS_OK) != 0 ||
        expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_TEST_RESOURCE, &payload,
                                               record_cleanup, &user, &id, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 55;
    }

    if (expect_status(sl_resource_table_close(NULL, id, &diag), SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_RESOURCE_INVALID_ID ||
        expect_status(sl_resource_table_close(&uninitialized, id, &diag),
                      SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_RESOURCE_INVALID_ID ||
        expect_status(sl_resource_table_close_kind(&table, id, SL_RESOURCE_KIND_NONE, &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_resource_table_close(&table, sl_resource_id_invalid(), &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_RESOURCE_INVALID_ID ||
        expect_status(sl_resource_table_close(&table, missing, &diag), SL_STATUS_OUT_OF_RANGE) !=
            0 ||
        diag.code != SL_DIAG_RESOURCE_INVALID_ID || record.count != 0U ||
        sl_resource_table_live_count(&table) != 1U)
    {
        return 56;
    }

    sl_resource_table_dispose(NULL);
    sl_resource_table_dispose(&uninitialized);
    sl_resource_table_dispose(&table);
    sl_resource_table_dispose(&table);
    if (record.count != 1U || record.values[0] != 77 || record.users[0] != 700 ||
        sl_resource_table_capacity(&table) != 0U || sl_resource_table_live_count(&table) != 0U)
    {
        return 57;
    }

    if (expect_status(sl_resource_table_close(&table, id, &diag), SL_STATUS_OUT_OF_RANGE) != 0 ||
        diag.code != SL_DIAG_RESOURCE_INVALID_ID || record.count != 1U)
    {
        return 58;
    }

    return 0;
}

static int test_current_empty_slot_and_generation_overflow(void)
{
    SlResourceEntry storage[1];
    SlResourceTable table = {0};
    SlResourceId current_empty = {1U, 1U};
    SlResourceId overflow_id = {1U, UINT32_MAX};
    int value = 41;
    void* ptr = &table;
    SlDiag diag = {0};

    if (expect_status(sl_resource_table_init(&table, storage, 1U), SL_STATUS_OK) != 0) {
        return 59;
    }

    if (expect_status(sl_resource_table_get(&table, current_empty, SL_RESOURCE_KIND_TEST_RESOURCE,
                                            &ptr, &diag),
                      SL_STATUS_INVALID_STATE) != 0 ||
        ptr != &table || diag.code != SL_DIAG_RESOURCE_CLOSED ||
        expect_status(sl_resource_table_close(&table, current_empty, &diag),
                      SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_RESOURCE_CLOSED || sl_resource_table_is_alive(&table, current_empty) ||
        sl_resource_table_contains(&table, current_empty, SL_RESOURCE_KIND_TEST_RESOURCE))
    {
        return 60;
    }

    storage[0].ptr = &value;
    storage[0].kind = SL_RESOURCE_KIND_TEST_RESOURCE;
    storage[0].occupied = true;
    storage[0].generation = UINT32_MAX;
    ptr = &table;
    if (expect_status(
            sl_resource_table_get(&table, overflow_id, SL_RESOURCE_KIND_TEST_RESOURCE, &ptr, NULL),
            SL_STATUS_OK) != 0 ||
        ptr != &value ||
        expect_status(sl_resource_table_close(&table, overflow_id, &diag), SL_STATUS_OVERFLOW) !=
            0 ||
        !sl_resource_table_is_alive(&table, overflow_id) ||
        sl_resource_table_live_count(&table) != 1U)
    {
        return 61;
    }

    storage[0].ptr = NULL;
    storage[0].kind = SL_RESOURCE_KIND_NONE;
    storage[0].occupied = false;
    storage[0].generation = 0U;
    if (expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_TEST_RESOURCE, &value, NULL,
                                               NULL, &overflow_id, NULL),
                      SL_STATUS_OK) != 0 ||
        overflow_id.slot != 1U || overflow_id.generation != 1U)
    {
        return 62;
    }

    return 0;
}

static int test_close_cleanup_reentrant_self_close_is_stale_and_runs_once(void)
{
    SlResourceEntry storage[1];
    SlResourceTable table = {0};
    CleanupRecord record = {{0}, {0}, 0U};
    CleanupPayload payload = {&record, 88};
    ReentrantCloseUser cleanup_user = {0};
    SlResourceId id = sl_resource_id_invalid();
    SlDiag diag = {0};

    if (expect_status(sl_resource_table_init(&table, storage, 1U), SL_STATUS_OK) != 0) {
        return 59;
    }

    cleanup_user.table = &table;
    cleanup_user.value = 808;
    cleanup_user.close_status = sl_status_ok();
    if (expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_TEST_RESOURCE, &payload,
                                               reentrant_self_close_cleanup, &cleanup_user, &id,
                                               NULL),
                      SL_STATUS_OK) != 0)
    {
        return 60;
    }
    cleanup_user.target = id;

    if (expect_status(
            sl_resource_table_close_kind(&table, id, SL_RESOURCE_KIND_TEST_RESOURCE, &diag),
            SL_STATUS_OK) != 0)
    {
        return 61;
    }

    if (record.count != 1U || record.values[0] != 88 || record.users[0] != 808 ||
        cleanup_user.live_count_before_reentrant_close != 0U ||
        expect_status(cleanup_user.close_status, SL_STATUS_STALE_RESOURCE) != 0 ||
        cleanup_user.diag.code != SL_DIAG_RESOURCE_STALE_ID ||
        sl_resource_table_live_count(&table) != 0U)
    {
        return 62;
    }

    if (expect_status(sl_resource_table_close(&table, id, &diag), SL_STATUS_STALE_RESOURCE) != 0 ||
        record.count != 1U)
    {
        return 63;
    }

    sl_resource_table_dispose(&table);
    return 0;
}

static int test_dispose_cleanup_may_close_other_live_id_without_double_cleanup(void)
{
    SlResourceEntry storage[2];
    SlResourceTable table = {0};
    CleanupRecord record = {{0}, {0}, 0U};
    CleanupPayload first = {&record, 1};
    CleanupPayload second = {&record, 2};
    ReentrantCloseUser first_user = {0};
    CleanupUser second_user = {22};
    SlResourceId first_id = sl_resource_id_invalid();
    SlResourceId second_id = sl_resource_id_invalid();

    if (expect_status(sl_resource_table_init(&table, storage, 2U), SL_STATUS_OK) != 0) {
        return 64;
    }

    first_user.table = &table;
    first_user.value = 11;
    first_user.close_status = sl_status_from_code(SL_STATUS_INTERNAL);
    if (expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_TEST_RESOURCE, &first,
                                               reentrant_close_other_cleanup, &first_user,
                                               &first_id, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_FS_FILE_HANDLE, &second,
                                               record_cleanup, &second_user, &second_id, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 65;
    }
    first_user.target = second_id;

    sl_resource_table_dispose(&table);

    if (expect_status(first_user.close_status, SL_STATUS_OK) != 0 ||
        first_user.live_count_before_reentrant_close != 1U || record.count != 2U ||
        record.values[0] != 1 || record.users[0] != 11 || record.values[1] != 2 ||
        record.users[1] != 22 || sl_resource_table_live_count(&table) != 0U)
    {
        return 66;
    }

    sl_resource_table_dispose(&table);
    if (record.count != 2U) {
        return 67;
    }
    return 0;
}

static int test_snapshot_and_leak_assertion(void)
{
    SlResourceTable uninitialized = {0};
    SlResourceEntry storage[3];
    SlResourceTable table = {0};
    CleanupRecord record = {{0}, {0}, 0U};
    CleanupPayload first_payload = {&record, 1};
    CleanupPayload second_payload = {&record, 2};
    SlResourceId first = sl_resource_id_invalid();
    SlResourceId second = sl_resource_id_invalid();
    SlResourceTableSnapshot snapshot = {0};
    SlDiag diag = {0};

    if (expect_status(sl_resource_table_assert_no_leaks(NULL, &diag), SL_STATUS_INVALID_ARGUMENT) !=
            0 ||
        diag.code != SL_DIAG_RESOURCE_INVALID_ID ||
        expect_status(sl_resource_table_assert_no_leaks(&uninitialized, &diag),
                      SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_RESOURCE_INVALID_ID)
    {
        return 48;
    }

    if (expect_status(sl_resource_table_init(&table, storage, 3U), SL_STATUS_OK) != 0 ||
        expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_TEST_RESOURCE,
                                               &first_payload, record_cleanup, NULL, &first, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_SQLITE_CONNECTION,
                                               &second_payload, record_cleanup, NULL, &second,
                                               NULL),
                      SL_STATUS_OK) != 0)
    {
        return 49;
    }

    snapshot = sl_resource_table_snapshot(&table);
    if (snapshot.capacity != 3U || snapshot.live_count != 2U ||
        snapshot.leaked_resource_count != 2U ||
        snapshot.live_by_kind[SL_RESOURCE_KIND_TEST_RESOURCE] != 1U ||
        snapshot.live_by_kind[SL_RESOURCE_KIND_SQLITE_CONNECTION] != 1U)
    {
        return 50;
    }
    if (expect_status(sl_resource_table_assert_no_leaks(&table, &diag), SL_STATUS_INVALID_STATE) !=
            0 ||
        diag.code != SL_DIAG_LIFECYCLE_LEAK_DETECTED)
    {
        return 51;
    }

    if (expect_status(sl_resource_table_close(&table, first, &diag), SL_STATUS_OK) != 0 ||
        expect_status(sl_resource_table_close(&table, second, &diag), SL_STATUS_OK) != 0)
    {
        return 52;
    }

    snapshot = sl_resource_table_snapshot(&table);
    if (snapshot.live_count != 0U || snapshot.closed_count != 2U ||
        snapshot.leaked_resource_count != 0U || record.count != 2U)
    {
        return 53;
    }
    if (expect_status(sl_resource_table_assert_no_leaks(&table, &diag), SL_STATUS_OK) != 0) {
        return 54;
    }

    return 0;
}

int main(void)
{
    int result = 0;

    result = test_id_model_and_init();
    if (result != 0) {
        return result;
    }

    result = test_insert_lookup_and_failure_atomicity();
    if (result != 0) {
        return result;
    }

    result = test_invalid_wrong_kind_and_output_atomicity();
    if (result != 0) {
        return result;
    }

    result = test_close_stale_reuse_and_double_close();
    if (result != 0) {
        return result;
    }

    result = test_close_kind_rejects_wrong_kind_without_cleanup();
    if (result != 0) {
        return result;
    }

    result = test_exhaustion_cleanup_and_dispose();
    if (result != 0) {
        return result;
    }

    result = test_invalid_close_and_dispose_are_cleanup_safe();
    if (result != 0) {
        return result;
    }

    result = test_current_empty_slot_and_generation_overflow();
    if (result != 0) {
        return result;
    }

    result = test_close_cleanup_reentrant_self_close_is_stale_and_runs_once();
    if (result != 0) {
        return result;
    }

    result = test_dispose_cleanup_may_close_other_live_id_without_double_cleanup();
    if (result != 0) {
        return result;
    }

    return test_snapshot_and_leak_assertion();
}
