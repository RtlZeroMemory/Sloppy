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

typedef struct SlByteBuilder SlByteBuilder;
typedef struct SlStringBuilder SlStringBuilder;
typedef SlStatus (*SlJsonWriterSink)(void* user, SlBytes bytes);

SlStatus sl_json_writer_init_fixed(SlJsonWriter* writer, unsigned char* buffer, size_t capacity);
SlBytes sl_json_writer_view(const SlJsonWriter* writer);
SlStatus sl_json_writer_write_bytes(SlJsonWriter* writer, SlBytes bytes);
SlStatus sl_json_writer_write_str(SlJsonWriter* writer, SlStr text);
SlStatus sl_json_writer_write_char(SlJsonWriter* writer, char value);
SlStatus sl_json_writer_escaped_string_length(SlStr text, size_t* out_length);
SlStatus sl_json_writer_escaped_string_codepoint_controls_length(SlStr text, size_t* out_length);
SlStatus sl_json_writer_write_escaped_string_to(SlStr text, SlJsonWriterSink sink, void* user);
SlStatus sl_json_writer_write_escaped_string_codepoint_controls_to(SlStr text,
                                                                   SlJsonWriterSink sink,
                                                                   void* user);
SlStatus sl_json_writer_append_escaped_string_bytes(SlByteBuilder* builder, SlStr text);
SlStatus sl_json_writer_append_escaped_string_codepoint_controls(SlStringBuilder* builder,
                                                                 SlStr text);
SlStatus sl_json_writer_append_escaped_string(SlStringBuilder* builder, SlStr text);
SlStatus sl_json_writer_append_escaped_string_profiled(SlStringBuilder* builder, SlStr text);
SlStatus sl_json_writer_write_string(SlJsonWriter* writer, SlStr text);
SlStatus sl_json_writer_write_i64(SlJsonWriter* writer, int64_t value);
SlStatus sl_json_writer_write_f64(SlJsonWriter* writer, double value);

#ifdef __cplusplus
}
#endif

#endif
