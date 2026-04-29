/*
 * src/engine/v8/intrinsics_sqlite.cc
 *
 * Installs the V8-internal SQLite bridge under __sloppy.data.sqlite.
 * Provider bridge modules may include V8 and provider headers because they
 * remain inside src/engine/v8/, but engine_v8.cc must stay focused on isolate,
 * context, and handler orchestration.
 */
#include "engine_v8_internal.h"

#include "sloppy/data_sqlite.h"

#include <limits>
#include <new>
#include <string>
#include <vector>

namespace {

SlStatus sqlite_v8_to_local_string(v8::Isolate* isolate, SlStr str, v8::Local<v8::String>* out)
{
    v8::MaybeLocal<v8::String> maybe;

    if (isolate == nullptr || out == nullptr || (str.length != 0U && str.ptr == nullptr) ||
        str.length > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    maybe = v8::String::NewFromUtf8(isolate, str.ptr, v8::NewStringType::kNormal,
                                    static_cast<int>(str.length));
    if (!maybe.ToLocal(out)) {
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }

    return sl_status_ok();
}

std::string sqlite_v8_diag_to_string(const char* fallback, const SlDiag& diag)
{
    std::string message = fallback == nullptr ? "Sloppy native operation failed" : fallback;

    if (diag.message.ptr != nullptr && diag.message.length != 0U) {
        message.assign(diag.message.ptr, diag.message.length);
    }

    for (size_t index = 0U; index < diag.hint_count; index += 1U) {
        if (diag.hints[index].ptr != nullptr && diag.hints[index].length != 0U) {
            message += "\n";
            message.append(diag.hints[index].ptr, diag.hints[index].length);
        }
    }

    return message;
}

void sqlite_v8_throw_type_error(v8::Isolate* isolate, const char* message)
{
    v8::Local<v8::String> local_message;
    if (!sl_status_is_ok(
            sqlite_v8_to_local_string(isolate, sl_str_from_cstr(message), &local_message)))
    {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Sloppy native type error")));
        return;
    }

    isolate->ThrowException(v8::Exception::TypeError(local_message));
}

void sqlite_v8_throw_error(v8::Isolate* isolate, const std::string& message)
{
    v8::Local<v8::String> local_message;
    if (!sl_status_is_ok(sqlite_v8_to_local_string(
            isolate, sl_str_from_parts(message.data(), message.size()), &local_message)))
    {
        isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8Literal(isolate, "Sloppy native operation failed")));
        return;
    }

    isolate->ThrowException(v8::Exception::Error(local_message));
}

void sqlite_v8_throw_diag(v8::Isolate* isolate, const char* fallback, const SlDiag& diag)
{
    sqlite_v8_throw_error(isolate, sqlite_v8_diag_to_string(fallback, diag));
}

bool sqlite_v8_value_to_std_string(v8::Isolate* isolate, v8::Local<v8::Value> value,
                                   std::string* out)
{
    if (out == nullptr || !value->IsString()) {
        return false;
    }

    v8::String::Utf8Value utf8(isolate, value);
    if (*utf8 == nullptr) {
        return false;
    }

    *out = std::string(*utf8, static_cast<size_t>(utf8.length()));
    return true;
}

bool sqlite_v8_get_object_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                 v8::Local<v8::Object> object, const char* key, std::string* out)
{
    v8::Local<v8::String> local_key;
    v8::Local<v8::Value> value;

    if (out == nullptr ||
        !sl_status_is_ok(sqlite_v8_to_local_string(isolate, sl_str_from_cstr(key), &local_key)) ||
        !object->Get(context, local_key).ToLocal(&value))
    {
        return false;
    }

    return sqlite_v8_value_to_std_string(isolate, value, out);
}

bool sqlite_v8_get_optional_object_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                          v8::Local<v8::Object> object, const char* key,
                                          std::string* out, bool* present)
{
    v8::Local<v8::String> local_key;
    v8::Local<v8::Value> value;

    if (out == nullptr || present == nullptr ||
        !sl_status_is_ok(sqlite_v8_to_local_string(isolate, sl_str_from_cstr(key), &local_key)) ||
        !object->Get(context, local_key).ToLocal(&value))
    {
        return false;
    }

    if (value->IsUndefined() || value->IsNull()) {
        *present = false;
        out->clear();
        return true;
    }

    *present = true;
    return sqlite_v8_value_to_std_string(isolate, value, out);
}

