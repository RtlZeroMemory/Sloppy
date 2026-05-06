#include "sloppy/diagnostics.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int expect_str_equal(SlStr actual, SlStr expected)
{
    return expect_true(sl_str_equal(actual, expected));
}

typedef struct ExpectedDiagCodeName
{
    SlDiagCode code;
    const char* name;
} ExpectedDiagCodeName;

static int expect_diag_code_names(const ExpectedDiagCodeName* expected, size_t count,
                                  int failure_base)
{
    size_t index = 0U;

    for (index = 0U; index < count; index += 1U) {
        if (expect_str_equal(sl_diag_code_name(expected[index].code),
                             sl_str_from_cstr(expected[index].name)) != 0)
        {
            return failure_base + (int)index;
        }
    }
    return 0;
}

static SlStatus make_arena(SlArena* arena, unsigned char* buffer, size_t size)
{
    return sl_arena_init(arena, buffer, size);
}

static int expect_snapshot(SlStr actual, const char* path)
{
    char expected[1024];
    FILE* file = NULL;
    size_t length = 0U;

#ifdef _MSC_VER
    if (fopen_s(&file, path, "rb") != 0) {
        return 1;
    }
#else
    file = fopen(path, "rb");
#endif

    if (file == NULL) {
        return 1;
    }

    length = fread(expected, 1U, sizeof(expected), file);
    if (ferror(file) != 0) {
        (void)fclose(file);
        return 2;
    }

    if (fclose(file) != 0) {
        return 3;
    }

    if (length == sizeof(expected)) {
        return 4;
    }

    return expect_str_equal(actual, sl_str_from_parts(expected, length));
}

static int test_missing_service_snapshot(void)
{
    unsigned char buffer[2048];
    SlArena arena;
    SlDiagBuilder builder;
    SlDiag diag;
    SlStr rendered;

    if (expect_status(make_arena(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 10;
    }

    if (expect_status(sl_diag_builder_init(&builder, &arena, SL_DIAG_SEVERITY_ERROR,
                                           SL_DIAG_MISSING_SERVICE,
                                           sl_str_from_cstr("service not registered")),
                      SL_STATUS_OK) != 0)
    {
        return 11;
    }

    if (expect_status(
            sl_diag_builder_set_primary_span(
                &builder, sl_source_span_make(sl_str_from_cstr("src/users.ts"), 12U, 7U, 0U)),
            SL_STATUS_OK) != 0)
    {
        return 12;
    }

    if (expect_status(sl_diag_builder_add_related(
                          &builder, sl_source_span_make(sl_str_from_cstr("src/app.ts"), 4U, 1U, 0U),
                          sl_str_from_cstr("service registration happens here")),
                      SL_STATUS_OK) != 0)
    {
        return 13;
    }

    if (expect_status(sl_diag_builder_add_hint(
                          &builder, sl_str_from_cstr(
                                        "Register service \"data.main\" before building the app.")),
                      SL_STATUS_OK) != 0)
    {
        return 14;
    }

    if (expect_status(sl_diag_builder_finish(&builder, &diag), SL_STATUS_OK) != 0) {
        return 15;
    }

    if (expect_status(sl_diag_render_text(&arena, &diag, &rendered), SL_STATUS_OK) != 0) {
        return 16;
    }

    if (expect_snapshot(rendered, "tests/golden/diagnostics/missing_service.snap") != 0) {
        return 17;
    }

    return 0;
}

static int test_invalid_plan_version_snapshot(void)
{
    unsigned char buffer[2048];
    SlArena arena;
    SlDiagBuilder builder;
    SlDiag diag;
    SlStr rendered;

    if (expect_status(make_arena(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 20;
    }

    if (expect_status(sl_diag_builder_init(
                          &builder, &arena, SL_DIAG_SEVERITY_ERROR, SL_DIAG_INVALID_PLAN_VERSION,
                          sl_str_from_cstr("app.plan.json schema version is not supported")),
                      SL_STATUS_OK) != 0)
    {
        return 21;
    }

    if (expect_status(
            sl_diag_builder_set_primary_span(
                &builder, sl_source_span_make(sl_str_from_cstr("app.plan.json"), 1U, 1U, 0U)),
            SL_STATUS_OK) != 0)
    {
        return 22;
    }

    if (expect_status(
            sl_diag_builder_add_hint(
                &builder, sl_str_from_cstr("Rebuild the app with a compatible sloppyc version.")),
            SL_STATUS_OK) != 0)
    {
        return 23;
    }

    if (expect_status(sl_diag_builder_finish(&builder, &diag), SL_STATUS_OK) != 0) {
        return 24;
    }

    if (expect_status(sl_diag_render_text(&arena, &diag, &rendered), SL_STATUS_OK) != 0) {
        return 25;
    }

    if (expect_snapshot(rendered, "tests/golden/diagnostics/invalid_plan_version.snap") != 0) {
        return 26;
    }

    return 0;
}

static int test_names_and_spans(void)
{
    SlSourceSpan unknown = sl_source_span_unknown();
    SlSourceSpan partial = sl_source_span_make(sl_str_from_cstr("app.ts"), 0U, 5U, 0U);

    if (expect_str_equal(sl_diag_severity_name(SL_DIAG_SEVERITY_NOTE), sl_str_from_cstr("note")) !=
        0)
    {
        return 30;
    }

    if (expect_str_equal(sl_diag_severity_name(SL_DIAG_SEVERITY_WARNING),
                         sl_str_from_cstr("warning")) != 0)
    {
        return 31;
    }

    if (expect_str_equal(sl_diag_severity_name(SL_DIAG_SEVERITY_ERROR),
                         sl_str_from_cstr("error")) != 0)
    {
        return 32;
    }

    if (expect_str_equal(sl_diag_severity_name(SL_DIAG_SEVERITY_FATAL),
                         sl_str_from_cstr("fatal")) != 0)
    {
        return 33;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_NONE), sl_str_from_cstr("SLOPPY_NONE")) != 0) {
        return 35;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_INVALID_ARGUMENT),
                         sl_str_from_cstr("SLOPPY_E_INVALID_ARGUMENT")) != 0)
    {
        return 36;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_MISSING_SERVICE),
                         sl_str_from_cstr("SLOPPY_E_MISSING_SERVICE")) != 0)
    {
        return 37;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_INVALID_PLAN_FIELD),
                         sl_str_from_cstr("SLOPPY_E_INVALID_PLAN_FIELD")) != 0)
    {
        return 38;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_DUPLICATE_HANDLER_ID),
                         sl_str_from_cstr("SLOPPY_E_DUPLICATE_HANDLER_ID")) != 0)
    {
        return 39;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_MALFORMED_JSON),
                         sl_str_from_cstr("SLOPPY_E_MALFORMED_JSON")) != 0)
    {
        return 40;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_UNSUPPORTED_ENGINE),
                         sl_str_from_cstr("SLOPPY_E_UNSUPPORTED_ENGINE")) != 0)
    {
        return 41;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_ENGINE_EXCEPTION),
                         sl_str_from_cstr("SLOPPY_E_ENGINE_EXCEPTION")) != 0)
    {
        return 42;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_ENGINE_COMPILE_ERROR),
                         sl_str_from_cstr("SLOPPY_E_ENGINE_COMPILE_ERROR")) != 0)
    {
        return 43;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_ENGINE_CALL_ERROR),
                         sl_str_from_cstr("SLOPPY_E_ENGINE_CALL_ERROR")) != 0)
    {
        return 44;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_INVALID_ROUTE_PATTERN),
                         sl_str_from_cstr("SLOPPY_E_INVALID_ROUTE_PATTERN")) != 0)
    {
        return 49;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_DUPLICATE_ROUTE_PARAM),
                         sl_str_from_cstr("SLOPPY_E_DUPLICATE_ROUTE_PARAM")) != 0)
    {
        return 50;
    }

    if (unknown.has_location || unknown.path.length != 0U) {
        return 51;
    }

    if (partial.has_location) {
        return 52;
    }

    return 0;
}

static int test_engine_async_code_names(void)
{
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_ENGINE_PROMISE_REJECTION),
                         sl_str_from_cstr("SLOPPY_E_ENGINE_PROMISE_REJECTION")) != 0)
    {
        return 45;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_ENGINE_PROMISE_PENDING),
                         sl_str_from_cstr("SLOPPY_E_ENGINE_PROMISE_PENDING")) != 0)
    {
        return 46;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_ENGINE_CANCELLED),
                         sl_str_from_cstr("SLOPPY_E_ENGINE_CANCELLED")) != 0)
    {
        return 47;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_ENGINE_BACKPRESSURE),
                         sl_str_from_cstr("SLOPPY_E_ENGINE_BACKPRESSURE")) != 0)
    {
        return 48;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_APP_LIFECYCLE),
                         sl_str_from_cstr("SLOPPY_E_APP_LIFECYCLE")) != 0)
    {
        return 49;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_LIFECYCLE_START_FAILED),
                         sl_str_from_cstr("SLOPPY_E_LIFECYCLE_START_FAILED")) != 0)
    {
        return 80;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_LIFECYCLE_ALREADY_STARTED),
                         sl_str_from_cstr("SLOPPY_E_LIFECYCLE_ALREADY_STARTED")) != 0)
    {
        return 81;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_LIFECYCLE_NOT_STARTED),
                         sl_str_from_cstr("SLOPPY_E_LIFECYCLE_NOT_STARTED")) != 0)
    {
        return 82;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_LIFECYCLE_SHUTDOWN_STARTED),
                         sl_str_from_cstr("SLOPPY_E_LIFECYCLE_SHUTDOWN_STARTED")) != 0)
    {
        return 83;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_LIFECYCLE_SHUTDOWN_FORCED),
                         sl_str_from_cstr("SLOPPY_E_LIFECYCLE_SHUTDOWN_FORCED")) != 0)
    {
        return 84;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_LIFECYCLE_REQUEST_SCOPE_CLOSED),
                         sl_str_from_cstr("SLOPPY_E_LIFECYCLE_REQUEST_SCOPE_CLOSED")) != 0)
    {
        return 85;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_LIFECYCLE_LATE_COMPLETION_DROPPED),
                         sl_str_from_cstr("SLOPPY_E_LIFECYCLE_LATE_COMPLETION_DROPPED")) != 0)
    {
        return 86;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_LIFECYCLE_CLEANUP_FAILED),
                         sl_str_from_cstr("SLOPPY_E_LIFECYCLE_CLEANUP_FAILED")) != 0)
    {
        return 87;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_LIFECYCLE_LEAK_DETECTED),
                         sl_str_from_cstr("SLOPPY_E_LIFECYCLE_LEAK_DETECTED")) != 0)
    {
        return 88;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_LIFECYCLE_IDENTITY_UNAVAILABLE),
                         sl_str_from_cstr("SLOPPY_E_LIFECYCLE_IDENTITY_UNAVAILABLE")) != 0)
    {
        return 89;
    }

    return 0;
}

