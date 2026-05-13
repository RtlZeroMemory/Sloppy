/*
 * src/engine/v8/engine_v8.cc
 *
 * Implements the V8-backed engine path behind Sloppy's engine-neutral C ABI. It owns
 * isolate/context lifetime, installs engine-level intrinsics, evaluates classic JavaScript
 * source strings, calls handlers, and orchestrates bounded Promise microtask drains. Framework
 * HTTP context/result conversion and provider-specific JS-to-native bridges live in sibling
 * modules under src/engine/v8/.
 *
 * Safety invariants:
 * - no V8 handle, value, or type escapes this file;
 * - JS never receives raw native pointers;
 * - result strings are copied out of V8 before returning to C;
 * - Promise settlement and microtask drains happen only on the isolate owner thread;
 * - one owner thread creates and enters each isolate/context; wrong-thread entry fails
 *   before touching V8 state;
 * - V8's required process-wide platform state is initialized once, kept for process
 *   lifetime, and private to this module because runtime shutdown semantics are not yet
 *   defined.
 *
 * Tests: tests/unit/engine/test_v8_smoke.c when SLOPPY_ENABLE_V8 is enabled.
 */
#include "engine_v8_internal.h"
#include "string_interop.h"

#include "sloppy/breadcrumbs.h"
#include "sloppy/http_profile.h"

#include "sloppy/data_sqlite.h"

#include <libplatform/libplatform.h>
#include <v8.h>
#include <yyjson.h>

#include <atomic>
#include <cstdint>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace {

std::mutex g_v8_platform_mutex;
v8::Platform* g_v8_platform = nullptr;
bool g_v8_platform_initialized = false;
constexpr size_t kSlV8MicrotaskDrainLimit = 1024U;
constexpr const char* kSlV8CodeCacheEnv = "SLOPPY_V8_CODE_CACHE_DIR";
constexpr const char* kSlV8StartupSnapshotEnv = "SLOPPY_V8_SNAPSHOT_DIR";

struct SlV8MicrotaskDrainGuard
{
    size_t ran = 0U;
    bool exceeded = false;
};

struct SlV8SourceMapLocation
{
    bool mapped = false;
    bool malformed = false;
    std::string path;
    size_t line = 0U;
    size_t column = 0U;
};

struct SlV8CodeCacheProbe
{
    bool enabled = false;
    std::filesystem::path path;
    std::vector<uint8_t> bytes;
};

struct SlV8StartupSnapshotProbe
{
    bool enabled = false;
    bool active = false;
    std::filesystem::path path;
    std::vector<uint8_t> bytes;
    v8::StartupData blob = {};
};

thread_local SlV8MicrotaskDrainGuard* g_sl_v8_microtask_guard = nullptr;

SlV8Engine* sl_v8_backend(SlEngine* engine);
void sl_v8_microtask_drain_promise_hook(v8::PromiseHookType type, v8::Local<v8::Promise> promise,
                                        v8::Local<v8::Value> parent);
void sl_v8_register_handler_callback(const v8::FunctionCallbackInfo<v8::Value>& args);
bool sl_v8_install_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context);

std::vector<intptr_t> sl_v8_make_external_references()
{
    std::vector<intptr_t> refs;
    refs.reserve(160U);
    refs.push_back(reinterpret_cast<intptr_t>(sl_v8_register_handler_callback));
    sl_v8_append_provider_external_references(&refs);
    sl_v8_append_fs_external_references(&refs);
    sl_v8_append_time_external_references(&refs);
    sl_v8_append_crypto_external_references(&refs);
    sl_v8_append_net_external_references(&refs);
    sl_v8_append_os_external_references(&refs);
    sl_v8_append_codec_external_references(&refs);
    sl_v8_append_workers_external_references(&refs);
    refs.push_back(0);
    return refs;
}

const intptr_t* sl_v8_external_references()
{
    static const std::vector<intptr_t> refs = sl_v8_make_external_references();
    return refs.data();
}

void sl_v8_reset_http_bridge_caches(SlV8Engine* backend)
{
    if (backend == nullptr) {
        return;
    }

    for (v8::Global<v8::String>& value : backend->http_strings) {
        value.Reset();
    }
    for (v8::Global<v8::Private>& value : backend->http_private_keys) {
        value.Reset();
    }
    for (v8::Global<v8::Function>& value : backend->http_functions) {
        value.Reset();
    }
    backend->http_log_noop_function.Reset();
    for (v8::Global<v8::Object>& value : backend->http_prototypes) {
        value.Reset();
    }
}

void sl_v8_reset_db_bridge_caches(SlV8Engine* backend)
{
    if (backend == nullptr) {
        return;
    }

    for (v8::Global<v8::String>& value : backend->db_strings) {
        value.Reset();
    }
    for (v8::Global<v8::Private>& value : backend->db_private_keys) {
        value.Reset();
    }
}

SlStr sl_v8_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

SlStr sl_v8_str_from_string(const std::string& str)
{
    return sl_str_from_parts(str.data(), str.size());
}

