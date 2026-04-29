#include "bench_internal.h"

#include "sloppy/arena.h"
#include "sloppy/bytes.h"
#include "sloppy/http.h"
#include "sloppy/route.h"
#include "sloppy/string.h"

#include <string.h>

static SlBytes sl_bench_bytes_from_cstr(const char* text)
{
    return sl_bytes_from_parts((const unsigned char*)text, strlen(text));
}

static SlStatus sl_bench_parse_route(const char* pattern_text, SlRoutePattern* out_pattern,
                                     SlArena* pattern_arena)
{
    return sl_route_pattern_parse(pattern_arena, sl_str_from_cstr(pattern_text), out_pattern, NULL);
}

static SlStatus sl_bench_route_match_loop(const char* pattern_text, const char* path,
                                          uint64_t iterations, uint64_t* out_checksum)
{
    unsigned char pattern_storage[4096];
    unsigned char match_storage[4096];
    SlArena pattern_arena;
    SlArena match_arena;
    SlRoutePattern pattern = {0};
    uint64_t checksum = 0U;
    uint64_t index;
    SlStatus status;

    status = sl_arena_init(&pattern_arena, pattern_storage, sizeof(pattern_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_init(&match_arena, match_storage, sizeof(match_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_bench_parse_route(pattern_text, &pattern, &pattern_arena);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < iterations; index += 1U) {
        SlRouteMatch match = {0};
        sl_arena_reset(&match_arena);
        status = sl_route_pattern_match(&match_arena, &pattern, sl_str_from_cstr(path), &match);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        checksum += match.matched ? UINT64_C(1) : UINT64_C(17);
        checksum += (uint64_t)match.param_count;
        if (match.param_count > 0U && match.params != NULL) {
            checksum += (uint64_t)match.params[0].value.length;
        }
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus sl_bench_route_parse_loop(const char* pattern_text, uint64_t iterations,
                                          uint64_t* out_checksum)
{
    unsigned char pattern_storage[4096];
    SlArena pattern_arena;
    uint64_t checksum = 0U;
    uint64_t index;
    SlStatus status = sl_arena_init(&pattern_arena, pattern_storage, sizeof(pattern_storage));

    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < iterations; index += 1U) {
        SlRoutePattern pattern = {0};
        sl_arena_reset(&pattern_arena);
        status =
            sl_route_pattern_parse(&pattern_arena, sl_str_from_cstr(pattern_text), &pattern, NULL);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        checksum += (uint64_t)pattern.segment_count + (uint64_t)pattern.param_count;
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus sl_bench_http_request_head_parse_loop(uint64_t iterations, uint64_t* out_checksum)
{
    static const char request[] = "GET /users/123/posts/456?include=author HTTP/1.1\r\n"
                                  "Host: localhost\r\n"
                                  "User-Agent: sloppy-bench\r\n"
                                  "Accept: application/json\r\n"
                                  "\r\n";
    unsigned char parse_storage[8192];
    SlArena parse_arena;
    uint64_t checksum = 0U;
    uint64_t index;
    SlStatus status = sl_arena_init(&parse_arena, parse_storage, sizeof(parse_storage));

    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < iterations; index += 1U) {
        SlHttpRequestHead parsed = {0};
        sl_arena_reset(&parse_arena);
        status = sl_http_parse_request_head(&parse_arena, sl_bench_bytes_from_cstr(request), NULL,
                                            &parsed, NULL);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        checksum += (uint64_t)parsed.method;
        checksum += (uint64_t)parsed.path.length;
        checksum += (uint64_t)parsed.header_count;
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_route_match_static(const SlBenchContext* context, uint64_t iterations,
                                         uint64_t* out_checksum)
{
    (void)context;
    return sl_bench_route_match_loop("/users/profile", "/users/profile", iterations, out_checksum);
}

static SlStatus bench_route_match_param(const SlBenchContext* context, uint64_t iterations,
                                        uint64_t* out_checksum)
{
    (void)context;
    return sl_bench_route_match_loop("/users/{id}", "/users/alice", iterations, out_checksum);
}

static SlStatus bench_route_match_int_param(const SlBenchContext* context, uint64_t iterations,
                                            uint64_t* out_checksum)
{
    (void)context;
    return sl_bench_route_match_loop("/users/{id:int}", "/users/12345", iterations, out_checksum);
}

static SlStatus bench_route_match_multi_param(const SlBenchContext* context, uint64_t iterations,
                                              uint64_t* out_checksum)
{
    (void)context;
    return sl_bench_route_match_loop("/users/{id:int}/posts/{postId:int}",
                                     "/users/12345/posts/67890", iterations, out_checksum);
}

static SlStatus bench_route_match_no_match(const SlBenchContext* context, uint64_t iterations,
                                           uint64_t* out_checksum)
{
    (void)context;
    return sl_bench_route_match_loop("/users/{id:int}/posts/{postId:int}",
                                     "/users/not-an-int/posts/67890", iterations, out_checksum);
}

static SlStatus bench_route_parse_multi_param(const SlBenchContext* context, uint64_t iterations,
                                              uint64_t* out_checksum)
{
    (void)context;
    return sl_bench_route_parse_loop("/users/{id:int}/posts/{postId:int}", iterations,
                                     out_checksum);
}

static SlStatus bench_http_request_head_parse(const SlBenchContext* context, uint64_t iterations,
                                              uint64_t* out_checksum)
{
    (void)context;
    return sl_bench_http_request_head_parse_loop(iterations, out_checksum);
}

static const SlBenchDefinition route_definitions[] = {
    {"route.match.static", "route", "match a pre-parsed static route", 10000U, 1000000U,
     bench_route_match_static, "route pattern is parsed before timing", false},
    {"route.match.param", "route", "match a pre-parsed string parameter route", 10000U, 1000000U,
     bench_route_match_param, "route pattern is parsed before timing", false},
    {"route.match.int_param", "route", "match a pre-parsed integer parameter route", 10000U,
     1000000U, bench_route_match_int_param, "route pattern is parsed before timing", false},
    {"route.match.multi_param", "route", "match a pre-parsed multi-parameter route", 10000U,
     1000000U, bench_route_match_multi_param, "route pattern is parsed before timing", false},
    {"route.match.no_match", "route", "fail a pre-parsed route match", 10000U, 1000000U,
     bench_route_match_no_match, "no-match path should not allocate match params", false},
    {"route.parse.multi_param", "route", "parse a route pattern each iteration", 1000U, 100000U,
     bench_route_parse_multi_param, "measures parser cost separately from match cost", false},
    {"http.request_head.parse", "http", "parse a complete in-memory HTTP request head", 1000U,
     100000U, bench_http_request_head_parse,
     "microbenchmark only; not an HTTP server throughput benchmark", false},
};

const SlBenchDefinition* sl_bench_route_definitions(size_t* out_count)
{
    if (out_count != NULL) {
        *out_count = sizeof(route_definitions) / sizeof(route_definitions[0]);
    }

    return route_definitions;
}