static int test_provider_code_names(void)
{
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_SQLITE_PROVIDER_ERROR),
                         sl_str_from_cstr("SLOPPY_E_SQLITE_PROVIDER")) != 0)
    {
        return 49;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_DATABASE_UNSUPPORTED_VALUE),
                         sl_str_from_cstr("SLOPPY_E_DATABASE_UNSUPPORTED_VALUE")) != 0)
    {
        return 51;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_SQLSERVER_PROVIDER_ERROR),
                         sl_str_from_cstr("SLOPPY_E_SQLSERVER_PROVIDER")) != 0)
    {
        return 52;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_SQLSERVER_POOL_EXHAUSTED),
                         sl_str_from_cstr("SLOPPY_E_SQLSERVER_POOL_EXHAUSTED")) != 0)
    {
        return 53;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_RESOURCE_INVALID_ID),
                         sl_str_from_cstr("SLOPPY_E_RESOURCE_INVALID_ID")) != 0)
    {
        return 54;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_RESOURCE_STALE_ID),
                         sl_str_from_cstr("SLOPPY_E_RESOURCE_STALE_ID")) != 0)
    {
        return 55;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_RESOURCE_WRONG_KIND),
                         sl_str_from_cstr("SLOPPY_E_RESOURCE_WRONG_KIND")) != 0)
    {
        return 56;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_RESOURCE_CLOSED),
                         sl_str_from_cstr("SLOPPY_E_RESOURCE_CLOSED")) != 0)
    {
        return 57;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_RESOURCE_TABLE_EXHAUSTED),
                         sl_str_from_cstr("SLOPPY_E_RESOURCE_TABLE_EXHAUSTED")) != 0)
    {
        return 58;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_DUPLICATE_ROUTE),
                         sl_str_from_cstr("SLOPPY_E_DUPLICATE_ROUTE")) != 0)
    {
        return 59;
    }

    return 0;
}

static int test_http_code_names(void)
{
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_HTTP_UNSUPPORTED_BODY),
                         sl_str_from_cstr("SLOPPY_E_HTTP_UNSUPPORTED_BODY")) != 0)
    {
        return 60;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_INVALID_HTTP_RESULT),
                         sl_str_from_cstr("SLOPPY_E_INVALID_HTTP_RESULT")) != 0)
    {
        return 61;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_HTTP_BODY_LIMIT),
                         sl_str_from_cstr("SLOPPY_E_HTTP_BODY_LIMIT")) != 0)
    {
        return 62;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_HTTP_UNSUPPORTED_MEDIA_TYPE),
                         sl_str_from_cstr("SLOPPY_E_HTTP_UNSUPPORTED_MEDIA_TYPE")) != 0)
    {
        return 63;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_HTTP_TARGET_LIMIT),
                         sl_str_from_cstr("SLOPPY_E_HTTP_TARGET_LIMIT")) != 0)
    {
        return 64;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_HTTP_HEADER_NAME_LIMIT),
                         sl_str_from_cstr("SLOPPY_E_HTTP_HEADER_NAME_LIMIT")) != 0)
    {
        return 65;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_HTTP_HEADER_VALUE_LIMIT),
                         sl_str_from_cstr("SLOPPY_E_HTTP_HEADER_VALUE_LIMIT")) != 0)
    {
        return 66;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_HTTP_HEADER_BYTES_LIMIT),
                         sl_str_from_cstr("SLOPPY_E_HTTP_HEADER_BYTES_LIMIT")) != 0)
    {
        return 67;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_HTTP_CONNECTION_CLOSED),
                         sl_str_from_cstr("SLOPPY_E_HTTP_CONNECTION_CLOSED")) != 0)
    {
        return 68;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_HTTP_REQUEST_TIMEOUT),
                         sl_str_from_cstr("SLOPPY_E_HTTP_REQUEST_TIMEOUT")) != 0)
    {
        return 69;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_HTTP_OVERLOAD),
                         sl_str_from_cstr("SLOPPY_E_HTTP_OVERLOAD")) != 0)
    {
        return 70;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_HTTP_KEEP_ALIVE_UNSUPPORTED),
                         sl_str_from_cstr("SLOPPY_E_HTTP_KEEP_ALIVE_UNSUPPORTED")) != 0)
    {
        return 71;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_HTTP_TRANSPORT_CONFIG),
                         sl_str_from_cstr("SLOPPY_E_HTTP_TRANSPORT_CONFIG")) != 0)
    {
        return 72;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_HTTP_BIND_FAILED),
                         sl_str_from_cstr("SLOPPY_E_HTTP_BIND_FAILED")) != 0)
    {
        return 73;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_HTTP_LISTEN_FAILED),
                         sl_str_from_cstr("SLOPPY_E_HTTP_LISTEN_FAILED")) != 0)
    {
        return 74;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_HTTP_ACCEPT_FAILED),
                         sl_str_from_cstr("SLOPPY_E_HTTP_ACCEPT_FAILED")) != 0)
    {
        return 75;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_UNKNOWN_RUNTIME_FEATURE),
                         sl_str_from_cstr("SLOPPY_E_UNKNOWN_RUNTIME_FEATURE")) != 0)
    {
        return 76;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE),
                         sl_str_from_cstr("SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE")) != 0)
    {
        return 77;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_RUNTIME_FEATURE_DEPENDENCY_MISSING),
                         sl_str_from_cstr("SLOPPY_E_RUNTIME_FEATURE_DEPENDENCY_MISSING")) != 0)
    {
        return 78;
    }

    return 0;
}

static int test_time_code_names(void)
{
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_TIME_TIMEOUT),
                         sl_str_from_cstr("SLOPPY_E_TIME_TIMEOUT")) != 0)
    {
        return 90;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_TIME_CANCELLED),
                         sl_str_from_cstr("SLOPPY_E_TIME_CANCELLED")) != 0)
    {
        return 91;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_TIME_TIMER_DISPOSED),
                         sl_str_from_cstr("SLOPPY_E_TIME_TIMER_DISPOSED")) != 0)
    {
        return 92;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_TIME_INVALID_DELAY),
                         sl_str_from_cstr("SLOPPY_E_TIME_INVALID_DELAY")) != 0)
    {
        return 93;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_TIME_DEADLINE_EXPIRED),
                         sl_str_from_cstr("SLOPPY_E_TIME_DEADLINE_EXPIRED")) != 0)
    {
        return 94;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_TIME_INTERVAL_OVERFLOW),
                         sl_str_from_cstr("SLOPPY_E_TIME_INTERVAL_OVERFLOW")) != 0)
    {
        return 95;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_TIME_SCHEDULE_SKIPPED),
                         sl_str_from_cstr("SLOPPY_E_TIME_SCHEDULE_SKIPPED")) != 0)
    {
        return 96;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_TIME_FAKE_CLOCK_MISUSE),
                         sl_str_from_cstr("SLOPPY_E_TIME_FAKE_CLOCK_MISUSE")) != 0)
    {
        return 97;
    }

    return 0;
}

static int test_crypto_code_names(void)
{
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_CRYPTO_FEATURE_UNAVAILABLE),
                         sl_str_from_cstr("SLOPPY_E_CRYPTO_FEATURE_UNAVAILABLE")) != 0)
    {
        return 120;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_CRYPTO_UNSUPPORTED_ALGORITHM),
                         sl_str_from_cstr("SLOPPY_E_CRYPTO_UNSUPPORTED_ALGORITHM")) != 0)
    {
        return 121;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_CRYPTO_INSECURE_LEGACY_ALGORITHM),
                         sl_str_from_cstr("SLOPPY_E_CRYPTO_INSECURE_LEGACY_ALGORITHM")) != 0)
    {
        return 122;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_CRYPTO_INVALID_KEY_SECRET),
                         sl_str_from_cstr("SLOPPY_E_CRYPTO_INVALID_KEY_SECRET")) != 0)
    {
        return 123;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_CRYPTO_PASSWORD_VERIFY_FAILED),
                         sl_str_from_cstr("SLOPPY_E_CRYPTO_PASSWORD_VERIFY_FAILED")) != 0)
    {
        return 124;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_CRYPTO_PASSWORD_HASH_UNSUPPORTED),
                         sl_str_from_cstr("SLOPPY_E_CRYPTO_PASSWORD_HASH_UNSUPPORTED")) != 0)
    {
        return 125;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_CRYPTO_RANDOM_SOURCE_UNAVAILABLE),
                         sl_str_from_cstr("SLOPPY_E_CRYPTO_RANDOM_SOURCE_UNAVAILABLE")) != 0)
    {
        return 126;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_CRYPTO_SECRET_DISPOSED),
                         sl_str_from_cstr("SLOPPY_E_CRYPTO_SECRET_DISPOSED")) != 0)
    {
        return 127;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_CRYPTO_CONSTANT_TIME_INVALID_INPUT),
                         sl_str_from_cstr("SLOPPY_E_CRYPTO_CONSTANT_TIME_INVALID_INPUT")) != 0)
    {
        return 128;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_CRYPTO_BACKEND_UNAVAILABLE),
                         sl_str_from_cstr("SLOPPY_E_CRYPTO_BACKEND_UNAVAILABLE")) != 0)
    {
        return 129;
    }

    return 0;
}

