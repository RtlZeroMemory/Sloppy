/*
 * src/engine/v8/intrinsics_db_bridge.cc
 *
 * Shared V8-only helpers used by SQLite, PostgreSQL, and SQL Server provider
 * bridges. Provider-specific state machines remain in their own modules.
 */
#include "intrinsics_db_bridge.h"
#include "string_interop.h"

#include <algorithm>
#include <limits>
#include <memory>

namespace {

const char* sl_v8_db_string_key_name(SlV8DbStringKey name)
{
    switch (name) {
    case SL_V8_DB_STRING_RESOURCE_SLOT:
        return "sloppy.db.resource.slot";
    case SL_V8_DB_STRING_RESOURCE_GENERATION:
        return "sloppy.db.resource.generation";
    case SL_V8_DB_STRING_KIND:
        return "kind";
    case SL_V8_DB_STRING_VALUE:
        return "value";
    case SL_V8_DB_STRING_SLOPPY_DB_VALUE:
        return "__sloppyDbValue";
    case SL_V8_DB_STRING_COLUMNS:
        return "columns";
    case SL_V8_DB_STRING_COLUMN_NAMES:
        return "columnNames";
    case SL_V8_DB_STRING_ROWS:
        return "rows";
    case SL_V8_DB_STRING_MODE:
        return "mode";
    case SL_V8_DB_STRING_OBJECT:
        return "object";
    case SL_V8_DB_STRING_RAW:
        return "raw";
    case SL_V8_DB_STRING_NAME:
        return "name";
    case SL_V8_DB_STRING_INDEX:
        return "index";
    default:
        return nullptr;
    }
}

const char* sl_v8_db_private_key_name(SlV8DbPrivateKey name)
{
    switch (name) {
    case SL_V8_DB_PRIVATE_RESOURCE_SLOT:
        return "sloppy.db.resource.slot";
    case SL_V8_DB_PRIVATE_RESOURCE_GENERATION:
        return "sloppy.db.resource.generation";
    default:
        return nullptr;
    }
}

v8::Local<v8::String> sl_v8_db_fallback_string(v8::Isolate* isolate, const char* fallback)
{
    v8::Local<v8::String> value;
    if (fallback != nullptr &&
        v8::String::NewFromUtf8(isolate, fallback, v8::NewStringType::kNormal).ToLocal(&value))
    {
        return value;
    }
    return v8::String::Empty(isolate);
}

bool sl_v8_db_supported_value_kind(const std::string& kind)
{
    return kind == "decimal" || kind == "uuid" || kind == "date" || kind == "time" ||
           kind == "localDateTime" || kind == "instant" || kind == "offsetDateTime" ||
           kind == "json" || kind == "rawJson" || kind == "bytes";
}

bool sl_v8_db_str_equal(SlStr left, SlStr right)
{
    return sl_str_equal(left, right);
}

bool sl_v8_db_define_hidden_readonly(v8::Local<v8::Context> context, v8::Local<v8::Object> object,
                                     v8::Local<v8::Name> key, v8::Local<v8::Value> value)
{
    return object
        ->DefineOwnProperty(context, key, value,
                            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontEnum))
        .FromMaybe(false);
}

} // namespace

SlStatus sl_v8_db_to_local_string(v8::Isolate* isolate, SlStr str, v8::Local<v8::String>* out)
{
    SlV8Engine* backend =
        isolate == nullptr ? nullptr : static_cast<SlV8Engine*>(isolate->GetData(0));
    return sl_v8_string_from_native_view(backend, str, out);
}

SlStatus sl_v8_db_cached_string(v8::Isolate* isolate, SlV8DbStringKey name,
                                v8::Local<v8::String>* out)
{
    SlV8Engine* backend =
        isolate == nullptr ? nullptr : static_cast<SlV8Engine*>(isolate->GetData(0));
    const char* text = sl_v8_db_string_key_name(name);
    const size_t index = static_cast<size_t>(name);
    v8::Local<v8::String> value;
    SlStatus status;

    if (backend == nullptr || out == nullptr || text == nullptr ||
        index >= backend->db_strings.size())
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (!backend->db_strings[index].IsEmpty()) {
        *out = backend->db_strings[index].Get(isolate);
        return sl_status_ok();
    }

    status = sl_v8_string_from_native_view(backend, sl_str_from_cstr(text), &value);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    backend->db_strings[index].Reset(isolate, value);
    *out = value;
    return sl_status_ok();
}

