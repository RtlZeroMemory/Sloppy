#ifndef SLOPPY_ROUTE_H
#define SLOPPY_ROUTE_H

#include "sloppy/arena.h"
#include "sloppy/diagnostics.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_ROUTE_MAX_SEGMENTS 64U
#define SL_ROUTE_MAX_PARAMS 32U

typedef enum SlRouteSegmentKind
{
    SL_ROUTE_SEGMENT_STATIC = 0,
    SL_ROUTE_SEGMENT_PARAM = 1
} SlRouteSegmentKind;

typedef enum SlRouteParamKind
{
    SL_ROUTE_PARAM_STRING = 0,
    SL_ROUTE_PARAM_INT = 1
} SlRouteParamKind;

typedef struct SlRouteSegment
{
    SlStr text;
    SlStr param_name;
    SlRouteSegmentKind kind;
    SlRouteParamKind param_kind;
} SlRouteSegment;

/*
 * Parsed route pattern.
 *
 * sl_route_pattern_parse copies the source pattern, static segment text, and parameter
 * names into the supplied arena. The pattern and segment views remain valid until that
 * arena is reset or its caller-owned backing storage ends.
 */
typedef struct SlRoutePattern
{
    SlStr source;
    SlRouteSegment* segments;
    size_t segment_count;
    size_t param_count;
} SlRoutePattern;

/*
 * Captured route parameter.
 *
 * `name` points into the parsed pattern arena. `value` is a borrowed slice of the path
 * passed to sl_route_pattern_match and is valid only while that path storage remains valid.
 * The match object stores its parameter array in the match arena supplied by the caller.
 */
typedef struct SlRouteParam
{
    SlStr name;
    SlStr value;
    SlRouteParamKind kind;
} SlRouteParam;

typedef struct SlRouteMatch
{
    bool matched;
    SlRouteParam* params;
    size_t param_count;
} SlRouteMatch;

/*
 * Parses Sloppy's initial native route pattern subset.
 *
 * Supported syntax:
 * - `/`
 * - static non-empty segments, such as `/users/profile`
 * - string parameters, such as `/users/{id}` and `/users/{name:str}`
 * - integer parameters, such as `/users/{id:int}`
 *
 * Unsupported in this slice: query strings, catch-all, optional segments, regex
 * constraints, route groups, method matching, route tables, percent decoding, and public
 * TypeScript APIs. Empty segments are invalid except for the root pattern `/`.
 *
 * `arena` and `out_pattern` are required. `pattern_text` must be a non-empty borrowed view
 * that starts with `/`. On success, `out_pattern` contains arena-owned data. On parse
 * failure, `out_pattern` is cleared where practical and `out_diag`, when non-NULL, receives
 * a small arena-owned diagnostic.
 */
SlStatus sl_route_pattern_parse(SlArena* arena, SlStr pattern_text, SlRoutePattern* out_pattern,
                                SlDiag* out_diag);

/*
 * Matches one parsed route pattern against one path.
 *
 * `arena`, `pattern`, and `out_match` are required. `path` must be a non-empty borrowed view
 * that starts with `/` and must not contain a query string. Static segments match exactly.
 * String parameters capture one non-empty segment. Integer parameters capture one non-empty
 * ASCII decimal digit segment; signs and integer conversion are intentionally deferred.
 * Trailing slashes are strict: `/users` does not match `/users/`, and `/` matches only `/`.
 * No percent decoding is performed.
 */
SlStatus sl_route_pattern_match(SlArena* arena, const SlRoutePattern* pattern, SlStr path,
                                SlRouteMatch* out_match);

#ifdef __cplusplus
}
#endif

#endif
