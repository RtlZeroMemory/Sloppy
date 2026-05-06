/*
 * src/platform/win32/crypto_win32.c
 *
 * Windows crypto backend. Uses CNG for random, SHA-2, and HMAC. No weak random fallback is
 * provided; backend failures propagate as stable SlStatus failures to the core layer.
 */
#include "../../core/crypto_platform.h"

#include <windows.h>
#include <bcrypt.h>

#include <limits.h>
#include <stddef.h>

static SlStatus sl_win32_crypto_status(NTSTATUS status)
{
    return status >= 0 ? sl_status_ok() : sl_status_from_code(SL_STATUS_INTERNAL);
}

static const wchar_t* sl_win32_hash_algorithm(SlCryptoHashAlgorithm algorithm)
{
    switch (algorithm) {
    case SL_CRYPTO_HASH_SHA256:
        return BCRYPT_SHA256_ALGORITHM;
    case SL_CRYPTO_HASH_SHA384:
        return BCRYPT_SHA384_ALGORITHM;
    case SL_CRYPTO_HASH_SHA512:
        return BCRYPT_SHA512_ALGORITHM;
    default:
        return NULL;
    }
}

static SlStatus sl_win32_crypto_ulong_property(BCRYPT_ALG_HANDLE handle, const wchar_t* name,
                                               ULONG* out_value)
{
    ULONG written = 0U;
    NTSTATUS status;

    if (handle == NULL || name == NULL || out_value == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = BCryptGetProperty(handle, name, (PUCHAR)out_value, sizeof(*out_value), &written, 0U);
    if (status < 0 || written != sizeof(*out_value)) {
        return sl_win32_crypto_status(status);
    }
    return sl_status_ok();
}

static SlStatus sl_win32_crypto_hash_internal(SlCryptoHashAlgorithm algorithm, SlBytes key,
                                              SlBytes data, SlOwnedBytes out, bool hmac)
{
    unsigned char object_storage[512] = {0};
    BCRYPT_ALG_HANDLE algorithm_handle = NULL;
    BCRYPT_HASH_HANDLE hash_handle = NULL;
    ULONG object_length = 0U;
    ULONG digest_length = 0U;
    const wchar_t* algorithm_name = sl_win32_hash_algorithm(algorithm);
    SlStatus status = sl_status_ok();
    NTSTATUS ntstatus;

    if (algorithm_name == NULL || data.length > ULONG_MAX || key.length > ULONG_MAX ||
        out.length > ULONG_MAX)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    ntstatus = BCryptOpenAlgorithmProvider(&algorithm_handle, algorithm_name, NULL,
                                           hmac ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0U);
    status = sl_win32_crypto_status(ntstatus);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_win32_crypto_ulong_property(algorithm_handle, BCRYPT_OBJECT_LENGTH, &object_length);
    if (sl_status_is_ok(status)) {
        status =
            sl_win32_crypto_ulong_property(algorithm_handle, BCRYPT_HASH_LENGTH, &digest_length);
    }
    if (sl_status_is_ok(status) &&
        (object_length > sizeof(object_storage) || digest_length != out.length))
    {
        status = sl_status_from_code(SL_STATUS_UNSUPPORTED);
    }
    if (sl_status_is_ok(status)) {
        ntstatus =
            BCryptCreateHash(algorithm_handle, &hash_handle, object_storage, object_length,
                             hmac ? (PUCHAR)key.ptr : NULL, hmac ? (ULONG)key.length : 0U, 0U);
        status = sl_win32_crypto_status(ntstatus);
    }
    if (sl_status_is_ok(status) && data.length != 0U) {
        ntstatus = BCryptHashData(hash_handle, (PUCHAR)data.ptr, (ULONG)data.length, 0U);
        status = sl_win32_crypto_status(ntstatus);
    }
    if (sl_status_is_ok(status)) {
        ntstatus = BCryptFinishHash(hash_handle, out.ptr, (ULONG)out.length, 0U);
        status = sl_win32_crypto_status(ntstatus);
    }

    if (hash_handle != NULL) {
        (void)BCryptDestroyHash(hash_handle);
    }
    if (algorithm_handle != NULL) {
        (void)BCryptCloseAlgorithmProvider(algorithm_handle, 0U);
    }
    SecureZeroMemory(object_storage, sizeof(object_storage));
    return status;
}

SlStatus sl_platform_crypto_random_bytes(SlOwnedBytes out)
{
    if (out.length > ULONG_MAX) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_win32_crypto_status(
        BCryptGenRandom(NULL, out.ptr, (ULONG)out.length, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

SlStatus sl_platform_crypto_hash(SlCryptoHashAlgorithm algorithm, SlBytes data, SlOwnedBytes out)
{
    return sl_win32_crypto_hash_internal(algorithm, sl_bytes_empty(), data, out, false);
}

SlStatus sl_platform_crypto_hmac(SlCryptoHashAlgorithm algorithm, SlBytes key, SlBytes data,
                                 SlOwnedBytes out)
{
    return sl_win32_crypto_hash_internal(algorithm, key, data, out, true);
}
