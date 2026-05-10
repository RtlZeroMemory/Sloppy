/*
 * src/engine/v8/intrinsics_db_bridge.h
 *
 * Shared helpers for V8-internal database provider bridges. This file stays inside the
 * engine/v8 boundary because the helpers intentionally traffic in V8 handles.
 */
#ifndef SLOPPY_ENGINE_V8_INTRINSICS_DB_BRIDGE_H
#define SLOPPY_ENGINE_V8_INTRINSICS_DB_BRIDGE_H

#include "engine_v8_internal.h"

#include <string>
#include <vector>

struct SlV8DbColumnSet
{
    std::vector<v8::Local<v8::Name>> keys;
    v8::Local<v8::Array> column_names;
    v8::Local<v8::Array> columns;
    v8::Local<v8::Value> row_prototype;
    bool has_duplicate_names = false;
};

SlStatus sl_v8_db_to_local_string(v8::Isolate* isolate, SlStr str, v8::Local<v8::String>* out);
SlStatus sl_v8_db_cached_string(v8::Isolate* isolate, SlV8DbStringKey name,
                                v8::Local<v8::String>* out);
bool sl_v8_db_cached_private(v8::Isolate* isolate, SlV8DbPrivateKey name,
                             v8::Local<v8::Private>* out);
bool sl_v8_db_value_to_std_string(v8::Isolate* isolate, v8::Local<v8::Value> value,
                                  std::string* out);
bool sl_v8_db_local_string_from_std_string(v8::Isolate* isolate, const std::string& value,
                                           v8::Local<v8::String>* out);

void sl_v8_db_throw_type_error(v8::Isolate* isolate, const char* message, const char* fallback);
void sl_v8_db_throw_error(v8::Isolate* isolate, const std::string& message, const char* fallback);

bool sl_v8_db_get_object_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                  v8::Local<v8::Object> object, const char* key,
                                  v8::Local<v8::Value>* out);
bool sl_v8_db_get_object_property_key(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                      v8::Local<v8::Object> object, SlV8DbStringKey key,
                                      v8::Local<v8::Value>* out);
bool sl_v8_db_get_object_string_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                         v8::Local<v8::Object> object, const char* key,
                                         std::string* out);
bool sl_v8_db_get_optional_object_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                         v8::Local<v8::Object> object, const char* key,
                                         std::string* out, bool* present);
bool sl_v8_db_copy_uint8_array(v8::Local<v8::Value> value, std::vector<unsigned char>* out);

bool sl_v8_db_is_value_wrapper(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Value> value);
bool sl_v8_db_make_typed_string_value(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                      const char* kind, SlStr value, v8::Local<v8::Value>* out);

bool sl_v8_db_get_resource_id(v8::Isolate* isolate, v8::Local<v8::Context> context,
                              v8::Local<v8::Value> value, SlResourceId* out);
bool sl_v8_db_make_resource_handle(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                   SlResourceId id, const char* kind, v8::Local<v8::Object>* out);

bool sl_v8_db_prepare_column_set(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                 const SlStr* names, size_t count, SlV8DbColumnSet* out);
bool sl_v8_db_attach_result_metadata(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                     v8::Local<v8::Object> target, SlV8DbColumnSet* columns,
                                     SlV8DbStringKey mode);
bool sl_v8_db_make_row_object(v8::Isolate* isolate, v8::Local<v8::Context> context,
                              SlV8DbColumnSet* columns, v8::Local<v8::Value>* values,
                              size_t value_count, v8::Local<v8::Object>* out);
bool sl_v8_db_make_raw_row(v8::Isolate* isolate, v8::Local<v8::Context> context,
                           v8::Local<v8::Value>* values, size_t value_count,
                           v8::Local<v8::Array>* out);
bool sl_v8_db_make_raw_result(v8::Isolate* isolate, v8::Local<v8::Context> context,
                              SlV8DbColumnSet* columns, v8::Local<v8::Array> rows,
                              v8::Local<v8::Object>* out);
bool sl_v8_db_uint8_array_from_bytes(v8::Isolate* isolate, SlBytes bytes,
                                     v8::Local<v8::Value>* out);

bool sl_v8_db_make_promise(v8::Isolate* isolate, v8::Local<v8::Context> context,
                           v8::Global<v8::Promise::Resolver>* resolver,
                           v8::Local<v8::Promise>* out);
bool sl_v8_db_resolve_promise(v8::Local<v8::Context> context,
                              v8::Local<v8::Promise::Resolver> resolver,
                              v8::Local<v8::Value> value);
bool sl_v8_db_reject_promise(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             v8::Local<v8::Promise::Resolver> resolver, const std::string& message,
                             const char* fallback);

#endif
