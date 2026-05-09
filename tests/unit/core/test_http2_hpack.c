#include "sloppy/http2_hpack.h"

#include <stdbool.h>
#include <stdint.h>

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

static SlHttp2HeaderField header_field(const char* name, const char* value, bool sensitive)
{
    return (SlHttp2HeaderField){
        .name = sl_str_from_cstr(name), .value = sl_str_from_cstr(value), .sensitive = sensitive};
}

static int test_decode_rfc_huffman_request_example(void)
{
    unsigned char arena_storage[4096];
    SlArena arena = {0};
    SlHttp2HpackDecoder decoder = {0};
    SlHttp2HeaderList headers = {0};
    static const unsigned char block[] = {0x82, 0x86, 0x84, 0x41, 0x8c, 0xf1, 0xe3, 0xc2, 0xe5,
                                          0xf2, 0x3a, 0x6b, 0xa0, 0xab, 0x90, 0xf4, 0xff};

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_http2_hpack_decoder_init(&decoder, 8U, 0U), SL_STATUS_OK) != 0)
    {
        return 1;
    }

    if (expect_status(sl_http2_hpack_decode(&decoder, &arena,
                                            sl_bytes_from_parts(block, sizeof(block)), true,
                                            &headers),
                      SL_STATUS_OK) != 0)
    {
        sl_http2_hpack_decoder_dispose(&decoder);
        return 2;
    }

    if (headers.count != 4U || expect_str_equal(headers.fields[0].name, ":method") != 0 ||
        expect_str_equal(headers.fields[0].value, "GET") != 0 ||
        expect_str_equal(headers.fields[1].name, ":scheme") != 0 ||
        expect_str_equal(headers.fields[1].value, "http") != 0 ||
        expect_str_equal(headers.fields[2].name, ":path") != 0 ||
        expect_str_equal(headers.fields[2].value, "/") != 0 ||
        expect_str_equal(headers.fields[3].name, ":authority") != 0 ||
        expect_str_equal(headers.fields[3].value, "www.example.com") != 0 ||
        sl_http2_hpack_decoder_dynamic_table_size(&decoder) == 0U)
    {
        sl_http2_hpack_decoder_dispose(&decoder);
        return 3;
    }

    sl_http2_hpack_decoder_dispose(&decoder);
    return 0;
}

static int test_decode_rejects_invalid_index_and_preserves_arena(void)
{
    unsigned char arena_storage[256];
    SlArena arena = {0};
    SlArenaMark mark = {0};
    SlHttp2HpackDecoder decoder = {0};
    SlHttp2HeaderList headers = {0};
    static const unsigned char invalid_index_zero[] = {0x80};

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_http2_hpack_decoder_init(&decoder, 4U, 0U), SL_STATUS_OK) != 0)
    {
        return 1;
    }

    mark = sl_arena_mark(&arena);
    if (expect_status(sl_http2_hpack_decode(
                          &decoder, &arena,
                          sl_bytes_from_parts(invalid_index_zero, sizeof(invalid_index_zero)), true,
                          &headers),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        sl_arena_stats(&arena).used != mark.offset)
    {
        sl_http2_hpack_decoder_dispose(&decoder);
        return 2;
    }

    sl_http2_hpack_decoder_dispose(&decoder);
    return 0;
}

static int test_decode_enforces_header_count_limit(void)
{
    unsigned char arena_storage[1024];
    SlArena arena = {0};
    SlHttp2HpackDecoder decoder = {0};
    SlHttp2HeaderList headers = {0};
    static const unsigned char block[] = {0x82, 0x86};

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_http2_hpack_decoder_init(&decoder, 1U, 0U), SL_STATUS_OK) != 0)
    {
        return 1;
    }

    if (expect_status(sl_http2_hpack_decode(&decoder, &arena,
                                            sl_bytes_from_parts(block, sizeof(block)), true,
                                            &headers),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0)
    {
        sl_http2_hpack_decoder_dispose(&decoder);
        return 2;
    }

    sl_http2_hpack_decoder_dispose(&decoder);
    return 0;
}

