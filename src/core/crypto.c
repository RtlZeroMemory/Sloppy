/*
 * src/core/crypto.c
 *
 * Implements Sloppy-owned crypto API shape and lifecycle helpers. Primitive operations are
 * delegated to platform/dependency backends; this file only validates buffers, encodes
 * bytes, formats UUIDs/tokens, and applies constant-time/zeroization ownership rules.
 *
 * Tests: tests/unit/core/test_crypto.c.
 */
#include "sloppy/crypto.h"

#include "crypto_platform.h"
#include "sloppy/checked_math.h"

static bool sl_crypto_bytes_valid(SlBytes bytes)
{
    return bytes.length == 0U || bytes.ptr != NULL;
}

static bool sl_crypto_output_valid(SlOwnedBytes out)
{
    return out.length == 0U || out.ptr != NULL;
}

static void sl_crypto_secure_zero(unsigned char* ptr, size_t length)
{
    volatile unsigned char* cursor = ptr;
    size_t index = 0U;

    if (ptr == NULL) {
        return;
    }

    for (index = 0U; index < length; index += 1U) {
        cursor[index] = 0U;
    }
}

static SlStatus sl_crypto_random_index(size_t alphabet_length, size_t* out_index)
{
    const size_t bucket_count = 256U / alphabet_length;
    const size_t limit = bucket_count * alphabet_length;
    unsigned char byte = 0U;
    SlOwnedBytes random_byte = {&byte, 1U};
    SlStatus status;

    if (out_index == NULL || alphabet_length == 0U || alphabet_length > 256U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    do {
        status = sl_crypto_random_bytes(random_byte);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    } while ((size_t)byte >= limit);

    *out_index = (size_t)(byte % alphabet_length);
    return sl_status_ok();
}

size_t sl_crypto_hash_digest_size(SlCryptoHashAlgorithm algorithm)
{
    switch (algorithm) {
    case SL_CRYPTO_HASH_SHA256:
        return SL_CRYPTO_SHA256_SIZE;
    case SL_CRYPTO_HASH_SHA384:
        return SL_CRYPTO_SHA384_SIZE;
    case SL_CRYPTO_HASH_SHA512:
        return SL_CRYPTO_SHA512_SIZE;
    default:
        return 0U;
    }
}

SlStatus sl_crypto_hash_algorithm_from_str(SlStr name, SlCryptoHashAlgorithm* out)
{
    if (out == NULL ||
        !sl_crypto_bytes_valid(sl_bytes_from_parts((const unsigned char*)name.ptr, name.length)))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (sl_str_equal(name, sl_str_from_cstr("sha256"))) {
        *out = SL_CRYPTO_HASH_SHA256;
        return sl_status_ok();
    }
    if (sl_str_equal(name, sl_str_from_cstr("sha384"))) {
        *out = SL_CRYPTO_HASH_SHA384;
        return sl_status_ok();
    }
    if (sl_str_equal(name, sl_str_from_cstr("sha512"))) {
        *out = SL_CRYPTO_HASH_SHA512;
        return sl_status_ok();
    }
    return sl_status_from_code(SL_STATUS_UNSUPPORTED);
}

SlStatus sl_crypto_random_bytes(SlOwnedBytes out)
{
    if (!sl_crypto_output_valid(out)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (out.length == 0U) {
        return sl_status_ok();
    }
    return sl_platform_crypto_random_bytes(out);
}

SlStatus sl_crypto_hex_encode(SlBytes data, char* out, size_t out_length)
{
    static const char alphabet[] = "0123456789abcdef";
    size_t required = 0U;
    size_t index = 0U;
    SlStatus status;

    if (out == NULL || !sl_crypto_bytes_valid(data)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_checked_mul_size(data.length, 2U, &required);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (out_length < required) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    for (index = 0U; index < data.length; index += 1U) {
        out[index * 2U] = alphabet[(data.ptr[index] >> 4U) & 0x0FU];
        out[(index * 2U) + 1U] = alphabet[data.ptr[index] & 0x0FU];
    }
    return sl_status_ok();
}

SlStatus sl_crypto_base64_encode(SlBytes data, char* out, size_t out_length, size_t* out_written)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t required = 0U;
    size_t input = 0U;
    size_t output = 0U;
    SlStatus status;

    if (out == NULL || out_written == NULL || !sl_crypto_bytes_valid(data)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_checked_add_size(data.length, 2U, &required);
    if (sl_status_is_ok(status)) {
        required = (required / 3U) * 4U;
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (out_length < required) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    while (input < data.length) {
        const size_t remaining = data.length - input;
        const uint32_t a = data.ptr[input++];
        const uint32_t b = remaining > 1U ? data.ptr[input++] : 0U;
        const uint32_t c = remaining > 2U ? data.ptr[input++] : 0U;
        const uint32_t triple = (a << 16U) | (b << 8U) | c;

        out[output++] = alphabet[(triple >> 18U) & 0x3FU];
        out[output++] = alphabet[(triple >> 12U) & 0x3FU];
        if (remaining > 1U) {
            out[output++] = alphabet[(triple >> 6U) & 0x3FU];
        }
        else {
            out[output++] = '=';
        }
        if (remaining > 2U) {
            out[output++] = alphabet[triple & 0x3FU];
        }
        else {
            out[output++] = '=';
        }
    }

    *out_written = output;
    return sl_status_ok();
}

SlStatus sl_crypto_random_uuid_v4(char* out, size_t out_length)
{
    unsigned char bytes[16] = {0};
    SlStatus status;

    if (out == NULL || out_length < SL_CRYPTO_UUID_V4_TEXT_LENGTH) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_crypto_random_bytes((SlOwnedBytes){bytes, sizeof(bytes)});
    if (!sl_status_is_ok(status)) {
        return status;
    }

    bytes[6] = (unsigned char)((bytes[6] & 0x0FU) | 0x40U);
    bytes[8] = (unsigned char)((bytes[8] & 0x3FU) | 0x80U);

    status = sl_crypto_hex_encode(sl_bytes_from_parts(bytes, 4U), out, 8U);
    if (sl_status_is_ok(status)) {
        out[8] = '-';
        status = sl_crypto_hex_encode(sl_bytes_from_parts(bytes + 4U, 2U), out + 9U, 4U);
    }
    if (sl_status_is_ok(status)) {
        out[13] = '-';
        status = sl_crypto_hex_encode(sl_bytes_from_parts(bytes + 6U, 2U), out + 14U, 4U);
    }
    if (sl_status_is_ok(status)) {
        out[18] = '-';
        status = sl_crypto_hex_encode(sl_bytes_from_parts(bytes + 8U, 2U), out + 19U, 4U);
    }
    if (sl_status_is_ok(status)) {
        out[23] = '-';
        status = sl_crypto_hex_encode(sl_bytes_from_parts(bytes + 10U, 6U), out + 24U, 12U);
    }
    sl_crypto_secure_zero(bytes, sizeof(bytes));
    return status;
}

SlStatus sl_crypto_random_hex(size_t byte_length, char* out, size_t out_length)
{
    unsigned char stack[1024];
    size_t required = 0U;
    SlStatus status;

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_checked_mul_size(byte_length, 2U, &required);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (out_length < required || byte_length > sizeof(stack)) {
        return byte_length > sizeof(stack) ? sl_status_from_code(SL_STATUS_UNSUPPORTED)
                                           : sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    status = sl_crypto_random_bytes((SlOwnedBytes){stack, byte_length});
    if (sl_status_is_ok(status)) {
        status = sl_crypto_hex_encode(sl_bytes_from_parts(stack, byte_length), out, out_length);
    }
    sl_crypto_secure_zero(stack, sizeof(stack));
    return status;
}

SlStatus sl_crypto_random_token(size_t length, char* out, size_t out_length)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t index = 0U;

    if (out == NULL || out_length < length) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (index = 0U; index < length; index += 1U) {
        size_t alphabet_index = 0U;
        SlStatus status = sl_crypto_random_index(sizeof(alphabet) - 1U, &alphabet_index);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        out[index] = alphabet[alphabet_index];
    }
    return sl_status_ok();
}

SlStatus sl_crypto_random_numeric_code(size_t length, char* out, size_t out_length)
{
    size_t index = 0U;

    if (out == NULL || out_length < length) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (index = 0U; index < length; index += 1U) {
        size_t digit = 0U;
        SlStatus status = sl_crypto_random_index(10U, &digit);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        out[index] = (char)('0' + digit);
    }
    return sl_status_ok();
}

SlStatus sl_crypto_hash(SlCryptoHashAlgorithm algorithm, SlBytes data, SlOwnedBytes out)
{
    if (!sl_crypto_bytes_valid(data) || !sl_crypto_output_valid(out) ||
        out.length != sl_crypto_hash_digest_size(algorithm))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_platform_crypto_hash(algorithm, data, out);
}

SlStatus sl_crypto_hmac(SlCryptoHashAlgorithm algorithm, SlBytes key, SlBytes data,
                        SlOwnedBytes out)
{
    if (!sl_crypto_bytes_valid(key) || !sl_crypto_bytes_valid(data) ||
        !sl_crypto_output_valid(out) || key.length == 0U ||
        out.length != sl_crypto_hash_digest_size(algorithm))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_platform_crypto_hmac(algorithm, key, data, out);
}

SlStatus sl_crypto_hmac_verify(SlCryptoHashAlgorithm algorithm, SlBytes key, SlBytes data,
                               SlBytes signature, bool* out_equal)
{
    unsigned char digest[SL_CRYPTO_SHA512_SIZE] = {0};
    size_t digest_size = sl_crypto_hash_digest_size(algorithm);
    SlStatus status;

    if (out_equal == NULL || digest_size == 0U || !sl_crypto_bytes_valid(signature)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (signature.length != digest_size) {
        *out_equal = false;
        return sl_status_ok();
    }

    status = sl_crypto_hmac(algorithm, key, data, (SlOwnedBytes){digest, digest_size});
    if (sl_status_is_ok(status)) {
        status = sl_crypto_constant_time_equals(sl_bytes_from_parts(digest, digest_size), signature,
                                                out_equal);
    }
    sl_crypto_secure_zero(digest, sizeof(digest));
    return status;
}

SlStatus sl_crypto_constant_time_equals(SlBytes left, SlBytes right, bool* out_equal)
{
    size_t index = 0U;
    unsigned char diff = 0U;

    if (out_equal == NULL || !sl_crypto_bytes_valid(left) || !sl_crypto_bytes_valid(right)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (left.length != right.length) {
        *out_equal = false;
        return sl_status_ok();
    }

    for (index = 0U; index < left.length; index += 1U) {
        diff = (unsigned char)(diff | (left.ptr[index] ^ right.ptr[index]));
    }
    *out_equal = diff == 0U;
    return sl_status_ok();
}

SlStatus sl_crypto_secret_init(SlCryptoSecret* secret, SlOwnedBytes storage)
{
    if (secret == NULL || !sl_crypto_output_valid(storage)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    secret->ptr = storage.ptr;
    secret->length = storage.length;
    secret->disposed = false;
    return sl_status_ok();
}

SlStatus sl_crypto_secret_from_bytes(SlCryptoSecret* secret, SlOwnedBytes storage, SlBytes src)
{
    if (secret == NULL || !sl_crypto_output_valid(storage) || !sl_crypto_bytes_valid(src) ||
        storage.length < src.length)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (src.length != 0U) {
        size_t index = 0U;
        for (index = 0U; index < src.length; index += 1U) {
            storage.ptr[index] = src.ptr[index];
        }
    }
    return sl_crypto_secret_init(secret, (SlOwnedBytes){storage.ptr, src.length});
}

SlStatus sl_crypto_secret_dispose(SlCryptoSecret* secret)
{
    if (secret == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (secret->disposed) {
        return sl_status_ok();
    }
    sl_crypto_secure_zero(secret->ptr, secret->length);
    secret->disposed = true;
    return sl_status_ok();
}

SlBytes sl_crypto_secret_bytes(const SlCryptoSecret* secret)
{
    if (secret == NULL || secret->disposed) {
        return sl_bytes_empty();
    }
    return sl_bytes_from_parts(secret->ptr, secret->length);
}
