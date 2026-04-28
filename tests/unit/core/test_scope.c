#include "sloppy/scope.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct CleanupRecord
{
    int order[8];
    int payloads[8];
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

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static void record_cleanup(void* payload, void* user)
{
    CleanupPayload* cleanup_payload = (CleanupPayload*)payload;
    CleanupUser* cleanup_user = (CleanupUser*)user;
    CleanupRecord* record =
        cleanup_payload == NULL ? (CleanupRecord*)user : cleanup_payload->record;
    size_t index = 0U;

    if (record == NULL || record->count >= 8U) {
        return;
    }

    index = record->count;
    record->order[index] = cleanup_payload == NULL ? -1 : cleanup_payload->value;
    record->payloads[index] = cleanup_payload == NULL ? -1 : cleanup_payload->value;
    record->users[index] = cleanup_user == NULL ? -1 : cleanup_user->value;
    record->count += 1U;
}

static void increment_cleanup(void* payload, void* user)
{
    int* value = (int*)payload;

    (void)user;
    if (value != NULL) {
        *value += 1;
    }
}

static int test_initialization(void)
{
    SlScopeCleanup storage[2];
    SlScope scope;
    SlScope zero;

    if (expect_status(sl_scope_init(&scope, storage, 2U), SL_STATUS_OK) != 0) {
        return 1;
    }

    if (sl_scope_cleanup_count(&scope) != 0U || sl_scope_cleanup_capacity(&scope) != 2U ||
        sl_scope_is_closed(&scope))
    {
        return 2;
    }

    if (expect_status(sl_scope_init(NULL, storage, 2U), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 3;
    }

    if (expect_status(sl_scope_init(&scope, NULL, 2U), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 4;
    }

    if (expect_status(sl_scope_init(&zero, NULL, 0U), SL_STATUS_OK) != 0 ||
        sl_scope_cleanup_capacity(&zero) != 0U)
    {
        return 5;
    }

    if (expect_status(sl_scope_add_cleanup(&zero, increment_cleanup, NULL, NULL),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        sl_scope_cleanup_count(&zero) != 0U)
    {
        return 6;
    }

    if (sl_scope_cleanup_count(NULL) != 0U || sl_scope_cleanup_capacity(NULL) != 0U ||
        sl_scope_is_closed(NULL))
    {
        return 7;
    }

    return 0;
}

static int test_registration_and_failure_atomicity(void)
{
    SlScopeCleanup storage[2];
    SlScope scope;
    int value = 0;

    if (expect_status(sl_scope_init(&scope, storage, 2U), SL_STATUS_OK) != 0) {
        return 10;
    }

    if (expect_status(sl_scope_add_cleanup(NULL, increment_cleanup, &value, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 11;
    }

    if (expect_status(sl_scope_add_cleanup(&scope, NULL, &value, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        sl_scope_cleanup_count(&scope) != 0U)
    {
        return 12;
    }

    if (expect_status(sl_scope_add_cleanup(&scope, increment_cleanup, &value, NULL),
                      SL_STATUS_OK) != 0 ||
        sl_scope_cleanup_count(&scope) != 1U)
    {
        return 13;
    }

    if (expect_status(sl_scope_add_cleanup(&scope, increment_cleanup, NULL, NULL), SL_STATUS_OK) !=
            0 ||
        sl_scope_cleanup_count(&scope) != 2U)
    {
        return 14;
    }

    if (expect_status(sl_scope_add_cleanup(&scope, increment_cleanup, &value, NULL),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        sl_scope_cleanup_count(&scope) != 2U || storage[0].fn != increment_cleanup ||
        storage[0].payload != &value)
    {
        return 15;
    }

    if (expect_status(sl_scope_close(&scope), SL_STATUS_OK) != 0) {
        return 16;
    }

    if (expect_status(sl_scope_add_cleanup(&scope, increment_cleanup, &value, NULL),
                      SL_STATUS_INVALID_STATE) != 0 ||
        sl_scope_cleanup_count(&scope) != 0U)
    {
        return 17;
    }

    return 0;
}

static int test_close_order_and_payload_user(void)
{
    SlScopeCleanup storage[3];
    SlScope scope;
    CleanupRecord record = {{0}, {0}, {0}, 0U};
    CleanupPayload first = {&record, 1};
    CleanupPayload second = {&record, 2};
    CleanupPayload third = {&record, 3};
    CleanupUser user_a = {10};
    CleanupUser user_b = {20};

    if (expect_status(sl_scope_init(&scope, storage, 3U), SL_STATUS_OK) != 0) {
        return 20;
    }

    if (expect_status(sl_scope_add_cleanup(&scope, record_cleanup, &first, &user_a),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_scope_add_cleanup(&scope, record_cleanup, &second, NULL), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_scope_add_cleanup(&scope, record_cleanup, &third, &user_b),
                      SL_STATUS_OK) != 0)
    {
        return 21;
    }

    if (expect_status(sl_scope_close(&scope), SL_STATUS_OK) != 0) {
        return 22;
    }

    if (record.count != 3U || record.order[0] != 3 || record.order[1] != 2 || record.order[2] != 1)
    {
        return 23;
    }

    if (record.payloads[0] != 3 || record.users[0] != 20 || record.payloads[1] != 2 ||
        record.users[1] != -1 || record.payloads[2] != 1 || record.users[2] != 10)
    {
        return 24;
    }

    if (!sl_scope_is_closed(&scope) || sl_scope_cleanup_count(&scope) != 0U) {
        return 25;
    }

    return 0;
}

static int test_close_idempotent_and_empty(void)
{
    SlScopeCleanup storage[1];
    SlScope scope;
    int value = 0;

    if (expect_status(sl_scope_close(NULL), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 30;
    }

    if (expect_status(sl_scope_init(&scope, storage, 1U), SL_STATUS_OK) != 0) {
        return 31;
    }

    if (expect_status(sl_scope_close(&scope), SL_STATUS_OK) != 0 || !sl_scope_is_closed(&scope)) {
        return 32;
    }

    if (expect_status(sl_scope_close(&scope), SL_STATUS_OK) != 0 ||
        sl_scope_cleanup_count(&scope) != 0U)
    {
        return 33;
    }

    sl_scope_reset(&scope);
    if (sl_scope_is_closed(&scope) ||
        expect_status(sl_scope_add_cleanup(&scope, increment_cleanup, &value, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 34;
    }

    if (expect_status(sl_scope_close(&scope), SL_STATUS_OK) != 0 || value != 1) {
        return 35;
    }

    if (expect_status(sl_scope_close(&scope), SL_STATUS_OK) != 0 || value != 1) {
        return 36;
    }

    return 0;
}

static int test_reset_discards_without_cleanup(void)
{
    SlScopeCleanup storage[2];
    SlScope scope;
    int value = 0;

    if (expect_status(sl_scope_init(&scope, storage, 2U), SL_STATUS_OK) != 0) {
        return 40;
    }

    if (expect_status(sl_scope_add_cleanup(&scope, increment_cleanup, &value, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_scope_add_cleanup(&scope, increment_cleanup, &value, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 41;
    }

    sl_scope_reset(&scope);
    if (value != 0 || sl_scope_cleanup_count(&scope) != 0U || sl_scope_is_closed(&scope)) {
        return 42;
    }

    if (expect_status(sl_scope_add_cleanup(&scope, increment_cleanup, &value, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_scope_close(&scope), SL_STATUS_OK) != 0 || value != 1)
    {
        return 43;
    }

    sl_scope_reset(NULL);
    return 0;
}

static int test_init_from_arena(void)
{
    unsigned char buffer[128];
    SlArena arena;
    SlScope scope;
    int value = 0;

    if (expect_status(sl_arena_init(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 50;
    }

    if (expect_status(sl_scope_init_from_arena(NULL, &arena, 1U), SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 51;
    }

    if (expect_status(sl_scope_init_from_arena(&scope, NULL, 1U), SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 52;
    }

    if (expect_status(sl_scope_init_from_arena(&scope, &arena, 2U), SL_STATUS_OK) != 0 ||
        sl_scope_cleanup_capacity(&scope) != 2U)
    {
        return 53;
    }

    if (expect_status(sl_scope_add_cleanup(&scope, increment_cleanup, &value, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_scope_close(&scope), SL_STATUS_OK) != 0 || value != 1)
    {
        return 54;
    }

    if (expect_status(sl_scope_init_from_arena(&scope, &arena, 0U), SL_STATUS_OK) != 0 ||
        sl_scope_cleanup_capacity(&scope) != 0U)
    {
        return 55;
    }

    return 0;
}

int main(void)
{
    int result = 0;

    result = test_initialization();
    if (result != 0) {
        return result;
    }

    result = test_registration_and_failure_atomicity();
    if (result != 0) {
        return result;
    }

    result = test_close_order_and_payload_user();
    if (result != 0) {
        return result;
    }

    result = test_close_idempotent_and_empty();
    if (result != 0) {
        return result;
    }

    result = test_reset_discards_without_cleanup();
    if (result != 0) {
        return result;
    }

    return test_init_from_arena();
}
