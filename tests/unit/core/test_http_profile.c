#include "sloppy/http_profile.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void set_env_value(const char* name, const char* value)
{
#if defined(_WIN32)
    _putenv_s(name, value == NULL ? "" : value);
#else
    if (value == NULL || value[0] == '\0') {
        unsetenv(name);
    }
    else {
        setenv(name, value, 1);
    }
#endif
}

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int json_contains(SlBytes json, const char* text)
{
    if (json.ptr == NULL || text == NULL) {
        return 0;
    }
    return strstr((const char*)json.ptr, text) != NULL;
}

static void terminate_json(unsigned char* storage, size_t capacity, SlBytes json)
{
    if (storage == NULL || capacity == 0U) {
        return;
    }
    if (json.length < capacity) {
        storage[json.length] = '\0';
        return;
    }
    storage[capacity - 1U] = '\0';
}

static int test_disabled_profile_is_noop(void)
{
    unsigned char storage[16384];
    SlByteBuilder builder = {0};
    SlBytes json = {0};

    set_env_value("SLOPPY_HTTP_PROFILE", "");
    set_env_value("SLOPPY_HTTP_PROFILE_SCENARIO", "disabled-test");
    sl_http_profile_reset();
    sl_http_profile_count(SL_HTTP_PROFILE_COUNTER_REQUESTS_TOTAL, 7U);
    sl_http_profile_record_phase(SL_HTTP_PROFILE_PHASE_HTTP_PARSE, 42U);
    if (expect_true(!sl_http_profile_enabled()) != 0 ||
        !sl_status_is_ok(sl_byte_builder_init_fixed(&builder, storage, sizeof(storage))) ||
        !sl_status_is_ok(sl_http_profile_write_json(&builder)))
    {
        return 1;
    }
    json = sl_byte_builder_view(&builder);
    terminate_json(storage, sizeof(storage), json);
    return expect_true(json_contains(json, "\"requests\": 0") &&
                       json_contains(json, "\"requestsTotal\": 0") &&
                       json_contains(json, "\"http_parse\": { \"totalNs\": 0"));
}

static int test_enabled_profile_emits_phase_and_counters(void)
{
    unsigned char storage[16384];
    SlByteBuilder builder = {0};
    SlBytes json = {0};
    bool ok = false;

    set_env_value("SLOPPY_HTTP_PROFILE", "1");
    set_env_value("SLOPPY_HTTP_PROFILE_SCENARIO", "profile-test");
    sl_http_profile_reset();
    sl_http_profile_count(SL_HTTP_PROFILE_COUNTER_REQUESTS_TOTAL, 3U);
    sl_http_profile_count(SL_HTTP_PROFILE_COUNTER_NATIVE_JSON_HITS, 2U);
    sl_http_profile_record_phase(SL_HTTP_PROFILE_PHASE_HTTP_PARSE, 10U);
    sl_http_profile_record_phase(SL_HTTP_PROFILE_PHASE_HTTP_PARSE, 30U);
    if (expect_true(sl_http_profile_enabled()) != 0) {
        fprintf(stderr, "profile should be enabled\n");
        return 1;
    }
    if (!sl_status_is_ok(sl_byte_builder_init_fixed(&builder, storage, sizeof(storage)))) {
        fprintf(stderr, "profile builder init failed\n");
        return 1;
    }
    if (!sl_status_is_ok(sl_http_profile_write_json(&builder))) {
        fprintf(stderr, "profile json write failed\n");
        return 1;
    }
    json = sl_byte_builder_view(&builder);
    terminate_json(storage, sizeof(storage), json);
    ok = json_contains(json, "\"scenario\": \"profile-test\"") &&
         json_contains(json, "\"requests\": 3") &&
         json_contains(json, "\"http_parse\": { \"totalNs\": 40") &&
         json_contains(json, "\"count\": 2") && json_contains(json, "\"avgNs\": 20") &&
         json_contains(json, "\"nativeJsonHits\": 2");
    if (!ok) {
        fprintf(stderr, "profile json did not contain expected enabled counters:\n%s\n",
                (const char*)json.ptr);
    }
    return expect_true(ok);
}

static int test_profile_json_escapes_control_bytes(void)
{
    unsigned char storage[16384];
    SlByteBuilder builder = {0};
    SlBytes json = {0};

    set_env_value("SLOPPY_HTTP_PROFILE", "1");
    set_env_value("SLOPPY_HTTP_PROFILE_SCENARIO", "line\nback\bform\fctl\001");
    sl_http_profile_reset();
    if (expect_true(sl_http_profile_enabled()) != 0 ||
        !sl_status_is_ok(sl_byte_builder_init_fixed(&builder, storage, sizeof(storage))) ||
        !sl_status_is_ok(sl_http_profile_write_json(&builder)))
    {
        return 1;
    }
    json = sl_byte_builder_view(&builder);
    terminate_json(storage, sizeof(storage), json);
    return expect_true(json_contains(json, "\"scenario\": \"line\\nback\\bform\\fctl\\u0001\""));
}

int main(void)
{
    if (test_disabled_profile_is_noop() != 0) {
        return 1;
    }
    if (test_enabled_profile_emits_phase_and_counters() != 0) {
        return 2;
    }
    if (test_profile_json_escapes_control_bytes() != 0) {
        return 3;
    }
    return 0;
}
