#include "sloppy/http_context.h"

#include <stdbool.h>

#define TEST_ARENA_SIZE 4096U

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int expect_str_equal(SlStr actual, const char* expected)
{
    return expect_true(sl_str_equal(actual, sl_str_from_cstr(expected)));
}

static int parse_query(const char* raw_target, SlArena* arena, SlHttpQuery* out)
{
    return expect_status(sl_http_query_parse(arena, sl_str_from_cstr(raw_target), out),
                         SL_STATUS_OK);
}

static int test_no_query(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpQuery query = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        parse_query("/users/123", &arena, &query) != 0)
    {
        return 1;
    }

    return expect_true(query.param_count == 0U && query.params == NULL);
}

static int test_multiple_values_empty_and_repeated_last_wins(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpQuery query = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        parse_query("/users/123?q=abc&page=2&empty=&q=last", &arena, &query) != 0)
    {
        return 10;
    }

    if (query.param_count != 3U || expect_str_equal(query.params[0].name, "q") != 0 ||
        expect_str_equal(query.params[0].value, "last") != 0 ||
        expect_str_equal(query.params[1].name, "page") != 0 ||
        expect_str_equal(query.params[1].value, "2") != 0 ||
        expect_str_equal(query.params[2].name, "empty") != 0 ||
        expect_str_equal(query.params[2].value, "") != 0)
    {
        return 11;
    }

    return 0;
}

static int test_percent_decoding_and_plus(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpQuery query = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        parse_query("/search?q=hello+world&sym=%7Bok%7D", &arena, &query) != 0)
    {
        return 20;
    }

    if (query.param_count != 2U || expect_str_equal(query.params[0].value, "hello world") != 0 ||
        expect_str_equal(query.params[1].value, "{ok}") != 0)
    {
        return 21;
    }

    return 0;
}

static int test_invalid_percent_encoding_fails(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpQuery query = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0) {
        return 30;
    }

    return expect_status(sl_http_query_parse(&arena, sl_str_from_cstr("/bad?q=%zz"), &query),
                         SL_STATUS_INVALID_ARGUMENT);
}

static int test_query_param_limit_is_enforced(void)
{
    enum
    {
        TARGET_CAPACITY = 2 + ((SL_HTTP_DEFAULT_MAX_QUERY_PARAMS + 1) * 5) + 1
    };
    unsigned char storage[TEST_ARENA_SIZE];
    char target[TARGET_CAPACITY];
    SlArena arena = {0};
    SlHttpQuery query = {0};
    size_t length = 0U;
    size_t index = 0U;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0) {
        return 40;
    }

    target[length] = '/';
    length += 1U;
    target[length] = '?';
    length += 1U;
    for (index = 0U; index <= SL_HTTP_DEFAULT_MAX_QUERY_PARAMS; index += 1U) {
        if (index != 0U) {
            target[length] = '&';
            length += 1U;
        }
        target[length] = 'a';
        length += 1U;
        target[length] = (char)('0' + (index % 10U));
        length += 1U;
        target[length] = '=';
        length += 1U;
        target[length] = '1';
        length += 1U;
    }
    target[length] = '\0';

    return expect_status(sl_http_query_parse(&arena, sl_str_from_cstr(target), &query),
                         SL_STATUS_CAPACITY_EXCEEDED);
}

int main(void)
{
    int result = test_no_query();
    if (result != 0) {
        return result;
    }

    result = test_multiple_values_empty_and_repeated_last_wins();
    if (result != 0) {
        return result;
    }

    result = test_percent_decoding_and_plus();
    if (result != 0) {
        return result;
    }

    result = test_invalid_percent_encoding_fails();
    if (result != 0) {
        return result;
    }

    return test_query_param_limit_is_enforced();
}
