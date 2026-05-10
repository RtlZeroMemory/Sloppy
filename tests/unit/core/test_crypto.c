#include "sloppy/crypto.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int expect_text_equal(const char* actual, const char* expected, size_t length)
{
    return expect_true(memcmp(actual, expected, length) == 0);
}

static int digest_hex(SlCryptoHashAlgorithm algorithm, SlBytes data, char* out, size_t out_length)
{
    unsigned char digest[SL_CRYPTO_SHA512_SIZE] = {0};
    size_t digest_size = sl_crypto_hash_digest_size(algorithm);

    if (expect_status(sl_crypto_hash(algorithm, data, (SlOwnedBytes){digest, digest_size}),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (expect_status(
            sl_crypto_hex_encode(sl_bytes_from_parts(digest, digest_size), out, out_length),
            SL_STATUS_OK) != 0)
    {
        return 2;
    }
    return 0;
}

static int test_random_shapes(void)
{
    unsigned char first[32] = {0};
    char uuid[SL_CRYPTO_UUID_V4_TEXT_LENGTH] = {0};
    char token[24] = {0};
    char hex[16] = {0};
    char code[8] = {0};
    size_t index = 0U;

    if (expect_status(sl_crypto_random_bytes((SlOwnedBytes){first, sizeof(first)}), SL_STATUS_OK) !=
        0)
    {
        return 1;
    }

    if (expect_status(sl_crypto_random_uuid_v4(uuid, sizeof(uuid)), SL_STATUS_OK) != 0 ||
        uuid[8] != '-' || uuid[13] != '-' || uuid[18] != '-' || uuid[23] != '-' ||
        uuid[14] != '4' ||
        !(uuid[19] == '8' || uuid[19] == '9' || uuid[19] == 'a' || uuid[19] == 'b'))
    {
        return 2;
    }

    if (expect_status(sl_crypto_random_token(sizeof(token), token, sizeof(token)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_crypto_random_hex(8U, hex, sizeof(hex)), SL_STATUS_OK) != 0 ||
        expect_status(sl_crypto_random_numeric_code(sizeof(code), code, sizeof(code)),
                      SL_STATUS_OK) != 0)
    {
        return 3;
    }

    for (index = 0U; index < sizeof(code); index += 1U) {
        if (code[index] < '0' || code[index] > '9') {
            return 4;
        }
    }
    for (index = 0U; index < sizeof(hex); index += 1U) {
        if (!((hex[index] >= '0' && hex[index] <= '9') || (hex[index] >= 'a' && hex[index] <= 'f')))
        {
            return 5;
        }
    }
    for (index = 0U; index < sizeof(token); index += 1U) {
        if (!((token[index] >= '0' && token[index] <= '9') ||
              (token[index] >= 'A' && token[index] <= 'Z') ||
              (token[index] >= 'a' && token[index] <= 'z') || token[index] == '-' ||
              token[index] == '_'))
        {
            return 6;
        }
    }
    return 0;
}

static int test_hash_vectors(void)
{
    const unsigned char abc[] = {'a', 'b', 'c'};
    char hex[SL_CRYPTO_SHA512_SIZE * 2U] = {0};

    if (digest_hex(SL_CRYPTO_HASH_SHA256, sl_bytes_from_parts(abc, sizeof(abc)), hex,
                   sizeof(hex)) != 0 ||
        expect_text_equal(hex,
                          "ba7816bf8f01cfea414140de5dae2223"
                          "b00361a396177a9cb410ff61f20015ad",
                          (size_t)SL_CRYPTO_SHA256_SIZE * 2U) != 0)
    {
        return 10;
    }
    if (digest_hex(SL_CRYPTO_HASH_SHA384, sl_bytes_from_parts(abc, sizeof(abc)), hex,
                   sizeof(hex)) != 0 ||
        expect_text_equal(hex,
                          "cb00753f45a35e8bb5a03d699ac65007"
                          "272c32ab0eded1631a8b605a43ff5bed"
                          "8086072ba1e7cc2358baeca134c825a7",
                          (size_t)SL_CRYPTO_SHA384_SIZE * 2U) != 0)
    {
        return 11;
    }
    if (digest_hex(SL_CRYPTO_HASH_SHA512, sl_bytes_from_parts(abc, sizeof(abc)), hex,
                   sizeof(hex)) != 0 ||
        expect_text_equal(hex,
                          "ddaf35a193617abacc417349ae204131"
                          "12e6fa4e89a97ea20a9eeee64b55d39a"
                          "2192992a274fc1a836ba3c23a3feebbd"
                          "454d4423643ce80e2a9ac94fa54ca49f",
                          (size_t)SL_CRYPTO_SHA512_SIZE * 2U) != 0)
    {
        return 12;
    }
    return 0;
}

static int test_hmac_and_constant_time(void)
{
    unsigned char key[20] = {0};
    const unsigned char data[] = {'H', 'i', ' ', 'T', 'h', 'e', 'r', 'e'};
    unsigned char digest[SL_CRYPTO_SHA256_SIZE] = {0};
    char hex[SL_CRYPTO_SHA256_SIZE * 2U] = {0};
    bool equal = false;
    size_t index = 0U;

    for (index = 0U; index < sizeof(key); index += 1U) {
        key[index] = 0x0bU;
    }

    if (expect_status(sl_crypto_hmac(SL_CRYPTO_HASH_SHA256, sl_bytes_from_parts(key, sizeof(key)),
                                     sl_bytes_from_parts(data, sizeof(data)),
                                     (SlOwnedBytes){digest, sizeof(digest)}),
                      SL_STATUS_OK) != 0 ||
        expect_status(
            sl_crypto_hex_encode(sl_bytes_from_parts(digest, sizeof(digest)), hex, sizeof(hex)),
            SL_STATUS_OK) != 0 ||
        expect_text_equal(hex,
                          "b0344c61d8db38535ca8afceaf0bf12b"
                          "881dc200c9833da726e9376c2e32cff7",
                          sizeof(hex)) != 0)
    {
        return 20;
    }

    if (expect_status(sl_crypto_hmac_verify(SL_CRYPTO_HASH_SHA256,
                                            sl_bytes_from_parts(key, sizeof(key)),
                                            sl_bytes_from_parts(data, sizeof(data)),
                                            sl_bytes_from_parts(digest, sizeof(digest)), &equal),
                      SL_STATUS_OK) != 0 ||
        expect_true(equal) != 0)
    {
        return 21;
    }
    digest[0] ^= 0x01U;
    if (expect_status(sl_crypto_hmac_verify(SL_CRYPTO_HASH_SHA256,
                                            sl_bytes_from_parts(key, sizeof(key)),
                                            sl_bytes_from_parts(data, sizeof(data)),
                                            sl_bytes_from_parts(digest, sizeof(digest)), &equal),
                      SL_STATUS_OK) != 0 ||
        expect_true(!equal) != 0)
    {
        return 22;
    }
    if (expect_status(sl_crypto_constant_time_equals(sl_bytes_from_parts(key, 3U),
                                                     sl_bytes_from_parts(key, 4U), &equal),
                      SL_STATUS_OK) != 0 ||
        expect_true(!equal) != 0)
    {
        return 23;
    }
    if (expect_status(
            sl_crypto_hmac_verify(SL_CRYPTO_HASH_SHA256, sl_bytes_from_parts(key, sizeof(key)),
                                  sl_bytes_from_parts(data, sizeof(data)),
                                  sl_bytes_from_parts(digest, sizeof(digest) - 1U), &equal),
            SL_STATUS_OK) != 0 ||
        expect_true(!equal) != 0)
    {
        return 24;
    }
    return 0;
}

static int test_encoding_and_secret_lifecycle(void)
{
    const unsigned char bytes[] = {'h', 'e', 'l', 'l', 'o'};
    unsigned char storage[5] = {0};
    SlCryptoSecret secret = {0};
    char b64[8] = {0};
    size_t written = 0U;

    if (expect_status(sl_crypto_base64_encode(sl_bytes_from_parts(bytes, sizeof(bytes)), b64,
                                              sizeof(b64), &written),
                      SL_STATUS_OK) != 0 ||
        written != sizeof(b64) || expect_text_equal(b64, "aGVsbG8=", sizeof(b64)) != 0)
    {
        return 30;
    }

    if (expect_status(sl_crypto_secret_from_bytes(&secret, (SlOwnedBytes){storage, sizeof(storage)},
                                                  sl_bytes_from_parts(bytes, sizeof(bytes))),
                      SL_STATUS_OK) != 0 ||
        !sl_bytes_equal(sl_crypto_secret_bytes(&secret), sl_bytes_from_parts(bytes, sizeof(bytes))))
    {
        return 31;
    }
    if (expect_status(sl_crypto_secret_dispose(&secret), SL_STATUS_OK) != 0 ||
        expect_status(sl_crypto_secret_dispose(&secret), SL_STATUS_OK) != 0 ||
        !sl_bytes_is_empty(sl_crypto_secret_bytes(&secret)))
    {
        return 32;
    }
    return expect_true(storage[0] == 0U && storage[1] == 0U && storage[2] == 0U &&
                       storage[3] == 0U && storage[4] == 0U);
}

static int test_password_hash_verify_and_rehash(void)
{
    const unsigned char password[] = "correct horse battery staple";
    const unsigned char wrong_password[] = "correct horse battery fail";
    char encoded[SL_CRYPTO_PASSWORD_HASH_ENCODED_MAX] = {0};
    size_t written = 0U;
    bool verified = false;
    bool needs_rehash = true;
    SlCryptoPasswordOptions stronger = sl_crypto_password_default_options();

    if (expect_status(sl_crypto_password_hash(sl_bytes_from_parts(password, sizeof(password) - 1U),
                                              NULL, encoded, sizeof(encoded), &written),
                      SL_STATUS_OK) != 0)
    {
        return 40;
    }
    if (written == 0U || written >= sizeof(encoded) ||
        memcmp(encoded, "$argon2id$", sizeof("$argon2id$") - 1U) != 0)
    {
        return 41;
    }

    if (expect_status(
            sl_crypto_password_verify(sl_bytes_from_parts(password, sizeof(password) - 1U),
                                      sl_str_from_parts(encoded, written), &verified),
            SL_STATUS_OK) != 0 ||
        !verified)
    {
        return 42;
    }

    verified = true;
    if (expect_status(sl_crypto_password_verify(
                          sl_bytes_from_parts(wrong_password, sizeof(wrong_password) - 1U),
                          sl_str_from_parts(encoded, written), &verified),
                      SL_STATUS_OK) != 0 ||
        verified)
    {
        return 43;
    }

    if (expect_status(sl_crypto_password_needs_rehash(sl_str_from_parts(encoded, written), NULL,
                                                      &needs_rehash),
                      SL_STATUS_OK) != 0 ||
        needs_rehash)
    {
        return 44;
    }

    stronger.ops_limit = SL_CRYPTO_PASSWORD_OPSLIMIT_DEFAULT + 1U;
    if (expect_status(sl_crypto_password_needs_rehash(sl_str_from_parts(encoded, written),
                                                      &stronger, &needs_rehash),
                      SL_STATUS_OK) != 0 ||
        !needs_rehash)
    {
        return 45;
    }
    return 0;
}

static int test_password_rejects_unsupported_format(void)
{
    const unsigned char password[] = "password-value-not-in-diagnostic";
    bool verified = true;
    bool needs_rehash = false;

    if (expect_status(
            sl_crypto_password_verify(sl_bytes_from_parts(password, sizeof(password) - 1U),
                                      sl_str_from_cstr("$bcrypt$unsupported"), &verified),
            SL_STATUS_UNSUPPORTED) != 0 ||
        verified)
    {
        return 50;
    }

    if (expect_status(sl_crypto_password_needs_rehash(sl_str_from_cstr("$bcrypt$unsupported"), NULL,
                                                      &needs_rehash),
                      SL_STATUS_UNSUPPORTED) != 0 ||
        needs_rehash)
    {
        return 51;
    }
    verified = true;
    if (expect_status(
            sl_crypto_password_verify(sl_bytes_from_parts(password, sizeof(password) - 1U),
                                      sl_str_from_cstr("$argon2id$malformed"), &verified),
            SL_STATUS_UNSUPPORTED) != 0 ||
        verified)
    {
        return 52;
    }
    return 0;
}

static int test_noncrypto_xxhash64_vectors(void)
{
    static const unsigned char hello[] = {'h', 'e', 'l', 'l', 'o'};
    uint64_t hash = 0U;
    char hex[SL_CRYPTO_XXHASH64_HEX_LENGTH] = {0};

    if (expect_status(sl_crypto_noncrypto_xxhash64(sl_bytes_empty(), &hash), SL_STATUS_OK) != 0 ||
        hash != UINT64_C(0xef46db3751d8e999))
    {
        return 60;
    }

    if (expect_status(
            sl_crypto_noncrypto_xxhash64(sl_bytes_from_parts(hello, sizeof(hello)), &hash),
            SL_STATUS_OK) != 0 ||
        hash != UINT64_C(0x26c7827d889f6da3))
    {
        return 61;
    }

    if (expect_status(sl_crypto_noncrypto_xxhash64_hex(sl_bytes_from_parts(hello, sizeof(hello)),
                                                       hex, sizeof(hex)),
                      SL_STATUS_OK) != 0 ||
        expect_text_equal(hex, "26c7827d889f6da3", sizeof(hex)) != 0)
    {
        return 62;
    }

    if (expect_status(sl_crypto_noncrypto_xxhash64(sl_bytes_from_parts(NULL, 1U), &hash),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_crypto_noncrypto_xxhash64_hex(sl_bytes_empty(), hex, sizeof(hex) - 1U),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 63;
    }

    return 0;
}

static int test_invalid_and_capacity_guards(void)
{
    static const unsigned char password[] = "pw";
    unsigned char digest[SL_CRYPTO_SHA256_SIZE] = {0};
    char encoded[SL_CRYPTO_PASSWORD_HASH_ENCODED_MAX] = {0};
    char token[8] = {0};
    char tiny[3] = {0};
    char huge_hex[(1025U * 2U) + 1U] = {0};
    size_t written = 999U;
    bool equal = true;
    SlCryptoHashAlgorithm algorithm = SL_CRYPTO_HASH_SHA256;
    SlCryptoPasswordOptions bad = sl_crypto_password_default_options();

    bad.ops_limit = SL_CRYPTO_PASSWORD_OPSLIMIT_MIN - 1U;

    if (expect_status(sl_crypto_hash_algorithm_from_str(sl_str_from_parts(NULL, 1U), &algorithm),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_crypto_hash_algorithm_from_str(sl_str_from_cstr("sha3-256"), &algorithm),
                      SL_STATUS_UNSUPPORTED) != 0)
    {
        return 70;
    }

    if (expect_status(sl_crypto_hex_encode(sl_bytes_from_parts((const unsigned char*)"ab", 2U),
                                           tiny, sizeof(tiny)),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        expect_status(sl_crypto_base64_encode(sl_bytes_from_parts((const unsigned char*)"abc", 3U),
                                              tiny, sizeof(tiny), &written),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0)
    {
        return 71;
    }

    if (expect_status(sl_crypto_random_uuid_v4(token, sizeof(token) - 1U),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_crypto_random_hex(2U, tiny, sizeof(tiny)), SL_STATUS_CAPACITY_EXCEEDED) !=
            0 ||
        expect_status(sl_crypto_random_hex(1025U, huge_hex, sizeof(huge_hex)),
                      SL_STATUS_UNSUPPORTED) != 0 ||
        expect_status(sl_crypto_random_token(sizeof(token), token, sizeof(token) - 1U),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_crypto_random_numeric_code(sizeof(token), token, sizeof(token) - 1U),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 72;
    }

    if (expect_status(sl_crypto_hash(SL_CRYPTO_HASH_SHA256, sl_bytes_from_parts(NULL, 1U),
                                     (SlOwnedBytes){digest, sizeof(digest)}),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_crypto_hmac(SL_CRYPTO_HASH_SHA256, sl_bytes_empty(),
                                     sl_bytes_from_parts(password, sizeof(password) - 1U),
                                     (SlOwnedBytes){digest, sizeof(digest)}),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(
            sl_crypto_constant_time_equals(sl_bytes_from_parts(NULL, 1U), sl_bytes_empty(), &equal),
            SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 73;
    }

    if (expect_status(sl_crypto_password_hash(sl_bytes_from_parts(password, sizeof(password) - 1U),
                                              NULL, encoded, sizeof(encoded) - 1U, &written),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        expect_status(sl_crypto_password_hash(sl_bytes_from_parts(password, sizeof(password) - 1U),
                                              &bad, encoded, sizeof(encoded), &written),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 74;
    }

    return 0;
}

int main(void)
{
    int result = test_random_shapes();
    if (result != 0) {
        return result;
    }
    result = test_hash_vectors();
    if (result != 0) {
        return result;
    }
    result = test_hmac_and_constant_time();
    if (result != 0) {
        return result;
    }
    result = test_encoding_and_secret_lifecycle();
    if (result != 0) {
        return result;
    }
    result = test_password_hash_verify_and_rehash();
    if (result != 0) {
        return result;
    }
    result = test_password_rejects_unsupported_format();
    if (result != 0) {
        return result;
    }
    result = test_noncrypto_xxhash64_vectors();
    if (result != 0) {
        return result;
    }
    return test_invalid_and_capacity_guards();
}
