#include "sloppy/http2_hpack.h"

#include "sloppy/checked_math.h"

#define NGHTTP2_NO_SSIZE_T 1
#include <nghttp2/nghttp2.h>

#include <stdint.h>

#define SL_HTTP2_HPACK_DECODER_INFLATER_SLOT 0U
#define SL_HTTP2_HPACK_DECODER_MAX_HEADERS_SLOT 1U
#define SL_HTTP2_HPACK_DECODER_MAX_HEADER_LIST_BYTES_SLOT 2U
#define SL_HTTP2_HPACK_ENCODER_DEFLATER_SLOT 0U
#define SL_HTTP2_HPACK_ENCODER_MAX_OUTPUT_BYTES_SLOT 1U

static nghttp2_hd_inflater* sl_http2_hpack_decoder_inflater(const SlHttp2HpackDecoder* decoder)
{
    return decoder == NULL
               ? NULL
               : (nghttp2_hd_inflater*)decoder->opaque[SL_HTTP2_HPACK_DECODER_INFLATER_SLOT];
}

static size_t sl_http2_hpack_decoder_max_headers(const SlHttp2HpackDecoder* decoder)
{
    return decoder == NULL ? 0U : (size_t)decoder->opaque[SL_HTTP2_HPACK_DECODER_MAX_HEADERS_SLOT];
}

static size_t sl_http2_hpack_decoder_max_header_list_bytes(const SlHttp2HpackDecoder* decoder)
{
    return decoder == NULL
               ? 0U
               : (size_t)decoder->opaque[SL_HTTP2_HPACK_DECODER_MAX_HEADER_LIST_BYTES_SLOT];
}

static nghttp2_hd_deflater* sl_http2_hpack_encoder_deflater(const SlHttp2HpackEncoder* encoder)
{
    return encoder == NULL
               ? NULL
               : (nghttp2_hd_deflater*)encoder->opaque[SL_HTTP2_HPACK_ENCODER_DEFLATER_SLOT];
}

static size_t sl_http2_hpack_encoder_max_output_bytes(const SlHttp2HpackEncoder* encoder)
{
    return encoder == NULL ? 0U
                           : (size_t)encoder->opaque[SL_HTTP2_HPACK_ENCODER_MAX_OUTPUT_BYTES_SLOT];
}

static bool sl_http2_hpack_valid_str(SlStr str)
{
    return str.length == 0U || str.ptr != NULL;
}

static bool sl_http2_hpack_valid_bytes(SlBytes bytes)
{
    return bytes.length == 0U || bytes.ptr != NULL;
}

static SlStatus sl_http2_hpack_status_from_nghttp2(int rv)
{
    if (rv == 0) {
        return sl_status_ok();
    }
    if (rv == NGHTTP2_ERR_NOMEM) {
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }
    if (rv == NGHTTP2_ERR_INSUFF_BUFSIZE) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }
    if (rv == NGHTTP2_ERR_INVALID_STATE) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
}

static SlStatus sl_http2_hpack_status_from_nghttp2_ssize(nghttp2_ssize rv)
{
    if (rv >= 0) {
        return sl_status_ok();
    }
    return sl_http2_hpack_status_from_nghttp2((int)rv);
}

