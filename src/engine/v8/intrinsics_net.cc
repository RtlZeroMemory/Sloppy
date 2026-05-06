/*
 * src/engine/v8/intrinsics_net.cc
 *
 * Installs the V8-internal TCP bridge under __sloppy.net. Blocking connect/read/write
 * work runs on owned native worker threads; completions settle Promises on the V8 owner
 * thread through SlAsyncLoop.
 */
#include "engine_v8_internal.h"
#include "string_interop.h"

#include "sloppy/net.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

constexpr size_t kNetArenaBytes = 256U * 1024U;
constexpr size_t kNetMaxBufferBytes = 64U * 1024U;

struct NetV8Listener;

enum class NetV8Operation
{
    Connect,
    Listen,
    Accept,
    Write,
    Read,
    ReadLine,
    ReadUntil,
    Close,
    Abort,
    CloseListener,
    AbortListener
};

struct NetV8Connection
{
    std::vector<unsigned char> storage;
    SlArena arena = {};
    SlTcpConnection* native = nullptr;
    std::mutex mutex;
    std::shared_ptr<NetV8Listener> listener_owner;
    bool closed = false;

    NetV8Connection() : storage(kNetArenaBytes)
    {
        sl_arena_init(&arena, storage.data(), storage.size());
    }

    ~NetV8Connection()
    {
        if (native != nullptr && !closed) {
            (void)sl_tcp_connection_close(native, nullptr);
            closed = true;
        }
    }
};

struct NetV8Listener
{
    std::vector<unsigned char> storage;
    SlArena arena = {};
    SlTcpListener* native = nullptr;
    std::mutex mutex;
    bool closed = false;
    bool accepting = false;
    bool close_requested = false;
    bool abort_requested = false;

    NetV8Listener() : storage(kNetArenaBytes)
    {
        sl_arena_init(&arena, storage.data(), storage.size());
    }

    ~NetV8Listener()
    {
        if (native != nullptr && !closed) {
            if (sl_status_is_ok(sl_tcp_listener_close(native, nullptr))) {
                closed = true;
            }
        }
    }
};

struct SlV8NetRequest
{
    SlV8Engine* backend = nullptr;
    v8::Global<v8::Promise::Resolver> resolver;
    std::atomic_bool cancelled = false;
    std::atomic_bool completion_posted = false;
    std::thread worker;
    NetV8Operation operation = NetV8Operation::Connect;
    std::shared_ptr<NetV8Connection> connection;
    std::shared_ptr<NetV8Listener> listener;
    SlResourceId resource_id = {};
    std::string host;
    uint16_t port = 0U;
    uint32_t backlog = 128U;
    bool has_timeout_ms = false;
    uint32_t timeout_ms = 0U;
    bool no_delay = false;
    std::vector<unsigned char> bytes;
    std::vector<unsigned char> delimiter;
    size_t max_bytes = 8192U;
    SlStatus status = sl_status_ok();
    SlDiag diag = {};
    std::vector<unsigned char> result_bytes;
    std::string result_text;
};