static int test_time_and_crypto_code_names(void)
{
    int result = test_time_code_names();
    if (result != 0) {
        return result;
    }

    return test_crypto_code_names();
}

static int test_codec_code_names(void)
{
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_CODEC_FEATURE_UNAVAILABLE),
                         sl_str_from_cstr("SLOPPY_E_CODEC_FEATURE_UNAVAILABLE")) != 0)
    {
        return 150;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_CODEC_UNSUPPORTED_ENCODING),
                         sl_str_from_cstr("SLOPPY_E_CODEC_UNSUPPORTED_ENCODING")) != 0)
    {
        return 151;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_CODEC_INVALID_BASE64),
                         sl_str_from_cstr("SLOPPY_E_CODEC_INVALID_BASE64")) != 0)
    {
        return 152;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_CODEC_INVALID_BASE64URL),
                         sl_str_from_cstr("SLOPPY_E_CODEC_INVALID_BASE64URL")) != 0)
    {
        return 153;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_CODEC_INVALID_HEX),
                         sl_str_from_cstr("SLOPPY_E_CODEC_INVALID_HEX")) != 0)
    {
        return 154;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_CODEC_MALFORMED_UTF8),
                         sl_str_from_cstr("SLOPPY_E_CODEC_MALFORMED_UTF8")) != 0)
    {
        return 155;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_CODEC_BINARY_READ_OUT_OF_BOUNDS),
                         sl_str_from_cstr("SLOPPY_E_CODEC_BINARY_READ_OUT_OF_BOUNDS")) != 0)
    {
        return 156;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_CODEC_BINARY_INVALID_ENDIAN_OR_FIELD_SIZE),
                         sl_str_from_cstr("SLOPPY_E_CODEC_BINARY_INVALID_ENDIAN_OR_FIELD_SIZE")) !=
        0)
    {
        return 157;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_CODEC_COMPRESSION_BACKEND_UNAVAILABLE),
                         sl_str_from_cstr("SLOPPY_E_CODEC_COMPRESSION_BACKEND_UNAVAILABLE")) != 0)
    {
        return 158;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_CODEC_DECOMPRESSION_LIMIT_EXCEEDED),
                         sl_str_from_cstr("SLOPPY_E_CODEC_DECOMPRESSION_LIMIT_EXCEEDED")) != 0)
    {
        return 159;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_CODEC_COMPRESSED_STREAM_CORRUPT),
                         sl_str_from_cstr("SLOPPY_E_CODEC_COMPRESSED_STREAM_CORRUPT")) != 0)
    {
        return 160;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_CODEC_CHECKSUM_UNSUPPORTED_ALGORITHM),
                         sl_str_from_cstr("SLOPPY_E_CODEC_CHECKSUM_UNSUPPORTED_ALGORITHM")) != 0)
    {
        return 161;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_CODEC_CHECKSUM_SECURITY_CONTEXT_WARNING),
                         sl_str_from_cstr("SLOPPY_W_CODEC_CHECKSUM_SECURITY_CONTEXT_WARNING")) != 0)
    {
        return 162;
    }
    return 0;
}

static int test_net_code_names(void)
{
    static const ExpectedDiagCodeName expected[] = {
        {SL_DIAG_NET_FEATURE_UNAVAILABLE, "SLOPPY_E_NET_FEATURE_UNAVAILABLE"},
        {SL_DIAG_NET_CONNECT_DENIED, "SLOPPY_E_NET_CONNECT_DENIED"},
        {SL_DIAG_NET_LISTEN_DENIED, "SLOPPY_E_NET_LISTEN_DENIED"},
        {SL_DIAG_NET_INVALID_HOST, "SLOPPY_E_NET_INVALID_HOST"},
        {SL_DIAG_NET_INVALID_PORT, "SLOPPY_E_NET_INVALID_PORT"},
        {SL_DIAG_NET_DNS_FAILURE, "SLOPPY_E_NET_DNS_FAILURE"},
        {SL_DIAG_NET_CONNECT_TIMEOUT, "SLOPPY_E_NET_CONNECT_TIMEOUT"},
        {SL_DIAG_NET_CONNECT_CANCELLED, "SLOPPY_E_NET_CONNECT_CANCELLED"},
        {SL_DIAG_NET_CONNECTION_CLOSED, "SLOPPY_E_NET_CONNECTION_CLOSED"},
        {SL_DIAG_NET_STALE_HANDLE, "SLOPPY_E_NET_STALE_HANDLE"},
        {SL_DIAG_NET_READ_WRITE_TIMEOUT, "SLOPPY_E_NET_READ_WRITE_TIMEOUT"},
        {SL_DIAG_NET_READ_WRITE_CANCELLED, "SLOPPY_E_NET_READ_WRITE_CANCELLED"},
        {SL_DIAG_NET_BACKPRESSURE_OVERFLOW, "SLOPPY_E_NET_BACKPRESSURE_OVERFLOW"},
        {SL_DIAG_NET_UNSUPPORTED_OPTION, "SLOPPY_E_NET_UNSUPPORTED_OPTION"},
        {SL_DIAG_NET_BACKEND_UNAVAILABLE, "SLOPPY_E_NET_BACKEND_UNAVAILABLE"},
        {SL_DIAG_NET_LOCAL_IPC_FEATURE_UNAVAILABLE, "SLOPPY_E_NET_LOCAL_IPC_FEATURE_UNAVAILABLE"},
        {SL_DIAG_NET_LOCAL_IPC_UNSUPPORTED_PLATFORM, "SLOPPY_E_NET_LOCAL_IPC_UNSUPPORTED_PLATFORM"},
        {SL_DIAG_NET_LOCAL_IPC_INVALID_PATH, "SLOPPY_E_NET_LOCAL_IPC_INVALID_PATH"},
        {SL_DIAG_NET_LOCAL_IPC_PATH_DENIED, "SLOPPY_E_NET_LOCAL_IPC_PATH_DENIED"},
        {SL_DIAG_NET_LOCAL_IPC_STALE_CLEANUP_FAILED, "SLOPPY_E_NET_LOCAL_IPC_STALE_CLEANUP_FAILED"},
        {SL_DIAG_NET_LOCAL_IPC_ENDPOINT_EXISTS, "SLOPPY_E_NET_LOCAL_IPC_ENDPOINT_EXISTS"},
        {SL_DIAG_NET_LOCAL_IPC_CONNECT_FAILED, "SLOPPY_E_NET_LOCAL_IPC_CONNECT_FAILED"},
        {SL_DIAG_NET_LOCAL_IPC_LISTEN_FAILED, "SLOPPY_E_NET_LOCAL_IPC_LISTEN_FAILED"},
        {SL_DIAG_NET_LOCAL_IPC_ACCEPT_CANCELLED, "SLOPPY_E_NET_LOCAL_IPC_ACCEPT_CANCELLED"},
        {SL_DIAG_NET_LOCAL_IPC_READ_WRITE_CANCELLED, "SLOPPY_E_NET_LOCAL_IPC_READ_WRITE_CANCELLED"},
        {SL_DIAG_NET_LOCAL_IPC_DISPOSED, "SLOPPY_E_NET_LOCAL_IPC_DISPOSED"},
        {SL_DIAG_NET_LOCAL_IPC_BACKEND_UNAVAILABLE, "SLOPPY_E_NET_LOCAL_IPC_BACKEND_UNAVAILABLE"},
        {SL_DIAG_NET_LOCAL_IPC_PERMISSION_UNSUPPORTED,
         "SLOPPY_E_NET_LOCAL_IPC_PERMISSION_UNSUPPORTED"}};

    return expect_diag_code_names(expected, sizeof(expected) / sizeof(expected[0]), 180);
}

static int test_os_code_names(void)
{
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_OS_FEATURE_UNAVAILABLE),
                         sl_str_from_cstr("SLOPPY_E_OS_FEATURE_UNAVAILABLE")) != 0)
    {
        return 220;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_OS_ENV_ACCESS_DENIED),
                         sl_str_from_cstr("SLOPPY_E_OS_ENV_ACCESS_DENIED")) != 0)
    {
        return 221;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_OS_ENV_SECRET_REDACTED),
                         sl_str_from_cstr("SLOPPY_E_OS_ENV_SECRET_REDACTED")) != 0)
    {
        return 222;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_OS_PROCESS_EXECUTION_DENIED),
                         sl_str_from_cstr("SLOPPY_E_OS_PROCESS_EXECUTION_DENIED")) != 0)
    {
        return 223;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_OS_SHELL_EXECUTION_DENIED),
                         sl_str_from_cstr("SLOPPY_E_OS_SHELL_EXECUTION_DENIED")) != 0)
    {
        return 224;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_OS_COMMAND_NOT_FOUND),
                         sl_str_from_cstr("SLOPPY_E_OS_COMMAND_NOT_FOUND")) != 0)
    {
        return 225;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_OS_INVALID_CWD),
                         sl_str_from_cstr("SLOPPY_E_OS_INVALID_CWD")) != 0)
    {
        return 226;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_OS_INVALID_ENV_OVERRIDE),
                         sl_str_from_cstr("SLOPPY_E_OS_INVALID_ENV_OVERRIDE")) != 0)
    {
        return 227;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_OS_PROCESS_TIMEOUT),
                         sl_str_from_cstr("SLOPPY_E_OS_PROCESS_TIMEOUT")) != 0)
    {
        return 228;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_OS_PROCESS_CANCELLED),
                         sl_str_from_cstr("SLOPPY_E_OS_PROCESS_CANCELLED")) != 0)
    {
        return 229;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_OS_PROCESS_KILLED),
                         sl_str_from_cstr("SLOPPY_E_OS_PROCESS_KILLED")) != 0)
    {
        return 230;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_OS_PROCESS_START_FAILED),
                         sl_str_from_cstr("SLOPPY_E_OS_PROCESS_START_FAILED")) != 0)
    {
        return 231;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_OS_PIPE_CLOSED),
                         sl_str_from_cstr("SLOPPY_E_OS_PIPE_CLOSED")) != 0)
    {
        return 232;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_OS_UNSUPPORTED_PLATFORM_SIGNAL),
                         sl_str_from_cstr("SLOPPY_E_OS_UNSUPPORTED_PLATFORM_SIGNAL")) != 0)
    {
        return 233;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_OS_SIGNAL_HANDLER_FAILURE),
                         sl_str_from_cstr("SLOPPY_E_OS_SIGNAL_HANDLER_FAILURE")) != 0)
    {
        return 234;
    }
    return 0;
}

