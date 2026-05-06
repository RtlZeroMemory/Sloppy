#include "sloppy/crypto.h"

#include <xxhash.h>

static bool sl_crypto_noncrypto_bytes_valid(SlBytes bytes)
{
    return bytes.ptr != NULL || bytes.length == 0U;
}

SlStatus sl_crypto_noncrypto_xxhash64(SlBytes data, uint64_t* out_hash)
{
    if (out_hash == NULL || !sl_crypto_noncrypto_bytes_valid(data)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_hash = (uint64_t)XXH64(data.ptr, data.length, 0U);
    return sl_status_ok();
}

SlStatus sl_crypto_noncrypto_xxhash64_hex(SlBytes data, char* out, size_t out_length)
{
    static const char digits[] = "0123456789abcdef";
    uint64_t hash = 0U;
    SlStatus status;
    size_t index = 0U;

    if (out == NULL || out_length < SL_CRYPTO_XXHASH64_HEX_LENGTH ||
        !sl_crypto_noncrypto_bytes_valid(data))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_crypto_noncrypto_xxhash64(data, &hash);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < SL_CRYPTO_XXHASH64_HEX_LENGTH; index += 1U) {
        const unsigned shift = (unsigned)((SL_CRYPTO_XXHASH64_HEX_LENGTH - 1U - index) * 4U);
        out[index] = digits[(hash >> shift) & 0x0FU];
    }
    return sl_status_ok();
}
