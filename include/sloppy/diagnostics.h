#ifndef SLOPPY_DIAGNOSTICS_H
#define SLOPPY_DIAGNOSTICS_H

#include "sloppy/arena.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_DIAG_MAX_RELATED 4U
#define SL_DIAG_MAX_HINTS 4U

typedef enum SlDiagSeverity
{
    SL_DIAG_SEVERITY_NOTE = 0,
    SL_DIAG_SEVERITY_WARNING = 1,
    SL_DIAG_SEVERITY_ERROR = 2,
    SL_DIAG_SEVERITY_FATAL = 3
} SlDiagSeverity;

typedef enum SlDiagCode
{
    SL_DIAG_NONE = 0,
    SL_DIAG_INVALID_ARGUMENT = 1,
    SL_DIAG_OUT_OF_MEMORY = 2,
    SL_DIAG_OVERFLOW = 3,
    SL_DIAG_INVALID_PLAN_VERSION = 4,
    SL_DIAG_MISSING_SERVICE = 5,
    SL_DIAG_PERMISSION_DENIED = 6,
    SL_DIAG_INTERNAL_ERROR = 7,
    SL_DIAG_INVALID_PLAN_FIELD = 8,
    SL_DIAG_DUPLICATE_HANDLER_ID = 9,
    SL_DIAG_MALFORMED_JSON = 10,
    SL_DIAG_UNSUPPORTED_ENGINE = 11,
    SL_DIAG_ENGINE_EXCEPTION = 12,
    SL_DIAG_ENGINE_COMPILE_ERROR = 13,
    SL_DIAG_ENGINE_CALL_ERROR = 14,
    SL_DIAG_INVALID_ROUTE_PATTERN = 15,
    SL_DIAG_DUPLICATE_ROUTE_PARAM = 16,
    SL_DIAG_INVALID_HTTP_REQUEST = 17,
    SL_DIAG_HTTP_HEADER_LIMIT = 18,
    SL_DIAG_HTTP_UNSUPPORTED_METHOD = 19,
    SL_DIAG_HTTP_ROUTE_NOT_FOUND = 20
} SlDiagCode;

/*
 * User/app source span. This is distinct from SlSourceLoc, which describes C call sites.
 *
 * `path` is a borrowed view unless a builder API stores the span in an arena-owned
 * diagnostic. Line and column are 1-based when `has_location` is true. `length` is optional
 * and may be zero when a precise highlight length is unknown.
 */
typedef struct SlSourceSpan
{
    SlStr path;
    size_t line;
    size_t column;
    size_t length;
    bool has_location;
} SlSourceSpan;

typedef struct SlDiagRelated
{
    SlSourceSpan span;
    SlStr message;
} SlDiagRelated;

typedef struct SlDiag
{
    SlDiagSeverity severity;
    SlDiagCode code;
    SlStr message;
    SlSourceSpan primary_span;
    SlDiagRelated related[SL_DIAG_MAX_RELATED];
    size_t related_count;
    SlStr hints[SL_DIAG_MAX_HINTS];
    size_t hint_count;
} SlDiag;

/*
 * Builds an arena-owned diagnostic from borrowed inputs.
 *
 * The builder copies message text, hint text, related messages, and span paths into `arena`.
 * Finished diagnostics remain valid until the arena is reset or its caller-owned backing
 * buffer ends. The builder does not allocate with malloc and is not thread-safe.
 */
typedef struct SlDiagBuilder
{
    SlArena* arena;
    SlDiag diag;
} SlDiagBuilder;

SlStr sl_diag_severity_name(SlDiagSeverity severity);
SlStr sl_diag_code_name(SlDiagCode code);
SlStr sl_diag_redacted(void);

SlSourceSpan sl_source_span_unknown(void);
SlSourceSpan sl_source_span_make(SlStr path, size_t line, size_t column, size_t length);

SlStatus sl_diag_builder_init(SlDiagBuilder* builder, SlArena* arena, SlDiagSeverity severity,
                              SlDiagCode code, SlStr message);
SlStatus sl_diag_builder_set_primary_span(SlDiagBuilder* builder, SlSourceSpan span);
SlStatus sl_diag_builder_add_related(SlDiagBuilder* builder, SlSourceSpan span, SlStr message);
SlStatus sl_diag_builder_add_hint(SlDiagBuilder* builder, SlStr hint);
SlStatus sl_diag_builder_finish(SlDiagBuilder* builder, SlDiag* out);

/*
 * Renders deterministic plain text into `arena`.
 *
 * The returned string is not NUL-terminated and remains valid until the arena is reset.
 * The format is intentionally small and may evolve before a public CLI output contract is
 * declared; tests pin the current foundation behavior.
 */
SlStatus sl_diag_render_text(SlArena* arena, const SlDiag* diag, SlStr* out);

#ifdef __cplusplus
}
#endif

#endif
