#ifndef SLOPPY_CRYPTO_PLATFORM_H
#define SLOPPY_CRYPTO_PLATFORM_H

#include "sloppy/crypto.h"

SlStatus sl_platform_crypto_random_bytes(SlOwnedBytes out);
SlStatus sl_platform_crypto_hash(SlCryptoHashAlgorithm algorithm, SlBytes data, SlOwnedBytes out);
SlStatus sl_platform_crypto_hmac(SlCryptoHashAlgorithm algorithm, SlBytes key, SlBytes data,
                                 SlOwnedBytes out);

#endif