bool sl_v8_db_cached_private(v8::Isolate* isolate, SlV8DbPrivateKey name,
                             v8::Local<v8::Private>* out)
{
    SlV8Engine* backend =
        isolate == nullptr ? nullptr : static_cast<SlV8Engine*>(isolate->GetData(0));
    const char* text = sl_v8_db_private_key_name(name);
    const size_t index = static_cast<size_t>(name);
    v8::Local<v8::String> key;
    v8::Local<v8::Private> private_key;

    if (backend == nullptr || out == nullptr || text == nullptr ||
        index >= backend->db_private_keys.size())
    {
        return false;
    }

    if (!backend->db_private_keys[index].IsEmpty()) {
        *out = backend->db_private_keys[index].Get(isolate);
        return true;
    }

    if (!sl_status_is_ok(sl_v8_string_from_native_view(backend, sl_str_from_cstr(text), &key))) {
        return false;
    }

    private_key = v8::Private::ForApi(isolate, key);
    backend->db_private_keys[index].Reset(isolate, private_key);
    *out = private_key;
    return true;
}

bool sl_v8_db_value_to_std_string(v8::Isolate* isolate, v8::Local<v8::Value> value,
                                  std::string* out)
{
    return out != nullptr && value->IsString() && sl_v8_std_string_from_value(isolate, value, out);
}

bool sl_v8_db_local_string_from_std_string(v8::Isolate* isolate, const std::string& value,
                                           v8::Local<v8::String>* out)
{
    return sl_status_is_ok(
        sl_v8_db_to_local_string(isolate, sl_str_from_parts(value.data(), value.size()), out));
}

void sl_v8_db_throw_type_error(v8::Isolate* isolate, const char* message, const char* fallback)
{
    v8::Local<v8::String> local_message;
    if (isolate == nullptr) {
        return;
    }
    if (!sl_status_is_ok(
            sl_v8_db_to_local_string(isolate, sl_str_from_cstr(message), &local_message)))
    {
        isolate->ThrowException(
            v8::Exception::TypeError(sl_v8_db_fallback_string(isolate, fallback)));
        return;
    }
    isolate->ThrowException(v8::Exception::TypeError(local_message));
}

void sl_v8_db_throw_error(v8::Isolate* isolate, const std::string& message, const char* fallback)
{
    v8::Local<v8::String> local_message;
    if (isolate == nullptr) {
        return;
    }
    if (!sl_v8_db_local_string_from_std_string(isolate, message, &local_message)) {
        isolate->ThrowException(v8::Exception::Error(sl_v8_db_fallback_string(isolate, fallback)));
        return;
    }
    isolate->ThrowException(v8::Exception::Error(local_message));
}

bool sl_v8_db_get_object_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                  v8::Local<v8::Object> object, const char* key,
                                  v8::Local<v8::Value>* out)
{
    v8::Local<v8::String> local_key;
    v8::Maybe<bool> has_own = v8::Nothing<bool>();

    if (out == nullptr ||
        !sl_status_is_ok(sl_v8_db_to_local_string(isolate, sl_str_from_cstr(key), &local_key)))
    {
        return false;
    }
    *out = v8::Undefined(isolate);
    has_own = object->HasOwnProperty(context, local_key);
    if (has_own.IsNothing()) {
        return false;
    }
    if (!has_own.FromJust()) {
        return true;
    }

    return object->Get(context, local_key).ToLocal(out);
}

bool sl_v8_db_get_object_property_key(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                      v8::Local<v8::Object> object, SlV8DbStringKey key,
                                      v8::Local<v8::Value>* out)
{
    v8::Local<v8::String> local_key;
    v8::Maybe<bool> has_own = v8::Nothing<bool>();

    if (out == nullptr || !sl_status_is_ok(sl_v8_db_cached_string(isolate, key, &local_key))) {
        return false;
    }
    *out = v8::Undefined(isolate);
    has_own = object->HasOwnProperty(context, local_key);
    if (has_own.IsNothing()) {
        return false;
    }
    if (!has_own.FromJust()) {
        return true;
    }

    return object->Get(context, local_key).ToLocal(out);
}