bool sqlite_v8_parse_open_options(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                  v8::Local<v8::Value> value, std::string* path,
                                  SlSqliteAccess* access)
{
    if (path == nullptr || access == nullptr) {
        return false;
    }

    *access = SL_SQLITE_ACCESS_READWRITE;

    if (sqlite_v8_value_to_std_string(isolate, value, path)) {
        return !path->empty();
    }

    if (!value->IsObject()) {
        return false;
    }

    v8::Local<v8::Object> object = value.As<v8::Object>();
    if (!sqlite_v8_get_object_string(isolate, context, object, "path", path) || path->empty()) {
        return false;
    }

    std::string access_text;
    bool access_present = false;
    if (!sqlite_v8_get_optional_object_string(isolate, context, object, "access", &access_text,
                                              &access_present))
    {
        return false;
    }

    if (!access_present || access_text == "readwrite") {
        *access = SL_SQLITE_ACCESS_READWRITE;
        return true;
    }

    if (access_text == "read") {
        *access = SL_SQLITE_ACCESS_READ;
        return true;
    }

    return false;
}

bool sqlite_v8_get_resource_id(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Value> value, SlResourceId* out)
{
    v8::Local<v8::Object> object;
    v8::Local<v8::String> slot_key;
    v8::Local<v8::String> generation_key;
    v8::Local<v8::Value> slot_value;
    v8::Local<v8::Value> generation_value;

    if (out == nullptr || !value->IsObject()) {
        return false;
    }

    object = value.As<v8::Object>();
    if (!sl_status_is_ok(sqlite_v8_to_local_string(isolate, sl_str_from_cstr("slot"), &slot_key)) ||
        !sl_status_is_ok(
            sqlite_v8_to_local_string(isolate, sl_str_from_cstr("generation"), &generation_key)) ||
        !object->Get(context, slot_key).ToLocal(&slot_value) ||
        !object->Get(context, generation_key).ToLocal(&generation_value) ||
        !slot_value->IsUint32() || !generation_value->IsUint32())
    {
        return false;
    }

    out->slot = slot_value.As<v8::Uint32>()->Value();
    out->generation = generation_value.As<v8::Uint32>()->Value();
    return true;
}

bool sqlite_v8_make_resource_handle(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                    SlResourceId id, v8::Local<v8::Object>* out)
{
    v8::Local<v8::Object> handle = v8::Object::New(isolate);
    v8::Local<v8::String> slot_key;
    v8::Local<v8::String> generation_key;
    v8::Local<v8::String> kind_key;
    v8::Local<v8::String> kind_value;

    if (out == nullptr ||
        !sl_status_is_ok(sqlite_v8_to_local_string(isolate, sl_str_from_cstr("slot"), &slot_key)) ||
        !sl_status_is_ok(
            sqlite_v8_to_local_string(isolate, sl_str_from_cstr("generation"), &generation_key)) ||
        !sl_status_is_ok(sqlite_v8_to_local_string(isolate, sl_str_from_cstr("kind"), &kind_key)) ||
        !sl_status_is_ok(sqlite_v8_to_local_string(isolate, sl_str_from_cstr("sqlite.connection"),
                                                   &kind_value)) ||
        !handle->Set(context, slot_key, v8::Integer::NewFromUnsigned(isolate, id.slot))
             .FromMaybe(false) ||
        !handle->Set(context, generation_key, v8::Integer::NewFromUnsigned(isolate, id.generation))
             .FromMaybe(false) ||
        !handle->Set(context, kind_key, kind_value).FromMaybe(false))
    {
        return false;
    }

    *out = handle;
    return true;
}

void sqlite_v8_connection_cleanup(void* ptr, void* user)
{
    (void)user;

    SlSqliteConnection* connection = static_cast<SlSqliteConnection*>(ptr);
    if (connection != nullptr) {
        (void)sl_sqlite_close(connection);
        delete connection;
    }
}

