#include "sloppy/route.h"

#include <stdbool.h>
#include <stddef.h>

#define TEST_ARENA_SIZE 32768U

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static SlStatus init_arena(SlArena* arena, unsigned char* storage, size_t storage_size)
{
    return sl_arena_init(arena, storage, storage_size);
}

static SlStatus parse_pattern(SlArena* arena, const char* text, SlRoutePattern* out_pattern,
                              SlDiag* out_diag)
{
    return sl_route_pattern_parse(arena, sl_str_from_cstr(text), out_pattern, out_diag);
}

static int expect_str_equal(SlStr actual, const char* expected)
{
    return expect_true(sl_str_equal(actual, sl_str_from_cstr(expected)));
}

static int test_parse_valid_patterns(void)
{
    static const char* cases[] = {"/",
                                  "/users",
                                  "/users/{id}",
                                  "/users/{id:int}",
                                  "/users/{name:str}",
                                  "/users/{id:uuid}",
                                  "/users/{slug:alpha}",
                                  "/values/{value:float}",
                                  "/users/{id}/posts/{postId:int}"};
    size_t index = 0U;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index += 1U) {
        unsigned char storage[TEST_ARENA_SIZE];
        SlArena arena = {0};
        SlRoutePattern pattern = {0};
        SlDiag diag = {0};
        SlStatus status;

        status = init_arena(&arena, storage, sizeof(storage));
        if (!sl_status_is_ok(status)) {
            return 1;
        }

        status = parse_pattern(&arena, cases[index], &pattern, &diag);
        if (expect_status(status, SL_STATUS_OK) != 0) {
            return 10 + (int)index;
        }

        if (expect_str_equal(pattern.source, cases[index]) != 0 || diag.code != SL_DIAG_NONE) {
            return 20 + (int)index;
        }
    }

    return 0;
}

static int test_parse_valid_pattern_details(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlRoutePattern pattern = {0};
    SlStatus status;

    status = init_arena(&arena, storage, sizeof(storage));
    if (!sl_status_is_ok(status)) {
        return 30;
    }

    status = parse_pattern(&arena, "/users/{id}/posts/{postId:int}", &pattern, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 31;
    }

    if (pattern.segment_count != 4U || pattern.param_count != 2U || pattern.segments == NULL) {
        return 32;
    }

    if (pattern.segments[0].kind != SL_ROUTE_SEGMENT_STATIC ||
        expect_str_equal(pattern.segments[0].text, "users") != 0)
    {
        return 33;
    }

    if (pattern.segments[1].kind != SL_ROUTE_SEGMENT_PARAM ||
        pattern.segments[1].param_kind != SL_ROUTE_PARAM_STRING ||
        expect_str_equal(pattern.segments[1].param_name, "id") != 0)
    {
        return 34;
    }

    if (pattern.segments[3].kind != SL_ROUTE_SEGMENT_PARAM ||
        pattern.segments[3].param_kind != SL_ROUTE_PARAM_INT ||
        expect_str_equal(pattern.segments[3].param_name, "postId") != 0)
    {
        return 35;
    }

    return 0;
}

static int test_parse_invalid_patterns(void)
{
    typedef struct InvalidCase
    {
        const char* pattern;
        SlDiagCode diag_code;
    } InvalidCase;

    static const InvalidCase cases[] = {{"", SL_DIAG_INVALID_ROUTE_PATTERN},
                                        {"users", SL_DIAG_INVALID_ROUTE_PATTERN},
                                        {"/users?id=1", SL_DIAG_INVALID_ROUTE_PATTERN},
                                        {"/users//posts", SL_DIAG_INVALID_ROUTE_PATTERN},
                                        {"/users/{id", SL_DIAG_INVALID_ROUTE_PATTERN},
                                        {"/users/id}", SL_DIAG_INVALID_ROUTE_PATTERN},
                                        {"/users/{}", SL_DIAG_INVALID_ROUTE_PATTERN},
                                        {"/users/{123}", SL_DIAG_INVALID_ROUTE_PATTERN},
                                        {"/{id}/{id}", SL_DIAG_DUPLICATE_ROUTE_PARAM},
                                        {"/{id:bool}", SL_DIAG_INVALID_ROUTE_PATTERN},
                                        {"/{foo{bar}}", SL_DIAG_INVALID_ROUTE_PATTERN}};
    size_t index = 0U;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index += 1U) {
        unsigned char storage[TEST_ARENA_SIZE];
        SlArena arena = {0};
        SlRoutePattern pattern = {0};
        SlDiag diag = {0};
        SlStatus status;

        status = init_arena(&arena, storage, sizeof(storage));
        if (!sl_status_is_ok(status)) {
            return 40;
        }

        status = parse_pattern(&arena, cases[index].pattern, &pattern, &diag);
        if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0) {
            return 50 + (int)index;
        }

        if (pattern.source.ptr != NULL || pattern.segment_count != 0U ||
            pattern.param_count != 0U || pattern.segments != NULL)
        {
            return 70 + (int)index;
        }

        if (cases[index].diag_code == SL_DIAG_NONE) {
            if (diag.code != SL_DIAG_NONE) {
                return 90 + (int)index;
            }
        }
        else if (diag.severity != SL_DIAG_SEVERITY_ERROR || diag.code != cases[index].diag_code) {
            return 110 + (int)index;
        }
    }

    return 0;
}