static int expect_time_json_snapshot(SlDiagCode code, const char* message, const char* hint,
                                     const char* snapshot)
{
    unsigned char buffer[2048];
    SlArena arena;
    SlDiagBuilder builder;
    SlDiag diag;
    SlStr rendered;

    if (expect_status(make_arena(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 1;
    }
    if (expect_status(sl_diag_builder_init(&builder, &arena, SL_DIAG_SEVERITY_ERROR, code,
                                           sl_str_from_cstr(message)),
                      SL_STATUS_OK) != 0)
    {
        return 2;
    }
    if (expect_status(sl_diag_builder_add_hint(&builder, sl_str_from_cstr(hint)), SL_STATUS_OK) !=
        0)
    {
        return 3;
    }
    if (expect_status(sl_diag_builder_finish(&builder, &diag), SL_STATUS_OK) != 0) {
        return 4;
    }
    if (expect_status(sl_diag_render_json(&arena, &diag, &rendered), SL_STATUS_OK) != 0) {
        return 5;
    }
    return expect_snapshot(rendered, snapshot);
}

static int test_time_diagnostic_json_goldens(void)
{
    if (expect_time_json_snapshot(SL_DIAG_TIME_TIMEOUT, "time operation exceeded deadline",
                                  "TimeoutError is distinct from caller cancellation.",
                                  "tests/golden/diagnostics/time_timeout.json") != 0)
    {
        return 100;
    }
    if (expect_time_json_snapshot(SL_DIAG_TIME_CANCELLED, "time operation was cancelled",
                                  "CancelledError preserves the explicit cancellation reason.",
                                  "tests/golden/diagnostics/time_cancelled.json") != 0)
    {
        return 101;
    }
    if (expect_time_json_snapshot(SL_DIAG_TIME_TIMER_DISPOSED, "timer handle was disposed",
                                  "Late timer completion must be cleanup-only.",
                                  "tests/golden/diagnostics/time_timer_disposed.json") != 0)
    {
        return 102;
    }
    if (expect_time_json_snapshot(SL_DIAG_TIME_INVALID_DELAY,
                                  "delay must be finite and non-negative",
                                  "Validate delay inputs before scheduling native timers.",
                                  "tests/golden/diagnostics/time_invalid_delay.json") != 0)
    {
        return 103;
    }
    if (expect_time_json_snapshot(SL_DIAG_TIME_DEADLINE_EXPIRED,
                                  "deadline expired before scheduling",
                                  "Expired deadlines fail before native timer admission.",
                                  "tests/golden/diagnostics/time_deadline_expired.json") != 0)
    {
        return 104;
    }
    if (expect_time_json_snapshot(
            SL_DIAG_TIME_INTERVAL_OVERFLOW, "interval tick queue overflowed",
            "Bounded interval queues skip or fail instead of growing without limit.",
            "tests/golden/diagnostics/time_interval_overflow.json") != 0)
    {
        return 105;
    }
    if (expect_time_json_snapshot(
            SL_DIAG_TIME_SCHEDULE_SKIPPED, "scheduled run was skipped",
            "No-overlap scheduled jobs report skipped runs deterministically.",
            "tests/golden/diagnostics/time_schedule_skipped.json") != 0)
    {
        return 106;
    }
    if (expect_time_json_snapshot(
            SL_DIAG_TIME_FAKE_CLOCK_MISUSE, "fake clock was misused or disposed",
            "Fake clocks are explicit test-scoped providers, not global timer mutation.",
            "tests/golden/diagnostics/time_fake_clock_misuse.json") != 0)
    {
        return 107;
    }
    return 0;
}

static int expect_stdlib_json_snapshot(SlDiagSeverity severity, SlDiagCode code,
                                       const char* message, const char* hint, const char* snapshot)
{
    unsigned char buffer[2048];
    SlArena arena;
    SlDiagBuilder builder;
    SlDiag diag;
    SlStr rendered;

    if (expect_status(make_arena(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 1;
    }
    if (expect_status(
            sl_diag_builder_init(&builder, &arena, severity, code, sl_str_from_cstr(message)),
            SL_STATUS_OK) != 0)
    {
        return 2;
    }
    if (expect_status(sl_diag_builder_add_hint(&builder, sl_str_from_cstr(hint)), SL_STATUS_OK) !=
        0)
    {
        return 3;
    }
    if (expect_status(sl_diag_builder_finish(&builder, &diag), SL_STATUS_OK) != 0) {
        return 4;
    }
    if (expect_status(sl_diag_render_json(&arena, &diag, &rendered), SL_STATUS_OK) != 0) {
        return 5;
    }
    return expect_snapshot(rendered, snapshot);
}

static int test_crypto_diagnostic_json_goldens(void)
{
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_CRYPTO_FEATURE_UNAVAILABLE,
            "crypto feature is unavailable",
            "Enable stdlib.crypto only in a runtime lane with registered crypto backends.",
            "tests/golden/diagnostics/crypto_feature_unavailable.json") != 0)
    {
        return 130;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_CRYPTO_UNSUPPORTED_ALGORITHM,
            "unsupported crypto algorithm", "Use a supported algorithm from sloppy/crypto.",
            "tests/golden/diagnostics/crypto_unsupported_algorithm.json") != 0)
    {
        return 131;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_WARNING, SL_DIAG_CRYPTO_INSECURE_LEGACY_ALGORITHM,
            "insecure or legacy crypto algorithm",
            "Legacy algorithms require an explicit compatibility policy and are never defaults.",
            "tests/golden/diagnostics/crypto_insecure_legacy_algorithm.json") != 0)
    {
        return 132;
    }
    if (expect_stdlib_json_snapshot(SL_DIAG_SEVERITY_ERROR, SL_DIAG_CRYPTO_INVALID_KEY_SECRET,
                                    "invalid key or secret",
                                    "Diagnostics must describe shape, not secret contents.",
                                    "tests/golden/diagnostics/crypto_invalid_key_secret.json") != 0)
    {
        return 133;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_CRYPTO_PASSWORD_VERIFY_FAILED,
            "password verification failed",
            "Verification failures must not reveal password or encoded-hash internals.",
            "tests/golden/diagnostics/crypto_password_verify_failed.json") != 0)
    {
        return 134;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_CRYPTO_PASSWORD_HASH_UNSUPPORTED,
            "password hash format is unsupported",
            "Only documented encoded hash formats are accepted.",
            "tests/golden/diagnostics/crypto_password_hash_unsupported.json") != 0)
    {
        return 135;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_CRYPTO_RANDOM_SOURCE_UNAVAILABLE,
            "secure random source is unavailable",
            "Random APIs must fail closed instead of using weak fallback output.",
            "tests/golden/diagnostics/crypto_random_source_unavailable.json") != 0)
    {
        return 136;
    }
    if (expect_stdlib_json_snapshot(SL_DIAG_SEVERITY_ERROR, SL_DIAG_CRYPTO_SECRET_DISPOSED,
                                    "secret was disposed",
                                    "Disposed Secret values reject further use deterministically.",
                                    "tests/golden/diagnostics/crypto_secret_disposed.json") != 0)
    {
        return 137;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_CRYPTO_CONSTANT_TIME_INVALID_INPUT,
            "constant-time comparison input is invalid",
            "Validate byte lengths and owned input views before comparing.",
            "tests/golden/diagnostics/crypto_constant_time_invalid_input.json") != 0)
    {
        return 138;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_CRYPTO_BACKEND_UNAVAILABLE,
            "crypto backend is unavailable",
            "Backend diagnostics must name the operation without leaking secrets.",
            "tests/golden/diagnostics/crypto_backend_unavailable.json") != 0)
    {
        return 139;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_WARNING, SL_DIAG_CRYPTO_NONCRYPTO_HASH_SECURITY_CONTEXT_WARNING,
            "non-cryptographic hash used in a security-looking context",
            "Use Hash or Hmac for security or attacker-resistance.",
            "tests/golden/diagnostics/crypto_noncrypto_hash_security_context_warning.json") != 0)
    {
        return 140;
    }

    return 0;
}

