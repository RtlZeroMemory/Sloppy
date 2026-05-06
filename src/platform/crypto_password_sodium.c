/*
 * src/platform/crypto_password_sodium.c
 *
 * Vetted password-hashing backend. Uses libsodium Argon2id PHC strings; Sloppy owns only
 * the API shape, option bounds, diagnostics mapping, and lifecycle behavior.
 */
#include "../core/crypto_platform.h"

#include <sodium.h>

static SlStatus sl_sodium_status(void)
{
    return sodium_init() < 0 ? sl_status_from_code(SL_STATUS_UNSUPPORTED) : sl_status_ok();
}

static SlStatus sl_sodium_copy_encoded_hash(SlStr encoded_hash,
                                            char out[SL_CRYPTO_PASSWORD_HASH_ENCODED_MAX])
{
    if (encoded_hash.ptr == NULL || encoded_hash.length == 0U ||
        encoded_hash.length >= SL_CRYPTO_PASSWORD_HASH_ENCODED_MAX)
    {
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    }

    for (size_t index = 0U; index < encoded_hash.length; index += 1U) {
        out[index] = encoded_hash.ptr[index];
    }
    out[encoded_hash.length] = '\0';
    return sl_status_ok();
}

static size_t sl_sodium_encoded_length(const char out[SL_CRYPTO_PASSWORD_HASH_ENCODED_MAX])
{
    size_t index = 0U;

    while (index < SL_CRYPTO_PASSWORD_HASH_ENCODED_MAX && out[index] != '\0') {
        index += 1U;
    }
    return index;
}

SlStatus sl_platform_crypto_password_hash(SlBytes password, SlCryptoPasswordOptions options,
                                          char* out, size_t out_length, size_t* out_written)
{
    SlStatus status = sl_sodium_status();
    size_t written = 0U;

    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (out == NULL || out_written == NULL || out_length < SL_CRYPTO_PASSWORD_HASH_ENCODED_MAX) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (crypto_pwhash_str_alg(out, (const char*)password.ptr, (unsigned long long)password.length,
                              (unsigned long long)options.ops_limit, (size_t)options.mem_limit,
                              crypto_pwhash_ALG_ARGON2ID13) != 0)
    {
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    }

    written = sl_sodium_encoded_length(out);
    if (written == SL_CRYPTO_PASSWORD_HASH_ENCODED_MAX) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    *out_written = written;
    return sl_status_ok();
}

SlStatus sl_platform_crypto_password_verify(SlBytes password, SlStr encoded_hash,
                                            bool* out_verified)
{
    char encoded[SL_CRYPTO_PASSWORD_HASH_ENCODED_MAX] = {0};
    SlCryptoPasswordOptions options = sl_crypto_password_default_options();
    SlStatus status = sl_sodium_status();
    int result = 0;
    int rehash_result = 0;

    if (out_verified == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_verified = false;
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_sodium_copy_encoded_hash(encoded_hash, encoded);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    result = crypto_pwhash_str_verify(encoded, (const char*)password.ptr,
                                      (unsigned long long)password.length);
    if (result == 0) {
        *out_verified = true;
        sodium_memzero(encoded, sizeof(encoded));
        return sl_status_ok();
    }
    rehash_result = crypto_pwhash_str_needs_rehash(encoded, (unsigned long long)options.ops_limit,
                                                   (size_t)options.mem_limit);
    sodium_memzero(encoded, sizeof(encoded));
    if (rehash_result < 0) {
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    }
    return sl_status_ok();
}

SlStatus sl_platform_crypto_password_needs_rehash(SlStr encoded_hash,
                                                  SlCryptoPasswordOptions options,
                                                  bool* out_needs_rehash)
{
    char encoded[SL_CRYPTO_PASSWORD_HASH_ENCODED_MAX] = {0};
    SlStatus status = sl_sodium_status();
    int result = 0;

    if (out_needs_rehash == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_needs_rehash = false;
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_sodium_copy_encoded_hash(encoded_hash, encoded);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    result = crypto_pwhash_str_needs_rehash(encoded, (unsigned long long)options.ops_limit,
                                            (size_t)options.mem_limit);
    sodium_memzero(encoded, sizeof(encoded));
    if (result < 0) {
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    }
    *out_needs_rehash = result != 0;
    return sl_status_ok();
}