static int test_parse_invalid_arguments(void)
{
    unsigned char storage[1024];
    SlArena arena = {0};
    SlRoutePattern pattern = {0};
    SlStatus status;

    status = init_arena(&arena, storage, sizeof(storage));
    if (!sl_status_is_ok(status)) {
        return 130;
    }

    if (expect_status(sl_route_pattern_parse(NULL, sl_str_from_cstr("/"), &pattern, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 131;
    }

    if (expect_status(sl_route_pattern_parse(&arena, sl_str_from_cstr("/"), NULL, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 132;
    }

    if (expect_status(sl_route_pattern_parse(&arena, sl_str_from_parts(NULL, 1U), &pattern, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 133;
    }

    return 0;
}

static int parse_and_match(const char* pattern_text, const char* path, SlRoutePattern* out_pattern,
                           SlRouteMatch* out_match, unsigned char* parse_storage,
                           unsigned char* match_storage)
{
    SlArena parse_arena = {0};
    SlArena match_arena = {0};
    SlStatus status;

    status = init_arena(&parse_arena, parse_storage, TEST_ARENA_SIZE);
    if (!sl_status_is_ok(status)) {
        return 1;
    }

    status = init_arena(&match_arena, match_storage, TEST_ARENA_SIZE);
    if (!sl_status_is_ok(status)) {
        return 2;
    }

    status = parse_pattern(&parse_arena, pattern_text, out_pattern, NULL);
    if (!sl_status_is_ok(status)) {
        return 3;
    }

    status = sl_route_pattern_match(&match_arena, out_pattern, sl_str_from_cstr(path), out_match);
    if (!sl_status_is_ok(status)) {
        return 4;
    }

    return 0;
}

static int test_match_static_patterns(void)
{
    unsigned char parse_storage[TEST_ARENA_SIZE];
    unsigned char match_storage[TEST_ARENA_SIZE];
    SlRoutePattern pattern = {0};
    SlRouteMatch match = {0};

    if (parse_and_match("/", "/", &pattern, &match, parse_storage, match_storage) != 0 ||
        !match.matched || match.param_count != 0U)
    {
        return 140;
    }

    if (parse_and_match("/users", "/users", &pattern, &match, parse_storage, match_storage) != 0 ||
        !match.matched)
    {
        return 141;
    }

    if (parse_and_match("/users", "/users/", &pattern, &match, parse_storage, match_storage) != 0 ||
        match.matched)
    {
        return 142;
    }

    if (parse_and_match("/users", "/users/1", &pattern, &match, parse_storage, match_storage) !=
            0 ||
        match.matched)
    {
        return 143;
    }

    return 0;
}

static int test_match_params(void)
{
    unsigned char parse_storage[TEST_ARENA_SIZE];
    unsigned char match_storage[TEST_ARENA_SIZE];
    SlRoutePattern pattern = {0};
    SlRouteMatch match = {0};

    if (parse_and_match("/users/{id}", "/users/abc", &pattern, &match, parse_storage,
                        match_storage) != 0 ||
        !match.matched || match.param_count != 1U ||
        expect_str_equal(match.params[0].name, "id") != 0 ||
        expect_str_equal(match.params[0].value, "abc") != 0 ||
        match.params[0].kind != SL_ROUTE_PARAM_STRING)
    {
        return 150;
    }

    if (parse_and_match("/users/{id:int}", "/users/123", &pattern, &match, parse_storage,
                        match_storage) != 0 ||
        !match.matched || match.param_count != 1U ||
        expect_str_equal(match.params[0].value, "123") != 0 ||
        match.params[0].kind != SL_ROUTE_PARAM_INT)
    {
        return 151;
    }

    if (parse_and_match("/users/{id:int}", "/users/abc", &pattern, &match, parse_storage,
                        match_storage) != 0 ||
        match.matched)
    {
        return 152;
    }

    if (parse_and_match("/users/{id:int}", "/users/", &pattern, &match, parse_storage,
                        match_storage) != 0 ||
        match.matched)
    {
        return 153;
    }

    if (parse_and_match("/users/{id}/posts/{postId:int}", "/users/ada/posts/42", &pattern, &match,
                        parse_storage, match_storage) != 0 ||
        !match.matched || match.param_count != 2U ||
        expect_str_equal(match.params[0].name, "id") != 0 ||
        expect_str_equal(match.params[0].value, "ada") != 0 ||
        expect_str_equal(match.params[1].name, "postId") != 0 ||
        expect_str_equal(match.params[1].value, "42") != 0)
    {
        return 154;
    }

    if (parse_and_match("/users/{id:uuid}", "/users/00000000-0000-4000-8000-000000000000", &pattern,
                        &match, parse_storage, match_storage) != 0 ||
        !match.matched || match.params[0].kind != SL_ROUTE_PARAM_UUID)
    {
        return 155;
    }

    if (parse_and_match("/users/{id:uuid}", "/users/not-a-uuid", &pattern, &match, parse_storage,
                        match_storage) != 0 ||
        match.matched)
    {
        return 156;
    }

    if (parse_and_match("/tags/{slug:alpha}", "/tags/abcDEF", &pattern, &match, parse_storage,
                        match_storage) != 0 ||
        !match.matched || match.params[0].kind != SL_ROUTE_PARAM_ALPHA)
    {
        return 157;
    }

    if (parse_and_match("/tags/{slug:alpha}", "/tags/abc123", &pattern, &match, parse_storage,
                        match_storage) != 0 ||
        match.matched)
    {
        return 158;
    }

    if (parse_and_match("/values/{value:float}", "/values/12.5", &pattern, &match, parse_storage,
                        match_storage) != 0 ||
        !match.matched || match.params[0].kind != SL_ROUTE_PARAM_FLOAT)
    {
        return 159;
    }

    return 0;
}

static int test_match_failure_shapes(void)
{
    unsigned char parse_storage[TEST_ARENA_SIZE];
    unsigned char match_storage[TEST_ARENA_SIZE];
    SlRoutePattern pattern = {0};
    SlRouteMatch match = {0};

    if (parse_and_match("/users/{id}", "/users", &pattern, &match, parse_storage, match_storage) !=
            0 ||
        match.matched || match.params != NULL || match.param_count != 0U)
    {
        return 160;
    }

    if (parse_and_match("/users/{id}", "/users/abc/extra", &pattern, &match, parse_storage,
                        match_storage) != 0 ||
        match.matched || match.params != NULL || match.param_count != 0U)
    {
        return 161;
    }

    return 0;
}

static int test_match_rejects_malformed_public_patterns(void)
{
    unsigned char storage[1024];
    SlArena arena = {0};
    SlRouteMatch match = {0};
    SlRouteSegment static_segment = {sl_str_from_cstr("users"), sl_str_empty(),
                                     SL_ROUTE_SEGMENT_STATIC, SL_ROUTE_PARAM_STRING};
    SlRouteSegment param_segment = {sl_str_empty(), sl_str_from_cstr("id"), SL_ROUTE_SEGMENT_PARAM,
                                    SL_ROUTE_PARAM_STRING};
    SlRouteSegment bad_kind_segment = {sl_str_from_cstr("users"), sl_str_empty(),
                                       SL_ROUTE_SEGMENT_INVALID, SL_ROUTE_PARAM_STRING};
    SlRouteSegment bad_param_kind_segment = {sl_str_empty(), sl_str_from_cstr("id"),
                                             SL_ROUTE_SEGMENT_PARAM, SL_ROUTE_PARAM_INVALID};
    SlRoutePattern zeroed_pattern = {0};
    SlRoutePattern root_without_source = {sl_str_empty(), NULL, 0U, 0U};
    SlRoutePattern root_with_params = {sl_str_from_cstr("/"), NULL, 0U, 1U};
    SlRoutePattern non_root_without_source = {sl_str_empty(), &static_segment, 1U, 0U};
    SlRoutePattern missing_segments = {sl_str_empty(), NULL, 1U, 0U};
    SlRoutePattern too_many_segments = {sl_str_empty(), &static_segment, SL_ROUTE_MAX_SEGMENTS + 1U,
                                        0U};
    SlRoutePattern too_many_params = {sl_str_empty(), &param_segment, 1U, SL_ROUTE_MAX_PARAMS + 1U};
    SlRoutePattern bad_kind = {sl_str_from_cstr("/users"), &bad_kind_segment, 1U, 0U};
    SlRoutePattern bad_param_kind = {sl_str_from_cstr("/{id}"), &bad_param_kind_segment, 1U, 1U};
    SlRoutePattern bad_param_count = {sl_str_from_cstr("/{id}"), &param_segment, 1U, 0U};
    SlStatus status;

    status = init_arena(&arena, storage, sizeof(storage));
    if (!sl_status_is_ok(status)) {
        return 162;
    }

    if (expect_status(
            sl_route_pattern_match(&arena, &zeroed_pattern, sl_str_from_cstr("/"), &match),
            SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 163;
    }

    if (expect_status(
            sl_route_pattern_match(&arena, &root_without_source, sl_str_from_cstr("/"), &match),
            SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 164;
    }

    if (expect_status(
            sl_route_pattern_match(&arena, &root_with_params, sl_str_from_cstr("/"), &match),
            SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 165;
    }

    if (expect_status(sl_route_pattern_match(&arena, &non_root_without_source,
                                             sl_str_from_cstr("/users"), &match),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 166;
    }

    if (expect_status(
            sl_route_pattern_match(&arena, &missing_segments, sl_str_from_cstr("/users"), &match),
            SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 167;
    }

    if (expect_status(
            sl_route_pattern_match(&arena, &too_many_segments, sl_str_from_cstr("/users"), &match),
            SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 168;
    }

    if (expect_status(
            sl_route_pattern_match(&arena, &too_many_params, sl_str_from_cstr("/123"), &match),
            SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 169;
    }

    if (expect_status(sl_route_pattern_match(&arena, &bad_kind, sl_str_from_cstr("/users"), &match),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 170;
    }

    if (expect_status(
            sl_route_pattern_match(&arena, &bad_param_kind, sl_str_from_cstr("/123"), &match),
            SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 171;
    }

    if (expect_status(
            sl_route_pattern_match(&arena, &bad_param_count, sl_str_from_cstr("/123"), &match),
            SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 172;
    }

    return 0;
}

static int test_failed_match_does_not_consume_arena(void)
{
    unsigned char parse_storage[TEST_ARENA_SIZE];
    unsigned char match_storage[256];
    SlArena parse_arena = {0};
    SlArena match_arena = {0};
    SlRoutePattern pattern = {0};
    SlRouteMatch match = {0};
    size_t used_before = 0U;
    size_t index = 0U;
    SlStatus status;

    status = init_arena(&parse_arena, parse_storage, sizeof(parse_storage));
    if (!sl_status_is_ok(status)) {
        return 173;
    }

    status = init_arena(&match_arena, match_storage, sizeof(match_storage));
    if (!sl_status_is_ok(status)) {
        return 174;
    }

    status = parse_pattern(&parse_arena, "/users/{id:int}", &pattern, NULL);
    if (!sl_status_is_ok(status)) {
        return 175;
    }

    used_before = sl_arena_used(&match_arena);
    for (index = 0U; index < 32U; index += 1U) {
        status =
            sl_route_pattern_match(&match_arena, &pattern, sl_str_from_cstr("/users/abc"), &match);
        if (!sl_status_is_ok(status)) {
            return 176;
        }

        if (match.matched || match.params != NULL || match.param_count != 0U ||
            sl_arena_used(&match_arena) != used_before)
        {
            return 177;
        }
    }

    status = sl_route_pattern_match(&match_arena, &pattern, sl_str_from_cstr("/users/123"), &match);
    if (!sl_status_is_ok(status) || !match.matched || match.params == NULL ||
        match.param_count != 1U || sl_arena_used(&match_arena) <= used_before)
    {
        return 178;
    }

    return 0;
}

static int test_match_rejects_invalid_path_input(void)
{
    unsigned char parse_storage[TEST_ARENA_SIZE];
    unsigned char match_storage[TEST_ARENA_SIZE];
    SlArena parse_arena = {0};
    SlArena match_arena = {0};
    SlRoutePattern pattern = {0};
    SlRouteMatch match = {0};
    SlStatus status;

    status = init_arena(&parse_arena, parse_storage, sizeof(parse_storage));
    if (!sl_status_is_ok(status)) {
        return 170;
    }

    status = init_arena(&match_arena, match_storage, sizeof(match_storage));
    if (!sl_status_is_ok(status)) {
        return 171;
    }

    status = parse_pattern(&parse_arena, "/users/{id}", &pattern, NULL);
    if (!sl_status_is_ok(status)) {
        return 172;
    }

    if (expect_status(sl_route_pattern_match(NULL, &pattern, sl_str_from_cstr("/users/1"), &match),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 173;
    }

    if (expect_status(
            sl_route_pattern_match(&match_arena, NULL, sl_str_from_cstr("/users/1"), &match),
            SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 174;
    }

    if (expect_status(
            sl_route_pattern_match(&match_arena, &pattern, sl_str_from_cstr("/users/1"), NULL),
            SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 175;
    }

    if (expect_status(
            sl_route_pattern_match(&match_arena, &pattern, sl_str_from_cstr("users/1"), &match),
            SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 176;
    }

    if (expect_status(sl_route_pattern_match(&match_arena, &pattern,
                                             sl_str_from_cstr("/users/1?x=1"), &match),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 177;
    }

    return 0;
}

static int test_match_values_are_borrowed_from_path(void)
{
    static const char path[] = "/users/borrowed";
    unsigned char parse_storage[TEST_ARENA_SIZE];
    unsigned char match_storage[TEST_ARENA_SIZE];
    SlRoutePattern pattern = {0};
    SlRouteMatch match = {0};

    if (parse_and_match("/users/{id}", path, &pattern, &match, parse_storage, match_storage) != 0 ||
        !match.matched || match.param_count != 1U)
    {
        return 180;
    }

    if (match.params[0].value.ptr != path + sizeof("/users/") - 1U ||
        match.params[0].value.length != sizeof("borrowed") - 1U)
    {
        return 181;
    }

    return 0;
}

static int test_match_non_nul_terminated_path_view(void)
{
    static const char path[] = {'/', 'u', 's', 'e', 'r', 's', '/', 'a', '\0', 'b', 'Z'};
    unsigned char parse_storage[TEST_ARENA_SIZE];
    unsigned char match_storage[TEST_ARENA_SIZE];
    SlArena parse_arena = {0};
    SlArena match_arena = {0};
    SlRoutePattern pattern = {0};
    SlRouteMatch match = {0};
    SlStatus status;

    status = init_arena(&parse_arena, parse_storage, sizeof(parse_storage));
    if (!sl_status_is_ok(status)) {
        return 182;
    }

    status = init_arena(&match_arena, match_storage, sizeof(match_storage));
    if (!sl_status_is_ok(status)) {
        return 183;
    }

    status = parse_pattern(&parse_arena, "/users/{id}", &pattern, NULL);
    if (!sl_status_is_ok(status)) {
        return 184;
    }

    status = sl_route_pattern_match(&match_arena, &pattern,
                                    sl_str_from_parts(path, sizeof(path) - 1U), &match);
    if (!sl_status_is_ok(status) || !match.matched || match.param_count != 1U ||
        match.params[0].value.ptr != path + sizeof("/users/") - 1U ||
        match.params[0].value.length != 3U ||
        !sl_str_equal(match.params[0].value, sl_str_from_parts("a\0b", 3U)))
    {
        return 185;
    }

    return 0;
}

int main(void)
{
    int result = 0;

    result = test_parse_valid_patterns();
    if (result != 0) {
        return result;
    }

    result = test_parse_valid_pattern_details();
    if (result != 0) {
        return result;
    }

    result = test_parse_invalid_patterns();
    if (result != 0) {
        return result;
    }

    result = test_parse_invalid_arguments();
    if (result != 0) {
        return result;
    }

    result = test_match_static_patterns();
    if (result != 0) {
        return result;
    }

    result = test_match_params();
    if (result != 0) {
        return result;
    }

    result = test_match_failure_shapes();
    if (result != 0) {
        return result;
    }

    result = test_match_rejects_malformed_public_patterns();
    if (result != 0) {
        return result;
    }

    result = test_failed_match_does_not_consume_arena();
    if (result != 0) {
        return result;
    }

    result = test_match_rejects_invalid_path_input();
    if (result != 0) {
        return result;
    }

    result = test_match_values_are_borrowed_from_path();
    if (result != 0) {
        return result;
    }

    return test_match_non_nul_terminated_path_view();
}