static int test_codec_diagnostic_json_goldens(void)
{
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_CODEC_FEATURE_UNAVAILABLE,
            "codec feature is unavailable",
            "Enable stdlib.codec only in a codec-enabled runtime lane; compression also "
            "requires registered codec backends.",
            "tests/golden/diagnostics/codec_feature_unavailable.json") != 0)
    {
        return 170;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_CODEC_UNSUPPORTED_ENCODING,
            "unsupported codec encoding", "Use a supported encoding from sloppy/codec.",
            "tests/golden/diagnostics/codec_unsupported_encoding.json") != 0)
    {
        return 171;
    }
    if (expect_stdlib_json_snapshot(SL_DIAG_SEVERITY_ERROR, SL_DIAG_CODEC_INVALID_BASE64,
                                    "invalid base64 input",
                                    "Base64 decoding is strict and reports malformed input.",
                                    "tests/golden/diagnostics/codec_invalid_base64.json") != 0)
    {
        return 172;
    }
    if (expect_stdlib_json_snapshot(SL_DIAG_SEVERITY_ERROR, SL_DIAG_CODEC_INVALID_BASE64URL,
                                    "invalid base64url input",
                                    "Base64Url decoding uses its own alphabet and padding policy.",
                                    "tests/golden/diagnostics/codec_invalid_base64url.json") != 0)
    {
        return 173;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_CODEC_INVALID_HEX, "invalid hex input",
            "Hex input must use complete byte pairs in the selected policy.",
            "tests/golden/diagnostics/codec_invalid_hex.json") != 0)
    {
        return 174;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_CODEC_MALFORMED_UTF8, "malformed UTF-8 input",
            "Fatal mode rejects malformed input; replacement mode is explicit.",
            "tests/golden/diagnostics/codec_malformed_utf8.json") != 0)
    {
        return 175;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_CODEC_BINARY_READ_OUT_OF_BOUNDS,
            "binary read exceeded available bytes",
            "Binary readers must check remaining bytes before advancing.",
            "tests/golden/diagnostics/codec_binary_read_out_of_bounds.json") != 0)
    {
        return 176;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_CODEC_BINARY_INVALID_ENDIAN_OR_FIELD_SIZE,
            "invalid binary endian or field size",
            "Binary APIs expose explicit endian and supported width methods only.",
            "tests/golden/diagnostics/codec_binary_invalid_endian_or_field_size.json") != 0)
    {
        return 177;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_CODEC_COMPRESSION_BACKEND_UNAVAILABLE,
            "compression backend is unavailable",
            "Dependency-backed compression lanes must fail closed when unavailable.",
            "tests/golden/diagnostics/codec_compression_backend_unavailable.json") != 0)
    {
        return 178;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_CODEC_DECOMPRESSION_LIMIT_EXCEEDED,
            "decompression output limit exceeded",
            "Decompression must enforce the configured max output policy.",
            "tests/golden/diagnostics/codec_decompression_limit_exceeded.json") != 0)
    {
        return 179;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_CODEC_COMPRESSED_STREAM_CORRUPT,
            "compressed stream is corrupt",
            "Corrupt compressed input fails deterministically without partial success.",
            "tests/golden/diagnostics/codec_compressed_stream_corrupt.json") != 0)
    {
        return 180;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_CODEC_CHECKSUM_UNSUPPORTED_ALGORITHM,
            "checksum algorithm is unsupported",
            "Checksums are non-security utilities and expose only documented algorithms.",
            "tests/golden/diagnostics/codec_checksum_unsupported_algorithm.json") != 0)
    {
        return 181;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_WARNING, SL_DIAG_CODEC_CHECKSUM_SECURITY_CONTEXT_WARNING,
            "checksum used in a security-looking context",
            "Use sloppy/crypto Hash or Hmac for security or attacker-resistance.",
            "tests/golden/diagnostics/codec_checksum_security_context_warning.json") != 0)
    {
        return 182;
    }
    return 0;
}

static int test_net_diagnostic_json_goldens(void)
{
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_FEATURE_UNAVAILABLE,
            "network feature is unavailable",
            "Enable stdlib.net only in a runtime lane with registered TCP backends.",
            "tests/golden/diagnostics/net_feature_unavailable.json") != 0)
    {
        return 200;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_CONNECT_DENIED,
            "network connect was denied by policy",
            "Strict network policy requires an allow rule for external connects.",
            "tests/golden/diagnostics/net_connect_denied.json") != 0)
    {
        return 201;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_LISTEN_DENIED,
            "network listen was denied by policy",
            "Strict network policy requires an allow rule for external listens.",
            "tests/golden/diagnostics/net_listen_denied.json") != 0)
    {
        return 202;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_INVALID_HOST, "network host is invalid",
            "Validate host values before DNS resolution or address parsing.",
            "tests/golden/diagnostics/net_invalid_host.json") != 0)
    {
        return 203;
    }
    if (expect_stdlib_json_snapshot(SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_INVALID_PORT,
                                    "network port is invalid",
                                    "Ports must be integer values in the documented TCP range.",
                                    "tests/golden/diagnostics/net_invalid_port.json") != 0)
    {
        return 204;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_DNS_FAILURE, "DNS resolution failed",
            "DNS diagnostics must not leak sensitive endpoint details in strict policy reports.",
            "tests/golden/diagnostics/net_dns_failure.json") != 0)
    {
        return 205;
    }
    if (expect_stdlib_json_snapshot(SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_CONNECT_TIMEOUT,
                                    "TCP connect timed out",
                                    "TimeoutError is distinct from caller cancellation.",
                                    "tests/golden/diagnostics/net_connect_timeout.json") != 0)
    {
        return 206;
    }
    if (expect_stdlib_json_snapshot(SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_CONNECT_CANCELLED,
                                    "TCP connect was cancelled",
                                    "CancelledError preserves the explicit cancellation reason.",
                                    "tests/golden/diagnostics/net_connect_cancelled.json") != 0)
    {
        return 207;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_CONNECTION_CLOSED, "TCP connection is closed",
            "Closed connections reject further reads and writes deterministically.",
            "tests/golden/diagnostics/net_connection_closed.json") != 0)
    {
        return 208;
    }
    if (expect_stdlib_json_snapshot(SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_STALE_HANDLE,
                                    "TCP handle is stale",
                                    "Resource handles must not be reused after close or abort.",
                                    "tests/golden/diagnostics/net_stale_handle.json") != 0)
    {
        return 209;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_READ_WRITE_TIMEOUT, "TCP read or write timed out",
            "Read and write deadlines map to deterministic timeout diagnostics.",
            "tests/golden/diagnostics/net_read_write_timeout.json") != 0)
    {
        return 210;
    }
    if (expect_stdlib_json_snapshot(SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_READ_WRITE_CANCELLED,
                                    "TCP read or write was cancelled",
                                    "Cancellation must settle on the V8 owner thread.",
                                    "tests/golden/diagnostics/net_read_write_cancelled.json") != 0)
    {
        return 211;
    }
    if (expect_stdlib_json_snapshot(SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_BACKPRESSURE_OVERFLOW,
                                    "TCP backpressure buffer overflowed",
                                    "Bounded network queues fail instead of growing without limit.",
                                    "tests/golden/diagnostics/net_backpressure_overflow.json") != 0)
    {
        return 212;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_UNSUPPORTED_OPTION, "TCP option is unsupported",
            "Unsupported socket options fail with platform-aware diagnostics.",
            "tests/golden/diagnostics/net_unsupported_option.json") != 0)
    {
        return 213;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_BACKEND_UNAVAILABLE, "TCP backend is unavailable",
            "Backend diagnostics name the operation without exposing raw handles.",
            "tests/golden/diagnostics/net_backend_unavailable.json") != 0)
    {
        return 214;
    }
    return 0;
}

static int test_os_diagnostic_json_goldens(void)
{
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_OS_FEATURE_UNAVAILABLE, "OS feature is unavailable",
            "Enable stdlib.os only in a runtime lane with registered OS backends.",
            "tests/golden/diagnostics/os_feature_unavailable.json") != 0)
    {
        return 230;
    }
    if (expect_stdlib_json_snapshot(SL_DIAG_SEVERITY_ERROR, SL_DIAG_OS_ENV_ACCESS_DENIED,
                                    "environment access was denied",
                                    "Strict OS policy requires an env.read or env.list allowance.",
                                    "tests/golden/diagnostics/os_env_access_denied.json") != 0)
    {
        return 231;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_WARNING, SL_DIAG_OS_ENV_SECRET_REDACTED,
            "environment value was redacted",
            "Diagnostics may name environment keys but must not print values.",
            "tests/golden/diagnostics/os_env_secret_redacted.json") != 0)
    {
        return 232;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_OS_PROCESS_EXECUTION_DENIED,
            "process execution was denied",
            "Strict OS policy requires explicit process.run allowance before native admission.",
            "tests/golden/diagnostics/os_process_execution_denied.json") != 0)
    {
        return 233;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_OS_SHELL_EXECUTION_DENIED, "shell execution was denied",
            "Shell execution is not implicit and must remain separately gated if added.",
            "tests/golden/diagnostics/os_shell_execution_denied.json") != 0)
    {
        return 234;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_OS_COMMAND_NOT_FOUND, "command was not found",
            "Process APIs execute explicit argv only and report lookup failure deterministically.",
            "tests/golden/diagnostics/os_command_not_found.json") != 0)
    {
        return 235;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_OS_INVALID_CWD, "process working directory is invalid",
            "Validate cwd before process admission and avoid leaking machine-local paths.",
            "tests/golden/diagnostics/os_invalid_cwd.json") != 0)
    {
        return 236;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_OS_INVALID_ENV_OVERRIDE,
            "process environment override is invalid",
            "Environment override diagnostics must name keys without printing values.",
            "tests/golden/diagnostics/os_invalid_env_override.json") != 0)
    {
        return 237;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_OS_PROCESS_TIMEOUT, "process timed out",
            "Timeout and caller cancellation must remain distinguishable terminal states.",
            "tests/golden/diagnostics/os_process_timeout.json") != 0)
    {
        return 238;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_OS_PROCESS_CANCELLED, "process was cancelled",
            "Caller cancellation must not be reported as timeout or kill success.",
            "tests/golden/diagnostics/os_process_cancelled.json") != 0)
    {
        return 239;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_OS_PROCESS_KILLED, "process was killed",
            "Kill and terminate outcomes must report explicit process terminal state.",
            "tests/golden/diagnostics/os_process_killed.json") != 0)
    {
        return 240;
    }
    if (expect_stdlib_json_snapshot(SL_DIAG_SEVERITY_ERROR, SL_DIAG_OS_PROCESS_START_FAILED,
                                    "process start failed",
                                    "Start failures must clean up native resources exactly once.",
                                    "tests/golden/diagnostics/os_process_start_failed.json") != 0)
    {
        return 241;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_OS_PIPE_CLOSED, "process pipe is closed",
            "Stale or closed process pipes fail without exposing native handles.",
            "tests/golden/diagnostics/os_pipe_closed.json") != 0)
    {
        return 242;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_OS_UNSUPPORTED_PLATFORM_SIGNAL,
            "platform signal is unsupported",
            "Signal support must be reported honestly per platform.",
            "tests/golden/diagnostics/os_unsupported_platform_signal.json") != 0)
    {
        return 243;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_OS_SIGNAL_HANDLER_FAILURE, "signal handler failed",
            "Shutdown handlers must surface failures without leaking secrets or native handles.",
            "tests/golden/diagnostics/os_signal_handler_failure.json") != 0)
    {
        return 244;
    }
    return 0;
}

