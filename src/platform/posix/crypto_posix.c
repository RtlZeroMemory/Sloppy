/*
 * src/platform/posix/crypto_posix.c
 *
 * POSIX crypto backend. Linux random uses getrandom; Apple random uses Security.framework.
 * SHA-2 and HMAC use OpenSSL EVP/HMAC as the vetted dependency backend selected for
 * platforms without a single OS hash API.
 */
#include "../../core/crypto_platform.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>

#if defined(__APPLE__)
#include <Security/Security.h>
#elif defined(__linux__)
#include <errno.h>
#include <sys/random.h>
#endif

#include <limits.h>
#include <stddef.h>

static const EVP_MD* sl_posix_crypto_evp_md(SlCryptoHashAlgorithm algorithm)
{
    switch (algorithm) {
    case SL_CRYPTO_HASH_SHA256:
        return EVP_sha256();
    case SL_CRYPTO_HASH_SHA384:
        return EVP_sha384();
    case SL_CRYPTO_HASH_SHA512:
        return EVP_sha512();
    default:
        return NULL;
    }
}

SlStatus sl_platform_crypto_random_bytes(SlOwnedBytes out)
{
#if defined(__APPLE__)
    return SecRandomCopyBytes(kSecRandomDefault, out.length, out.ptr) == errSecSuccess
               ? sl_status_ok()
               : sl_status_from_code(SL_STATUS_INTERNAL);
#elif defined(__linux__)
    size_t offset = 0U;

    while (offset < out.length) {
        ssize_t result = getrandom(out.ptr + offset, out.length - offset, 0U);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        if (result == 0) {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        offset += (size_t)result;
    }
    return sl_status_ok();
#else
    (void)out;
    return sl_status_from_code(SL_STATUS_UNSUPPORTED);
#endif
}

SlStatus sl_platform_crypto_hash(SlCryptoHashAlgorithm algorithm, SlBytes data, SlOwnedBytes out)
{
    const EVP_MD* md = sl_posix_crypto_evp_md(algorithm);
    EVP_MD_CTX* context = NULL;
    unsigned int written = 0U;
    SlStatus status = sl_status_ok();

    if (md == NULL || data.length > INT_MAX || out.length > UINT_MAX) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    context = EVP_MD_CTX_new();
    if (context == NULL) {
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }
    if (EVP_DigestInit_ex(context, md, NULL) != 1 ||
        (data.length != 0U && EVP_DigestUpdate(context, data.ptr, data.length) != 1) ||
        EVP_DigestFinal_ex(context, out.ptr, &written) != 1 || written != out.length)
    {
        status = sl_status_from_code(SL_STATUS_INTERNAL);
    }
    EVP_MD_CTX_free(context);
    return status;
}

SlStatus sl_platform_crypto_hmac(SlCryptoHashAlgorithm algorithm, SlBytes key, SlBytes data,
                                 SlOwnedBytes out)
{
    const EVP_MD* md = sl_posix_crypto_evp_md(algorithm);
    unsigned int written = 0U;

    if (md == NULL || key.length > INT_MAX || data.length > INT_MAX || out.length > UINT_MAX) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (HMAC(md, key.ptr, (int)key.length, data.ptr, data.length, out.ptr, &written) == NULL ||
        written != out.length)
    {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    return sl_status_ok();
}