SlStatus sl_v8_write_diag(SlArena* arena, SlDiag* out_diag, SlDiagCode code,
                          SlStatusCode failure_code, SlStr message, SlStr source_name, SlStr hint)
{
    SlDiagBuilder builder;
    SlStatus status;

    if (out_diag == nullptr) {
        return sl_status_from_code(failure_code);
    }

    if (arena == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_diag_builder_init(&builder, arena, SL_DIAG_SEVERITY_ERROR, code, message);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (source_name.length != 0U) {
        status = sl_diag_builder_set_primary_span(&builder,
                                                  sl_source_span_make(source_name, 1U, 1U, 0U));
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    if (hint.length != 0U) {
        status = sl_diag_builder_add_hint(&builder, hint);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    status = sl_diag_builder_finish(&builder, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_status_from_code(failure_code);
}

SlStatus sl_v8_write_diag_with_span(SlArena* arena, SlDiag* out_diag, SlDiagCode code,
                                    SlStatusCode failure_code, SlStr message, SlSourceSpan span,
                                    SlStr hint, const std::string& stack_summary)
{
    SlDiagBuilder builder;
    SlStatus status;

    if (out_diag == nullptr) {
        return sl_status_from_code(failure_code);
    }

    if (arena == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_diag_builder_init(&builder, arena, SL_DIAG_SEVERITY_ERROR, code, message);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (span.path.length != 0U || span.has_location) {
        status = sl_diag_builder_set_primary_span(&builder, span);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    if (!stack_summary.empty()) {
        status = sl_diag_builder_add_related(&builder, span, sl_v8_str_from_string(stack_summary));
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    if (hint.length != 0U) {
        status = sl_diag_builder_add_hint(&builder, hint);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    status = sl_diag_builder_finish(&builder, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_status_from_code(failure_code);
}

SlStatus sl_v8_write_diag_string(SlArena* arena, SlDiag* out_diag, SlDiagCode code,
                                 SlStatusCode failure_code, const std::string& message,
                                 SlStr source_name, SlStr hint)
{
    SlStr copied_message = {};
    SlStatus status;

    if (out_diag == nullptr) {
        return sl_status_from_code(failure_code);
    }

    status = sl_v8_std_string_copy_to_arena(arena, message, &copied_message);

    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_v8_write_diag(arena, out_diag, code, failure_code, copied_message, source_name, hint);
}

SlStatus sl_v8_write_diag_string_with_span(SlArena* arena, SlDiag* out_diag, SlDiagCode code,
                                           SlStatusCode failure_code, const std::string& message,
                                           SlSourceSpan span, SlStr hint,
                                           const std::string& stack_summary)
{
    SlStr copied_message = {};
    SlStatus status;

    if (out_diag == nullptr) {
        return sl_status_from_code(failure_code);
    }

    status = sl_v8_std_string_copy_to_arena(arena, message, &copied_message);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_v8_write_diag_with_span(arena, out_diag, code, failure_code, copied_message, span,
                                      hint, stack_summary);
}

std::string sl_v8_value_to_string(v8::Isolate* isolate, v8::Local<v8::Value> value)
{
    std::string text;

    if (!sl_v8_std_string_from_value(isolate, value, &text)) {
        return std::string("JavaScript exception");
    }

    return text;
}

std::string sl_v8_maybe_value_to_string(v8::Isolate* isolate, v8::Local<v8::Value> value)
{
    if (value.IsEmpty() || value->IsUndefined() || value->IsNull()) {
        return std::string();
    }

    return sl_v8_value_to_string(isolate, value);
}

std::string sl_v8_exception_message(v8::Isolate* isolate, v8::TryCatch& try_catch,
                                    const char* fallback)
{
    if (!try_catch.HasCaught()) {
        return std::string(fallback);
    }

    return sl_v8_value_to_string(isolate, try_catch.Exception());
}

std::string sl_v8_stack_summary(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                v8::TryCatch& try_catch)
{
    v8::Local<v8::Value> stack;
    std::string summary;
    const size_t max_stack_summary = 512U;

    if (!try_catch.StackTrace(context).ToLocal(&stack)) {
        return std::string();
    }

    summary = "JavaScript stack: " + sl_v8_value_to_string(isolate, stack);
    if (summary.size() > max_stack_summary) {
        summary.resize(max_stack_summary);
        summary += "...";
    }

    return summary;
}

bool sl_v8_str_equals_string(SlStr left, const std::string& right)
{
    return sl_str_equal(left, sl_str_from_parts(right.data(), right.size()));
}

uint64_t sl_v8_hash_step(uint64_t hash, unsigned char byte)
{
    return (hash ^ static_cast<uint64_t>(byte)) * UINT64_C(1099511628211);
}

uint64_t sl_v8_hash_bytes(uint64_t hash, const void* data, size_t length)
{
    const unsigned char* bytes = static_cast<const unsigned char*>(data);

    for (size_t index = 0U; index < length; index += 1U) {
        hash = sl_v8_hash_step(hash, bytes[index]);
    }
    return hash;
}

uint64_t sl_v8_script_cache_key(SlStr source_name, SlStr source)
{
    const char* v8_version = v8::V8::GetVersion();
    uint64_t hash = UINT64_C(1469598103934665603);

    hash = sl_v8_hash_bytes(hash, "sloppy-v8-script-cache-v1",
                            sizeof("sloppy-v8-script-cache-v1") - 1U);
    hash = sl_v8_hash_step(hash, 0U);
    if (v8_version != nullptr) {
        SlStr version = sl_str_from_cstr(v8_version);
        hash = sl_v8_hash_bytes(hash, version.ptr, version.length);
    }
    hash = sl_v8_hash_step(hash, 0U);
    hash = sl_v8_hash_bytes(hash, source_name.ptr, source_name.length);
    hash = sl_v8_hash_step(hash, 0U);
    hash = sl_v8_hash_bytes(hash, source.ptr, source.length);
    return hash;
}

std::string sl_v8_hex_u64(uint64_t value)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string text(16U, '0');

    for (size_t index = 0U; index < text.size(); index += 1U) {
        unsigned int shift = static_cast<unsigned int>((text.size() - index - 1U) * 4U);
        text[index] = digits[(value >> shift) & 0xFU];
    }
    return text;
}

std::string sl_v8_env_string(const char* name)
{
    std::string dir;

    if (name == nullptr) {
        return dir;
    }
#ifdef _WIN32
    size_t required = 0U;
    if (getenv_s(&required, nullptr, 0U, name) == 0 && required > 0U) {
        dir.resize(required);
        if (getenv_s(&required, dir.data(), dir.size(), name) != 0 || required == 0U) {
            dir.clear();
        }
        else {
            dir.resize(required - 1U);
        }
    }
#else
    const char* value = std::getenv(name);
    if (value != nullptr) {
        dir = value;
    }
#endif
    return dir;
}

SlV8CodeCacheProbe sl_v8_code_cache_probe(SlStr source_name, SlStr source)
{
    SlV8CodeCacheProbe probe;
    std::string dir = sl_v8_env_string(kSlV8CodeCacheEnv);

    if (dir.empty()) {
        return probe;
    }

    probe.enabled = true;
    probe.path =
        std::filesystem::path(dir) /
        ("sloppy-v8-" + sl_v8_hex_u64(sl_v8_script_cache_key(source_name, source)) + ".cache");

    std::ifstream input(probe.path, std::ios::binary);
    if (!input) {
        return probe;
    }
    input.seekg(0, std::ios::end);
    std::streamoff length = input.tellg();
    if (length <= 0 ||
        static_cast<uint64_t>(length) > static_cast<uint64_t>(std::numeric_limits<int>::max()))
    {
        probe.bytes.clear();
        return probe;
    }
    input.seekg(0, std::ios::beg);
    probe.bytes.resize(static_cast<size_t>(length));
    input.read(reinterpret_cast<char*>(probe.bytes.data()), length);
    if (!input) {
        probe.bytes.clear();
    }
    return probe;
}

void sl_v8_code_cache_store(const SlV8CodeCacheProbe& probe,
                            const v8::ScriptCompiler::CachedData* data)
{
    if (!probe.enabled || data == nullptr || data->data == nullptr || data->length <= 0) {
        return;
    }

    std::error_code error;
    std::filesystem::create_directories(probe.path.parent_path(), error);
    if (error) {
        return;
    }

    std::ofstream output(probe.path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return;
    }
    output.write(reinterpret_cast<const char*>(data->data), data->length);
}

uint64_t sl_v8_runtime_feature_mask(const SlEngineOptions* options)
{
    return options == nullptr || options->runtime_features == nullptr
               ? UINT64_C(0)
               : options->runtime_features->active_mask;
}

bool sl_v8_startup_snapshot_supported(const SlEngineOptions* options)
{
    return options != nullptr && options->runtime_features != nullptr;
}

uint64_t sl_v8_startup_snapshot_key(const SlEngineOptions* options)
{
    const char* v8_version = v8::V8::GetVersion();
    uint64_t mask = sl_v8_runtime_feature_mask(options);
    uint64_t hash = UINT64_C(1469598103934665603);

    hash = sl_v8_hash_bytes(hash, "sloppy-v8-startup-snapshot-v1",
                            sizeof("sloppy-v8-startup-snapshot-v1") - 1U);
    hash = sl_v8_hash_step(hash, 0U);
    if (v8_version != nullptr) {
        SlStr version = sl_str_from_cstr(v8_version);
        hash = sl_v8_hash_bytes(hash, version.ptr, version.length);
    }
    hash = sl_v8_hash_step(hash, 0U);
    for (size_t index = 0U; index < sizeof(mask); index += 1U) {
        hash = sl_v8_hash_step(hash, static_cast<unsigned char>((mask >> (index * 8U)) & 0xFFU));
    }
    return hash;
}

bool sl_v8_read_file(const std::filesystem::path& path, std::vector<uint8_t>* out)
{
    if (out == nullptr) {
        return false;
    }
    out->clear();
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    input.seekg(0, std::ios::end);
    std::streamoff length = input.tellg();
    if (length <= 0 ||
        static_cast<uint64_t>(length) > static_cast<uint64_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }
    input.seekg(0, std::ios::beg);
    out->resize(static_cast<size_t>(length));
    input.read(reinterpret_cast<char*>(out->data()), length);
    if (!input) {
        out->clear();
        return false;
    }
    return true;
}

bool sl_v8_write_file(const std::filesystem::path& path, const char* data, int length)
{
    static std::atomic<uint64_t> temp_counter{0U};

    if (data == nullptr || length <= 0) {
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }

    uint64_t temp_id = temp_counter.fetch_add(1U, std::memory_order_relaxed);
    uint64_t tick =
        static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::path temp_path = path;
    temp_path += ".";
    temp_path += std::to_string(tick);
    temp_path += ".";
    temp_path += std::to_string(temp_id);
    temp_path += ".tmp";

    {
        std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            std::filesystem::remove(temp_path, error);
            return false;
        }
        output.write(data, length);
        if (!output) {
            std::filesystem::remove(temp_path, error);
            return false;
        }
    }
    std::filesystem::rename(temp_path, path, error);
    if (error) {
        std::filesystem::remove(temp_path, error);
        return false;
    }
    return true;
}

bool sl_v8_build_startup_snapshot(const SlEngineOptions* options, std::vector<uint8_t>* out)
{
    if (out == nullptr) {
        return false;
    }
    std::unique_ptr<v8::ArrayBuffer::Allocator> allocator(
        v8::ArrayBuffer::Allocator::NewDefaultAllocator());
    if (allocator == nullptr) {
        return false;
    }

    v8::Isolate::CreateParams params;
    params.array_buffer_allocator = allocator.get();
    params.external_references = sl_v8_external_references();
    v8::StartupData blob = {};
    bool installed = false;
    {
        v8::SnapshotCreator creator(params);
        v8::Isolate* isolate = creator.GetIsolate();
        SlV8Engine snapshot_backend;
        snapshot_backend.allocator = allocator.get();
        snapshot_backend.isolate = isolate;
        snapshot_backend.owner_thread = std::this_thread::get_id();
        snapshot_backend.has_runtime_features =
            options != nullptr && options->runtime_features != nullptr;
        if (snapshot_backend.has_runtime_features) {
            snapshot_backend.runtime_features = *options->runtime_features;
        }

        {
            v8::HandleScope handle_scope(isolate);
            isolate->SetData(0, &snapshot_backend);
            isolate->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);
            isolate->SetPromiseHook(sl_v8_microtask_drain_promise_hook);
            v8::Local<v8::Context> context = v8::Context::New(isolate);
            v8::Context::Scope context_scope(context);
            installed = sl_v8_install_intrinsics(&snapshot_backend, context);
            if (installed) {
                creator.SetDefaultContext(context);
            }
            isolate->SetData(0, nullptr);
        }
        if (installed) {
            blob = creator.CreateBlob(v8::SnapshotCreator::FunctionCodeHandling::kKeep);
        }
    }

    std::unique_ptr<const char[]> blob_data(blob.data);
    if (blob.data == nullptr || blob.raw_size <= 0) {
        return false;
    }
    out->assign(reinterpret_cast<const uint8_t*>(blob.data),
                reinterpret_cast<const uint8_t*>(blob.data) + blob.raw_size);
    return !out->empty();
}

SlV8StartupSnapshotProbe sl_v8_startup_snapshot_probe(const SlEngineOptions* options)
{
    SlV8StartupSnapshotProbe probe;
    std::string dir = sl_v8_env_string(kSlV8StartupSnapshotEnv);

    if (dir.empty() || !sl_v8_startup_snapshot_supported(options)) {
        return probe;
    }

    probe.enabled = true;
    probe.path =
        std::filesystem::path(dir) /
        ("sloppy-v8-startup-" + sl_v8_hex_u64(sl_v8_startup_snapshot_key(options)) + ".blob");

    if (!sl_v8_read_file(probe.path, &probe.bytes)) {
        if (!sl_v8_build_startup_snapshot(options, &probe.bytes)) {
            probe.bytes.clear();
            return probe;
        }
        sl_v8_write_file(probe.path, reinterpret_cast<const char*>(probe.bytes.data()),
                         static_cast<int>(probe.bytes.size()));
    }

    probe.blob.data = reinterpret_cast<const char*>(probe.bytes.data());
    probe.blob.raw_size = static_cast<int>(probe.bytes.size());
    probe.active = probe.blob.data != nullptr && probe.blob.raw_size > 0;
    return probe;
}

int sl_v8_source_map_base64_value(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 26;
    }
    if (ch >= '0' && ch <= '9') {
        return ch - '0' + 52;
    }
    if (ch == '+') {
        return 62;
    }
    if (ch == '/') {
        return 63;
    }
    return -1;
}

bool sl_v8_source_map_decode_vlq(const char* mappings, size_t length, size_t* cursor, int64_t* out)
{
    uint64_t value = 0U;
    unsigned int shift = 0U;

    if (mappings == nullptr || cursor == nullptr || out == nullptr) {
        return false;
    }

    while (*cursor < length) {
        int digit = sl_v8_source_map_base64_value(mappings[*cursor]);
        bool continuation = false;

        if (digit < 0) {
            return false;
        }

        *cursor += 1U;
        continuation = (digit & 32) != 0;
        digit &= 31;
        if (shift >= 64U ||
            static_cast<uint64_t>(digit) > (std::numeric_limits<uint64_t>::max() >> shift))
        {
            return false;
        }
        uint64_t part = static_cast<uint64_t>(digit) << shift;
        if (value > std::numeric_limits<uint64_t>::max() - part) {
            return false;
        }
        value += part;
        shift += 5U;

        if (!continuation) {
            uint64_t magnitude = value >> 1U;
            int64_t decoded = 0;
            if (magnitude > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
                return false;
            }
            decoded = static_cast<int64_t>(magnitude);
            if ((value & 1) != 0) {
                decoded = -decoded;
            }
            *out = decoded;
            return true;
        }
    }

    return false;
}

static bool sl_v8_checked_add_i64(int64_t left, int64_t right, int64_t* out)
{
    if (out == nullptr) {
        return false;
    }
    if ((right > 0 && left > std::numeric_limits<int64_t>::max() - right) ||
        (right < 0 && left < std::numeric_limits<int64_t>::min() - right))
    {
        return false;
    }
    *out = left + right;
    return true;
}

static constexpr int64_t kSlV8MaxTrustedSourceMapPosition = 1LL << 30;

static bool sl_v8_i64_to_zero_based_size(int64_t value, size_t* out)
{
    if (out == nullptr || value < 0 || value > kSlV8MaxTrustedSourceMapPosition ||
        static_cast<uint64_t>(value) > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    {
        return false;
    }
    *out = static_cast<size_t>(value);
    return true;
}

static bool sl_v8_i64_to_one_based_size(int64_t value, size_t* out)
{
    if (out == nullptr || value < 0 || value > kSlV8MaxTrustedSourceMapPosition ||
        static_cast<uint64_t>(value) >= static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    {
        return false;
    }
    *out = static_cast<size_t>(value) + 1U;
    return true;
}

void sl_v8_source_map_cache_dispose(SlV8Engine* backend)
{
    if (backend == nullptr) {
        return;
    }
    if (backend->source_map_cache.doc != nullptr) {
        yyjson_doc_free(backend->source_map_cache.doc);
    }
    backend->source_map_cache = SlV8SourceMapCache{};
}

bool sl_v8_source_map_read_sources(yyjson_val* root, std::vector<std::string>* sources)
{
    yyjson_val* source_array = yyjson_obj_get(root, "sources");
    yyjson_arr_iter iter;
    yyjson_val* value = nullptr;

    if (sources == nullptr || !yyjson_is_arr(source_array)) {
        return false;
    }

    yyjson_arr_iter_init(source_array, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != nullptr) {
        if (!yyjson_is_str(value)) {
            return false;
        }
        sources->emplace_back(yyjson_get_str(value), yyjson_get_len(value));
    }

    return !sources->empty();
}

bool sl_v8_source_map_cache_validation_failed(SlV8SourceMapCache* cache, SlV8SourceMapLocation* out)
{
    if (cache != nullptr) {
        if (cache->doc != nullptr) {
            yyjson_doc_free(cache->doc);
        }
        cache->doc = nullptr;
        cache->root = nullptr;
        cache->sources.clear();
        cache->mappings = nullptr;
        cache->mappings_length = 0U;
        cache->lines.clear();
        cache->parsed = false;
        cache->malformed = true;
    }
    if (out != nullptr) {
        out->malformed = true;
    }
    return false;
}

bool sl_v8_source_map_cache_parse(SlV8Engine* backend, SlV8SourceMapLocation* out)
{
    yyjson_read_err error = {};
    yyjson_val* mappings_value = nullptr;
    SlV8SourceMapCache* cache = backend == nullptr ? nullptr : &backend->source_map_cache;

    if (cache == nullptr || backend->source_map.length == 0U || backend->source_map.ptr == nullptr)
    {
        return false;
    }

    if (cache->parsed) {
        if (cache->malformed && out != nullptr) {
            out->malformed = true;
        }
        return !cache->malformed;
    }

    cache->malformed = false;
    cache->doc = yyjson_read_opts((char*)backend->source_map.ptr, backend->source_map.length, 0U,
                                  nullptr, &error);
    if (cache->doc == nullptr) {
        cache->parsed = true;
        cache->malformed = true;
        if (out != nullptr) {
            out->malformed = true;
        }
        return false;
    }

    cache->root = yyjson_doc_get_root(cache->doc);
    if (!yyjson_is_obj(cache->root)) {
        return sl_v8_source_map_cache_validation_failed(cache, out);
    }

    mappings_value = yyjson_obj_get(cache->root, "mappings");
    if (!yyjson_is_str(mappings_value) ||
        !sl_v8_source_map_read_sources(cache->root, &cache->sources))
    {
        return sl_v8_source_map_cache_validation_failed(cache, out);
    }

    cache->mappings = yyjson_get_str(mappings_value);
    cache->mappings_length = yyjson_get_len(mappings_value);
    cache->lines.push_back(SlV8SourceMapLineState{});
    cache->parsed = true;
    return true;
}

bool sl_v8_source_map_decode_segment(const SlV8SourceMapCache& cache, size_t* cursor,
                                     int64_t* previous_generated_column, int64_t* previous_source,
                                     int64_t* previous_original_line,
                                     int64_t* previous_original_column,
                                     size_t* out_generated_column, bool* out_has_original)
{
    int64_t generated_delta = 0;
    int64_t current_generated_column = 0;

    if (cursor == nullptr || previous_generated_column == nullptr || previous_source == nullptr ||
        previous_original_line == nullptr || previous_original_column == nullptr ||
        out_generated_column == nullptr || out_has_original == nullptr)
    {
        return false;
    }

    *out_has_original = false;
    if (*cursor < cache.mappings_length && cache.mappings[*cursor] == ',') {
        *cursor += 1U;
    }

    if (!sl_v8_source_map_decode_vlq(cache.mappings, cache.mappings_length, cursor,
                                     &generated_delta) ||
        !sl_v8_checked_add_i64(*previous_generated_column, generated_delta,
                               &current_generated_column) ||
        !sl_v8_i64_to_zero_based_size(current_generated_column, out_generated_column))
    {
        return false;
    }
    *previous_generated_column = current_generated_column;

    if (*cursor >= cache.mappings_length || cache.mappings[*cursor] == ',' ||
        cache.mappings[*cursor] == ';')
    {
        return true;
    }

    int64_t source_delta = 0;
    int64_t original_line_delta = 0;
    int64_t original_column_delta = 0;
    if (!sl_v8_source_map_decode_vlq(cache.mappings, cache.mappings_length, cursor,
                                     &source_delta) ||
        !sl_v8_source_map_decode_vlq(cache.mappings, cache.mappings_length, cursor,
                                     &original_line_delta) ||
        !sl_v8_source_map_decode_vlq(cache.mappings, cache.mappings_length, cursor,
                                     &original_column_delta))
    {
        return false;
    }

    if (!sl_v8_checked_add_i64(*previous_source, source_delta, previous_source) ||
        !sl_v8_checked_add_i64(*previous_original_line, original_line_delta,
                               previous_original_line) ||
        !sl_v8_checked_add_i64(*previous_original_column, original_column_delta,
                               previous_original_column) ||
        *previous_source < 0 || static_cast<size_t>(*previous_source) >= cache.sources.size() ||
        *previous_original_line < 0 || *previous_original_column < 0)
    {
        return false;
    }

    *out_has_original = true;
    return true;
}

bool sl_v8_source_map_ensure_line_state(SlV8SourceMapCache* cache, size_t target_line_index,
                                        SlV8SourceMapLocation* out)
{
    if (cache == nullptr || cache->mappings == nullptr || cache->lines.empty()) {
        return false;
    }

    while (cache->lines.size() <= target_line_index) {
        SlV8SourceMapLineState state = cache->lines.back();
        size_t cursor = state.offset;
        int64_t previous_generated_column = 0;

        while (cursor < cache->mappings_length && cache->mappings[cursor] != ';') {
            [[maybe_unused]] size_t generated_column = 0U;
            [[maybe_unused]] bool has_original = false;

            if (!sl_v8_source_map_decode_segment(
                    *cache, &cursor, &previous_generated_column, &state.previous_source,
                    &state.previous_original_line, &state.previous_original_column,
                    &generated_column, &has_original))
            {
                if (out != nullptr) {
                    out->malformed = true;
                }
                return false;
            }
        }

        if (cursor >= cache->mappings_length) {
            return false;
        }
        cursor += 1U;
        state.offset = cursor;
        cache->lines.push_back(state);
    }

    return true;
}

bool sl_v8_source_map_find_mapping(SlV8Engine* backend, size_t generated_line,
                                   size_t generated_column, SlV8SourceMapLocation* out)
{
    SlV8SourceMapCache* cache = backend == nullptr ? nullptr : &backend->source_map_cache;
    size_t line_index = 0U;
    SlV8SourceMapLineState state = {};
    size_t cursor = 0U;
    int64_t previous_generated_column = 0;

    if (out == nullptr || generated_line == 0U || generated_column == 0U ||
        !sl_v8_source_map_cache_parse(backend, out) || cache == nullptr)
    {
        return false;
    }

    line_index = generated_line - 1U;
    if (!sl_v8_source_map_ensure_line_state(cache, line_index, out) ||
        line_index >= cache->lines.size())
    {
        return false;
    }

    state = cache->lines[line_index];
    cursor = state.offset;
    while (cursor < cache->mappings_length && cache->mappings[cursor] != ';') {
        size_t current_generated_column = 0U;
        bool has_original = false;

        if (!sl_v8_source_map_decode_segment(*cache, &cursor, &previous_generated_column,
                                             &state.previous_source, &state.previous_original_line,
                                             &state.previous_original_column,
                                             &current_generated_column, &has_original))
        {
            out->malformed = true;
            return false;
        }
        if (current_generated_column > generated_column - 1U) {
            return out->mapped;
        }
        if (!has_original) {
            *out = SlV8SourceMapLocation{};
            continue;
        }

        size_t original_line = 0U;
        size_t original_column = 0U;
        if (!sl_v8_i64_to_one_based_size(state.previous_original_line, &original_line) ||
            !sl_v8_i64_to_one_based_size(state.previous_original_column, &original_column))
        {
            out->malformed = true;
            return false;
        }
        out->mapped = true;
        out->path = cache->sources[static_cast<size_t>(state.previous_source)];
        out->line = original_line;
        out->column = original_column;
    }

    return out->mapped;
}

bool sl_v8_source_map_applies(const SlV8Engine* backend, const std::string& source_name)
{
    if (backend == nullptr || backend->source_map.length == 0U ||
        backend->source_map.ptr == nullptr)
    {
        return false;
    }

    return backend->source_map_source_name.length == 0U ||
           sl_v8_str_equals_string(backend->source_map_source_name, source_name);
}

SlV8SourceMapLocation sl_v8_remap_generated_span(SlV8Engine* backend,
                                                 const SlSourceSpan& generated_span)
{
    SlV8SourceMapLocation result = {};
    std::string source_name(generated_span.path.ptr == nullptr ? "" : generated_span.path.ptr,
                            generated_span.path.length);

    if (!generated_span.has_location || generated_span.line == 0U || generated_span.column == 0U ||
        !sl_v8_source_map_applies(backend, source_name))
    {
        return result;
    }

    sl_v8_source_map_find_mapping(backend, generated_span.line, generated_span.column, &result);
    return result;
}

std::string sl_v8_message_source_name(v8::Isolate* isolate, v8::TryCatch& try_catch,
                                      SlStr fallback_source_name)
{
    v8::Local<v8::Message> message = try_catch.Message();
    std::string source_name;

    if (!message.IsEmpty()) {
        source_name = sl_v8_maybe_value_to_string(isolate, message->GetScriptResourceName());
    }

    if (!source_name.empty()) {
        return source_name;
    }

    if (fallback_source_name.length != 0U) {
        return std::string(fallback_source_name.ptr, fallback_source_name.length);
    }

    return std::string();
}

SlSourceSpan sl_v8_exception_span(v8::Local<v8::Context> context, v8::TryCatch& try_catch,
                                  const std::string& source_name)
{
    v8::Local<v8::Message> message = try_catch.Message();
    int line = 0;
    int start_column = -1;
    size_t sloppy_column = 0U;

    if (message.IsEmpty()) {
        return sl_source_span_make(sl_v8_str_from_string(source_name), 0U, 0U, 0U);
    }

    line = message->GetLineNumber(context).FromMaybe(0);
    start_column = message->GetStartColumn(context).FromMaybe(-1);
    if (start_column >= 0) {
        // V8 reports start columns as zero-based; Sloppy diagnostics store 1-based columns.
        sloppy_column = static_cast<size_t>(start_column) + 1U;
    }

    return sl_source_span_make(sl_v8_str_from_string(source_name),
                               line > 0 ? static_cast<size_t>(line) : 0U, sloppy_column, 0U);
}

SlStatus sl_v8_write_exception_diag(SlEngine* engine, SlDiag* out_diag, SlDiagCode code,
                                    SlStatusCode failure_code, v8::Isolate* isolate,
                                    v8::Local<v8::Context> context, v8::TryCatch& try_catch,
                                    SlStr fallback_source_name, const char* fallback_message,
                                    SlStr hint)
{
    std::string message = sl_v8_exception_message(isolate, try_catch, fallback_message);
    std::string source_name = sl_v8_message_source_name(isolate, try_catch, fallback_source_name);
    SlSourceSpan generated_span = sl_v8_exception_span(context, try_catch, source_name);
    SlSourceSpan span = generated_span;
    SlStr final_hint = hint;
    std::string stack_summary = sl_v8_stack_summary(isolate, context, try_catch);
    SlV8SourceMapLocation remapped =
        sl_v8_remap_generated_span(sl_v8_backend(engine), generated_span);

    if (remapped.mapped) {
        span = sl_source_span_make(sl_v8_str_from_string(remapped.path), remapped.line,
                                   remapped.column, generated_span.length);
        final_hint = sl_str_empty();
    }
    else if (remapped.malformed) {
        final_hint = sl_v8_literal(
            "Malformed source map; reporting the generated JavaScript location.",
            sizeof("Malformed source map; reporting the generated JavaScript location.") - 1U);
    }

    return sl_v8_write_diag_string_with_span(engine->arena, out_diag, code, failure_code, message,
                                             span, final_hint, stack_summary);
}

void sl_v8_microtask_drain_promise_hook(v8::PromiseHookType type, v8::Local<v8::Promise> promise,
                                        v8::Local<v8::Value> parent)
{
    SlV8MicrotaskDrainGuard* guard = g_sl_v8_microtask_guard;

    (void)promise;
    (void)parent;
    if (type != v8::PromiseHookType::kBefore || guard == nullptr || guard->exceeded) {
        return;
    }

    guard->ran += 1U;
    if (guard->ran <= kSlV8MicrotaskDrainLimit) {
        return;
    }

    guard->exceeded = true;
    if (v8::Isolate* isolate = v8::Isolate::TryGetCurrent()) {
        isolate->TerminateExecution();
    }
}

SlStatus sl_v8_write_missing_registered_handler_diag(SlEngine* engine, SlDiag* out_diag,
                                                     SlHandlerId handler_id)
{
    std::string message =
        "app plan references unregistered handler ID " + std::to_string(handler_id);
    return sl_v8_write_diag_string(
        engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE, message,
        sl_str_empty(),
        sl_v8_literal("Generated app modules must call __sloppy_register_handler(id, handler).",
                      sizeof("Generated app modules must call "
                             "__sloppy_register_handler(id, handler).") -
                          1U));
}

SlStatus sl_v8_write_wrong_thread_diag(SlEngine* engine, SlDiag* out_diag)
{
    return sl_v8_write_diag(
        engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE,
        sl_v8_literal("V8 engine entered from a non-owner thread",
                      sizeof("V8 engine entered from a non-owner thread") - 1U),
        sl_str_empty(),
        sl_v8_literal("Create, evaluate, call, validate, and dispose a V8 engine on its owner "
                      "thread. Worker threads must post work back to the owner thread.",
                      sizeof("Create, evaluate, call, validate, and dispose a V8 engine on its "
                             "owner thread. Worker threads must post work back to the owner "
                             "thread.") -
                          1U));
}

bool sl_v8_on_owner_thread(const SlV8Engine* backend)
{
    return backend != nullptr && backend->owner_thread == std::this_thread::get_id();
}

SlStatus sl_v8_check_owner_thread(SlEngine* engine, const SlV8Engine* backend, SlDiag* out_diag)
{
    if (!sl_v8_on_owner_thread(backend)) {
        return sl_v8_write_wrong_thread_diag(engine, out_diag);
    }

    return sl_status_ok();
}

SlStatus sl_v8_write_cancelled_diag(SlEngine* engine, const SlCancellationToken* cancellation,
                                    SlDiag* out_diag)
{
    SlCancellationReason reason = sl_cancellation_token_reason(cancellation);
    SlStatusCode status_code = sl_cancellation_status_code(reason);
    SlDiagCode diag_code = reason == SL_CANCELLATION_REASON_BACKPRESSURE
                               ? SL_DIAG_ENGINE_BACKPRESSURE
                               : SL_DIAG_ENGINE_CANCELLED;
    SlStr reason_name = sl_cancellation_reason_name(reason);
    std::string message = "JavaScript handler request was cancelled";

    if (reason == SL_CANCELLATION_REASON_DEADLINE_EXCEEDED) {
        message = "JavaScript handler request deadline was exceeded";
    }
    else if (reason == SL_CANCELLATION_REASON_BACKPRESSURE) {
        message = "JavaScript handler request was rejected by backpressure";
    }
    else if (reason == SL_CANCELLATION_REASON_SHUTDOWN) {
        message = "JavaScript handler request was cancelled during shutdown";
    }

    if (cancellation != nullptr && !sl_str_is_empty(cancellation->detail)) {
        message += ": ";
        message.append(cancellation->detail.ptr, cancellation->detail.length);
    }

    return sl_v8_write_diag_string(
        engine->arena, out_diag, diag_code, status_code, message, sl_str_empty(),
        reason == SL_CANCELLATION_REASON_NONE ? sl_str_empty() : reason_name);
}

SlStatus sl_v8_check_cancelled(SlEngine* engine, const SlCancellationToken* cancellation,
                               SlDiag* out_diag)
{
    if (!sl_cancellation_token_is_cancelled(cancellation)) {
        return sl_status_ok();
    }

    return sl_v8_write_cancelled_diag(engine, cancellation, out_diag);
}

SlStatus sl_v8_write_microtask_limit_diag(SlEngine* engine, SlDiag* out_diag)
{
    std::string message = "JavaScript microtask drain exceeded bounded checkpoint limit (";
    message += std::to_string(kSlV8MicrotaskDrainLimit);
    message += ")";

    return sl_v8_write_diag_string(
        engine->arena, out_diag, SL_DIAG_ENGINE_PROMISE_PENDING, SL_STATUS_DEADLINE_EXCEEDED,
        message, sl_str_empty(),
        sl_v8_literal("Break recursive Promise microtask chains with a real async boundary; "
                      "timers and arbitrary native async sources remain deferred.",
                      sizeof("Break recursive Promise microtask chains with a real async boundary; "
                             "timers and arbitrary native async sources remain deferred.") -
                          1U));
}

SlStatus sl_v8_drain_microtasks(SlEngine* engine, v8::Isolate* isolate,
                                v8::Local<v8::Context> context, SlDiag* out_diag)
{
    v8::TryCatch try_catch(isolate);
    SlV8MicrotaskDrainGuard guard;
    SlV8MicrotaskDrainGuard* previous_guard = g_sl_v8_microtask_guard;

    g_sl_v8_microtask_guard = &guard;

    isolate->PerformMicrotaskCheckpoint();
    g_sl_v8_microtask_guard = previous_guard;

    if (guard.exceeded) {
        isolate->CancelTerminateExecution();
        return sl_v8_write_microtask_limit_diag(engine, out_diag);
    }

    if (try_catch.HasCaught()) {
        return sl_v8_write_exception_diag(
            engine, out_diag, SL_DIAG_ENGINE_EXCEPTION, SL_STATUS_INVALID_STATE, isolate, context,
            try_catch, sl_str_empty(), "JavaScript microtask failed",
            sl_v8_literal("V8 microtasks are drained only on the owning engine thread.",
                          sizeof("V8 microtasks are drained only on the owning engine thread.") -
                              1U));
    }

    return sl_status_ok();
}

void sl_v8_register_handler_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);

    if (backend == nullptr) {
        isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8Literal(
            isolate, "__sloppy_register_handler requires handler ID and handler function")));
        return;
    }

    if (args.Length() != 2) {
        sl_v8_throw_type_error_from_native_view(
            backend,
            sl_v8_literal(
                "__sloppy_register_handler requires handler ID and handler function",
                sizeof("__sloppy_register_handler requires handler ID and handler function") - 1U));
        return;
    }

    v8::Local<v8::Value> id_value = args[0];
    v8::Local<v8::Value> handler_value = args[1];

    if (!id_value->IsUint32()) {
        sl_v8_throw_type_error_from_native_view(
            backend,
            sl_v8_literal(
                "__sloppy_register_handler handler ID must be a positive integer",
                sizeof("__sloppy_register_handler handler ID must be a positive integer") - 1U));
        return;
    }

    uint32_t handler_id = id_value.As<v8::Uint32>()->Value();
    if (handler_id == 0U) {
        sl_v8_throw_type_error_from_native_view(
            backend,
            sl_v8_literal(
                "__sloppy_register_handler handler ID must be a positive integer",
                sizeof("__sloppy_register_handler handler ID must be a positive integer") - 1U));
        return;
    }

    if (!handler_value->IsFunction()) {
        sl_v8_throw_type_error_from_native_view(
            backend,
            sl_v8_literal("__sloppy_register_handler handler must be callable",
                          sizeof("__sloppy_register_handler handler must be callable") - 1U));
        return;
    }

    if (backend->pending_handlers == nullptr) {
        sl_v8_throw_error_from_native_view(
            backend,
            sl_v8_literal("__sloppy_register_handler is only valid during app evaluation",
                          sizeof("__sloppy_register_handler is only valid during app evaluation") -
                              1U));
        return;
    }

    if (backend->handlers.find(handler_id) != backend->handlers.end() ||
        backend->pending_handlers->find(handler_id) != backend->pending_handlers->end())
    {
        sl_v8_throw_error_from_native_view(
            backend, sl_v8_literal("__sloppy_register_handler duplicate handler ID",
                                   sizeof("__sloppy_register_handler duplicate handler ID") - 1U));
        return;
    }

    backend->pending_handlers->emplace(
        handler_id, v8::Global<v8::Function>(isolate, handler_value.As<v8::Function>()));
    args.GetReturnValue().Set(v8::Undefined(isolate));
}

bool sl_v8_install_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context)
{
    v8::Isolate* isolate = backend == nullptr ? nullptr : backend->isolate;
    v8::Local<v8::String> name;
    v8::Local<v8::Function> function;
    v8::Local<v8::String> sloppy_key;
    v8::Local<v8::String> data_key;
    v8::Local<v8::Object> sloppy = v8::Object::New(isolate);
    v8::Local<v8::Object> data = v8::Object::New(isolate);

    // Preserve legacy low-level bridge coverage when callers do not provide a validated
    // feature set; app-host startup passes one for strict SL_RUNTIME_FEATURE_STDLIB_APP gating.
    if (!backend->has_runtime_features ||
        sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_STDLIB_APP))
    {
        v8::Local<v8::FunctionTemplate> function_template =
            v8::FunctionTemplate::New(isolate, sl_v8_register_handler_callback);
        if (!sl_status_is_ok(sl_v8_string_from_native_view(
                backend, sl_str_from_cstr("__sloppy_register_handler"), &name)) ||
            !function_template->GetFunction(context).ToLocal(&function))
        {
            return false;
        }

        if (!context->Global()->Set(context, name, function).FromMaybe(false)) {
            return false;
        }
    }

    if (!sl_status_is_ok(
            sl_v8_string_from_native_view(backend, sl_str_from_cstr("__sloppy"), &sloppy_key)) ||
        !sl_status_is_ok(
            sl_v8_string_from_native_view(backend, sl_str_from_cstr("data"), &data_key)))
    {
        return false;
    }

    if (!sl_v8_install_provider_intrinsics(backend, context, data) ||
        !sloppy->Set(context, data_key, data).FromMaybe(false) ||
        !sl_v8_install_fs_intrinsics(backend, context, sloppy) ||
        !sl_v8_install_crypto_intrinsics(backend, context, sloppy) ||
        !sl_v8_install_net_intrinsics(backend, context, sloppy) ||
        !sl_v8_install_os_intrinsics(backend, context, sloppy) ||
        !sl_v8_install_codec_intrinsics(backend, context, sloppy) ||
        !sl_v8_install_workers_intrinsics(backend, context, sloppy) ||
        !sl_v8_install_ffi_intrinsics(backend, context, sloppy) ||
        !sl_v8_install_time_intrinsics(backend, context, sloppy) ||
        !context->Global()->Set(context, sloppy_key, sloppy).FromMaybe(false))
    {
        return false;
    }

    return true;
}

SlStatus sl_v8_platform_acquire(void)
{
    std::lock_guard<std::mutex> lock(g_v8_platform_mutex);

    if (!g_v8_platform_initialized) {
#ifdef V8_INTL_SUPPORT
        v8::V8::InitializeICUDefaultLocation("");
#endif
#ifdef V8_USE_EXTERNAL_STARTUP_DATA
        v8::V8::InitializeExternalStartupData("");
#endif
        v8::V8::SetFlagsFromString("--stack-size=4096");
        std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
        if (!platform) {
            return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
        }

        g_v8_platform = platform.release();
        v8::V8::InitializePlatform(g_v8_platform);
        v8::V8::Initialize();
        g_v8_platform_initialized = true;
    }

    return sl_status_ok();
}

SlV8Engine* sl_v8_backend(SlEngine* engine)
{
    return engine == nullptr ? nullptr : static_cast<SlV8Engine*>(engine->backend);
}

const SlV8Engine* sl_v8_backend_const(const SlEngine* engine)
{
    return engine == nullptr ? nullptr : static_cast<const SlV8Engine*>(engine->backend);
}

SlStatus sl_v8_fs_capability_allow(const SlCapabilityRegistry* registry, SlArena* diag_arena,
                                   SlStr token, SlCapabilityOperation operation,
                                   SlStr provider_token, SlStr provider_kind, SlDiag* out_diag,
                                   void* user)
{
    (void)registry;
    (void)diag_arena;
    (void)token;
    (void)operation;
    (void)provider_token;
    (void)provider_kind;
    (void)out_diag;
    (void)user;
    return sl_status_ok();
}

SlStatus sl_v8_sqlite_capability_allow(const SlCapabilityRegistry* registry, SlArena* diag_arena,
                                       SlStr token, SlCapabilityOperation operation,
                                       SlStr provider_token, SlStr provider_kind, SlDiag* out_diag,
                                       void* user)
{
    (void)provider_token;
    (void)user;
    return sl_capability_check_database_provider(registry, diag_arena, token, operation,
                                                 provider_kind, out_diag);
}

bool sl_v8_fs_feature_enabled(const SlV8Engine* backend)
{
    return backend != nullptr && backend->has_runtime_features &&
           sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_STDLIB_FS);
}