SlSqliteConnection* sqlite_v8_lookup_connection(v8::Isolate* isolate,
                                                v8::Local<v8::Context> context, SlV8Engine* backend,
                                                v8::Local<v8::Value> handle_value)
{
    SlResourceId id = {};
    SlDiag diag = {};
    void* ptr = nullptr;

    if (!sqlite_v8_get_resource_id(isolate, context, handle_value, &id)) {
        sqlite_v8_throw_type_error(isolate,
                                   "sqlite resource handle must be an opaque Sloppy handle");
        return nullptr;
    }

    SlStatus status = sl_resource_table_get(&backend->resources, id,
                                            SL_RESOURCE_KIND_SQLITE_CONNECTION, &ptr, &diag);
    if (!sl_status_is_ok(status)) {
        sqlite_v8_throw_diag(isolate, "sqlite resource handle is invalid", diag);
        return nullptr;
    }

    return static_cast<SlSqliteConnection*>(ptr);
}

bool sqlite_v8_convert_params(v8::Isolate* isolate, v8::Local<v8::Context> context,
                              v8::Local<v8::Value> value, std::vector<std::string>* text_storage,
                              std::vector<SlSqliteParam>* out)
{
    v8::Local<v8::Array> array;
    uint32_t length = 0U;

    if (out == nullptr || text_storage == nullptr) {
        return false;
    }

    out->clear();
    text_storage->clear();

    if (value->IsUndefined() || value->IsNull()) {
        return true;
    }

    if (!value->IsArray()) {
        sqlite_v8_throw_type_error(isolate, "sqlite parameters must be an array when supplied");
        return false;
    }

    array = value.As<v8::Array>();
    length = array->Length();
    out->reserve(length);
    text_storage->reserve(length);

    for (uint32_t index = 0U; index < length; index += 1U) {
        v8::Local<v8::Value> item;
        SlSqliteParam param = {};

        if (!array->Get(context, index).ToLocal(&item)) {
            return false;
        }

        if (item->IsNull() || item->IsUndefined()) {
            param.kind = SL_SQLITE_PARAM_NULL;
        }
        else if (item->IsBoolean()) {
            param.kind = SL_SQLITE_PARAM_BOOL;
            param.value.boolean = item->BooleanValue(isolate);
        }
        else if (item->IsInt32()) {
            param.kind = SL_SQLITE_PARAM_INTEGER;
            param.value.integer = item.As<v8::Int32>()->Value();
        }
        else if (item->IsNumber()) {
            param.kind = SL_SQLITE_PARAM_FLOAT;
            param.value.number = item.As<v8::Number>()->Value();
        }
        else if (item->IsString()) {
            std::string text;
            if (!sqlite_v8_value_to_std_string(isolate, item, &text)) {
                return false;
            }
            text_storage->push_back(std::move(text));
            const std::string& stored = text_storage->back();
            param.kind = SL_SQLITE_PARAM_TEXT;
            param.value.text = sl_str_from_parts(stored.data(), stored.size());
        }
        else {
            sqlite_v8_throw_type_error(
                isolate,
                "sqlite parameters support only null, string, integer, float, and boolean values");
            return false;
        }

        out->push_back(param);
    }

    return true;
}

bool sqlite_v8_set_cell(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> row, SlStr column_name, const SlSqliteValue* value)
{
    v8::Local<v8::String> key;
    v8::Local<v8::Value> js_value;

    if (value == nullptr || !sl_status_is_ok(sqlite_v8_to_local_string(isolate, column_name, &key)))
    {
        return false;
    }

    switch (value->kind) {
    case SL_SQLITE_VALUE_NULL:
        js_value = v8::Null(isolate);
        break;
    case SL_SQLITE_VALUE_TEXT: {
        v8::Local<v8::String> text;
        if (!sl_status_is_ok(sqlite_v8_to_local_string(isolate, value->value.text, &text))) {
            return false;
        }
        js_value = text;
        break;
    }
    case SL_SQLITE_VALUE_INTEGER:
        js_value = v8::Number::New(isolate, static_cast<double>(value->value.integer));
        break;
    case SL_SQLITE_VALUE_FLOAT:
        js_value = v8::Number::New(isolate, value->value.number);
        break;
    default:
        return false;
    }

    return row->Set(context, key, js_value).FromMaybe(false);
}

