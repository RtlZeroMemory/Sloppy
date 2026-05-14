#ifndef SLOPPY_CORE_ENV_H
#define SLOPPY_CORE_ENV_H

#include <stdbool.h>
#include <stddef.h>

static inline unsigned char sl_env_ascii_lower(unsigned char ch)
{
    unsigned int upper_delta = (unsigned int)ch - (unsigned int)(unsigned char)'A';
    unsigned char mask = (unsigned char)((upper_delta <= 25U) << 5U);
    return (unsigned char)(ch | mask);
}

static inline bool sl_env_cstr_token_equal_ci_ascii(const char* value, const char* token)
{
    size_t index = 0U;

    if (value == NULL || token == NULL) {
        return false;
    }
    while (token[index] != '\0') {
        unsigned char value_ch = sl_env_ascii_lower((unsigned char)value[index]);
        unsigned char token_ch = sl_env_ascii_lower((unsigned char)token[index]);

        if (value[index] == '\0' || value_ch != token_ch) {
            return false;
        }
        index += 1U;
    }
    return value[index] == '\0';
}

static inline bool sl_env_value_is_truthy(const char* value)
{
    if (value == NULL) {
        return false;
    }
    switch (value[0]) {
    case '1':
        return value[1] == '\0';
    case 'o':
    case 'O':
        return sl_env_cstr_token_equal_ci_ascii(value, "on");
    case 't':
    case 'T':
        return sl_env_cstr_token_equal_ci_ascii(value, "true");
    default:
        return false;
    }
}

#endif
