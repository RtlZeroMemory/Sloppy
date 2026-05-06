#ifndef SLOPPY_CRYPTO_H
#define SLOPPY_CRYPTO_H

#include "sloppy/bytes.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_CRYPTO_SHA256_SIZE 32U
#define SL_CRYPTO_SHA384_SIZE 48U
#define SL_CRYPTO_SHA512_SIZE 64U
#define SL_CRYPTO_UUID_V4_TEXT_LENGTH 36U

typedef enum SlCryptoHashAlgorithm
{
    SL_CRYPTO_HASH_SHA256 = 0,
    SL_CRYPTO_HASH_SHA384 = 1,
    SL_CRYPTO_HASH_SHA512 = 2
} SlCryptoHashAlgorithm;

typedef struct SlCryptoSecret
{
    unsigned char* ptr;
    size_t length;
    bool disposed;
} SlCryptoSecret;

size_t sl_crypto_hash_digest_size(SlCryptoHashAlgorithm algorithm);
SlStatus sl_crypto_hash_algorithm_from_str(SlStr name, SlCryptoHashAlgorithm* out);

SlStatus sl_crypto_random_bytes(SlOwnedBytes out);
SlStatus sl_crypto_random_uuid_v4(char* out, size_t out_length);
SlStatus sl_crypto_random_hex(size_t byte_length, char* out, size_t out_length);
SlStatus sl_crypto_random_token(size_t length, char* out, size_t out_length);
SlStatus sl_crypto_random_numeric_code(size_t length, char* out, size_t out_length);

SlStatus sl_crypto_hash(SlCryptoHashAlgorithm algorithm, SlBytes data, SlOwnedBytes out);
SlStatus sl_crypto_hmac(SlCryptoHashAlgorithm algorithm, SlBytes key, SlBytes data,
                        SlOwnedBytes out);
SlStatus sl_crypto_hmac_verify(SlCryptoHashAlgorithm algorithm, SlBytes key, SlBytes data,
                               SlBytes signature, bool* out_equal);

SlStatus sl_crypto_hex_encode(SlBytes data, char* out, size_t out_length);
SlStatus sl_crypto_base64_encode(SlBytes data, char* out, size_t out_length, size_t* out_written);

SlStatus sl_crypto_constant_time_equals(SlBytes left, SlBytes right, bool* out_equal);
SlStatus sl_crypto_secret_init(SlCryptoSecret* secret, SlOwnedBytes storage);
SlStatus sl_crypto_secret_from_bytes(SlCryptoSecret* secret, SlOwnedBytes storage, SlBytes src);
SlStatus sl_crypto_secret_dispose(SlCryptoSecret* secret);
SlBytes sl_crypto_secret_bytes(const SlCryptoSecret* secret);

#ifdef __cplusplus
}
#endif

#endif