bool sqlite_v8_result_to_array(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               const SlSqliteResult* result, v8::Local<v8::Array>* out)
{
    v8::Local<v8::Array> rows;

    if (result == nullptr || out == nullptr ||
        result->row_count > static_cast<size_t>(std::numeric_limits<uint32_t>::max()))
    {
        return false;
    }

    rows = v8::Array::New(isolate, static_cast<int>(result->row_count));
    for (size_t row_index = 0U; row_index < result->row_count; row_index += 1U) {
        v8::Local<v8::Object> row = v8::Object::New(isolate);
        for (size_t column_index = 0U; column_index < result->column_count; column_index += 1U) {
            if (!sqlite_v8_set_cell(isolate, context, row, result->column_names[column_index],
                                    &result->rows[row_index].values[column_index]))
            {
                return false;
            }
        }
        if (!rows->Set(context, static_cast<uint32_t>(row_index), row).FromMaybe(false)) {
            return false;
        }
    }

    *out = rows;
    return true;
}

bool sqlite_v8_one_to_value(v8::Isolate* isolate, v8::Local<v8::Context> context,
                            const SlSqliteQueryOneResult* result, v8::Local<v8::Value>* out)
{
    v8::Local<v8::Object> row;

    if (result == nullptr || out == nullptr) {
        return false;
    }

    if (!result->found) {
        *out = v8::Null(isolate);
        return true;
    }

    row = v8::Object::New(isolate);
    for (size_t column_index = 0U; column_index < result->column_count; column_index += 1U) {
        if (!sqlite_v8_set_cell(isolate, context, row, result->column_names[column_index],
                                &result->values[column_index]))
        {
            return false;
        }
    }

    *out = row;
    return true;
}

void sqlite_v8_open_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    constexpr size_t scratch_size = 65536U;
    unsigned char scratch[scratch_size];
    SlArena arena = {};
    SlDiag diag = {};
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string path;
    SlSqliteOpenOptions options = {};
    SlSqliteConnection* connection = nullptr;
    SlResourceId id = sl_resource_id_invalid();
    v8::Local<v8::Object> handle;

    if (backend == nullptr || args.Length() != 1 ||
        !sqlite_v8_parse_open_options(isolate, context, args[0], &path, &options.access))
    {
        sqlite_v8_throw_type_error(isolate, "__sloppy.data.sqlite.open requires a non-empty path "
                                            "and optional read/readwrite access");
        return;
    }

    if (!sl_status_is_ok(sl_arena_init(&arena, scratch, sizeof(scratch)))) {
        sqlite_v8_throw_error(isolate, "sqlite bridge scratch arena initialization failed");
        return;
    }

    connection = new (std::nothrow) SlSqliteConnection{};
    if (connection == nullptr) {
        sqlite_v8_throw_error(isolate, "sqlite bridge could not allocate a connection resource");
        return;
    }

    options.path = sl_str_from_parts(path.data(), path.size());
    SlStatus status = sl_sqlite_open(&arena, &options, connection, &diag);
    if (!sl_status_is_ok(status)) {
        delete connection;
        sqlite_v8_throw_diag(isolate, "sqlite open failed", diag);
        return;
    }

    status =
        sl_resource_table_insert(&backend->resources, SL_RESOURCE_KIND_SQLITE_CONNECTION,
                                 connection, sqlite_v8_connection_cleanup, nullptr, &id, &diag);
    if (!sl_status_is_ok(status)) {
        sqlite_v8_connection_cleanup(connection, nullptr);
        sqlite_v8_throw_diag(isolate, "sqlite resource registration failed", diag);
        return;
    }

    if (!sqlite_v8_make_resource_handle(isolate, context, id, &handle)) {
        (void)sl_resource_table_close(&backend->resources, id, nullptr);
        sqlite_v8_throw_error(isolate, "sqlite bridge could not create a resource handle");
        return;
    }

    args.GetReturnValue().Set(handle);
}

