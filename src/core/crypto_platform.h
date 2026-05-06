#ifndef SLOPPY_CRYPTO_PLATFORM_H
#define SLOPPY_CRYPTO_PLATFORM_H

#include "sloppy/crypto.h"

SlStatus sl_platform_crypto_random_bytes(SlOwnedBytes out);
SlStatus sl_platform_crypto_hash(SlCryptoHashAlgorithm algorithm, SlBytes data, SlOwnedBytes out);
SlStatus sl_platform_crypto_hmac(SlCryptoHashAlgorithm algorithm, SlBytes key, SlBytes data,
                                 SlOwnedBytes out);
SlStatus sl_platform_crypto_password_hash(SlBytes password, SlCryptoPasswordOptions options,
                                          char* out, size_t out_length, size_t* out_written);
SlStatus sl_platform_crypto_password_verify(SlBytes password, SlStr encoded_hash,
                                            bool* out_verified);
SlStatus sl_platform_crypto_password_needs_rehash(SlStr encoded_hash,
                                                  SlCryptoPasswordOptions options,
                                                  bool* out_needs_rehash);

#endif