static int test_local_ipc_diagnostic_json_goldens(void)
{
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_LOCAL_IPC_FEATURE_UNAVAILABLE,
            "local IPC feature is unavailable",
            "Enable LocalEndpoint only in a runtime lane with Unix socket or named pipe backends.",
            "tests/golden/diagnostics/net_local_ipc_feature_unavailable.json") != 0)
    {
        return 215;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_LOCAL_IPC_UNSUPPORTED_PLATFORM,
            "local IPC platform is unsupported",
            "UnixSocket is POSIX-specific and NamedPipe is Windows-specific.",
            "tests/golden/diagnostics/net_local_ipc_unsupported_platform.json") != 0)
    {
        return 216;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_LOCAL_IPC_INVALID_PATH,
            "local IPC endpoint path is invalid",
            "Use a configured named-root path such as runtime:/my-app.sock.",
            "tests/golden/diagnostics/net_local_ipc_invalid_path.json") != 0)
    {
        return 217;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_LOCAL_IPC_PATH_DENIED,
            "local IPC path was denied by policy",
            "Strict local IPC policy requires an allow rule for the named root and operation.",
            "tests/golden/diagnostics/net_local_ipc_path_denied.json") != 0)
    {
        return 218;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_LOCAL_IPC_STALE_CLEANUP_FAILED,
            "stale local IPC endpoint cleanup failed",
            "unlinkExisting may remove only policy-allowed stale socket files.",
            "tests/golden/diagnostics/net_local_ipc_stale_cleanup_failed.json") != 0)
    {
        return 219;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_LOCAL_IPC_ENDPOINT_EXISTS,
            "local IPC endpoint already exists",
            "Set unlinkExisting only when stale-socket cleanup is intended and policy-allowed.",
            "tests/golden/diagnostics/net_local_ipc_endpoint_exists.json") != 0)
    {
        return 220;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_LOCAL_IPC_CONNECT_FAILED,
            "local IPC connect failed",
            "Connect diagnostics must not expose raw socket, pipe, or OS handles.",
            "tests/golden/diagnostics/net_local_ipc_connect_failed.json") != 0)
    {
        return 221;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_LOCAL_IPC_LISTEN_FAILED, "local IPC listen failed",
            "Listen diagnostics include safe endpoint metadata only.",
            "tests/golden/diagnostics/net_local_ipc_listen_failed.json") != 0)
    {
        return 222;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_LOCAL_IPC_ACCEPT_CANCELLED,
            "local IPC accept was cancelled or timed out",
            "Accept cancellation and timeout remain distinguishable at the JS error boundary.",
            "tests/golden/diagnostics/net_local_ipc_accept_cancelled.json") != 0)
    {
        return 223;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_LOCAL_IPC_READ_WRITE_CANCELLED,
            "local IPC read or write was cancelled or timed out",
            "Read/write cancellation and timeout must not be conflated with backend failure.",
            "tests/golden/diagnostics/net_local_ipc_read_write_cancelled.json") != 0)
    {
        return 224;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_LOCAL_IPC_DISPOSED,
            "local IPC connection or server is disposed",
            "Disposed local IPC resources reject stale use without touching native handles.",
            "tests/golden/diagnostics/net_local_ipc_disposed.json") != 0)
    {
        return 225;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_LOCAL_IPC_BACKEND_UNAVAILABLE,
            "local IPC backend is unavailable",
            "Unsupported platform/backend combinations fail honestly before native admission.",
            "tests/golden/diagnostics/net_local_ipc_backend_unavailable.json") != 0)
    {
        return 226;
    }
    if (expect_stdlib_json_snapshot(
            SL_DIAG_SEVERITY_ERROR, SL_DIAG_NET_LOCAL_IPC_PERMISSION_UNSUPPORTED,
            "local IPC permission mode is unsupported",
            "POSIX mode strings are applied only where the backend can enforce them.",
            "tests/golden/diagnostics/net_local_ipc_permission_unsupported.json") != 0)
    {
        return 227;
    }
    return 0;
}

static int test_stable_code_registry_complete(void)
{
    size_t value = (size_t)SL_DIAG_NONE;

    for (; value <= (size_t)SL_DIAG_NET_LOCAL_IPC_PERMISSION_UNSUPPORTED; value += 1U) {
        if (expect_true(!sl_str_equal(sl_diag_code_name((SlDiagCode)value),
                                      sl_str_from_cstr("SLOPPY_E_UNKNOWN"))) != 0)
        {
            return 53;
        }
    }

    if (expect_str_equal(
            sl_diag_code_name(
                (SlDiagCode)((size_t)SL_DIAG_NET_LOCAL_IPC_PERMISSION_UNSUPPORTED + 1U)),
            sl_str_from_cstr("SLOPPY_E_UNKNOWN")) != 0)
    {
        return 54;
    }

    return 0;
}