static bool sl_v8_db_get_required_object_property(v8::Isolate* isolate,
                                                  v8::Local<v8::Context> context,
                                                  v8::Local<v8::Object> object, const char* key,
                                                  v8::Local<v8::Value>* out)
{
    v8::Local<v8::Value> value;

    return out != nullptr && sl_v8_db_get_object_property(isolate, context, object, key, &value) &&
           !value->IsUndefined() && ((*out = value), true);
}

bool sl_v8_db_get_object_string_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                         v8::Local<v8::Object> object, const char* key,
                                         std::string* out)
{
    v8::Local<v8::Value> value;
    return out != nullptr &&
           sl_v8_db_get_required_object_property(isolate, context, object, key, &value) &&
           value->IsString() && sl_v8_std_string_from_value(isolate, value, out);
}

bool sl_v8_db_get_optional_object_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                         v8::Local<v8::Object> object, const char* key,
                                         std::string* out, bool* present)
{
    v8::Local<v8::Value> value;
    if (out == nullptr || present == nullptr ||
        !sl_v8_db_get_object_property(isolate, context, object, key, &value))
    {
        return false;
    }
    if (value->IsUndefined() || value->IsNull()) {
        *present = false;
        out->clear();
        return true;
    }
    *present = true;
    return sl_v8_db_value_to_std_string(isolate, value, out);
}

bool sl_v8_db_copy_uint8_array(v8::Local<v8::Value> value, std::vector<unsigned char>* out)
{
    if (out == nullptr || !value->IsUint8Array()) {
        return false;
    }
    v8::Local<v8::Uint8Array> bytes = value.As<v8::Uint8Array>();
    std::shared_ptr<v8::BackingStore> backing = bytes->Buffer()->GetBackingStore();
    const size_t offset = bytes->ByteOffset();
    const size_t length = bytes->ByteLength();
    if (backing == nullptr || offset > backing->ByteLength() ||
        length > backing->ByteLength() - offset)
    {
        return false;
    }
    if (length == 0U) {
        out->clear();
        return true;
    }
    const auto* data = static_cast<const unsigned char*>(backing->Data());
    if (data == nullptr) {
        return false;
    }
    out->assign(data + offset, data + offset + length);
    return true;
}

bool sl_v8_db_is_value_wrapper(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Value> value)
{
    v8::Local<v8::Value> marker;
    v8::Local<v8::Value> kind_value;
    std::string kind;
    if (!value->IsObject()) {
        return false;
    }
    v8::Local<v8::Object> object = value.As<v8::Object>();
    return sl_v8_db_get_object_property_key(isolate, context, object,
                                            SL_V8_DB_STRING_SLOPPY_DB_VALUE, &marker) &&
           marker->IsTrue() &&
           sl_v8_db_get_object_property_key(isolate, context, object, SL_V8_DB_STRING_KIND,
                                            &kind_value) &&
           kind_value->IsString() && sl_v8_std_string_from_value(isolate, kind_value, &kind) &&
           sl_v8_db_supported_value_kind(kind);
}

bool sl_v8_db_make_typed_string_value(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                      const char* kind, SlStr value, v8::Local<v8::Value>* out)
{
    v8::Local<v8::String> text;
    v8::Local<v8::String> kind_value;
    v8::Local<v8::String> marker_key;
    v8::Local<v8::String> kind_key;
    v8::Local<v8::String> value_key;
    v8::Local<v8::Value> wrapper_value;
    v8::Local<v8::Object> wrapper;

    if (out == nullptr || kind == nullptr ||
        !sl_status_is_ok(sl_v8_db_to_local_string(isolate, value, &text)) ||
        !sl_status_is_ok(sl_v8_db_to_local_string(isolate, sl_str_from_cstr(kind), &kind_value)) ||
        !sl_status_is_ok(
            sl_v8_db_cached_string(isolate, SL_V8_DB_STRING_SLOPPY_DB_VALUE, &marker_key)) ||
        !sl_status_is_ok(sl_v8_db_cached_string(isolate, SL_V8_DB_STRING_KIND, &kind_key)) ||
        !sl_status_is_ok(sl_v8_db_cached_string(isolate, SL_V8_DB_STRING_VALUE, &value_key)))
    {
        return false;
    }
    wrapper_value = v8::StringObject::New(isolate, text);
    if (!wrapper_value->IsObject()) {
        return false;
    }
    wrapper = wrapper_value.As<v8::Object>();
    if (!wrapper->Set(context, marker_key, v8::Boolean::New(isolate, true)).FromMaybe(false) ||
        !wrapper->Set(context, kind_key, kind_value).FromMaybe(false) ||
        !wrapper->Set(context, value_key, text).FromMaybe(false) ||
        !wrapper->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen).FromMaybe(false))
    {
        return false;
    }
    *out = wrapper_value;
    return true;
}