bool sl_v8_sqlite_feature_enabled(const SlV8Engine* backend)
{
    return backend != nullptr &&
           (!backend->has_runtime_features ||
            sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_PROVIDER_SQLITE));
}

bool sl_v8_postgres_feature_enabled(const SlV8Engine* backend)
{
    return backend != nullptr && backend->has_runtime_features &&
           sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_PROVIDER_POSTGRES);
}

bool sl_v8_sqlserver_feature_enabled(const SlV8Engine* backend)
{
    return backend != nullptr && backend->has_runtime_features &&
           sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_PROVIDER_SQLSERVER);
}

bool sl_v8_time_feature_enabled(const SlV8Engine* backend)
{
    return backend != nullptr && backend->has_runtime_features &&
           sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_STDLIB_TIME);
}

bool sl_v8_crypto_feature_enabled(const SlV8Engine* backend)
{
    return backend != nullptr && backend->has_runtime_features &&
           sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_STDLIB_CRYPTO);
}

bool sl_v8_net_feature_enabled(const SlV8Engine* backend)
{
    return backend != nullptr && backend->has_runtime_features &&
           sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_STDLIB_NET);
}

bool sl_v8_os_feature_enabled(const SlV8Engine* backend)
{
    return backend != nullptr && backend->has_runtime_features &&
           sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_STDLIB_OS);
}

