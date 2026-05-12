#include "sloppy/http_response.h"

#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>

#define FUZZ_JSON_RESPONSE_OUTPUT_SIZE 4096U

static uint16_t fuzz_status(uint8_t value)
{
    static const uint16_t statuses[] = {200U, 201U, 202U, 204U, 304U, 400U,
                                        404U, 413U, 415U, 500U, 503U};
    return statuses[value % (sizeof(statuses) / sizeof(statuses[0]))];
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    unsigned char output[FUZZ_JSON_RESPONSE_OUTPUT_SIZE];
    SlBytes written = {0};
    SlHttpResponse response = {0};
    SlHttpResponseWriteOptions options = {0};
    size_t body_offset = 0U;
    size_t capacity = sizeof(output);

    if (data == NULL || size == 0U) {
        return 0;
    }

    response = sl_http_response_json(fuzz_status(data[0]), sl_bytes_empty());
    body_offset = 1U;
    if (size > 2U) {
        uint16_t capacity_seed = (uint16_t)(((uint16_t)data[1] << 8U) | (uint16_t)data[2]);
        capacity = (size_t)((capacity_seed % sizeof(output)) + 1U);
        body_offset = 3U;
    }
    else if (size > 1U) {
        capacity = (size_t)((data[1] % sizeof(output)) + 1U);
        body_offset = 2U;
    }
    if (size > 3U) {
        body_offset = 4U;
    }
    if (body_offset < size) {
        response.body = sl_bytes_from_parts(data + body_offset, size - body_offset);
    }
    if (size > 3U && (data[3] & 1U) != 0U) {
        options.suppress_body = true;
        sl_http_response_write_with_options(&response, &options, output, capacity, &written);
    }
    else {
        sl_http_response_write(&response, output, capacity, &written);
    }
    return 0;
}