bool sl_v8_db_get_resource_id(v8::Isolate* isolate, v8::Local<v8::Context> context,
                              v8::Local<v8::Value> value, SlResourceId* out)
{
    v8::Local<v8::Object> object;
    v8::Local<v8::Private> slot_key;
    v8::Local<v8::Private> generation_key;
    v8::Local<v8::Value> slot_value;
    v8::Local<v8::Value> generation_value;

    if (out == nullptr || !value->IsObject()) {
        return false;
    }
    object = value.As<v8::Object>();
    if (!sl_v8_db_cached_private(isolate, SL_V8_DB_PRIVATE_RESOURCE_SLOT, &slot_key) ||
        !sl_v8_db_cached_private(isolate, SL_V8_DB_PRIVATE_RESOURCE_GENERATION, &generation_key) ||
        !object->GetPrivate(context, slot_key).ToLocal(&slot_value) ||
        !object->GetPrivate(context, generation_key).ToLocal(&generation_value) ||
        !slot_value->IsUint32() || !generation_value->IsUint32())
    {
        return false;
    }
    out->slot = slot_value.As<v8::Uint32>()->Value();
    out->generation = generation_value.As<v8::Uint32>()->Value();
    return sl_resource_id_is_valid(*out);
}

bool sl_v8_db_make_resource_handle(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                   SlResourceId id, const char* kind, v8::Local<v8::Object>* out)
{
    v8::Local<v8::Object> handle = v8::Object::New(isolate);
    v8::Local<v8::String> kind_value;
    v8::Local<v8::String> kind_key;
    v8::Local<v8::Private> slot_key;
    v8::Local<v8::Private> generation_key;

    if (out == nullptr || kind == nullptr ||
        !sl_status_is_ok(sl_v8_db_to_local_string(isolate, sl_str_from_cstr(kind), &kind_value)) ||
        !sl_status_is_ok(sl_v8_db_cached_string(isolate, SL_V8_DB_STRING_KIND, &kind_key)) ||
        !sl_v8_db_cached_private(isolate, SL_V8_DB_PRIVATE_RESOURCE_SLOT, &slot_key) ||
        !sl_v8_db_cached_private(isolate, SL_V8_DB_PRIVATE_RESOURCE_GENERATION, &generation_key) ||
        !handle->SetPrivate(context, slot_key, v8::Integer::NewFromUnsigned(isolate, id.slot))
             .FromMaybe(false) ||
        !handle
             ->SetPrivate(context, generation_key,
                          v8::Integer::NewFromUnsigned(isolate, id.generation))
             .FromMaybe(false) ||
        !handle->Set(context, kind_key, kind_value).FromMaybe(false) ||
        !handle->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen).FromMaybe(false))
    {
        return false;
    }
    *out = handle;
    return true;
}