bool sl_v8_workers_feature_enabled(const SlV8Engine* backend)
{
    return backend != nullptr &&
           (!backend->has_runtime_features ||
            sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_STDLIB_WORKERS));
}

bool sl_v8_ffi_feature_enabled(const SlV8Engine* backend)
{
    return backend != nullptr && backend->has_runtime_features &&
           sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_STDLIB_FFI);
}

bool sl_v8_needs_async_loop(const SlV8Engine* backend)
{
    return backend != nullptr &&
           (sl_v8_sqlite_feature_enabled(backend) || sl_v8_fs_feature_enabled(backend) ||
            sl_v8_postgres_feature_enabled(backend) || sl_v8_time_feature_enabled(backend) ||
            sl_v8_sqlserver_feature_enabled(backend) || sl_v8_crypto_feature_enabled(backend) ||
            sl_v8_net_feature_enabled(backend) || sl_v8_os_feature_enabled(backend) ||
            sl_v8_workers_feature_enabled(backend));
}

size_t sl_v8_pending_native_activity(SlV8Engine* backend)
{
    size_t pending = 0U;

    if (backend == nullptr) {
        return 0U;
    }

    if (backend->async_loop != nullptr) {
        pending += sl_async_loop_pending_count(backend->async_loop);
    }
    if (backend->fs_executor_initialized) {
        pending += sl_provider_executor_pending_count(&backend->fs_executor);
    }
    if (backend->sqlite_executor_initialized) {
        pending += sl_provider_executor_pending_count(&backend->sqlite_executor);
    }
    pending += sl_v8_postgres_pending_native_activity(backend);
    pending += sl_v8_sqlserver_pending_native_activity(backend);
    {
        std::lock_guard<std::mutex> lock(backend->time_mutex);
        pending += backend->time_requests.size();
    }
    {
        std::lock_guard<std::mutex> lock(backend->crypto_mutex);
        pending += backend->crypto_password_requests.size();
    }
    {
        std::lock_guard<std::mutex> lock(backend->net_mutex);
        pending += backend->net_requests.size();
    }
    {
        std::lock_guard<std::mutex> lock(backend->os_mutex);
        pending += backend->os_requests.size();
    }
    {
        std::lock_guard<std::mutex> lock(backend->workers_mutex);
        pending += backend->worker_requests.size();
    }

    return pending;
}

