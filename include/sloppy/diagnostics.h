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
    SL_DIAG_HTTP_ROUTE_NOT_FOUND = 20,
    SL_DIAG_SQLITE_PROVIDER_ERROR = 21,
    SL_DIAG_DATABASE_UNSUPPORTED_VALUE = 22,
    SL_DIAG_POSTGRES_PROVIDER_ERROR = 23,
    SL_DIAG_POSTGRES_POOL_EXHAUSTED = 24,
    SL_DIAG_SQLSERVER_PROVIDER_ERROR = 25,
    SL_DIAG_SQLSERVER_POOL_EXHAUSTED = 26,
    SL_DIAG_RESOURCE_INVALID_ID = 27,
    SL_DIAG_RESOURCE_STALE_ID = 28,
    SL_DIAG_RESOURCE_WRONG_KIND = 29,
    SL_DIAG_RESOURCE_CLOSED = 30,
    SL_DIAG_RESOURCE_TABLE_EXHAUSTED = 31,
    SL_DIAG_DUPLICATE_ROUTE = 32,
    SL_DIAG_HTTP_UNSUPPORTED_BODY = 33,
    SL_DIAG_INVALID_HTTP_RESULT = 34,
    SL_DIAG_ENGINE_PROMISE_REJECTION = 35,
    SL_DIAG_ENGINE_PROMISE_PENDING = 36,
    SL_DIAG_ENGINE_CANCELLED = 37,
    SL_DIAG_ENGINE_BACKPRESSURE = 38,
    SL_DIAG_APP_LIFECYCLE = 39,
    SL_DIAG_HTTP_BODY_LIMIT = 40,
    SL_DIAG_HTTP_UNSUPPORTED_MEDIA_TYPE = 41,
    SL_DIAG_HTTP_TARGET_LIMIT = 42,
    SL_DIAG_HTTP_HEADER_NAME_LIMIT = 43,
    SL_DIAG_HTTP_HEADER_VALUE_LIMIT = 44,
    SL_DIAG_HTTP_HEADER_BYTES_LIMIT = 45,
    SL_DIAG_HTTP_CONNECTION_CLOSED = 46,
    SL_DIAG_HTTP_REQUEST_TIMEOUT = 47,
    SL_DIAG_HTTP_OVERLOAD = 48,
    SL_DIAG_HTTP_KEEP_ALIVE_UNSUPPORTED = 49,
    SL_DIAG_HTTP_SHUTDOWN = 50,
    SL_DIAG_HTTP_TRANSPORT_CONFIG = 51,
    SL_DIAG_HTTP_BIND_FAILED = 52,
    SL_DIAG_HTTP_LISTEN_FAILED = 53,
    SL_DIAG_HTTP_ACCEPT_FAILED = 54,
    SL_DIAG_HTTP_DISPATCH_FAILED = 55,
    SL_DIAG_HTTP_RESPONSE_SERIALIZATION_FAILED = 56,
    SL_DIAG_HTTP_WRITE_FAILED = 57,
    SL_DIAG_HTTP_CLOSE_FAILED = 58,
    SL_DIAG_HTTP_KEEP_ALIVE_IDLE_TIMEOUT = 59,
    SL_DIAG_HTTP_MAX_REQUESTS_REACHED = 60,
    SL_DIAG_HTTP_PIPELINING_UNSUPPORTED = 61
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
 * Optional source text supplied at render time.
 *
 * `text` is borrowed and is never stored in the diagnostic. The renderer uses it only to
 * print a single-line source frame for the diagnostic's primary span when path, line, and
 * column can be matched. Tabs and non-ASCII bytes are copied as-is; caret positioning is
 * byte-column based for this bounded renderer.
 */
typedef struct SlDiagSource
{
    SlStr path;
    SlStr text;
} SlDiagSource;

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
/*
 * Stores an already arena-owned hint without copying it again.
 *
 * `hint` must remain valid for the same lifetime as `builder->arena`. Callers should use
 * this only when they just built the hint in the diagnostic arena and want to avoid a
 * duplicate hot-path copy.
 */
SlStatus sl_diag_builder_add_hint_owned(SlDiagBuilder* builder, SlStr hint);
SlStatus sl_diag_builder_finish(SlDiagBuilder* builder, SlDiag* out);

/*
 * Renders deterministic plain text into `arena`.
 *
 * The returned string is not NUL-terminated and remains valid until the arena is reset.
 * The format is intentionally small and may evolve before a public CLI output contract is
 * declared; tests pin the current foundation behavior.
 */
SlStatus sl_diag_render_text(SlArena* arena, const SlDiag* diag, SlStr* out);

/*
 * Renders deterministic text with a single-line source frame when `source` matches the
 * primary span. Falls back to `sl_diag_render_text` when source text is unavailable or the
 * requested line cannot be found.
 */
SlStatus sl_diag_render_text_with_source(SlArena* arena, const SlDiag* diag,
                                         const SlDiagSource* source, SlStr* out);

/*
 * Renders a deterministic machine-readable diagnostic object. Field order is stable:
 * `code`, `severity`, `message`, optional `primary`, optional `related`, optional `hints`.
 * Output contains no timestamps, random IDs, or raw pointer values.
 */
SlStatus sl_diag_render_json(SlArena* arena, const SlDiag* diag, SlStr* out);

/*
 * Copies `input` into `arena` while redacting common secret-bearing diagnostic text:
 * password/pwd/token/secret/API_KEY-style key values and URI userinfo passwords. This is a
 * small audit helper for diagnostic paths, not a full data-loss-prevention engine.
 */
SlStatus sl_diag_redact_secrets(SlArena* arena, SlStr input, SlStr* out);

#ifdef __cplusplus
}
#endif

#endif