void sqlite_v8_close_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    SlResourceId id = {};
    SlDiag diag = {};
    void* ptr = nullptr;

    if (backend == nullptr || args.Length() != 1 ||
        !sqlite_v8_get_resource_id(isolate, context, args[0], &id))
    {
        sqlite_v8_throw_type_error(isolate,
                                   "__sloppy.data.sqlite.close requires a resource handle");
        return;
    }

    SlStatus status = sl_resource_table_get(&backend->resources, id,
                                            SL_RESOURCE_KIND_SQLITE_CONNECTION, &ptr, &diag);
    if (!sl_status_is_ok(status)) {
        sqlite_v8_throw_diag(isolate, "sqlite close failed", diag);
        return;
    }

    status = sl_resource_table_close(&backend->resources, id, &diag);
    if (!sl_status_is_ok(status)) {
        sqlite_v8_throw_diag(isolate, "sqlite close failed", diag);
        return;
    }

    args.GetReturnValue().Set(v8::Undefined(isolate));
}

void sqlite_v8_exec_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    constexpr size_t scratch_size = 65536U;
    unsigned char scratch[scratch_size];
    SlArena arena = {};
    SlDiag diag = {};
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string sql;
    std::vector<std::string> text_storage;
    std::vector<SlSqliteParam> params;
    SlSqliteExecResult result = {};
    SlSqliteConnection* connection = nullptr;

    if (backend == nullptr || args.Length() < 2 || args.Length() > 3 ||
        !sqlite_v8_value_to_std_string(isolate, args[1], &sql) || sql.empty())
    {
        sqlite_v8_throw_type_error(
            isolate,
            "__sloppy.data.sqlite.exec requires a handle, SQL string, and optional params");
        return;
    }

    connection = sqlite_v8_lookup_connection(isolate, context, backend, args[0]);
    if (connection == nullptr ||
        !sqlite_v8_convert_params(isolate, context,
                                  args.Length() == 3 ? args[2] : v8::Undefined(isolate),
                                  &text_storage, &params))
    {
        return;
    }

    if (!sl_status_is_ok(sl_arena_init(&arena, scratch, sizeof(scratch)))) {
        sqlite_v8_throw_error(isolate, "sqlite bridge scratch arena initialization failed");
        return;
    }

    SlStatus status =
        sl_sqlite_exec(&arena, connection, sl_str_from_parts(sql.data(), sql.size()),
                       params.empty() ? nullptr : params.data(), params.size(), &result, &diag);
    if (!sl_status_is_ok(status)) {
        sqlite_v8_throw_diag(isolate, "sqlite exec failed", diag);
        return;
    }

    v8::Local<v8::Object> output = v8::Object::New(isolate);
    v8::Local<v8::String> key;
    if (!sl_status_is_ok(
            sqlite_v8_to_local_string(isolate, sl_str_from_cstr("affectedRows"), &key)) ||
        !output->Set(context, key, v8::Int32::New(isolate, result.changes)).FromMaybe(false))
    {
        sqlite_v8_throw_error(isolate, "sqlite bridge could not create exec result");
        return;
    }

    args.GetReturnValue().Set(output);
}

void sqlite_v8_query_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    constexpr size_t scratch_size = 131072U;
    unsigned char scratch[scratch_size];
    SlArena arena = {};
    SlDiag diag = {};
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string sql;
    std::vector<std::string> text_storage;
    std::vector<SlSqliteParam> params;
    SlSqliteResult result = {};
    SlSqliteConnection* connection = nullptr;
    v8::Local<v8::Array> rows;

    if (backend == nullptr || args.Length() < 2 || args.Length() > 3 ||
        !sqlite_v8_value_to_std_string(isolate, args[1], &sql) || sql.empty())
    {
        sqlite_v8_throw_type_error(
            isolate,
            "__sloppy.data.sqlite.query requires a handle, SQL string, and optional params");
        return;
    }

    connection = sqlite_v8_lookup_connection(isolate, context, backend, args[0]);
    if (connection == nullptr ||
        !sqlite_v8_convert_params(isolate, context,
                                  args.Length() == 3 ? args[2] : v8::Undefined(isolate),
                                  &text_storage, &params))
    {
        return;
    }

    if (!sl_status_is_ok(sl_arena_init(&arena, scratch, sizeof(scratch)))) {
        sqlite_v8_throw_error(isolate, "sqlite bridge scratch arena initialization failed");
        return;
    }

    SlStatus status = sl_sqlite_query(&arena, connection, sl_str_from_parts(sql.data(), sql.size()),
                                      params.empty() ? nullptr : params.data(), params.size(),
                                      nullptr, &result, &diag);
    if (!sl_status_is_ok(status)) {
        sqlite_v8_throw_diag(isolate, "sqlite query failed", diag);
        return;
    }

    if (!sqlite_v8_result_to_array(isolate, context, &result, &rows)) {
        sqlite_v8_throw_error(isolate, "sqlite bridge could not materialize query rows");
        return;
    }

    args.GetReturnValue().Set(rows);
}