SlStatus sl_v8_init_async_features(SlV8Engine* backend, SlArena* arena)
{
    SlProviderExecutorConfig config = {};
    SlStatus status;

    if (backend == nullptr || arena == nullptr) {
        return sl_status_ok();
    }

    if (sl_v8_needs_async_loop(backend)) {
        status =
            sl_async_loop_create(SL_ASYNC_BACKEND_LIBUV, arena, backend->async_completions.data(),
                                 backend->async_completions.size(), &backend->async_loop);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    if (sl_v8_sqlite_feature_enabled(backend)) {
        SlSqliteProviderConfig sqlite_config = sl_sqlite_provider_config_default(
            sl_str_from_cstr("stdlib.sqlite"), sl_str_from_cstr("sqlite"));
        sqlite_config.queue_capacity = backend->sqlite_slots.size();
        sqlite_config.capability_registry = backend->capabilities;
        sqlite_config.capability_check = sl_v8_sqlite_capability_allow;

        status = sl_sqlite_provider_executor_config(&sqlite_config, &config);
        if (sl_status_is_ok(status)) {
            status = sl_provider_executor_init(&backend->sqlite_executor, arena, &config,
                                               backend->sqlite_slots.data(), backend->async_loop);
        }
        if (!sl_status_is_ok(status)) {
            sl_v8_workers_dispose(backend);
            sl_v8_time_dispose(backend);
            sl_v8_crypto_dispose(backend);
            sl_v8_net_dispose(backend);
            sl_v8_os_dispose(backend);
            sl_async_loop_dispose(backend->async_loop);
            backend->async_loop = nullptr;
            return status;
        }
        backend->sqlite_executor_initialized = true;
    }

    if (!sl_v8_fs_feature_enabled(backend)) {
        return sl_status_ok();
    }

    config.instance_id = sl_str_from_cstr("stdlib.fs");
    config.provider_kind = sl_str_from_cstr("filesystem");
    config.provider_token = sl_str_from_cstr("stdlib.fs");
    config.mode = SL_PROVIDER_EXECUTION_BLOCKING_POOL;
    config.queue_capacity = backend->fs_slots.size();
    config.worker_count = 2U;
    config.max_in_flight = 2U;
    config.capability_registry = backend->capabilities;
    config.capability_check = sl_v8_fs_capability_allow;

    status = sl_provider_executor_init(&backend->fs_executor, arena, &config,
                                       backend->fs_slots.data(), backend->async_loop);
    if (!sl_status_is_ok(status)) {
        if (backend->sqlite_executor_initialized) {
            sl_provider_executor_dispose(&backend->sqlite_executor);
            backend->sqlite_executor_initialized = false;
        }
        sl_v8_workers_dispose(backend);
        sl_v8_time_dispose(backend);
        sl_v8_crypto_dispose(backend);
        sl_v8_net_dispose(backend);
        sl_v8_os_dispose(backend);
        sl_async_loop_dispose(backend->async_loop);
        backend->async_loop = nullptr;
        return status;
    }
    backend->fs_executor_initialized = true;
    return sl_status_ok();
}

SlStatus sl_v8_reset_create_arena(SlArena* arena, SlArenaMark mark, SlStatus status)
{
    SlStatus reset_status = sl_arena_reset_to(arena, mark);

    if (!sl_status_is_ok(reset_status)) {
        return reset_status;
    }
    return status;
}

} // namespace

