#ifndef SLOPPY_HTTP2_HPACK_H
#define SLOPPY_HTTP2_HPACK_H

#include "sloppy/arena.h"
#include "sloppy/bytes.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_HTTP2_HPACK_DEFAULT_DYNAMIC_TABLE_SIZE 4096U
#define SL_HTTP2_HPACK_DEFAULT_MAX_HEADERS 128U
#define SL_HTTP2_HPACK_DEFAULT_MAX_HEADER_LIST_BYTES 65536U

typedef struct SlHttp2HeaderField
{
    SlStr name;
    SlStr value;
    bool sensitive;
} SlHttp2HeaderField;

typedef struct SlHttp2HeaderList
{
    SlHttp2HeaderField* fields;
    size_t count;
} SlHttp2HeaderList;

/*
 * HPACK codec adapters keep nghttp2 as an internal implementation detail.
 *
 * Header fields returned by decode, and encoded header blocks returned by encode, are
 * arena-owned and remain valid until the arena is reset/disposed. Codec objects must be
 * disposed when the owning connection is closed.
 */
typedef struct SlHttp2HpackDecoder
{
    uintptr_t opaque[3];
} SlHttp2HpackDecoder;

typedef struct SlHttp2HpackEncoder
{
    uintptr_t opaque[2];
} SlHttp2HpackEncoder;

SlStatus sl_http2_hpack_decoder_init(SlHttp2HpackDecoder* decoder, size_t max_headers,
                                     size_t max_header_list_bytes);
void sl_http2_hpack_decoder_dispose(SlHttp2HpackDecoder* decoder);
SlStatus sl_http2_hpack_decoder_set_table_size(SlHttp2HpackDecoder* decoder, size_t table_size);
size_t sl_http2_hpack_decoder_dynamic_table_size(const SlHttp2HpackDecoder* decoder);
size_t sl_http2_hpack_decoder_max_dynamic_table_size(const SlHttp2HpackDecoder* decoder);
SlStatus sl_http2_hpack_decode(SlHttp2HpackDecoder* decoder, SlArena* arena, SlBytes header_block,
                               bool final_fragment, SlHttp2HeaderList* out_headers);

SlStatus sl_http2_hpack_encoder_init(SlHttp2HpackEncoder* encoder, size_t max_dynamic_table_size,
                                     size_t max_output_bytes);
void sl_http2_hpack_encoder_dispose(SlHttp2HpackEncoder* encoder);
SlStatus sl_http2_hpack_encoder_set_peer_table_size(SlHttp2HpackEncoder* encoder,
                                                    size_t table_size);
SlStatus sl_http2_hpack_encode(SlHttp2HpackEncoder* encoder, SlArena* arena,
                               const SlHttp2HeaderList* headers, SlBytes* out_header_block);

#ifdef __cplusplus
}
#endif

#endif
