#ifndef SLOPPY_JSON_WRITER_H
#define SLOPPY_JSON_WRITER_H

#include "sloppy/bytes.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SlJsonWriter
{
    unsigned char* data;
    size_t length;
    size_t capacity;
} SlJsonWriter;

SlStatus sl_json_writer_init_fixed(SlJsonWriter* writer, unsigned char* buffer, size_t capacity);
SlBytes sl_json_writer_view(const SlJsonWriter* writer);
SlStatus sl_json_writer_write_bytes(SlJsonWriter* writer, SlBytes bytes);
SlStatus sl_json_writer_write_str(SlJsonWriter* writer, SlStr text);
SlStatus sl_json_writer_write_char(SlJsonWriter* writer, char value);
SlStatus sl_json_writer_write_string(SlJsonWriter* writer, SlStr text);
SlStatus sl_json_writer_write_i64(SlJsonWriter* writer, int64_t value);
SlStatus sl_json_writer_write_f64(SlJsonWriter* writer, double value);

#ifdef __cplusplus
}
#endif

#endif