static int test_decode_accepts_dynamic_table_size_update_at_block_start(void)
{
    unsigned char arena_storage[1024];
    SlArena arena = {0};
    SlHttp2HpackDecoder decoder = {0};
    SlHttp2HeaderList headers = {0};
    static const unsigned char block[] = {0x20, 0x82};

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_http2_hpack_decoder_init(&decoder, 4U, 0U), SL_STATUS_OK) != 0)
    {
        return 1;
    }

    if (expect_status(sl_http2_hpack_decode(&decoder, &arena,
                                            sl_bytes_from_parts(block, sizeof(block)), true,
                                            &headers),
                      SL_STATUS_OK) != 0 ||
        headers.count != 1U || expect_str_equal(headers.fields[0].name, ":method") != 0 ||
        expect_str_equal(headers.fields[0].value, "GET") != 0 ||
        sl_http2_hpack_decoder_max_dynamic_table_size(&decoder) != 0U)
    {
        sl_http2_hpack_decoder_dispose(&decoder);
        return 2;
    }

    sl_http2_hpack_decoder_dispose(&decoder);
    return 0;
}

static int test_encode_round_trips_header_list_and_sensitive_flag(void)
{
    unsigned char encode_storage[8192];
    unsigned char decode_storage[8192];
    SlArena encode_arena = {0};
    SlArena decode_arena = {0};
    SlHttp2HpackEncoder encoder = {0};
    SlHttp2HpackDecoder decoder = {0};
    SlHttp2HeaderField fields[] = {header_field(":method", "POST", false),
                                   header_field(":scheme", "https", false),
                                   header_field(":path", "/submit", false),
                                   header_field("content-type", "application/json", false),
                                   header_field("authorization", "Bearer test", true)};
    SlHttp2HeaderList input = {.fields = fields, .count = sizeof(fields) / sizeof(fields[0])};
    SlHttp2HeaderList decoded = {0};
    SlBytes block = {0};

    if (expect_status(sl_arena_init(&encode_arena, encode_storage, sizeof(encode_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&decode_arena, decode_storage, sizeof(decode_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http2_hpack_encoder_init(&encoder, 0U, 0U), SL_STATUS_OK) != 0 ||
        expect_status(sl_http2_hpack_decoder_init(&decoder, 8U, 0U), SL_STATUS_OK) != 0)
    {
        return 1;
    }

    if (expect_status(sl_http2_hpack_encode(&encoder, &encode_arena, &input, &block),
                      SL_STATUS_OK) != 0 ||
        block.length == 0U ||
        expect_status(sl_http2_hpack_decode(&decoder, &decode_arena, block, true, &decoded),
                      SL_STATUS_OK) != 0 ||
        decoded.count != input.count)
    {
        sl_http2_hpack_encoder_dispose(&encoder);
        sl_http2_hpack_decoder_dispose(&decoder);
        return 2;
    }

    for (size_t index = 0U; index < input.count; index += 1U) {
        if (!sl_str_equal(decoded.fields[index].name, input.fields[index].name) ||
            !sl_str_equal(decoded.fields[index].value, input.fields[index].value) ||
            decoded.fields[index].sensitive != input.fields[index].sensitive)
        {
            sl_http2_hpack_encoder_dispose(&encoder);
            sl_http2_hpack_decoder_dispose(&decoder);
            return 3;
        }
    }

    sl_http2_hpack_encoder_dispose(&encoder);
    sl_http2_hpack_decoder_dispose(&decoder);
    return 0;
}

static int test_decode_rejects_header_array_size_overflow(void)
{
    unsigned char storage[128];
    SlArena arena = {0};
    SlHttp2HpackDecoder decoder = {0};
    SlHttp2HeaderList decoded = {0};
    size_t impossible_count = (SIZE_MAX / sizeof(SlHttp2HeaderField)) + 1U;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_http2_hpack_decoder_init(&decoder, impossible_count, 0U), SL_STATUS_OK) !=
            0)
    {
        return 1;
    }

    if (expect_status(sl_http2_hpack_decode(&decoder, &arena, sl_bytes_empty(), true, &decoded),
                      SL_STATUS_OVERFLOW) != 0)
    {
        sl_http2_hpack_decoder_dispose(&decoder);
        return 2;
    }

    sl_http2_hpack_decoder_dispose(&decoder);
    return 0;
}

int main(void)
{
    int result = 0;

    result = test_decode_rfc_huffman_request_example();
    if (result != 0) {
        return result;
    }
    result = test_decode_rejects_invalid_index_and_preserves_arena();
    if (result != 0) {
        return result;
    }
    result = test_decode_enforces_header_count_limit();
    if (result != 0) {
        return result;
    }
    result = test_decode_accepts_dynamic_table_size_update_at_block_start();
    if (result != 0) {
        return result;
    }
    result = test_encode_round_trips_header_list_and_sensitive_flag();
    if (result != 0) {
        return result;
    }
    result = test_decode_rejects_header_array_size_overflow();
    if (result != 0) {
        return result;
    }

    return 0;
}