void sqlite_v8_query_one_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    constexpr size_t scratch_size = 65536U;
    unsigned char scratch[scratch_size];
    SlArena arena = {};
    SlDiag diag = {};
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string sql;
    std::vector<std::string> text_storage;
    std::vector<SlSqliteParam> params;
    SlSqliteQueryOneResult result = {};
    SlSqliteConnection* connection = nullptr;
    v8::Local<v8::Value> row;

    if (backend == nullptr || args.Length() < 2 || args.Length() > 3 ||
        !sqlite_v8_value_to_std_string(isolate, args[1], &sql) || sql.empty())
    {
        sqlite_v8_throw_type_error(
            isolate,
            "__sloppy.data.sqlite.queryOne requires a handle, SQL string, and optional params");
        return;
    }

    connection = sqlite_v8_lookup_connection(isolate, context, backend, args[0]);
    if (connection == nullptr ||
        !sqlite_v8_convert_params(isolate, context,
                                  args.Length() == 3 ? args[2] : v8::Undefined(isolate),
                                  &text_storage, &params))
    {
        return;
    }

    if (!sl_status_is_ok(sl_arena_init(&arena, scratch, sizeof(scratch)))) {
        sqlite_v8_throw_error(isolate, "sqlite bridge scratch arena initialization failed");
        return;
    }

    SlStatus status = sl_sqlite_query_one(
        &arena, connection, sl_str_from_parts(sql.data(), sql.size()),
        params.empty() ? nullptr : params.data(), params.size(), &result, &diag);
    if (!sl_status_is_ok(status)) {
        sqlite_v8_throw_diag(isolate, "sqlite queryOne failed", diag);
        return;
    }

    if (!sqlite_v8_one_to_value(isolate, context, &result, &row)) {
        sqlite_v8_throw_error(isolate, "sqlite bridge could not materialize queryOne row");
        return;
    }

    args.GetReturnValue().Set(row);
}

bool sqlite_v8_set_function(v8::Isolate* isolate, v8::Local<v8::Context> context,
                            v8::Local<v8::Object> object, const char* name,
                            v8::FunctionCallback callback)
{
    v8::Local<v8::String> key;
    v8::Local<v8::FunctionTemplate> function_template;
    v8::Local<v8::Function> function;

    if (!sl_status_is_ok(sqlite_v8_to_local_string(isolate, sl_str_from_cstr(name), &key))) {
        return false;
    }

    function_template = v8::FunctionTemplate::New(isolate, callback);
    if (!function_template->GetFunction(context).ToLocal(&function)) {
        return false;
    }

    return object->Set(context, key, function).FromMaybe(false);
}

} /* namespace */

bool sl_v8_install_sqlite_intrinsics(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                     v8::Local<v8::Object> data)
{
    v8::Local<v8::String> sqlite_key;
    v8::Local<v8::Object> sqlite = v8::Object::New(isolate);

    if (!sl_status_is_ok(
            sqlite_v8_to_local_string(isolate, sl_str_from_cstr("sqlite"), &sqlite_key)))
    {
        return false;
    }

    if (!sqlite_v8_set_function(isolate, context, sqlite, "open", sqlite_v8_open_callback) ||
        !sqlite_v8_set_function(isolate, context, sqlite, "close", sqlite_v8_close_callback) ||
        !sqlite_v8_set_function(isolate, context, sqlite, "exec", sqlite_v8_exec_callback) ||
        !sqlite_v8_set_function(isolate, context, sqlite, "query", sqlite_v8_query_callback) ||
        !sqlite_v8_set_function(isolate, context, sqlite, "queryOne",
                                sqlite_v8_query_one_callback) ||
        !data->Set(context, sqlite_key, sqlite).FromMaybe(false))
    {
        return false;
    }

    return true;
}