namespace {

struct NetV8CompletionPayload
{
    std::shared_ptr<SlV8NetRequest> request;
};

SlStatus net_v8_to_local_string(v8::Isolate* isolate, SlStr str, v8::Local<v8::String>* out)
{
    SlV8Engine* backend =
        isolate == nullptr ? nullptr : static_cast<SlV8Engine*>(isolate->GetData(0));
    return sl_v8_string_from_native_view(backend, str, out);
}

void net_v8_throw_type_error(v8::Isolate* isolate, const char* message)
{
    v8::Local<v8::String> local_message;
    if (!sl_status_is_ok(
            net_v8_to_local_string(isolate, sl_str_from_cstr(message), &local_message)))
    {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Sloppy network type error")));
        return;
    }
    isolate->ThrowException(v8::Exception::TypeError(local_message));
}

bool net_v8_set_function(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         v8::Local<v8::Object> object, const char* name,
                         v8::FunctionCallback callback)
{
    v8::Local<v8::String> key;
    v8::Local<v8::Function> function;

    if (!sl_status_is_ok(net_v8_to_local_string(isolate, sl_str_from_cstr(name), &key)) ||
        !v8::FunctionTemplate::New(isolate, callback)->GetFunction(context).ToLocal(&function))
    {
        return false;
    }
    return object->Set(context, key, function).FromMaybe(false);
}

std::string net_v8_diag_message(const SlV8NetRequest& request)
{
    SlStr code = sl_diag_code_name(request.diag.code);
    if (code.ptr != nullptr && code.length != 0U) {
        std::string message(code.ptr, code.length);
        if (request.diag.message.ptr != nullptr && request.diag.message.length != 0U) {
            message += ": ";
            message.append(request.diag.message.ptr, request.diag.message.length);
        }
        return message;
    }
    return "SLOPPY_E_NET_BACKEND_UNAVAILABLE: TCP operation failed";
}

void net_v8_remove_request(const std::shared_ptr<SlV8NetRequest>& request)
{
    SlV8Engine* backend = request == nullptr ? nullptr : request->backend;
    if (backend == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(backend->net_mutex);
    auto& requests = backend->net_requests;
    requests.erase(std::remove(requests.begin(), requests.end(), request), requests.end());
}

void net_v8_resource_cleanup(void* ptr, void* user)
{
    auto* holder = static_cast<std::shared_ptr<NetV8Connection>*>(ptr);
    (void)user;
    if (holder != nullptr) {
        std::shared_ptr<NetV8Connection> connection = *holder;
        if (connection != nullptr) {
            std::unique_lock<std::mutex> owner_lock;
            if (connection->listener_owner != nullptr) {
                owner_lock = std::unique_lock<std::mutex>(connection->listener_owner->mutex);
            }
            std::lock_guard<std::mutex> lock(connection->mutex);
            if (connection->native != nullptr && !connection->closed) {
                (void)sl_tcp_connection_close(connection->native, nullptr);
                connection->closed = true;
            }
        }
        delete holder;
    }
}

void net_v8_listener_resource_cleanup(void* ptr, void* user)
{
    auto* holder = static_cast<std::shared_ptr<NetV8Listener>*>(ptr);
    (void)user;
    if (holder != nullptr) {
        std::shared_ptr<NetV8Listener> listener = *holder;
        if (listener != nullptr) {
            std::lock_guard<std::mutex> lock(listener->mutex);
            if (listener->native != nullptr && !listener->closed) {
                if (listener->accepting) {
                    listener->close_requested = true;
                    listener->closed = true;
                    delete holder;
                    return;
                }
                if (sl_status_is_ok(sl_tcp_listener_close(listener->native, nullptr))) {
                    listener->closed = true;
                }
            }
        }
        delete holder;
    }
}

bool net_v8_handle_to_resource(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Value> value, SlResourceId* out)
{
    v8::Local<v8::Object> object;
    v8::Local<v8::String> slot_key;
    v8::Local<v8::String> generation_key;
    v8::Local<v8::Value> slot_value;
    v8::Local<v8::Value> generation_value;

    if (out == nullptr || value.IsEmpty() || !value->IsObject() ||
        !sl_status_is_ok(net_v8_to_local_string(isolate, sl_str_from_cstr("slot"), &slot_key)) ||
        !sl_status_is_ok(
            net_v8_to_local_string(isolate, sl_str_from_cstr("generation"), &generation_key)))
    {
        return false;
    }
    object = value.As<v8::Object>();
    if (!object->Get(context, slot_key).ToLocal(&slot_value) ||
        !object->Get(context, generation_key).ToLocal(&generation_value) ||
        !slot_value->IsUint32() || !generation_value->IsUint32())
    {
        return false;
    }
    out->slot = slot_value.As<v8::Uint32>()->Value();
    out->generation = generation_value.As<v8::Uint32>()->Value();
    return sl_resource_id_is_valid(*out);
}

bool net_v8_resource_to_handle(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               SlResourceId id, v8::Local<v8::Object>* out)
{
    v8::Local<v8::Object> object;
    v8::Local<v8::String> slot_key;
    v8::Local<v8::String> generation_key;

    if (out == nullptr ||
        !sl_status_is_ok(net_v8_to_local_string(isolate, sl_str_from_cstr("slot"), &slot_key)) ||
        !sl_status_is_ok(
            net_v8_to_local_string(isolate, sl_str_from_cstr("generation"), &generation_key)))
    {
        return false;
    }
    object = v8::Object::New(isolate);
    if (!object->Set(context, slot_key, v8::Uint32::New(isolate, static_cast<int32_t>(id.slot)))
             .FromMaybe(false) ||
        !object
             ->Set(context, generation_key,
                   v8::Uint32::New(isolate, static_cast<int32_t>(id.generation)))
             .FromMaybe(false))
    {
        return false;
    }
    *out = object;
    return true;
}

bool net_v8_bytes_arg(v8::Local<v8::Value> value, size_t max, std::vector<unsigned char>* out)
{
    if (out == nullptr || value.IsEmpty() || !value->IsUint8Array()) {
        return false;
    }
    v8::Local<v8::Uint8Array> view = value.As<v8::Uint8Array>();
    size_t length = view->ByteLength();
    size_t offset = view->ByteOffset();
    if (length > max) {
        return false;
    }
    std::shared_ptr<v8::BackingStore> backing = view->Buffer()->GetBackingStore();
    if (backing == nullptr || offset > backing->ByteLength() ||
        length > backing->ByteLength() - offset)
    {
        return false;
    }
    const unsigned char* start =
        static_cast<const unsigned char*>(backing->Data()) + static_cast<ptrdiff_t>(offset);
    out->assign(start, start + length);
    return true;
}

bool net_v8_lookup_connection(SlV8Engine* backend, SlResourceId id,
                              std::shared_ptr<NetV8Connection>* out)
{
    void* ptr = nullptr;
    if (backend == nullptr || out == nullptr ||
        !sl_status_is_ok(sl_resource_table_get(&backend->resources, id,
                                               SL_RESOURCE_KIND_TCP_CONNECTION, &ptr, nullptr)))
    {
        return false;
    }
    auto* holder = static_cast<std::shared_ptr<NetV8Connection>*>(ptr);
    if (holder == nullptr || *holder == nullptr) {
        return false;
    }
    *out = *holder;
    return true;
}

bool net_v8_lookup_listener(SlV8Engine* backend, SlResourceId id,
                            std::shared_ptr<NetV8Listener>* out)
{
    void* ptr = nullptr;
    if (backend == nullptr || out == nullptr ||
        !sl_status_is_ok(sl_resource_table_get(&backend->resources, id,
                                               SL_RESOURCE_KIND_TCP_LISTENER, &ptr, nullptr)))
    {
        return false;
    }
    auto* holder = static_cast<std::shared_ptr<NetV8Listener>*>(ptr);
    if (holder == nullptr || *holder == nullptr) {
        return false;
    }
    *out = *holder;
    return true;
}

void net_v8_worker(std::shared_ptr<SlV8NetRequest> request)
{
    if (request == nullptr || request->cancelled.load()) {
        return;
    }

    if (request->operation == NetV8Operation::Connect) {
        auto connection = std::make_shared<NetV8Connection>();
        SlTcpConnectOptions options = sl_tcp_connect_options_default(
            sl_str_from_parts(request->host.data(), request->host.size()), request->port);
        options.has_timeout_ms = request->has_timeout_ms;
        options.timeout_ms = request->timeout_ms;
        options.no_delay = request->no_delay;
        request->status = sl_tcp_client_connect(&connection->arena, &options, &connection->native,
                                                &request->diag);
        if (sl_status_is_ok(request->status)) {
            request->connection = connection;
        }
        return;
    }

    if (request->operation == NetV8Operation::Listen) {
        auto listener = std::make_shared<NetV8Listener>();
        SlTcpListenOptions options = sl_tcp_listen_options_default(
            sl_str_from_parts(request->host.data(), request->host.size()), request->port);
        options.backlog = request->backlog;
        request->status =
            sl_tcp_listener_listen(&listener->arena, &options, &listener->native, &request->diag);
        if (sl_status_is_ok(request->status)) {
            request->listener = listener;
        }
        return;
    }

    if (request->operation == NetV8Operation::Accept ||
        request->operation == NetV8Operation::CloseListener ||
        request->operation == NetV8Operation::AbortListener)
    {
        if (request->listener == nullptr) {
            request->status = sl_status_from_code(SL_STATUS_STALE_RESOURCE);
            request->diag.code = SL_DIAG_NET_STALE_HANDLE;
            request->diag.message = sl_str_from_cstr("TCP listener handle is stale");
            return;
        }
        if (request->operation == NetV8Operation::Accept) {
            auto connection = std::make_shared<NetV8Connection>();
            SlTcpAcceptOptions options = sl_tcp_accept_options_default();
            uint32_t remaining_timeout = request->timeout_ms;

            {
                std::lock_guard<std::mutex> lock(request->listener->mutex);
                if (request->listener->closed || request->listener->accepting) {
                    request->status = sl_status_from_code(SL_STATUS_INVALID_STATE);
                    request->diag.code = SL_DIAG_NET_STALE_HANDLE;
                    request->diag.message = sl_str_from_cstr("TCP listener is closed");
                    return;
                }
                request->listener->accepting = true;
            }

            for (;;) {
                uint32_t chunk_timeout = 100U;
                if (request->has_timeout_ms && remaining_timeout < chunk_timeout) {
                    chunk_timeout = remaining_timeout;
                }
                if (chunk_timeout == 0U) {
                    request->status = sl_status_from_code(SL_STATUS_DEADLINE_EXCEEDED);
                    request->diag.code = SL_DIAG_NET_READ_WRITE_TIMEOUT;
                    request->diag.message = sl_str_from_cstr("TCP accept timed out");
                    break;
                }
                options.has_timeout_ms = true;
                options.timeout_ms = chunk_timeout;
                request->status =
                    sl_tcp_listener_accept(request->listener->native, &connection->arena, &options,
                                           &connection->native, &request->diag);
                if (sl_status_is_ok(request->status)) {
                    break;
                }
                {
                    std::lock_guard<std::mutex> lock(request->listener->mutex);
                    if (request->listener->closed || request->listener->close_requested ||
                        request->listener->abort_requested)
                    {
                        request->status = sl_status_from_code(SL_STATUS_INVALID_STATE);
                        request->diag.code = SL_DIAG_NET_STALE_HANDLE;
                        request->diag.message = sl_str_from_cstr("TCP listener is closed");
                        break;
                    }
                }
                if (!request->has_timeout_ms || request->status.code != SL_STATUS_DEADLINE_EXCEEDED)
                {
                    continue;
                }
                if (remaining_timeout <= chunk_timeout) {
                    break;
                }
                remaining_timeout -= chunk_timeout;
            }

            {
                std::lock_guard<std::mutex> lock(request->listener->mutex);
                request->listener->accepting = false;
                if (request->listener->close_requested || request->listener->abort_requested) {
                    if (connection->native != nullptr) {
                        (void)sl_tcp_connection_abort(connection->native, nullptr);
                    }
                    if (request->listener->abort_requested) {
                        (void)sl_tcp_listener_abort(request->listener->native, nullptr);
                    }
                    else {
                        (void)sl_tcp_listener_close(request->listener->native, nullptr);
                    }
                    request->listener->closed = true;
                    request->status = sl_status_from_code(SL_STATUS_INVALID_STATE);
                    request->diag.code = SL_DIAG_NET_STALE_HANDLE;
                    request->diag.message = sl_str_from_cstr("TCP listener is closed");
                }
                else if (sl_status_is_ok(request->status)) {
                    connection->listener_owner = request->listener;
                    request->connection = connection;
                }
            }
        }
        else if (request->operation == NetV8Operation::CloseListener) {
            std::lock_guard<std::mutex> lock(request->listener->mutex);
            if (request->listener->closed) {
                request->status = sl_status_ok();
                return;
            }
            if (request->listener->accepting) {
                request->listener->close_requested = true;
                request->listener->closed = true;
                request->status = sl_status_ok();
                return;
            }
            request->status = sl_tcp_listener_close(request->listener->native, &request->diag);
            if (sl_status_is_ok(request->status)) {
                request->listener->closed = true;
            }
        }
        else if (request->operation == NetV8Operation::AbortListener) {
            std::lock_guard<std::mutex> lock(request->listener->mutex);
            if (request->listener->closed) {
                request->status = sl_status_ok();
                return;
            }
            if (request->listener->accepting) {
                request->listener->abort_requested = true;
                request->listener->closed = true;
                request->status = sl_status_ok();
                return;
            }
            request->status = sl_tcp_listener_abort(request->listener->native, &request->diag);
            if (sl_status_is_ok(request->status)) {
                request->listener->closed = true;
            }
        }
        return;
    }

    if (request->connection == nullptr) {
        request->status = sl_status_from_code(SL_STATUS_STALE_RESOURCE);
        request->diag.code = SL_DIAG_NET_STALE_HANDLE;
        request->diag.message = sl_str_from_cstr("TCP connection handle is stale");
        return;
    }

    std::unique_lock<std::mutex> owner_lock;
    if (request->connection->listener_owner != nullptr) {
        owner_lock = std::unique_lock<std::mutex>(request->connection->listener_owner->mutex);
    }
    std::lock_guard<std::mutex> lock(request->connection->mutex);
    if (request->connection->closed && request->operation != NetV8Operation::Close &&
        request->operation != NetV8Operation::Abort)
    {
        request->status = sl_status_from_code(SL_STATUS_INVALID_STATE);
        request->diag.code = SL_DIAG_NET_CONNECTION_CLOSED;
        request->diag.message = sl_str_from_cstr("TCP connection is closed");
    }
    else if (request->operation == NetV8Operation::Write) {
        request->status = sl_tcp_connection_write(
            request->connection->native,
            sl_bytes_from_parts(request->bytes.data(), request->bytes.size()), &request->diag);
    }
    else if (request->operation == NetV8Operation::Read) {
        SlOwnedBytes bytes = {};
        request->status =
            sl_tcp_connection_read(request->connection->native, &request->connection->arena,
                                   request->max_bytes, &bytes, &request->diag);
        if (sl_status_is_ok(request->status)) {
            request->result_bytes.assign(bytes.ptr, bytes.ptr + bytes.length);
        }
    }
    else if (request->operation == NetV8Operation::ReadUntil) {
        SlOwnedBytes bytes = {};
        request->status = sl_tcp_connection_read_until(
            request->connection->native, &request->connection->arena,
            sl_bytes_from_parts(request->delimiter.data(), request->delimiter.size()),
            request->max_bytes, &bytes, &request->diag);
        if (sl_status_is_ok(request->status)) {
            request->result_bytes.assign(bytes.ptr, bytes.ptr + bytes.length);
        }
    }
    else if (request->operation == NetV8Operation::ReadLine) {
        SlOwnedStr line = {};
        request->status =
            sl_tcp_connection_read_line(request->connection->native, &request->connection->arena,
                                        request->max_bytes, &line, &request->diag);
        if (sl_status_is_ok(request->status)) {
            request->result_text.assign(line.ptr, line.length);
        }
    }
    else if (request->operation == NetV8Operation::Close) {
        request->status = sl_tcp_connection_close(request->connection->native, &request->diag);
        request->connection->closed = true;
    }
    else if (request->operation == NetV8Operation::Abort) {
        request->status = sl_tcp_connection_abort(request->connection->native, &request->diag);
        request->connection->closed = true;
    }
}

SlStatus net_v8_completion_dispatch(SlAsyncLoop* loop, const SlAsyncCompletion* completion,
                                    void* user)
{
    NetV8CompletionPayload* payload =
        completion == nullptr ? nullptr : static_cast<NetV8CompletionPayload*>(completion->payload);
    std::shared_ptr<SlV8NetRequest> request = payload == nullptr ? nullptr : payload->request;
    SlV8Engine* backend = request == nullptr ? nullptr : request->backend;
    (void)loop;
    (void)user;
    if (request == nullptr || backend == nullptr || backend->isolate == nullptr ||
        request->resolver.IsEmpty() || backend->owner_thread != std::this_thread::get_id())
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    v8::Isolate* isolate = backend->isolate;
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = backend->context.Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::Local<v8::Promise::Resolver> resolver = request->resolver.Get(isolate);
    bool ok = false;

    if (!sl_status_is_ok(request->status)) {
        std::string message_text = net_v8_diag_message(*request);
        v8::Local<v8::String> message;
        if (!sl_status_is_ok(net_v8_to_local_string(
                isolate, sl_str_from_parts(message_text.data(), message_text.size()), &message)))
        {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        ok = resolver->Reject(context, v8::Exception::Error(message)).FromMaybe(false);
    }
    else if (request->operation == NetV8Operation::Connect ||
             request->operation == NetV8Operation::Accept)
    {
        auto* holder = new (std::nothrow) std::shared_ptr<NetV8Connection>(request->connection);
        SlResourceId id = {};
        v8::Local<v8::Object> handle;
        if (holder == nullptr ||
            !sl_status_is_ok(
                sl_resource_table_insert(&backend->resources, SL_RESOURCE_KIND_TCP_CONNECTION,
                                         holder, net_v8_resource_cleanup, nullptr, &id, nullptr)) ||
            !net_v8_resource_to_handle(isolate, context, id, &handle))
        {
            delete holder;
            ok = resolver
                     ->Reject(
                         context,
                         v8::Exception::Error(v8::String::NewFromUtf8Literal(
                             isolate, "SLOPPY_E_NET_BACKEND_UNAVAILABLE: resource insert failed")))
                     .FromMaybe(false);
        }
        else {
            ok = resolver->Resolve(context, handle).FromMaybe(false);
        }
    }
    else if (request->operation == NetV8Operation::Listen) {
        auto* holder = new (std::nothrow) std::shared_ptr<NetV8Listener>(request->listener);
        SlResourceId id = {};
        v8::Local<v8::Object> handle;
        if (holder == nullptr ||
            !sl_status_is_ok(sl_resource_table_insert(
                &backend->resources, SL_RESOURCE_KIND_TCP_LISTENER, holder,
                net_v8_listener_resource_cleanup, nullptr, &id, nullptr)) ||
            !net_v8_resource_to_handle(isolate, context, id, &handle))
        {
            delete holder;
            ok = resolver
                     ->Reject(
                         context,
                         v8::Exception::Error(v8::String::NewFromUtf8Literal(
                             isolate, "SLOPPY_E_NET_BACKEND_UNAVAILABLE: resource insert failed")))
                     .FromMaybe(false);
        }
        else {
            ok = resolver->Resolve(context, handle).FromMaybe(false);
        }
    }
    else if (request->operation == NetV8Operation::Read ||
             request->operation == NetV8Operation::ReadUntil)
    {
        std::unique_ptr<v8::BackingStore> backing =
            v8::ArrayBuffer::NewBackingStore(isolate, request->result_bytes.size());
        if (backing == nullptr) {
            return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
        }
        if (!request->result_bytes.empty()) {
            std::copy(request->result_bytes.begin(), request->result_bytes.end(),
                      static_cast<unsigned char*>(backing->Data()));
        }
        v8::Local<v8::ArrayBuffer> buffer = v8::ArrayBuffer::New(isolate, std::move(backing));
        v8::Local<v8::Uint8Array> view =
            v8::Uint8Array::New(buffer, 0U, request->result_bytes.size());
        ok = resolver->Resolve(context, view).FromMaybe(false);
    }
    else if (request->operation == NetV8Operation::ReadLine) {
        v8::Local<v8::String> line;
        if (!sl_status_is_ok(net_v8_to_local_string(
                isolate,
                sl_str_from_parts(request->result_text.data(), request->result_text.size()),
                &line)))
        {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        ok = resolver->Resolve(context, line).FromMaybe(false);
    }
    else {
        if ((request->operation == NetV8Operation::Close ||
             request->operation == NetV8Operation::Abort) &&
            sl_resource_id_is_valid(request->resource_id))
        {
            (void)sl_resource_table_close_kind(&backend->resources, request->resource_id,
                                               SL_RESOURCE_KIND_TCP_CONNECTION, nullptr);
        }
        if ((request->operation == NetV8Operation::CloseListener ||
             request->operation == NetV8Operation::AbortListener) &&
            sl_resource_id_is_valid(request->resource_id))
        {
            (void)sl_resource_table_close_kind(&backend->resources, request->resource_id,
                                               SL_RESOURCE_KIND_TCP_LISTENER, nullptr);
        }
        ok = resolver->Resolve(context, v8::Undefined(isolate)).FromMaybe(false);
    }

    isolate->PerformMicrotaskCheckpoint();
    return ok ? sl_status_ok() : sl_status_from_code(SL_STATUS_INVALID_STATE);
}

void net_v8_completion_cleanup(const SlAsyncCompletion* completion, void* user)
{
    NetV8CompletionPayload* payload =
        completion == nullptr ? nullptr : static_cast<NetV8CompletionPayload*>(completion->payload);
    std::shared_ptr<SlV8NetRequest> request = payload == nullptr ? nullptr : payload->request;
    (void)user;
    if (request != nullptr) {
        if (request->worker.joinable()) {
            request->worker.join();
        }
        request->resolver.Reset();
        request->completion_posted.store(false);
        net_v8_remove_request(request);
    }
    delete payload;
}

bool net_v8_post_completion(const std::shared_ptr<SlV8NetRequest>& request)
{
    SlV8Engine* backend = request == nullptr ? nullptr : request->backend;
    if (backend == nullptr) {
        return false;
    }
    while (!request->cancelled.load()) {
        {
            std::lock_guard<std::mutex> lock(backend->net_mutex);
            if (backend->net_shutting_down || backend->async_loop == nullptr) {
                request->cancelled.store(true);
                return false;
            }
        }
        auto* payload = new (std::nothrow) NetV8CompletionPayload();
        if (payload == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        payload->request = request;
        SlAsyncCompletion completion = {};
        completion.kind = SL_ASYNC_COMPLETION_V8_CONTINUATION;
        completion.operation_kind = SL_ASYNC_OPERATION_NONBLOCKING_IO;
        completion.status = request->status;
        completion.payload = payload;
        completion.dispatch = net_v8_completion_dispatch;
        completion.cleanup = net_v8_completion_cleanup;
        request->completion_posted.store(true);
        SlStatus status = sl_async_loop_post(backend->async_loop, &completion);
        if (sl_status_is_ok(status)) {
            return true;
        }
        request->completion_posted.store(false);
        delete payload;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool net_v8_start_request(const std::shared_ptr<SlV8NetRequest>& request)
{
    SlV8Engine* backend = request == nullptr ? nullptr : request->backend;
    if (backend == nullptr) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(backend->net_mutex);
        if (backend->net_shutting_down || backend->async_loop == nullptr) {
            return false;
        }
        backend->net_requests.push_back(request);
    }
    try {
        request->worker = std::thread([request]() {
            net_v8_worker(request);
            (void)net_v8_post_completion(request);
        });
    } catch (...) {
        net_v8_remove_request(request);
        return false;
    }
    return true;
}

std::shared_ptr<SlV8NetRequest> net_v8_make_request(const v8::FunctionCallbackInfo<v8::Value>& args,
                                                    NetV8Operation operation,
                                                    v8::Local<v8::Promise::Resolver>* out_resolver)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Promise::Resolver> resolver;

    if (backend == nullptr || !sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_STDLIB_NET))
    {
        net_v8_throw_type_error(isolate,
                                "__sloppy.net is unavailable because stdlib.net is inactive");
        return nullptr;
    }
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        net_v8_throw_type_error(isolate, "__sloppy.net could not create a Promise");
        return nullptr;
    }
    auto request = std::make_shared<SlV8NetRequest>();
    request->backend = backend;
    request->operation = operation;
    request->resolver.Reset(isolate, resolver);
    *out_resolver = resolver;
    return request;
}

bool net_v8_parse_uint(v8::Local<v8::Value> value, uint32_t min, uint32_t max, uint32_t* out)
{
    if (out == nullptr || value.IsEmpty() || !value->IsUint32()) {
        return false;
    }
    uint32_t number = value.As<v8::Uint32>()->Value();
    if (number < min || number > max) {
        return false;
    }
    *out = number;
    return true;
}

void net_v8_connect(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Promise::Resolver> resolver;
    auto request = net_v8_make_request(args, NetV8Operation::Connect, &resolver);
    v8::Local<v8::Object> options;
    v8::Local<v8::Value> value;
    uint32_t number = 0U;

    if (request == nullptr) {
        return;
    }
    if (args.Length() < 1 || !args[0]->IsObject()) {
        net_v8_throw_type_error(isolate, "__sloppy.net.connect requires options");
        return;
    }
    options = args[0].As<v8::Object>();
    auto get = [&](const char* key, v8::Local<v8::Value>* out) {
        v8::Local<v8::String> name;
        return sl_status_is_ok(net_v8_to_local_string(isolate, sl_str_from_cstr(key), &name)) &&
               options->Get(context, name).ToLocal(out);
    };
    if (!get("host", &value) || !sl_v8_std_string_from_value(isolate, value, &request->host) ||
        request->host.empty())
    {
        net_v8_throw_type_error(isolate, "__sloppy.net.connect host must be a string");
        return;
    }
    if (!get("port", &value) || !net_v8_parse_uint(value, 1U, 65535U, &number)) {
        net_v8_throw_type_error(isolate, "__sloppy.net.connect port is invalid");
        return;
    }
    request->port = static_cast<uint16_t>(number);
    if (get("timeoutMs", &value) && !value->IsUndefined()) {
        if (!net_v8_parse_uint(value, 0U, UINT32_MAX, &number)) {
            net_v8_throw_type_error(isolate, "__sloppy.net.connect timeoutMs is invalid");
            return;
        }
        request->has_timeout_ms = true;
        request->timeout_ms = number;
    }
    if (get("noDelay", &value) && value->IsBoolean()) {
        request->no_delay = value.As<v8::Boolean>()->Value();
    }
    if (!net_v8_start_request(request)) {
        net_v8_throw_type_error(isolate, "__sloppy.net.connect could not start worker");
        return;
    }
    args.GetReturnValue().Set(resolver->GetPromise());
}

void net_v8_listen(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Promise::Resolver> resolver;
    auto request = net_v8_make_request(args, NetV8Operation::Listen, &resolver);
    v8::Local<v8::Object> options;
    v8::Local<v8::Value> value;
    uint32_t number = 0U;

    if (request == nullptr) {
        return;
    }
    if (args.Length() < 1 || !args[0]->IsObject()) {
        net_v8_throw_type_error(isolate, "__sloppy.net.listen requires options");
        return;
    }
    options = args[0].As<v8::Object>();
    auto get = [&](const char* key, v8::Local<v8::Value>* out) {
        v8::Local<v8::String> name;
        return sl_status_is_ok(net_v8_to_local_string(isolate, sl_str_from_cstr(key), &name)) &&
               options->Get(context, name).ToLocal(out);
    };
    if (!get("host", &value) || !sl_v8_std_string_from_value(isolate, value, &request->host) ||
        request->host.empty())
    {
        net_v8_throw_type_error(isolate, "__sloppy.net.listen host must be a string");
        return;
    }
    if (!get("port", &value) || !net_v8_parse_uint(value, 0U, 65535U, &number)) {
        net_v8_throw_type_error(isolate, "__sloppy.net.listen port is invalid");
        return;
    }
    request->port = static_cast<uint16_t>(number);
    if (get("backlog", &value) && !value->IsUndefined()) {
        if (!net_v8_parse_uint(value, 1U, UINT32_MAX, &number)) {
            net_v8_throw_type_error(isolate, "__sloppy.net.listen backlog is invalid");
            return;
        }
        request->backlog = number;
    }
    if (!net_v8_start_request(request)) {
        net_v8_throw_type_error(isolate, "__sloppy.net.listen could not start worker");
        return;
    }
    args.GetReturnValue().Set(resolver->GetPromise());
}

void net_v8_with_listener(const v8::FunctionCallbackInfo<v8::Value>& args, NetV8Operation operation)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Promise::Resolver> resolver;
    auto request = net_v8_make_request(args, operation, &resolver);
    SlResourceId id = {};
    uint32_t timeout_ms = 0U;

    if (request == nullptr) {
        return;
    }
    if (args.Length() < 1 || !net_v8_handle_to_resource(isolate, context, args[0], &id) ||
        !net_v8_lookup_listener(request->backend, id, &request->listener))
    {
        net_v8_throw_type_error(isolate, "__sloppy.net listener handle is stale or closed");
        return;
    }
    request->resource_id = id;
    if (operation == NetV8Operation::Accept && args.Length() >= 2 && !args[1]->IsUndefined()) {
        if (!net_v8_parse_uint(args[1], 0U, UINT32_MAX, &timeout_ms)) {
            net_v8_throw_type_error(isolate, "__sloppy.net.accept timeoutMs is invalid");
            return;
        }
        request->has_timeout_ms = true;
        request->timeout_ms = timeout_ms;
    }
    if (!net_v8_start_request(request)) {
        net_v8_throw_type_error(isolate, "__sloppy.net listener operation could not start worker");
        return;
    }
    args.GetReturnValue().Set(resolver->GetPromise());
}

void net_v8_with_connection(const v8::FunctionCallbackInfo<v8::Value>& args,
                            NetV8Operation operation)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Promise::Resolver> resolver;
    auto request = net_v8_make_request(args, operation, &resolver);
    SlResourceId id = {};

    if (request == nullptr) {
        return;
    }
    if (args.Length() < 1 || !net_v8_handle_to_resource(isolate, context, args[0], &id) ||
        !net_v8_lookup_connection(request->backend, id, &request->connection))
    {
        net_v8_throw_type_error(isolate, "__sloppy.net connection handle is stale or closed");
        return;
    }
    request->resource_id = id;
    if (operation == NetV8Operation::Write) {
        if (args.Length() < 2 || !net_v8_bytes_arg(args[1], kNetMaxBufferBytes, &request->bytes)) {
            net_v8_throw_type_error(isolate, "__sloppy.net.write requires bounded bytes");
            return;
        }
    }
    else if (operation == NetV8Operation::Read || operation == NetV8Operation::ReadLine) {
        uint32_t max_bytes = 8192U;
        if (args.Length() >= 2 && !args[1]->IsUndefined() &&
            !net_v8_parse_uint(args[1], 1U, kNetMaxBufferBytes, &max_bytes))
        {
            net_v8_throw_type_error(isolate, "__sloppy.net read size is invalid");
            return;
        }
        request->max_bytes = max_bytes;
    }
    else if (operation == NetV8Operation::ReadUntil) {
        uint32_t max_bytes = 8192U;
        if (args.Length() < 2 ||
            !net_v8_bytes_arg(args[1], kNetMaxBufferBytes, &request->delimiter))
        {
            net_v8_throw_type_error(isolate, "__sloppy.net.readUntil requires delimiter bytes");
            return;
        }
        if (args.Length() >= 3 && !args[2]->IsUndefined() &&
            !net_v8_parse_uint(args[2], 1U, kNetMaxBufferBytes, &max_bytes))
        {
            net_v8_throw_type_error(isolate, "__sloppy.net read size is invalid");
            return;
        }
        request->max_bytes = max_bytes;
    }
    if (!net_v8_start_request(request)) {
        net_v8_throw_type_error(isolate, "__sloppy.net could not start worker");
        return;
    }
    args.GetReturnValue().Set(resolver->GetPromise());
}

void net_v8_write(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    net_v8_with_connection(args, NetV8Operation::Write);
}

void net_v8_read(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    net_v8_with_connection(args, NetV8Operation::Read);
}

void net_v8_read_line(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    net_v8_with_connection(args, NetV8Operation::ReadLine);
}

void net_v8_read_until(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    net_v8_with_connection(args, NetV8Operation::ReadUntil);
}

void net_v8_close(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    net_v8_with_connection(args, NetV8Operation::Close);
}

void net_v8_abort(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    net_v8_with_connection(args, NetV8Operation::Abort);
}

void net_v8_accept(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    net_v8_with_listener(args, NetV8Operation::Accept);
}

void net_v8_close_listener(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    net_v8_with_listener(args, NetV8Operation::CloseListener);
}

void net_v8_abort_listener(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    net_v8_with_listener(args, NetV8Operation::AbortListener);
}

} // namespace