extern "C" SlStatus sl_engine_v8_create(const SlEngineOptions* options, SlArena* arena,
                                        SlEngine** out_engine)
{
    void* engine_memory = nullptr;
    SlEngine* engine = nullptr;
    SlV8Engine* backend = nullptr;
    SlStatus status;
    SlArenaMark mark = {};
    SlV8StartupSnapshotProbe startup_snapshot;

    if (arena == nullptr || out_engine == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_engine = nullptr;

    mark = sl_arena_mark(arena);

    status = sl_v8_platform_acquire();
    if (!sl_status_is_ok(status)) {
        return status;
    }

    backend = new (std::nothrow) SlV8Engine();
    if (backend == nullptr) {
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }
    backend->arena = arena;
    backend->owner_thread = std::this_thread::get_id();
    backend->plan = options == nullptr ? nullptr : options->plan;
    backend->capabilities = options == nullptr ? nullptr : options->capabilities;
    backend->filesystem_policy = options == nullptr ? nullptr : options->filesystem_policy;
    backend->has_os_policy = options != nullptr && options->os_policy != nullptr;
    if (backend->has_os_policy) {
        backend->os_policy = *options->os_policy;
    }
    backend->logging = options == nullptr ? nullptr : options->logging;
    backend->ffi_library_overrides = options == nullptr ? nullptr : options->ffi_library_overrides;
    backend->ffi_library_override_count =
        options == nullptr ? 0U : options->ffi_library_override_count;
    backend->source_map = options == nullptr ? SlBytes{} : options->source_map;
    backend->has_runtime_features = options != nullptr && options->runtime_features != nullptr;
    if (backend->has_runtime_features) {
        backend->runtime_features = *options->runtime_features;
    }
    if (options != nullptr && options->source_map_source_name.length > 0U) {
        status = sl_str_copy_view_to_arena(arena, options->source_map_source_name,
                                           &backend->source_map_source_name);
        if (!sl_status_is_ok(status)) {
            delete backend;
            SlStatus reset_status = sl_arena_reset_to(arena, mark);
            if (!sl_status_is_ok(reset_status)) {
                return reset_status;
            }
            return status;
        }
    }
    else {
        backend->source_map_source_name = sl_str_empty();
    }

    status = sl_resource_table_init(&backend->resources, backend->resource_entries.data(),
                                    backend->resource_entries.size());
    if (!sl_status_is_ok(status)) {
        delete backend;
        return sl_v8_reset_create_arena(arena, mark, status);
    }

    if (sl_v8_ffi_feature_enabled(backend)) {
        status = sl_ffi_registry_init_from_plan(&backend->ffi_registry, arena, backend->plan,
                                                backend->ffi_library_overrides,
                                                backend->ffi_library_override_count, nullptr);
        if (!sl_status_is_ok(status)) {
            sl_resource_table_dispose(&backend->resources);
            delete backend;
            return sl_v8_reset_create_arena(arena, mark, status);
        }
        backend->ffi_registry_initialized = true;
    }

    status = sl_v8_init_async_features(backend, arena);
    if (!sl_status_is_ok(status)) {
        sl_v8_ffi_dispose(backend);
        sl_v8_websocket_dispose(backend);
        sl_resource_table_dispose(&backend->resources);
        delete backend;
        return sl_v8_reset_create_arena(arena, mark, status);
    }

    backend->allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    if (backend->allocator == nullptr) {
        sl_v8_ffi_dispose(backend);
        sl_resource_table_dispose(&backend->resources);
        if (backend->sqlite_executor_initialized) {
            sl_provider_executor_dispose(&backend->sqlite_executor);
        }
        if (backend->fs_executor_initialized) {
            sl_provider_executor_dispose(&backend->fs_executor);
        }
        if (backend->async_loop != nullptr) {
            sl_v8_workers_dispose(backend);
            sl_v8_time_dispose(backend);
            sl_v8_crypto_dispose(backend);
            sl_v8_net_dispose(backend);
            sl_v8_os_dispose(backend);
            sl_async_loop_dispose(backend->async_loop);
        }
        delete backend;
        return sl_v8_reset_create_arena(arena, mark, sl_status_from_code(SL_STATUS_OUT_OF_MEMORY));
    }

    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = backend->allocator;
    startup_snapshot = sl_v8_startup_snapshot_probe(options);
    backend->startup_snapshot_active = startup_snapshot.active;
    if (startup_snapshot.active) {
        backend->startup_snapshot_bytes = std::move(startup_snapshot.bytes);
        backend->startup_snapshot_blob.data =
            reinterpret_cast<const char*>(backend->startup_snapshot_bytes.data());
        backend->startup_snapshot_blob.raw_size =
            static_cast<int>(backend->startup_snapshot_bytes.size());
        create_params.snapshot_blob = &backend->startup_snapshot_blob;
        create_params.external_references = sl_v8_external_references();
    }
    backend->isolate = v8::Isolate::New(create_params);
    if (backend->isolate == nullptr) {
        sl_v8_ffi_dispose(backend);
        sl_resource_table_dispose(&backend->resources);
        if (backend->sqlite_executor_initialized) {
            sl_provider_executor_dispose(&backend->sqlite_executor);
        }
        if (backend->fs_executor_initialized) {
            sl_provider_executor_dispose(&backend->fs_executor);
        }
        if (backend->async_loop != nullptr) {
            sl_v8_workers_dispose(backend);
            sl_v8_time_dispose(backend);
            sl_v8_crypto_dispose(backend);
            sl_v8_net_dispose(backend);
            sl_v8_os_dispose(backend);
            sl_async_loop_dispose(backend->async_loop);
        }
        delete backend->allocator;
        delete backend;
        return sl_v8_reset_create_arena(arena, mark, sl_status_from_code(SL_STATUS_INTERNAL));
    }

    {
        v8::Isolate::Scope isolate_scope(backend->isolate);
        v8::HandleScope handle_scope(backend->isolate);
        backend->isolate->SetData(0, backend);
        backend->isolate->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);
        backend->isolate->SetPromiseHook(sl_v8_microtask_drain_promise_hook);
        v8::Local<v8::Context> context = v8::Context::New(backend->isolate);
        v8::Context::Scope context_scope(context);
        if (!startup_snapshot.active && !sl_v8_install_intrinsics(backend, context)) {
            backend->isolate->SetData(0, nullptr);
            backend->isolate->Dispose();
            sl_v8_ffi_dispose(backend);
            sl_resource_table_dispose(&backend->resources);
            if (backend->sqlite_executor_initialized) {
                sl_provider_executor_dispose(&backend->sqlite_executor);
            }
            if (backend->fs_executor_initialized) {
                sl_provider_executor_dispose(&backend->fs_executor);
            }
            if (backend->async_loop != nullptr) {
                sl_v8_workers_dispose(backend);
                sl_v8_time_dispose(backend);
                sl_v8_crypto_dispose(backend);
                sl_v8_net_dispose(backend);
                sl_v8_os_dispose(backend);
                sl_async_loop_dispose(backend->async_loop);
            }
            delete backend->allocator;
            delete backend;
            return sl_v8_reset_create_arena(arena, mark, sl_status_from_code(SL_STATUS_INTERNAL));
        }
        backend->context.Reset(backend->isolate, context);
    }

    status = sl_arena_alloc(arena, sizeof(SlEngine), alignof(SlEngine), &engine_memory);
    if (!sl_status_is_ok(status)) {
        backend->handlers.clear();
        backend->context.Reset();
        backend->isolate->SetData(0, nullptr);
        backend->isolate->Dispose();
        sl_v8_ffi_dispose(backend);
        sl_resource_table_dispose(&backend->resources);
        if (backend->sqlite_executor_initialized) {
            sl_provider_executor_dispose(&backend->sqlite_executor);
        }
        if (backend->fs_executor_initialized) {
            sl_provider_executor_dispose(&backend->fs_executor);
        }
        if (backend->async_loop != nullptr) {
            sl_v8_workers_dispose(backend);
            sl_v8_time_dispose(backend);
            sl_v8_crypto_dispose(backend);
            sl_v8_net_dispose(backend);
            sl_v8_os_dispose(backend);
            sl_async_loop_dispose(backend->async_loop);
        }
        delete backend->allocator;
        delete backend;
        SlStatus reset_status = sl_arena_reset_to(arena, mark);
        if (!sl_status_is_ok(reset_status)) {
            return reset_status;
        }

        return status;
    }

    engine = static_cast<SlEngine*>(engine_memory);
    engine->kind = SL_ENGINE_KIND_V8;
    engine->arena = arena;
    engine->active = true;
    engine->backend = backend;
    *out_engine = engine;
    return sl_status_ok();
}

extern "C" void sl_engine_v8_destroy(SlEngine* engine)
{
    SlV8Engine* backend = nullptr;

    if (engine == nullptr) {
        return;
    }

    backend = sl_v8_backend(engine);
    if (backend != nullptr && !sl_v8_on_owner_thread(backend)) {
        return;
    }

    engine->active = false;
    engine->backend = nullptr;

    if (backend != nullptr) {
        if (backend->sqlite_executor_initialized) {
            sl_provider_executor_dispose(&backend->sqlite_executor);
            backend->sqlite_executor_initialized = false;
        }
        if (backend->fs_executor_initialized) {
            sl_provider_executor_dispose(&backend->fs_executor);
            backend->fs_executor_initialized = false;
        }
        sl_v8_ffi_dispose(backend);
        sl_v8_websocket_dispose(backend);
        sl_resource_table_dispose(&backend->resources);
        if (backend->async_loop != nullptr) {
            sl_v8_workers_dispose(backend);
            sl_v8_time_dispose(backend);
            sl_v8_crypto_dispose(backend);
            sl_v8_net_dispose(backend);
            sl_v8_os_dispose(backend);
            sl_async_loop_dispose(backend->async_loop);
            backend->async_loop = nullptr;
        }

        if (backend->isolate != nullptr) {
            backend->handlers.clear();
            sl_v8_reset_http_bridge_caches(backend);
            sl_v8_reset_db_bridge_caches(backend);
            backend->context.Reset();
            backend->isolate->SetData(0, nullptr);
            backend->isolate->Dispose();
        }

        sl_v8_source_map_cache_dispose(backend);
        delete backend->allocator;
        delete backend;
    }
}