bool sl_v8_db_prepare_column_set(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                 const SlStr* names, size_t count, SlV8DbColumnSet* out)
{
    v8::Local<v8::String> name_key;
    v8::Local<v8::String> index_key;

    if (isolate == nullptr || out == nullptr || (count != 0U && names == nullptr) ||
        count > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        count > static_cast<size_t>(std::numeric_limits<uint32_t>::max()) ||
        !sl_status_is_ok(sl_v8_db_cached_string(isolate, SL_V8_DB_STRING_NAME, &name_key)) ||
        !sl_status_is_ok(sl_v8_db_cached_string(isolate, SL_V8_DB_STRING_INDEX, &index_key)))
    {
        return false;
    }

    out->keys.clear();
    out->keys.reserve(count);
    out->column_names = v8::Array::New(isolate, static_cast<int>(count));
    out->columns = v8::Array::New(isolate, static_cast<int>(count));
    out->row_prototype = v8::Object::New(isolate)->GetPrototype();
    out->has_duplicate_names = false;

    for (size_t index = 0U; index < count; index += 1U) {
        v8::Local<v8::String> key;
        v8::Local<v8::Name> column_property_names[2] = {name_key, index_key};
        v8::Local<v8::Value> column_property_values[2];
        v8::Local<v8::Object> column;

        if (!sl_status_is_ok(sl_v8_db_to_local_string(isolate, names[index], &key))) {
            return false;
        }
        for (size_t seen = 0U; seen < index; seen += 1U) {
            if (sl_v8_db_str_equal(names[seen], names[index])) {
                out->has_duplicate_names = true;
                break;
            }
        }

        column_property_values[0] = key;
        column_property_values[1] =
            v8::Integer::NewFromUnsigned(isolate, static_cast<uint32_t>(index));
        column = v8::Object::New(isolate, v8::Null(isolate), column_property_names,
                                 column_property_values, 2U);
        if (!column->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen).FromMaybe(false) ||
            !out->column_names->Set(context, static_cast<uint32_t>(index), key).FromMaybe(false) ||
            !out->columns->Set(context, static_cast<uint32_t>(index), column).FromMaybe(false))
        {
            return false;
        }

        out->keys.emplace_back(key);
    }

    return out->column_names->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen)
               .FromMaybe(false) &&
           out->columns->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen).FromMaybe(false);
}

bool sl_v8_db_attach_result_metadata(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                     v8::Local<v8::Object> target, SlV8DbColumnSet* columns,
                                     SlV8DbStringKey mode)
{
    v8::Local<v8::String> mode_key;
    v8::Local<v8::String> columns_key;
    v8::Local<v8::String> column_names_key;
    v8::Local<v8::String> mode_value;

    if (columns == nullptr || (mode != SL_V8_DB_STRING_OBJECT && mode != SL_V8_DB_STRING_RAW) ||
        !sl_status_is_ok(sl_v8_db_cached_string(isolate, SL_V8_DB_STRING_MODE, &mode_key)) ||
        !sl_status_is_ok(sl_v8_db_cached_string(isolate, SL_V8_DB_STRING_COLUMNS, &columns_key)) ||
        !sl_status_is_ok(
            sl_v8_db_cached_string(isolate, SL_V8_DB_STRING_COLUMN_NAMES, &column_names_key)) ||
        !sl_status_is_ok(sl_v8_db_cached_string(isolate, mode, &mode_value)))
    {
        return false;
    }

    return sl_v8_db_define_hidden_readonly(context, target, mode_key, mode_value) &&
           sl_v8_db_define_hidden_readonly(context, target, columns_key, columns->columns) &&
           sl_v8_db_define_hidden_readonly(context, target, column_names_key,
                                           columns->column_names);
}

bool sl_v8_db_make_row_object(v8::Isolate* isolate, v8::Local<v8::Context> context,
                              SlV8DbColumnSet* columns, v8::Local<v8::Value>* values,
                              size_t value_count, v8::Local<v8::Object>* out)
{
    v8::Local<v8::Object> row;

    if (columns == nullptr || out == nullptr || value_count != columns->keys.size() ||
        (value_count != 0U && values == nullptr))
    {
        return false;
    }

    if (!columns->has_duplicate_names) {
        row = v8::Object::New(isolate, columns->row_prototype, columns->keys.data(), values,
                              value_count);
        *out = row;
        return true;
    }

    row = v8::Object::New(isolate);
    for (size_t index = 0U; index < value_count; index += 1U) {
        if (!row->Set(context, columns->keys[index], values[index]).FromMaybe(false)) {
            return false;
        }
    }
    *out = row;
    return true;
}

bool sl_v8_db_make_raw_row(v8::Isolate* isolate, v8::Local<v8::Context> context,
                           v8::Local<v8::Value>* values, size_t value_count,
                           v8::Local<v8::Array>* out)
{
    v8::Local<v8::Array> row;
    if (out == nullptr || value_count > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        value_count > static_cast<size_t>(std::numeric_limits<uint32_t>::max()) ||
        (value_count != 0U && values == nullptr))
    {
        return false;
    }
    row = v8::Array::New(isolate, static_cast<int>(value_count));
    for (size_t index = 0U; index < value_count; index += 1U) {
        if (!row->Set(context, static_cast<uint32_t>(index), values[index]).FromMaybe(false)) {
            return false;
        }
    }
    *out = row;
    return true;
}