static SlStatus sl_http2_hpack_copy_nv(SlArena* arena, const nghttp2_nv* nv,
                                       SlHttp2HeaderField* out_field, size_t* inout_bytes)
{
    SlStatus status = sl_status_ok();
    SlStr name = {0};
    SlStr value = {0};
    size_t next_bytes = 0U;

    if (arena == NULL || nv == NULL || out_field == NULL || inout_bytes == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if ((nv->namelen != 0U && nv->name == NULL) || (nv->valuelen != 0U && nv->value == NULL)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (nv->namelen > SIZE_MAX - nv->valuelen ||
        *inout_bytes > SIZE_MAX - (nv->namelen + nv->valuelen))
    {
        return sl_status_from_code(SL_STATUS_OVERFLOW);
    }

    next_bytes = *inout_bytes + nv->namelen + nv->valuelen;
    status = sl_str_copy_view_to_arena(arena, sl_str_from_parts((const char*)nv->name, nv->namelen),
                                       &name);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_str_copy_view_to_arena(
        arena, sl_str_from_parts((const char*)nv->value, nv->valuelen), &value);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    out_field->name = name;
    out_field->value = value;
    out_field->sensitive = (nv->flags & NGHTTP2_NV_FLAG_NO_INDEX) != 0U;
    *inout_bytes = next_bytes;
    return sl_status_ok();
}

static SlStatus sl_http2_hpack_prepare_nghttp2_headers(SlArena* arena,
                                                       const SlHttp2HeaderList* headers,
                                                       nghttp2_nv** out_nva)
{
    SlStatus status = sl_status_ok();
    void* storage = NULL;
    nghttp2_nv* nva = NULL;
    size_t index = 0U;
    size_t allocation_size = 0U;

    if (arena == NULL || headers == NULL || out_nva == NULL ||
        (headers->count != 0U && headers->fields == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (headers->count == 0U) {
        *out_nva = NULL;
        return sl_status_ok();
    }

    status = sl_checked_array_size(headers->count, sizeof(nghttp2_nv), &allocation_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, allocation_size, _Alignof(nghttp2_nv), &storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    nva = (nghttp2_nv*)storage;
    for (index = 0U; index < headers->count; index += 1U) {
        const SlHttp2HeaderField* field = &headers->fields[index];
        if (!sl_http2_hpack_valid_str(field->name) || !sl_http2_hpack_valid_str(field->value)) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        nva[index].name = (uint8_t*)field->name.ptr;
        nva[index].value = (uint8_t*)field->value.ptr;
        nva[index].namelen = field->name.length;
        nva[index].valuelen = field->value.length;
        nva[index].flags = field->sensitive ? NGHTTP2_NV_FLAG_NO_INDEX : NGHTTP2_NV_FLAG_NONE;
    }

    *out_nva = nva;
    return sl_status_ok();
}

SlStatus sl_http2_hpack_decoder_init(SlHttp2HpackDecoder* decoder, size_t max_headers,
                                     size_t max_header_list_bytes)
{
    nghttp2_hd_inflater* inflater = NULL;
    int rv = 0;

    if (decoder == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    rv = nghttp2_hd_inflate_new(&inflater);
    if (rv != 0) {
        return sl_http2_hpack_status_from_nghttp2(rv);
    }

    *decoder = (SlHttp2HpackDecoder){0};
    decoder->opaque[SL_HTTP2_HPACK_DECODER_INFLATER_SLOT] = (uintptr_t)inflater;
    decoder->opaque[SL_HTTP2_HPACK_DECODER_MAX_HEADERS_SLOT] =
        max_headers == 0U ? SL_HTTP2_HPACK_DEFAULT_MAX_HEADERS : max_headers;
    decoder->opaque[SL_HTTP2_HPACK_DECODER_MAX_HEADER_LIST_BYTES_SLOT] =
        max_header_list_bytes == 0U ? SL_HTTP2_HPACK_DEFAULT_MAX_HEADER_LIST_BYTES
                                    : max_header_list_bytes;
    return sl_status_ok();
}

void sl_http2_hpack_decoder_dispose(SlHttp2HpackDecoder* decoder)
{
    if (decoder == NULL) {
        return;
    }
    if (sl_http2_hpack_decoder_inflater(decoder) != NULL) {
        nghttp2_hd_inflate_del(sl_http2_hpack_decoder_inflater(decoder));
    }
    *decoder = (SlHttp2HpackDecoder){0};
}

SlStatus sl_http2_hpack_decoder_set_table_size(SlHttp2HpackDecoder* decoder, size_t table_size)
{
    int rv = 0;

    if (decoder == NULL || sl_http2_hpack_decoder_inflater(decoder) == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    rv = nghttp2_hd_inflate_change_table_size(sl_http2_hpack_decoder_inflater(decoder), table_size);
    return sl_http2_hpack_status_from_nghttp2(rv);
}

size_t sl_http2_hpack_decoder_dynamic_table_size(const SlHttp2HpackDecoder* decoder)
{
    if (decoder == NULL || sl_http2_hpack_decoder_inflater(decoder) == NULL) {
        return 0U;
    }
    return nghttp2_hd_inflate_get_dynamic_table_size(sl_http2_hpack_decoder_inflater(decoder));
}

size_t sl_http2_hpack_decoder_max_dynamic_table_size(const SlHttp2HpackDecoder* decoder)
{
    if (decoder == NULL || sl_http2_hpack_decoder_inflater(decoder) == NULL) {
        return 0U;
    }
    return nghttp2_hd_inflate_get_max_dynamic_table_size(sl_http2_hpack_decoder_inflater(decoder));
}

SlStatus sl_http2_hpack_decode(SlHttp2HpackDecoder* decoder, SlArena* arena, SlBytes header_block,
                               bool final_fragment, SlHttp2HeaderList* out_headers)
{
    SlArenaMark mark = {0};
    SlStatus status = sl_status_ok();
    nghttp2_hd_inflater* inflater = NULL;
    SlHttp2HeaderField* fields = NULL;
    void* fields_storage = NULL;
    const uint8_t* cursor = NULL;
    size_t remaining = 0U;
    size_t count = 0U;
    size_t total_bytes = 0U;
    size_t allocation_size = 0U;
    bool final_seen = false;

    if (decoder == NULL || sl_http2_hpack_decoder_inflater(decoder) == NULL || arena == NULL ||
        out_headers == NULL || !sl_http2_hpack_valid_bytes(header_block))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    mark = sl_arena_mark(arena);
    status = sl_checked_array_size(sl_http2_hpack_decoder_max_headers(decoder),
                                   sizeof(SlHttp2HeaderField), &allocation_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, allocation_size, _Alignof(SlHttp2HeaderField), &fields_storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    fields = (SlHttp2HeaderField*)fields_storage;
    inflater = sl_http2_hpack_decoder_inflater(decoder);
    cursor = header_block.ptr;
    remaining = header_block.length;

    for (;;) {
        nghttp2_nv nv = {0};
        int inflate_flags = 0;
        nghttp2_ssize rv = nghttp2_hd_inflate_hd3(inflater, &nv, &inflate_flags, cursor, remaining,
                                                  final_fragment ? 1 : 0);

        if (rv < 0) {
            sl_arena_reset_to(arena, mark);
            return sl_http2_hpack_status_from_nghttp2_ssize(rv);
        }
        cursor += (size_t)rv;
        remaining -= (size_t)rv;

        if ((inflate_flags & NGHTTP2_HD_INFLATE_EMIT) != 0) {
            if (count >= sl_http2_hpack_decoder_max_headers(decoder)) {
                sl_arena_reset_to(arena, mark);
                return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
            }
            status = sl_http2_hpack_copy_nv(arena, &nv, &fields[count], &total_bytes);
            if (!sl_status_is_ok(status)) {
                sl_arena_reset_to(arena, mark);
                return status;
            }
            if (total_bytes > sl_http2_hpack_decoder_max_header_list_bytes(decoder)) {
                sl_arena_reset_to(arena, mark);
                return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
            }
            count += 1U;
        }

        if ((inflate_flags & NGHTTP2_HD_INFLATE_FINAL) != 0) {
            nghttp2_hd_inflate_end_headers(inflater);
            final_seen = true;
            break;
        }
        if ((inflate_flags & NGHTTP2_HD_INFLATE_EMIT) == 0 && remaining == 0U) {
            break;
        }
    }

    if (final_fragment && !final_seen) {
        sl_arena_reset_to(arena, mark);
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_headers = (SlHttp2HeaderList){.fields = fields, .count = count};
    return sl_status_ok();
}

SlStatus sl_http2_hpack_encoder_init(SlHttp2HpackEncoder* encoder, size_t max_dynamic_table_size,
                                     size_t max_output_bytes)
{
    nghttp2_hd_deflater* deflater = NULL;
    int rv = 0;

    if (encoder == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    rv = nghttp2_hd_deflate_new(&deflater, max_dynamic_table_size == 0U
                                               ? SL_HTTP2_HPACK_DEFAULT_DYNAMIC_TABLE_SIZE
                                               : max_dynamic_table_size);
    if (rv != 0) {
        return sl_http2_hpack_status_from_nghttp2(rv);
    }

    *encoder = (SlHttp2HpackEncoder){0};
    encoder->opaque[SL_HTTP2_HPACK_ENCODER_DEFLATER_SLOT] = (uintptr_t)deflater;
    encoder->opaque[SL_HTTP2_HPACK_ENCODER_MAX_OUTPUT_BYTES_SLOT] =
        max_output_bytes == 0U ? SL_HTTP2_HPACK_DEFAULT_MAX_HEADER_LIST_BYTES : max_output_bytes;
    return sl_status_ok();
}

void sl_http2_hpack_encoder_dispose(SlHttp2HpackEncoder* encoder)
{
    if (encoder == NULL) {
        return;
    }
    if (sl_http2_hpack_encoder_deflater(encoder) != NULL) {
        nghttp2_hd_deflate_del(sl_http2_hpack_encoder_deflater(encoder));
    }
    *encoder = (SlHttp2HpackEncoder){0};
}

SlStatus sl_http2_hpack_encoder_set_peer_table_size(SlHttp2HpackEncoder* encoder, size_t table_size)
{
    int rv = 0;

    if (encoder == NULL || sl_http2_hpack_encoder_deflater(encoder) == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    rv = nghttp2_hd_deflate_change_table_size(sl_http2_hpack_encoder_deflater(encoder), table_size);
    return sl_http2_hpack_status_from_nghttp2(rv);
}

SlStatus sl_http2_hpack_encode(SlHttp2HpackEncoder* encoder, SlArena* arena,
                               const SlHttp2HeaderList* headers, SlBytes* out_header_block)
{
    SlArenaMark mark = {0};
    SlStatus status = sl_status_ok();
    nghttp2_nv* nva = NULL;
    size_t bound = 0U;
    void* storage = NULL;
    nghttp2_ssize written = 0;

    if (encoder == NULL || sl_http2_hpack_encoder_deflater(encoder) == NULL || arena == NULL ||
        headers == NULL || out_header_block == NULL)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    mark = sl_arena_mark(arena);
    status = sl_http2_hpack_prepare_nghttp2_headers(arena, headers, &nva);
    if (!sl_status_is_ok(status)) {
        sl_arena_reset_to(arena, mark);
        return status;
    }

    bound = nghttp2_hd_deflate_bound(sl_http2_hpack_encoder_deflater(encoder), nva, headers->count);
    if (bound > sl_http2_hpack_encoder_max_output_bytes(encoder)) {
        sl_arena_reset_to(arena, mark);
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }
    if (bound == 0U) {
        *out_header_block = sl_bytes_empty();
        return sl_status_ok();
    }

    status = sl_arena_alloc(arena, bound, 1U, &storage);
    if (!sl_status_is_ok(status)) {
        sl_arena_reset_to(arena, mark);
        return status;
    }

    written = nghttp2_hd_deflate_hd2(sl_http2_hpack_encoder_deflater(encoder), storage, bound, nva,
                                     headers->count);
    if (written < 0) {
        sl_arena_reset_to(arena, mark);
        return sl_http2_hpack_status_from_nghttp2_ssize(written);
    }

    *out_header_block = sl_bytes_from_parts(storage, (size_t)written);
    return sl_status_ok();
}