extern "C" SlStatus sl_engine_v8_info(const SlEngine* engine, SlEngineInfo* out_info)
{
    if (engine == nullptr || out_info == nullptr || sl_v8_backend_const(engine) == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (!sl_v8_on_owner_thread(sl_v8_backend_const(engine))) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    out_info->kind = SL_ENGINE_KIND_V8;
    out_info->name = sl_v8_literal("v8", sizeof("v8") - 1U);
    out_info->version = sl_v8_literal("enabled", sizeof("enabled") - 1U);
    return sl_status_ok();
}

extern "C" SlStatus sl_engine_v8_eval_source(SlEngine* engine, SlStr source_name, SlStr source,
                                             SlDiag* out_diag)
{
    SlV8Engine* backend = sl_v8_backend(engine);
    v8::Local<v8::String> source_string;
    SlStatus status;

    if (engine == nullptr || backend == nullptr || backend->isolate == nullptr ||
        source.length == 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_v8_check_owner_thread(engine, backend, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    v8::Isolate* isolate = backend->isolate;
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = backend->context.Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::TryCatch try_catch(isolate);
    v8::Local<v8::String> source_name_string;

    status = sl_v8_string_from_native_view(backend, source, &source_string);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_v8_string_from_native_view(backend, source_name, &source_name_string);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    v8::ScriptOrigin origin(source_name_string);
    v8::Local<v8::Script> script;
    std::unordered_map<uint32_t, v8::Global<v8::Function>> pending_handlers;
    SlV8CodeCacheProbe code_cache = backend->startup_snapshot_active
                                        ? SlV8CodeCacheProbe{}
                                        : sl_v8_code_cache_probe(source_name, source);

    if (!code_cache.bytes.empty()) {
        v8::ScriptCompiler::CachedData cached_data(code_cache.bytes.data(),
                                                   static_cast<int>(code_cache.bytes.size()),
                                                   v8::ScriptCompiler::CachedData::BufferNotOwned);
        v8::ScriptCompiler::Source cached_source(source_string, origin, &cached_data);
        v8::MaybeLocal<v8::Script> maybe_cached_script = v8::ScriptCompiler::Compile(
            context, &cached_source, v8::ScriptCompiler::kConsumeCodeCache);
        if (cached_data.rejected || !maybe_cached_script.ToLocal(&script)) {
            try_catch.Reset();
        }
    }

    if (script.IsEmpty()) {
        v8::ScriptCompiler::Source script_source(source_string, origin);
        v8::MaybeLocal<v8::Script> maybe_script =
            v8::ScriptCompiler::Compile(context, &script_source);
        if (maybe_script.ToLocal(&script) && code_cache.enabled) {
            std::unique_ptr<v8::ScriptCompiler::CachedData> produced_cache(
                v8::ScriptCompiler::CreateCodeCache(script->GetUnboundScript()));
            sl_v8_code_cache_store(code_cache, produced_cache.get());
        }
    }
    if (script.IsEmpty()) {
        return sl_v8_write_exception_diag(
            engine, out_diag, SL_DIAG_ENGINE_COMPILE_ERROR, SL_STATUS_INVALID_STATE, isolate,
            context, try_catch, source_name, "JavaScript compile failed",
            sl_v8_literal(
                "Generated JavaScript locations are reported without source-map remapping.",
                sizeof("Generated JavaScript locations are reported without source-map "
                       "remapping.") -
                    1U));
    }
    backend->pending_handlers = &pending_handlers;
    if (script->Run(context).IsEmpty()) {
        backend->pending_handlers = nullptr;
        return sl_v8_write_exception_diag(
            engine, out_diag, SL_DIAG_ENGINE_EXCEPTION, SL_STATUS_INVALID_STATE, isolate, context,
            try_catch, source_name, "JavaScript evaluation failed",
            sl_v8_literal(
                "Generated JavaScript locations are reported without source-map remapping.",
                sizeof("Generated JavaScript locations are reported without source-map "
                       "remapping.") -
                    1U));
    }

    status = sl_v8_drain_microtasks(engine, isolate, context, out_diag);
    backend->pending_handlers = nullptr;
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (auto& entry : pending_handlers) {
        backend->handlers.emplace(entry.first, std::move(entry.second));
    }

    return sl_status_ok();
}

static SlStatus sl_v8_convert_handler_result(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                             SlEngine* engine, SlArena* arena,
                                             v8::Local<v8::Value> js_result,
                                             const SlHttpRequestContext* request_context,
                                             const SlCancellationToken* cancellation,
                                             SlEngineResult* out_result, SlDiag* out_diag);

static SlStatus sl_v8_write_promise_rejection_diag(SlEngine* engine, v8::Isolate* isolate,
                                                   v8::Local<v8::Value> reason, SlDiag* out_diag)
{
    std::string message = "JavaScript handler Promise rejected";
    std::string reason_text = sl_v8_maybe_value_to_string(isolate, reason);

    if (!reason_text.empty()) {
        message += ": ";
        message += reason_text;
    }

    sl_breadcrumb_global_record(SL_DIAG_SUBSYSTEM_V8, SL_BREADCRUMB_EVENT_V8_HANDLER_EXCEPTION,
                                SL_STATUS_INVALID_STATE, 0U, 0U, 0U, 0U,
                                sl_v8_literal("promise rejected", sizeof("promise rejected") - 1U));
    return sl_v8_write_diag_string(
        engine->arena, out_diag, SL_DIAG_V8_UNHANDLED_REJECTION, SL_STATUS_INVALID_STATE, message,
        sl_str_empty(),
        sl_v8_literal("Rejected async handlers produce a safe error response.",
                      sizeof("Rejected async handlers produce a safe error response.") - 1U));
}

static SlStatus sl_v8_write_pending_promise_diag(SlEngine* engine, SlDiag* out_diag)
{
    sl_breadcrumb_global_record(SL_DIAG_SUBSYSTEM_V8, SL_BREADCRUMB_EVENT_V8_HANDLER_EXCEPTION,
                                SL_STATUS_DEADLINE_EXCEEDED, 0U, 0U, 0U, 0U,
                                sl_v8_literal("pending promise", sizeof("pending promise") - 1U));
    return sl_v8_write_diag(
        engine->arena, out_diag, SL_DIAG_ENGINE_PROMISE_PENDING, SL_STATUS_DEADLINE_EXCEEDED,
        sl_v8_literal("JavaScript handler Promise did not settle during bounded microtask drain",
                      sizeof("JavaScript handler Promise did not settle during bounded microtask "
                             "drain") -
                          1U),
        sl_str_empty(),
        sl_v8_literal("This V8 runtime drains Promise microtasks and native async completions, "
                      "but does not implement timers, fetch, or Node APIs.",
                      sizeof("This V8 runtime drains Promise microtasks and native async "
                             "completions, but does not implement timers, fetch, or Node APIs.") -
                          1U));
}

static SlStatus sl_v8_drain_native_async_until_promise_settles(
    SlEngine* engine, v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Promise> promise, const SlCancellationToken* cancellation, SlDiag* out_diag)
{
    SlV8Engine* backend = sl_v8_backend(engine);

    if (backend == nullptr || backend->async_loop == nullptr) {
        return sl_status_ok();
    }

    while (promise->State() == v8::Promise::kPending) {
        size_t ran = 0U;
        SlStatus status = sl_async_loop_drain(backend->async_loop, 0U, &ran);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_v8_check_cancelled(engine, cancellation, out_diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_v8_drain_microtasks(engine, isolate, context, out_diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_v8_check_cancelled(engine, cancellation, out_diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (promise->State() != v8::Promise::kPending) {
            break;
        }
        if (ran == 0U && sl_v8_pending_native_activity(backend) == 0U) {
            break;
        }
        if (ran == 0U) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    return sl_status_ok();
}

static SlStatus sl_v8_convert_promise_result(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                             SlEngine* engine, SlArena* arena,
                                             v8::Local<v8::Promise> promise,
                                             const SlHttpRequestContext* request_context,
                                             const SlCancellationToken* cancellation,
                                             SlEngineResult* out_result, SlDiag* out_diag)
{
    SlStatus status = sl_v8_check_cancelled(engine, cancellation, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (promise->State() == v8::Promise::kPending) {
        status = sl_v8_drain_microtasks(engine, isolate, context, out_diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    if (promise->State() == v8::Promise::kPending) {
        status = sl_v8_drain_native_async_until_promise_settles(engine, isolate, context, promise,
                                                                cancellation, out_diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    status = sl_v8_check_cancelled(engine, cancellation, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (promise->State() == v8::Promise::kPending) {
        return sl_v8_write_pending_promise_diag(engine, out_diag);
    }

    if (promise->State() == v8::Promise::kRejected) {
        return sl_v8_write_promise_rejection_diag(engine, isolate, promise->Result(), out_diag);
    }

    return sl_v8_convert_handler_result(isolate, context, engine, arena, promise->Result(),
                                        request_context, cancellation, out_result, out_diag);
}

extern "C" SlStatus sl_engine_v8_call_function0(SlEngine* engine, SlArena* arena,
                                                SlStr function_name, SlEngineResult* out_result,
                                                SlDiag* out_diag)
{
    SlV8Engine* backend = sl_v8_backend(engine);
    v8::Local<v8::String> name_string;
    SlStatus status;

    if (out_result == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_result = SlEngineResult{};

    if (engine == nullptr || backend == nullptr || backend->isolate == nullptr ||
        arena == nullptr || function_name.length == 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_v8_check_owner_thread(engine, backend, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    v8::Isolate* isolate = backend->isolate;
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = backend->context.Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::TryCatch try_catch(isolate);

    status = sl_v8_string_from_native_view(backend, function_name, &name_string);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    v8::MaybeLocal<v8::Value> maybe_value = context->Global()->Get(context, name_string);
    v8::Local<v8::Value> value;
    if (!maybe_value.ToLocal(&value)) {
        return sl_v8_write_exception_diag(
            engine, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE, isolate, context,
            try_catch, sl_str_empty(), "JavaScript function lookup failed",
            sl_v8_literal("Use registered handler dispatch for compiler-generated app handlers.",
                          sizeof("Use registered handler dispatch for compiler-generated app "
                                 "handlers.") -
                              1U));
    }

    if (value->IsUndefined()) {
        std::string message = "JavaScript function was not found: " +
                              std::string(function_name.ptr, function_name.length);
        return sl_v8_write_diag_string(engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR,
                                       SL_STATUS_INVALID_STATE, message, sl_str_empty(),
                                       sl_v8_literal("Use registered handler dispatch for "
                                                     "compiler-generated app handlers.",
                                                     sizeof("Use registered handler dispatch for "
                                                            "compiler-generated app handlers.") -
                                                         1U));
    }

    if (!value->IsFunction()) {
        std::string message = "JavaScript global is not callable: " +
                              std::string(function_name.ptr, function_name.length);
        return sl_v8_write_diag_string(
            engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE, message,
            sl_str_empty(),
            sl_v8_literal("Only global functions can be called by the compatibility smoke API.",
                          sizeof("Only global functions can be called by the compatibility smoke "
                                 "API.") -
                              1U));
    }

    v8::Local<v8::Function> function = value.As<v8::Function>();
    v8::MaybeLocal<v8::Value> maybe_result = function->Call(context, context->Global(), 0, nullptr);
    v8::Local<v8::Value> js_result;
    if (!maybe_result.ToLocal(&js_result)) {
        return sl_v8_write_exception_diag(
            engine, out_diag, SL_DIAG_ENGINE_EXCEPTION, SL_STATUS_INVALID_STATE, isolate, context,
            try_catch, sl_str_empty(), "JavaScript function threw",
            sl_v8_literal(
                "Generated JavaScript locations are reported without source-map remapping.",
                sizeof("Generated JavaScript locations are reported without source-map "
                       "remapping.") -
                    1U));
    }

    status = sl_v8_drain_microtasks(engine, isolate, context, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_v8_convert_handler_result(isolate, context, engine, arena, js_result, nullptr,
                                        nullptr, out_result, out_diag);
}

static SlStatus sl_v8_convert_handler_result(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                             SlEngine* engine, SlArena* arena,
                                             v8::Local<v8::Value> js_result,
                                             const SlHttpRequestContext* request_context,
                                             const SlCancellationToken* cancellation,
                                             SlEngineResult* out_result, SlDiag* out_diag)
{
    SlStatus cancel_status = sl_v8_check_cancelled(engine, cancellation, out_diag);
    if (!sl_status_is_ok(cancel_status)) {
        return cancel_status;
    }

    if (js_result->IsPromise()) {
        return sl_v8_convert_promise_result(isolate, context, engine, arena,
                                            js_result.As<v8::Promise>(), request_context,
                                            cancellation, out_result, out_diag);
    }

    return sl_v8_convert_http_handler_result(isolate, context, engine, arena, js_result,
                                             request_context, out_result, out_diag);
}

extern "C" SlStatus
sl_engine_v8_call_function_with_context(SlEngine* engine, SlArena* arena, SlStr function_name,
                                        const SlHttpRequestContext* request_context,
                                        SlEngineResult* out_result, SlDiag* out_diag)
{
    SlV8Engine* backend = sl_v8_backend(engine);
    v8::Local<v8::String> name_string;
    v8::Local<v8::Object> context_arg;
    SlStatus status;

    if (out_result == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_result = SlEngineResult{};

    if (engine == nullptr || backend == nullptr || backend->isolate == nullptr ||
        arena == nullptr || request_context == nullptr || function_name.length == 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_v8_check_owner_thread(engine, backend, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_v8_check_cancelled(engine, request_context->cancellation, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    v8::Isolate* isolate = backend->isolate;
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = backend->context.Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::TryCatch try_catch(isolate);

    status = sl_v8_string_from_native_view(backend, function_name, &name_string);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    v8::MaybeLocal<v8::Value> maybe_value = context->Global()->Get(context, name_string);
    v8::Local<v8::Value> value;
    if (!maybe_value.ToLocal(&value)) {
        return sl_v8_write_exception_diag(
            engine, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE, isolate, context,
            try_catch, sl_str_empty(), "JavaScript function lookup failed",
            sl_v8_literal(
                "Generated app artifacts must register handler globals before dispatch.",
                sizeof("Generated app artifacts must register handler globals before dispatch.") -
                    1U));
    }

    if (value->IsUndefined()) {
        std::string message = "JavaScript function was not found: " +
                              std::string(function_name.ptr, function_name.length);
        return sl_v8_write_diag_string(engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR,
                                       SL_STATUS_INVALID_STATE, message, sl_str_empty(),
                                       sl_v8_literal("Regenerate app artifacts or fix the plan "
                                                     "handler export name.",
                                                     sizeof("Regenerate app artifacts or fix the "
                                                            "plan handler export name.") -
                                                         1U));
    }

    if (!value->IsFunction()) {
        std::string message = "JavaScript global is not callable: " +
                              std::string(function_name.ptr, function_name.length);
        return sl_v8_write_diag_string(engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR,
                                       SL_STATUS_INVALID_STATE, message, sl_str_empty(),
                                       sl_str_empty());
    }

    {
        uint64_t started_ns = sl_http_profile_now_ns();
        bool made_context =
            sl_v8_make_http_context_object(isolate, context, request_context, &context_arg);
        sl_http_profile_record_phase(SL_HTTP_PROFILE_PHASE_V8_CONTEXT_CONSTRUCTION,
                                     sl_http_profile_now_ns() - started_ns);
        if (!made_context) {
            return sl_v8_write_diag(
                engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE,
                sl_v8_literal("failed to materialize JavaScript request context",
                              sizeof("failed to materialize JavaScript request context") - 1U),
                sl_str_empty(), sl_str_empty());
        }
    }

    v8::Local<v8::Function> function = value.As<v8::Function>();
    v8::Local<v8::Value> args[1] = {context_arg};
    v8::MaybeLocal<v8::Value> maybe_result;
    {
        uint64_t started_ns = sl_http_profile_now_ns();
        maybe_result = function->Call(context, context->Global(), 1, args);
        sl_http_profile_record_phase(SL_HTTP_PROFILE_PHASE_V8_HANDLER_CALL,
                                     sl_http_profile_now_ns() - started_ns);
    }
    v8::Local<v8::Value> js_result;
    if (!maybe_result.ToLocal(&js_result)) {
        return sl_v8_write_exception_diag(
            engine, out_diag, SL_DIAG_ENGINE_EXCEPTION, SL_STATUS_INVALID_STATE, isolate, context,
            try_catch, sl_str_empty(), "JavaScript function threw",
            sl_v8_literal(
                "Generated JavaScript locations are reported without source-map remapping.",
                sizeof("Generated JavaScript locations are reported without source-map "
                       "remapping.") -
                    1U));
    }

    status = sl_v8_drain_microtasks(engine, isolate, context, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    {
        uint64_t started_ns = sl_http_profile_now_ns();
        status = sl_v8_convert_handler_result(isolate, context, engine, arena, js_result,
                                              request_context, request_context->cancellation,
                                              out_result, out_diag);
        sl_http_profile_record_phase(SL_HTTP_PROFILE_PHASE_V8_RESULT_CONVERSION,
                                     sl_http_profile_now_ns() - started_ns);
    }
    return status;
}

extern "C" SlStatus sl_engine_v8_validate_registered_handlers(SlEngine* engine, const SlPlan* plan,
                                                              SlDiag* out_diag)
{
    SlV8Engine* backend = sl_v8_backend(engine);
    size_t index = 0U;
    SlStatus status;

    if (engine == nullptr || backend == nullptr || plan == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_v8_check_owner_thread(engine, backend, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (sl_plan_has_duplicate_handler_ids(plan)) {
        return sl_v8_write_diag(
            engine->arena, out_diag, SL_DIAG_DUPLICATE_HANDLER_ID, SL_STATUS_INVALID_ARGUMENT,
            sl_v8_literal("Plan contains duplicate handler IDs.",
                          sizeof("Plan contains duplicate handler IDs.") - 1U),
            sl_str_empty(),
            sl_v8_literal("Handler IDs must be unique before runtime registration validation.",
                          sizeof("Handler IDs must be unique before runtime registration "
                                 "validation.") -
                              1U));
    }

    for (index = 0U; index < plan->handler_count; index += 1U) {
        SlHandlerId handler_id = plan->handlers[index].id;
        if (!sl_handler_id_valid(handler_id) ||
            backend->handlers.find(handler_id) == backend->handlers.end())
        {
            return sl_v8_write_missing_registered_handler_diag(engine, out_diag, handler_id);
        }
    }

    return sl_status_ok();
}

extern "C" SlStatus sl_engine_v8_call_registered_handler_with_context(
    SlEngine* engine, SlArena* arena, SlHandlerId handler_id,
    const SlHttpRequestContext* request_context, SlEngineResult* out_result, SlDiag* out_diag)
{
    SlV8Engine* backend = sl_v8_backend(engine);
    v8::Local<v8::Object> context_arg;

    if (out_result == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_result = SlEngineResult{};

    if (engine == nullptr || backend == nullptr || backend->isolate == nullptr ||
        arena == nullptr || request_context == nullptr || !sl_handler_id_valid(handler_id))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    SlStatus status = sl_v8_check_owner_thread(engine, backend, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_v8_check_cancelled(engine, request_context->cancellation, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    auto handler = backend->handlers.find(handler_id);
    if (handler == backend->handlers.end()) {
        return sl_v8_write_missing_registered_handler_diag(engine, out_diag, handler_id);
    }

    v8::Isolate* isolate = backend->isolate;
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = backend->context.Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::TryCatch try_catch(isolate);

    {
        uint64_t started_ns = sl_http_profile_now_ns();
        bool made_context =
            sl_v8_make_http_context_object(isolate, context, request_context, &context_arg);
        sl_http_profile_record_phase(SL_HTTP_PROFILE_PHASE_V8_CONTEXT_CONSTRUCTION,
                                     sl_http_profile_now_ns() - started_ns);
        if (!made_context) {
            return sl_v8_write_diag(
                engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE,
                sl_v8_literal("failed to materialize JavaScript request context",
                              sizeof("failed to materialize JavaScript request context") - 1U),
                sl_str_empty(), sl_str_empty());
        }
    }

    v8::Local<v8::Function> function = handler->second.Get(isolate);
    v8::Local<v8::Value> args[1] = {context_arg};
    v8::MaybeLocal<v8::Value> maybe_result;
    {
        uint64_t started_ns = sl_http_profile_now_ns();
        maybe_result = function->Call(context, context->Global(), 1, args);
        sl_http_profile_record_phase(SL_HTTP_PROFILE_PHASE_V8_HANDLER_CALL,
                                     sl_http_profile_now_ns() - started_ns);
    }
    v8::Local<v8::Value> js_result;
    if (!maybe_result.ToLocal(&js_result)) {
        return sl_v8_write_exception_diag(
            engine, out_diag, SL_DIAG_ENGINE_EXCEPTION, SL_STATUS_INVALID_STATE, isolate, context,
            try_catch, sl_str_empty(), "JavaScript handler threw",
            sl_v8_literal(
                "Generated JavaScript locations are reported without source-map remapping.",
                sizeof("Generated JavaScript locations are reported without source-map "
                       "remapping.") -
                    1U));
    }

    status = sl_v8_drain_microtasks(engine, isolate, context, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    {
        uint64_t started_ns = sl_http_profile_now_ns();
        status = sl_v8_convert_handler_result(isolate, context, engine, arena, js_result,
                                              request_context, request_context->cancellation,
                                              out_result, out_diag);
        sl_http_profile_record_phase(SL_HTTP_PROFILE_PHASE_V8_RESULT_CONVERSION,
                                     sl_http_profile_now_ns() - started_ns);
    }
    return status;
}