bool sl_v8_db_make_raw_result(v8::Isolate* isolate, v8::Local<v8::Context> context,
                              SlV8DbColumnSet* columns, v8::Local<v8::Array> rows,
                              v8::Local<v8::Object>* out)
{
    (void)context;

    v8::Local<v8::String> mode_key;
    v8::Local<v8::String> columns_key;
    v8::Local<v8::String> column_names_key;
    v8::Local<v8::String> rows_key;
    v8::Local<v8::String> raw_value;
    v8::Local<v8::Name> names[4];
    v8::Local<v8::Value> values[4];

    if (columns == nullptr || out == nullptr ||
        !sl_status_is_ok(sl_v8_db_cached_string(isolate, SL_V8_DB_STRING_MODE, &mode_key)) ||
        !sl_status_is_ok(sl_v8_db_cached_string(isolate, SL_V8_DB_STRING_COLUMNS, &columns_key)) ||
        !sl_status_is_ok(
            sl_v8_db_cached_string(isolate, SL_V8_DB_STRING_COLUMN_NAMES, &column_names_key)) ||
        !sl_status_is_ok(sl_v8_db_cached_string(isolate, SL_V8_DB_STRING_ROWS, &rows_key)) ||
        !sl_status_is_ok(sl_v8_db_cached_string(isolate, SL_V8_DB_STRING_RAW, &raw_value)))
    {
        return false;
    }

    names[0] = mode_key;
    values[0] = raw_value;
    names[1] = columns_key;
    values[1] = columns->columns;
    names[2] = column_names_key;
    values[2] = columns->column_names;
    names[3] = rows_key;
    values[3] = rows;
    *out = v8::Object::New(isolate, v8::Object::New(isolate)->GetPrototype(), names, values, 4U);
    return true;
}

bool sl_v8_db_uint8_array_from_bytes(v8::Isolate* isolate, SlBytes bytes, v8::Local<v8::Value>* out)
{
    if (isolate == nullptr || out == nullptr || (bytes.length != 0U && bytes.ptr == nullptr) ||
        bytes.length > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }

    std::unique_ptr<v8::BackingStore> backing =
        v8::ArrayBuffer::NewBackingStore(isolate, bytes.length);
    if (!backing) {
        return false;
    }
    if (bytes.length != 0U) {
        auto* target = static_cast<unsigned char*>(backing->Data());
        std::copy_n(bytes.ptr, bytes.length, target);
    }

    v8::Local<v8::ArrayBuffer> buffer = v8::ArrayBuffer::New(isolate, std::move(backing));
    *out = v8::Uint8Array::New(buffer, 0U, bytes.length);
    return true;
}

bool sl_v8_db_make_promise(v8::Isolate* isolate, v8::Local<v8::Context> context,
                           v8::Global<v8::Promise::Resolver>* resolver, v8::Local<v8::Promise>* out)
{
    v8::Local<v8::Promise::Resolver> local_resolver;
    if (isolate == nullptr || resolver == nullptr || out == nullptr ||
        !v8::Promise::Resolver::New(context).ToLocal(&local_resolver))
    {
        return false;
    }
    resolver->Reset(isolate, local_resolver);
    *out = local_resolver->GetPromise();
    return true;
}

bool sl_v8_db_resolve_promise(v8::Local<v8::Context> context,
                              v8::Local<v8::Promise::Resolver> resolver, v8::Local<v8::Value> value)
{
    return resolver->Resolve(context, value).FromMaybe(false);
}

bool sl_v8_db_reject_promise(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             v8::Local<v8::Promise::Resolver> resolver, const std::string& message,
                             const char* fallback)
{
    v8::Local<v8::String> local_message;
    if (isolate == nullptr) {
        return false;
    }
    if (!sl_v8_db_local_string_from_std_string(isolate, message, &local_message)) {
        local_message = sl_v8_db_fallback_string(isolate, fallback);
    }
    return resolver->Reject(context, v8::Exception::Error(local_message)).FromMaybe(false);
}