bool sl_v8_install_net_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context,
                                  v8::Local<v8::Object> sloppy)
{
    v8::Isolate* isolate = backend == nullptr ? nullptr : backend->isolate;
    v8::Local<v8::Object> net;
    v8::Local<v8::String> key;

    if (backend == nullptr || isolate == nullptr || sloppy.IsEmpty()) {
        return false;
    }
    if (!backend->has_runtime_features ||
        !sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_STDLIB_NET))
    {
        return true;
    }
    net = v8::Object::New(isolate);
    if (!net_v8_set_function(isolate, context, net, "connect", net_v8_connect) ||
        !net_v8_set_function(isolate, context, net, "listen", net_v8_listen) ||
        !net_v8_set_function(isolate, context, net, "accept", net_v8_accept) ||
        !net_v8_set_function(isolate, context, net, "write", net_v8_write) ||
        !net_v8_set_function(isolate, context, net, "read", net_v8_read) ||
        !net_v8_set_function(isolate, context, net, "readLine", net_v8_read_line) ||
        !net_v8_set_function(isolate, context, net, "readUntil", net_v8_read_until) ||
        !net_v8_set_function(isolate, context, net, "close", net_v8_close) ||
        !net_v8_set_function(isolate, context, net, "abort", net_v8_abort) ||
        !net_v8_set_function(isolate, context, net, "closeListener", net_v8_close_listener) ||
        !net_v8_set_function(isolate, context, net, "abortListener", net_v8_abort_listener) ||
        !sl_status_is_ok(net_v8_to_local_string(isolate, sl_str_from_cstr("net"), &key)))
    {
        return false;
    }
    return sloppy->Set(context, key, net).FromMaybe(false);
}

void sl_v8_net_dispose(SlV8Engine* backend)
{
    std::vector<std::shared_ptr<SlV8NetRequest>> requests;
    if (backend == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(backend->net_mutex);
        backend->net_shutting_down = true;
        requests = backend->net_requests;
    }
    for (const std::shared_ptr<SlV8NetRequest>& request : requests) {
        if (request != nullptr) {
            request->cancelled.store(true);
            if (request->worker.joinable()) {
                request->worker.join();
            }
            request->resolver.Reset();
        }
    }
    {
        std::lock_guard<std::mutex> lock(backend->net_mutex);
        backend->net_requests.clear();
    }
}
