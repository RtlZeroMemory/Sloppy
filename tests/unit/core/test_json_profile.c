#include "sloppy/json_profile.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static bool text_contains(const char* text, const char* needle)
{
    return text != NULL && needle != NULL && strstr(text, needle) != NULL;
}

static int test_profile_json_escapes_weird_scenario_names(void)
{
    char output[4096];
    size_t length = 0U;
    FILE* file = NULL;
    SlJsonProfileSnapshot snapshot = {0};

#if defined(_WIN32)
    if (tmpfile_s(&file) != 0) {
        file = NULL;
    }
#else
    file = tmpfile();
#endif
    if (file == NULL) {
        return 1;
    }

    snapshot.enabled = true;
    snapshot.scenario = "quote\"slash\\line\nctrl\x01utf8 \xc3\xa9";
    snapshot.iterations = 3U;
    snapshot.counters[SL_JSON_PROFILE_COUNTER_REQUESTS_TOTAL] = 1U;

    sl_json_profile_fprint_json(file, &snapshot, 0U);
    if (fflush(file) != 0 || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return 2;
    }
    length = fread(output, 1U, sizeof(output) - 1U, file);
    output[length] = '\0';
    fclose(file);

    return expect_true(text_contains(output,
                                     "\"scenario\": \"quote\\\"slash\\\\line\\nctrl\\u0001utf8 "
                                     "\xc3\xa9\"") &&
                       text_contains(output, "\"iterations\": 3") &&
                       text_contains(output, "\"requestsTotal\": 1")) != 0
               ? 3
               : 0;
}

int main(void)
{
    return test_profile_json_escapes_weird_scenario_names();
}