static int test_builder_and_bounds(void)
{
    unsigned char buffer[1024];
    SlArena arena;
    SlDiagBuilder builder;
    SlDiagBuilder zero_builder = {0};
    SlDiag diag;
    size_t index = 0U;
    SlStr bad = sl_str_from_parts(NULL, 3U);
    SlOwnedStr owned_hint = {0};

    if (expect_status(make_arena(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 50;
    }

    if (expect_status(sl_diag_builder_init(NULL, &arena, SL_DIAG_SEVERITY_ERROR,
                                           SL_DIAG_INTERNAL_ERROR, sl_str_from_cstr("internal")),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 51;
    }

    if (expect_status(sl_diag_builder_init(&builder, NULL, SL_DIAG_SEVERITY_ERROR,
                                           SL_DIAG_INTERNAL_ERROR, sl_str_from_cstr("internal")),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 52;
    }

    if (expect_status(sl_diag_builder_init(&builder, &arena, SL_DIAG_SEVERITY_ERROR,
                                           SL_DIAG_INTERNAL_ERROR, bad),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 53;
    }

    if (expect_status(sl_diag_builder_init(&builder, &arena, SL_DIAG_SEVERITY_WARNING,
                                           SL_DIAG_INVALID_ARGUMENT,
                                           sl_str_from_cstr("invalid argument")),
                      SL_STATUS_OK) != 0)
    {
        return 54;
    }

    for (index = 0U; index < SL_DIAG_MAX_RELATED; index += 1U) {
        if (expect_status(sl_diag_builder_add_related(&builder, sl_source_span_unknown(),
                                                      sl_str_from_cstr("related")),
                          SL_STATUS_OK) != 0)
        {
            return 55;
        }
    }

    if (expect_status(sl_diag_builder_add_related(&builder, sl_source_span_unknown(),
                                                  sl_str_from_cstr("extra")),
                      SL_STATUS_OUT_OF_RANGE) != 0)
    {
        return 56;
    }

    if (expect_status(sl_str_copy_to_arena(&arena, sl_str_from_cstr("owned hint"), &owned_hint),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_add_hint_owned(&builder, sl_owned_str_as_view(owned_hint)),
                      SL_STATUS_OK) != 0 ||
        builder.diag.hints[0].ptr != owned_hint.ptr)
    {
        return 57;
    }

    for (index = 1U; index < SL_DIAG_MAX_HINTS; index += 1U) {
        if (expect_status(sl_diag_builder_add_hint(&builder, sl_str_from_cstr("hint")),
                          SL_STATUS_OK) != 0)
        {
            return 58;
        }
    }

    if (expect_status(sl_diag_builder_add_hint(&builder, sl_str_from_cstr("extra")),
                      SL_STATUS_OUT_OF_RANGE) != 0)
    {
        return 59;
    }

    if (expect_status(sl_diag_builder_finish(&builder, &diag), SL_STATUS_OK) != 0 ||
        diag.related_count != SL_DIAG_MAX_RELATED || diag.hint_count != SL_DIAG_MAX_HINTS)
    {
        return 60;
    }

    if (expect_status(sl_diag_builder_finish(&builder, NULL), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 61;
    }

    if (expect_status(sl_diag_builder_finish(&zero_builder, &diag), SL_STATUS_INVALID_ARGUMENT) !=
        0)
    {
        return 62;
    }

    return 0;
}

static int test_related_failure_rolls_back_arena(void)
{
    unsigned char buffer[5];
    SlArena arena;
    SlDiagBuilder builder;
    size_t used_before = 0U;

    if (expect_status(make_arena(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 65;
    }

    if (expect_status(sl_diag_builder_init(&builder, &arena, SL_DIAG_SEVERITY_ERROR,
                                           SL_DIAG_INTERNAL_ERROR, sl_str_from_cstr("m")),
                      SL_STATUS_OK) != 0)
    {
        return 66;
    }

    used_before = sl_arena_used(&arena);
    if (expect_status(sl_diag_builder_add_related(
                          &builder, sl_source_span_make(sl_str_from_cstr("path"), 1U, 1U, 0U),
                          sl_str_from_cstr("x")),
                      SL_STATUS_OUT_OF_MEMORY) != 0)
    {
        return 67;
    }

    if (builder.diag.related_count != 0U || sl_arena_used(&arena) != used_before) {
        return 68;
    }

    return 0;
}

static int test_renderer_outputs(void)
{
    unsigned char buffer[1024];
    SlArena arena;
    SlDiagBuilder builder;
    SlDiag diag;
    SlStr rendered;
    SlStr expected_basic =
        sl_str_from_cstr("error SLOPPY_E_OVERFLOW: size calculation overflowed\n");
    SlStr expected_redacted = sl_str_from_cstr("error SLOPPY_E_PERMISSION_DENIED: <redacted>\n");

    if (expect_status(make_arena(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 70;
    }

    if (expect_status(sl_diag_builder_init(&builder, &arena, SL_DIAG_SEVERITY_ERROR,
                                           SL_DIAG_OVERFLOW,
                                           sl_str_from_cstr("size calculation overflowed")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_finish(&builder, &diag), SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_render_text(&arena, &diag, &rendered), SL_STATUS_OK) != 0 ||
        expect_str_equal(rendered, expected_basic) != 0)
    {
        return 71;
    }

    sl_arena_reset(&arena);
    if (expect_status(sl_diag_builder_init(&builder, &arena, SL_DIAG_SEVERITY_ERROR,
                                           SL_DIAG_PERMISSION_DENIED, sl_diag_redacted()),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_finish(&builder, &diag), SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_render_text(&arena, &diag, &rendered), SL_STATUS_OK) != 0 ||
        expect_str_equal(rendered, expected_redacted) != 0)
    {
        return 72;
    }

    if (expect_status(sl_diag_render_text(NULL, &diag, &rendered), SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 73;
    }

    return 0;
}

static int test_renderer_exact_preflight_capacity(void)
{
    unsigned char build_buffer[1024];
    unsigned char render_buffer[1024];
    unsigned char exact_buffer[1024];
    unsigned char short_buffer[1024];
    SlArena build_arena;
    SlArena render_arena;
    SlArena exact_arena;
    SlArena short_arena;
    SlDiagBuilder builder;
    SlDiag diag;
    SlStr rendered;
    SlStr exact;

    if (expect_status(make_arena(&build_arena, build_buffer, sizeof(build_buffer)), SL_STATUS_OK) !=
            0 ||
        expect_status(make_arena(&render_arena, render_buffer, sizeof(render_buffer)),
                      SL_STATUS_OK) != 0)
    {
        return 74;
    }

    if (expect_status(sl_diag_builder_init(&builder, &build_arena, SL_DIAG_SEVERITY_ERROR,
                                           SL_DIAG_INVALID_ROUTE_PATTERN,
                                           sl_str_from_cstr("unsupported dynamic route pattern")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_set_primary_span(
                          &builder, sl_source_span_make(sl_str_from_cstr("app.js"), 5U, 12U, 9U)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_add_hint(
                          &builder, sl_str_from_cstr("expected a string literal route pattern")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_finish(&builder, &diag), SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_render_text(&render_arena, &diag, &rendered), SL_STATUS_OK) != 0)
    {
        return 75;
    }

    if (rendered.length == 0U || rendered.length > sizeof(exact_buffer)) {
        return 76;
    }

    if (expect_status(sl_arena_init(&exact_arena, exact_buffer, rendered.length), SL_STATUS_OK) !=
        0)
    {
        return 77;
    }

    if (expect_status(sl_diag_render_text(&exact_arena, &diag, &exact), SL_STATUS_OK) != 0 ||
        exact.length != rendered.length || sl_arena_used(&exact_arena) != rendered.length ||
        expect_str_equal(exact, rendered) != 0)
    {
        return 77;
    }

    if (expect_status(sl_arena_init(&short_arena, short_buffer, rendered.length - 1U),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_render_text(&short_arena, &diag, &exact), SL_STATUS_OUT_OF_MEMORY) !=
            0)
    {
        return 78;
    }

    return 0;
}

static int test_json_renderer_snapshot(void)
{
    unsigned char buffer[4096];
    unsigned char exact_buffer[512];
    SlArena arena;
    SlArena exact_arena;
    SlDiagBuilder builder;
    SlDiag diag;
    SlStr rendered;
    SlStr exact;

    if (expect_status(make_arena(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 80;
    }
    if (expect_status(sl_diag_builder_init(
                          &builder, &arena, SL_DIAG_SEVERITY_ERROR, SL_DIAG_INVALID_ROUTE_PATTERN,
                          sl_str_from_cstr("unsupported \"dynamic\" route\npattern")),
                      SL_STATUS_OK) != 0)
    {
        return 81;
    }
    if (expect_status(sl_diag_builder_set_primary_span(
                          &builder, sl_source_span_make(sl_str_from_cstr("app.js"), 5U, 12U, 9U)),
                      SL_STATUS_OK) != 0)
    {
        return 82;
    }
    if (expect_status(sl_diag_builder_add_related(
                          &builder, sl_source_span_make(sl_str_from_cstr("routes.js"), 2U, 1U, 0U),
                          sl_str_from_cstr("route registration happens here")),
                      SL_STATUS_OK) != 0)
    {
        return 83;
    }
    if (expect_status(
            sl_diag_builder_add_hint(
                &builder, sl_str_from_cstr("use app.mapGet(\"/users/{id:int}\", handler)")),
            SL_STATUS_OK) != 0)
    {
        return 84;
    }
    if (expect_status(sl_diag_builder_finish(&builder, &diag), SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_render_json(&arena, &diag, &rendered), SL_STATUS_OK) != 0)
    {
        return 85;
    }
    if (expect_snapshot(rendered, "tests/golden/diagnostics/json_single.json") != 0) {
        return 86;
    }
    if (rendered.length == 0U || rendered.length > sizeof(exact_buffer)) {
        return 87;
    }
    if (expect_status(sl_arena_init(&exact_arena, exact_buffer, rendered.length), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_diag_render_json(&exact_arena, &diag, &exact), SL_STATUS_OK) != 0 ||
        exact.length != rendered.length || sl_arena_used(&exact_arena) != rendered.length ||
        expect_str_equal(exact, rendered) != 0)
    {
        return 87;
    }
    return 0;
}

static int test_source_frame_snapshot_and_fallback(void)
{
    unsigned char buffer[4096];
    SlArena arena;
    SlDiagBuilder builder;
    SlDiag diag;
    SlDiagSource source;
    SlStr rendered;
    SlStr fallback;

    if (expect_status(make_arena(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 90;
    }
    if (expect_status(sl_diag_builder_init(&builder, &arena, SL_DIAG_SEVERITY_ERROR,
                                           SL_DIAG_INVALID_ROUTE_PATTERN,
                                           sl_str_from_cstr("unsupported dynamic route pattern")),
                      SL_STATUS_OK) != 0)
    {
        return 91;
    }
    if (expect_status(sl_diag_builder_set_primary_span(
                          &builder, sl_source_span_make(sl_str_from_cstr("app.js"), 5U, 12U, 9U)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_add_hint(
                          &builder, sl_str_from_cstr("expected a string literal route pattern")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_finish(&builder, &diag), SL_STATUS_OK) != 0)
    {
        return 92;
    }

    source.path = sl_str_from_cstr("app.js");
    source.text = sl_str_from_cstr("import { Sloppy } from \"sloppy\";\n"
                                   "const app = Sloppy.create();\n"
                                   "const routePath = \"/users\";\n"
                                   "\n"
                                   "app.mapGet(routePath, handler)\n");
    if (expect_status(sl_diag_render_text_with_source(&arena, &diag, &source, &rendered),
                      SL_STATUS_OK) != 0)
    {
        return 93;
    }
    if (expect_snapshot(rendered, "tests/golden/diagnostics/source_frame.snap") != 0) {
        return 94;
    }

    source.text = sl_str_from_cstr("only one line\n");
    if (expect_status(sl_diag_render_text_with_source(&arena, &diag, &source, &fallback),
                      SL_STATUS_OK) != 0 ||
        expect_str_equal(fallback,
                         sl_str_from_cstr("error SLOPPY_E_INVALID_ROUTE_PATTERN: unsupported "
                                          "dynamic route pattern\n\n"
                                          "  at app.js:5:12 (len 9)\n\n"
                                          "  help:\n"
                                          "    expected a string literal route pattern\n")) != 0)
    {
        return 95;
    }
    return 0;
}

static int test_json_source_frame_snapshot(void)
{
    unsigned char buffer[8192];
    SlArena arena;
    SlDiagBuilder builder;
    SlDiag diag;
    SlDiagSource source;
    SlStr fallback;
    SlStr rendered;

    if (expect_status(make_arena(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 96;
    }
    if (expect_status(sl_diag_builder_init(&builder, &arena, SL_DIAG_SEVERITY_ERROR,
                                           SL_DIAG_INVALID_ROUTE_PATTERN,
                                           sl_str_from_cstr("unsupported dynamic route pattern")),
                      SL_STATUS_OK) != 0)
    {
        return 97;
    }
    if (expect_status(sl_diag_builder_set_primary_span(
                          &builder, sl_source_span_make(sl_str_from_cstr("app.js"), 5U, 12U, 9U)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_add_hint(
                          &builder, sl_str_from_cstr("expected a string literal route pattern")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_finish(&builder, &diag), SL_STATUS_OK) != 0)
    {
        return 98;
    }

    source.path = sl_str_from_cstr("app.js");
    source.text = sl_str_from_cstr("import { Sloppy } from \"sloppy\";\n"
                                   "const app = Sloppy.create();\n"
                                   "const routePath = \"/users\";\n"
                                   "\n"
                                   "app.mapGet(routePath, handler)\n");
    if (expect_status(sl_diag_render_json_with_source(&arena, &diag, &source, &rendered),
                      SL_STATUS_OK) != 0)
    {
        return 99;
    }
    if (expect_snapshot(rendered, "tests/golden/diagnostics/json_source_frame.json") != 0) {
        return 100;
    }

    source.path = sl_str_empty();
    if (expect_status(sl_diag_render_json(&arena, &diag, &fallback), SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_render_json_with_source(&arena, &diag, &source, &rendered),
                      SL_STATUS_OK) != 0 ||
        expect_str_equal(rendered, fallback) != 0)
    {
        return 101;
    }
    return 0;
}

static int test_redaction_helper(void)
{
    unsigned char buffer[1024];
    SlArena arena;
    SlStr redacted;

    if (expect_status(make_arena(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 100;
    }
    if (expect_status(sl_diag_redact_secrets(
                          &arena,
                          sl_str_from_cstr("password=secret PWD = {top;secret}; token:abc "
                                           "postgres://ada:secret@localhost/db API_KEY=xyz "
                                           "monkey=value donkey:abc key=plain "
                                           "key exchange: keep key steps: visible "
                                           "connectionString=Server=.;Password=p; "
                                           "clientSecret=abc private_key=pem passphrase=hunter2"),
                          &redacted),
                      SL_STATUS_OK) != 0)
    {
        return 101;
    }
    if (expect_str_equal(
            redacted, sl_str_from_cstr("password=<redacted> PWD = <redacted>; "
                                       "token:<redacted> postgres://ada:<redacted>@localhost/db "
                                       "API_KEY=<redacted> monkey=value donkey:abc key=<redacted> "
                                       "key exchange: keep key steps: visible "
                                       "connectionString=<redacted>; clientSecret=<redacted> "
                                       "private_key=<redacted> passphrase=<redacted>")) != 0)
    {
        return 102;
    }
    return 0;
}

static int test_diagnostic_golden_suite_expansion(void)
{
    unsigned char buffer[8192];
    SlArena arena;
    SlDiagBuilder builder;
    SlDiag diag;
    SlDiagSource source;
    SlStr rendered;

    if (expect_status(make_arena(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 103;
    }

    if (expect_status(sl_diag_builder_init(&builder, &arena, SL_DIAG_SEVERITY_ERROR,
                                           SL_DIAG_ENGINE_PROMISE_REJECTION,
                                           sl_str_from_cstr("async handler rejected: <redacted>")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_set_primary_span(
                          &builder, sl_source_span_make(sl_str_from_cstr("app.js"), 9U, 21U, 7U)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_add_related(
                          &builder, sl_source_span_make(sl_str_from_cstr("app.js"), 4U, 1U, 0U),
                          sl_str_from_cstr("route GET /users handler Users.List")),
                      SL_STATUS_OK) != 0 ||
        expect_status(
            sl_diag_builder_add_hint(
                &builder, sl_str_from_cstr("rejection reason redacted; inspect handler logs for "
                                           "safe context")),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_finish(&builder, &diag), SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_render_json(&arena, &diag, &rendered), SL_STATUS_OK) != 0 ||
        expect_snapshot(rendered, "tests/golden/diagnostics/async_rejection.json") != 0)
    {
        return 104;
    }

    sl_arena_reset(&arena);
    if (expect_status(sl_diag_builder_init(
                          &builder, &arena, SL_DIAG_SEVERITY_ERROR, SL_DIAG_PERMISSION_DENIED,
                          sl_str_from_cstr("capability denied for data.main read")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_set_primary_span(
                          &builder, sl_source_span_make(sl_str_from_cstr("app.js"), 7U, 15U, 8U)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_add_hint(
                          &builder, sl_str_from_cstr("declare data.main with read access")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_finish(&builder, &diag), SL_STATUS_OK) != 0)
    {
        return 105;
    }
    source.path = sl_str_from_cstr("app.js");
    source.text = sl_str_from_cstr("import { data } from \"sloppy\";\n"
                                   "\n"
                                   "const app = Sloppy.create();\n"
                                   "app.mapGet(\"/users\", handler);\n"
                                   "\n"
                                   "function handler() {\n"
                                   "  const rows = db.query(\"select * from users\");\n"
                                   "}\n");
    if (expect_status(sl_diag_render_text_with_source(&arena, &diag, &source, &rendered),
                      SL_STATUS_OK) != 0 ||
        expect_snapshot(rendered, "tests/golden/diagnostics/capability_denial_source_frame.snap") !=
            0)
    {
        return 106;
    }

    sl_arena_reset(&arena);
    if (expect_status(sl_diag_builder_init(&builder, &arena, SL_DIAG_SEVERITY_ERROR,
                                           SL_DIAG_MALFORMED_JSON,
                                           sl_str_from_cstr("request body is not valid JSON")),
                      SL_STATUS_OK) != 0 ||
        expect_status(
            sl_diag_builder_set_primary_span(
                &builder, sl_source_span_make(sl_str_from_cstr("<request-body>"), 1U, 9U, 1U)),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_add_hint(&builder, sl_str_from_cstr("expected a JSON value")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_finish(&builder, &diag), SL_STATUS_OK) != 0)
    {
        return 107;
    }
    source.path = sl_str_from_cstr("<request-body>");
    source.text = sl_str_from_cstr("{\"name\":");
    if (expect_status(sl_diag_render_json_with_source(&arena, &diag, &source, &rendered),
                      SL_STATUS_OK) != 0 ||
        expect_snapshot(rendered, "tests/golden/diagnostics/malformed_json_body.json") != 0)
    {
        return 108;
    }

    sl_arena_reset(&arena);
    if (expect_status(sl_diag_builder_init(
                          &builder, &arena, SL_DIAG_SEVERITY_ERROR, SL_DIAG_SQLITE_PROVIDER_ERROR,
                          sl_str_from_cstr("sqlite provider failed: sql parameters=<redacted> "
                                           "connectionString=<redacted>")),
                      SL_STATUS_OK) != 0 ||
        expect_status(
            sl_diag_builder_set_primary_span(
                &builder, sl_source_span_make(sl_str_from_cstr("providers.js"), 3U, 7U, 12U)),
            SL_STATUS_OK) != 0 ||
        expect_status(
            sl_diag_builder_add_hint(&builder, sl_str_from_cstr("provider metadata is redacted")),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_finish(&builder, &diag), SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_render_json(&arena, &diag, &rendered), SL_STATUS_OK) != 0 ||
        expect_snapshot(rendered, "tests/golden/diagnostics/provider_failure_redacted.json") != 0)
    {
        return 109;
    }

    return 0;
}

static int test_snapshots(void)
{
    int result = test_missing_service_snapshot();
    if (result != 0) {
        return result;
    }

    result = test_invalid_plan_version_snapshot();
    if (result != 0) {
        return result;
    }

    result = test_json_renderer_snapshot();
    if (result != 0) {
        return result;
    }

    result = test_source_frame_snapshot_and_fallback();
    if (result != 0) {
        return result;
    }

    result = test_json_source_frame_snapshot();
    if (result != 0) {
        return result;
    }

    return 0;
}

static int test_code_name_groups(void)
{
    int result = test_names_and_spans();
    if (result != 0) {
        return result;
    }

    result = test_provider_code_names();
    if (result != 0) {
        return result;
    }
    result = test_http_code_names();
    if (result != 0) {
        return result;
    }

    result = test_time_and_crypto_code_names();
    if (result != 0) {
        return result;
    }
    result = test_codec_code_names();
    if (result != 0) {
        return result;
    }
    result = test_net_code_names();
    if (result != 0) {
        return result;
    }
    result = test_os_code_names();
    if (result != 0) {
        return result;
    }

    result = test_engine_async_code_names();
    if (result != 0) {
        return result;
    }

    result = test_stable_code_registry_complete();
    if (result != 0) {
        return result;
    }

    return 0;
}

static int test_diagnostic_json_golden_groups(void)
{
    int result = test_diagnostic_golden_suite_expansion();
    if (result != 0) {
        return result;
    }

    result = test_time_diagnostic_json_goldens();
    if (result != 0) {
        return result;
    }

    result = test_crypto_diagnostic_json_goldens();
    if (result != 0) {
        return result;
    }

    result = test_codec_diagnostic_json_goldens();
    if (result != 0) {
        return result;
    }

    result = test_net_diagnostic_json_goldens();
    if (result != 0) {
        return result;
    }

    result = test_os_diagnostic_json_goldens();
    if (result != 0) {
        return result;
    }

    return test_local_ipc_diagnostic_json_goldens();
}

int main(void)
{
    int result = test_code_name_groups();
    if (result != 0) {
        return result;
    }

    result = test_builder_and_bounds();
    if (result != 0) {
        return result;
    }

    result = test_related_failure_rolls_back_arena();
    if (result != 0) {
        return result;
    }

    result = test_renderer_outputs();
    if (result != 0) {
        return result;
    }

    result = test_renderer_exact_preflight_capacity();
    if (result != 0) {
        return result;
    }

    result = test_redaction_helper();
    if (result != 0) {
        return result;
    }

    result = test_diagnostic_json_golden_groups();
    if (result != 0) {
        return result;
    }

    return test_snapshots();
}
