#ifndef SLOPPY_ENGINE_V8_STRING_INTEROP_H
#define SLOPPY_ENGINE_V8_STRING_INTEROP_H

#include "engine_v8_internal.h"

#include "sloppy/bytes.h"

#include <v8.h>

#include <string>

/*
 * V8/native string interop policy helpers.
 *
 * These helpers are private to src/engine/v8/. They keep V8 entry on the engine owner
 * thread, use explicit byte lengths, copy V8 UTF-8 output into documented native owners,
 * and never expose raw native pointers or V8 handles outside the V8 module.
 */

SlStatus sl_v8_string_from_native_view(SlV8Engine* backend, SlStr str, v8::Local<v8::String>* out);

bool sl_v8_std_string_from_value(v8::Isolate* isolate, v8::Local<v8::Value> value,
                                 std::string* out);

SlStatus sl_v8_string_from_value_copy_to_arena(v8::Isolate* isolate, SlArena* arena,
                                               v8::Local<v8::Value> value, SlStr* out);

SlStatus sl_v8_string_value_copy_bytes_to_arena(v8::Isolate* isolate, SlArena* arena,
                                                v8::Local<v8::Value> value, SlBytes* out);

SlStatus sl_v8_std_string_copy_to_arena(SlArena* arena, const std::string& src, SlStr* out);

SlStatus sl_v8_std_string_copy_bytes_to_arena(SlArena* arena, const std::string& src, SlBytes* out);

bool sl_v8_throw_type_error_from_native_view(SlV8Engine* backend, SlStr message);

bool sl_v8_throw_error_from_native_view(SlV8Engine* backend, SlStr message);

#endif
