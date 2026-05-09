/*
 * src/engine/v8/intrinsics_net.cc
 *
 * Installs the V8-internal network bridge under __sloppy.net. Blocking local/TCP
 * connect/read/write work runs on owned native worker threads; completions settle
 * Promises on the V8 owner thread through SlAsyncLoop.
 */
#include "engine_v8_internal.h"
#include "string_interop.h"

#include "sloppy/net.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>

constexpr size_t kNetArenaBytes = 256U * 1024U;
constexpr size_t kNetMaxBufferBytes = 64U * 1024U;
constexpr size_t kNetTlsIoBytes = 16U * 1024U;

struct NetV8Listener;
struct NetV8LocalServer;

enum class NetV8Operation
{
    Connect,
    ConnectTls,
    Listen,
    Accept,
    Write,
    Read,
    ReadLine,
    ReadUntil,
    Close,
    Abort,
    CloseListener,
    AbortListener,
    LocalConnect,
    LocalListen,
    LocalAccept,
    LocalWrite,
    LocalRead,
    LocalReadLine,
    LocalReadUntil,
    LocalClose,
    LocalAbort,
    LocalCloseServer,
    LocalAbortServer
};

struct NetV8Connection
{
    std::vector<unsigned char> storage;
    SlArena arena = {};
    SlTcpConnection* native = nullptr;
    SSL_CTX* tls_context = nullptr;
    SSL* tls_ssl = nullptr;
    BIO* tls_read_bio = nullptr;
    BIO* tls_write_bio = nullptr;
    std::mutex mutex;
    std::shared_ptr<NetV8Listener> listener_owner;
    bool closed = false;
    bool tls_active = false;

    NetV8Connection() : storage(kNetArenaBytes)
    {
        sl_arena_init(&arena, storage.data(), storage.size());
    }

    ~NetV8Connection()
    {
        if (tls_ssl != nullptr) {
            SSL_free(tls_ssl);
            tls_ssl = nullptr;
            tls_read_bio = nullptr;
            tls_write_bio = nullptr;
        }
        if (tls_context != nullptr) {
            SSL_CTX_free(tls_context);
            tls_context = nullptr;
        }
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

struct NetV8LocalConnection
{
    std::vector<unsigned char> storage;
    SlArena arena = {};
    SlLocalConnection* native = nullptr;
    std::mutex mutex;
    std::shared_ptr<NetV8LocalServer> server_owner;
    bool closed = false;

    NetV8LocalConnection() : storage(kNetArenaBytes)
    {
        sl_arena_init(&arena, storage.data(), storage.size());
    }

    ~NetV8LocalConnection()
    {
        if (native != nullptr && !closed) {
            (void)sl_local_connection_close(native, nullptr);
            closed = true;
        }
    }
};

struct NetV8LocalServer
{
    std::vector<unsigned char> storage;
    SlArena arena = {};
    SlLocalServer* native = nullptr;
    std::mutex mutex;
    bool closed = false;
    bool accepting = false;
    bool close_requested = false;
    bool abort_requested = false;

    NetV8LocalServer() : storage(kNetArenaBytes)
    {
        sl_arena_init(&arena, storage.data(), storage.size());
    }

    ~NetV8LocalServer()
    {
        if (native != nullptr && !closed) {
            if (sl_status_is_ok(sl_local_server_close(native, nullptr))) {
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
    std::shared_ptr<NetV8LocalConnection> local_connection;
    std::shared_ptr<NetV8LocalServer> local_server;
    SlResourceId resource_id = {};
    std::string host;
    std::string path;
    uint16_t port = 0U;
    uint32_t backlog = 128U;
    std::string tls_server_name;
    std::string tls_ca_path;
    std::string tls_ca_bundle_path;
    std::string tls_trust_store_path;
    std::string tls_client_certificate_path;
    std::string tls_client_private_key_path;
    std::string tls_client_private_key_passphrase;
    std::vector<unsigned char> tls_alpn_protocols;
    std::string tls_selected_alpn;
    SlLocalEndpointBackend local_backend = SL_LOCAL_ENDPOINT_BACKEND_AUTO;
    bool unlink_existing = false;
    bool has_permissions = false;
    uint16_t permissions = 0U;
    bool has_timeout_ms = false;
    uint32_t timeout_ms = 0U;
    bool no_delay = false;
    bool tls_insecure_skip_verify = false;
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

bool net_v8_set_bool(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     v8::Local<v8::Object> object, const char* name, bool value)
{
    v8::Local<v8::String> key;

    if (!sl_status_is_ok(net_v8_to_local_string(isolate, sl_str_from_cstr(name), &key))) {
        return false;
    }
    return object->Set(context, key, v8::Boolean::New(isolate, value)).FromMaybe(false);
}

bool net_v8_set_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
                       v8::Local<v8::Object> object, const char* name, const std::string& value)
{
    v8::Local<v8::String> key;
    v8::Local<v8::String> text;

    if (!sl_status_is_ok(net_v8_to_local_string(isolate, sl_str_from_cstr(name), &key)) ||
        !sl_status_is_ok(
            net_v8_to_local_string(isolate, sl_str_from_parts(value.data(), value.size()), &text)))
    {
        return false;
    }
    return object->Set(context, key, text).FromMaybe(false);
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

void net_v8_set_diag(SlV8NetRequest& request, SlDiagCode code, SlStatusCode status,
                     const char* message)
{
    request.status = sl_status_from_code(status);
    request.diag = {};
    request.diag.severity = SL_DIAG_SEVERITY_ERROR;
    request.diag.code = code;
    request.diag.message = sl_str_from_cstr(message);
    request.diag.primary_span = sl_source_span_unknown();
}

int net_v8_tls_passphrase_cb(char* buffer, int size, int rwflag, void* user)
{
    auto* passphrase = static_cast<std::string*>(user);
    (void)rwflag;
    if (buffer == nullptr || size <= 0 || passphrase == nullptr) {
        return 0;
    }
    if (passphrase->size() >= static_cast<size_t>(size)) {
        return 0;
    }
    std::copy(passphrase->begin(), passphrase->end(), buffer);
    buffer[passphrase->size()] = '\0';
    return static_cast<int>(passphrase->size());
}

SlStatus net_v8_tls_drain_write_bio(NetV8Connection& connection, SlDiag* out_diag)
{
    unsigned char buffer[kNetTlsIoBytes];
    if (connection.tls_write_bio == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    for (;;) {
        int pending = BIO_pending(connection.tls_write_bio);
        if (pending <= 0) {
            return sl_status_ok();
        }
        int chunk =
            pending > static_cast<int>(sizeof(buffer)) ? static_cast<int>(sizeof(buffer)) : pending;
        int read_count = BIO_read(connection.tls_write_bio, buffer, chunk);
        if (read_count <= 0) {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        SlStatus status = sl_tcp_connection_write(
            connection.native, sl_bytes_from_parts(buffer, static_cast<size_t>(read_count)),
            out_diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
}

SlStatus net_v8_tls_feed_tcp(NetV8Connection& connection, SlArena& arena, SlDiag* out_diag)
{
    SlOwnedBytes encrypted = {};
    SlStatus status =
        sl_tcp_connection_read(connection.native, &arena, kNetTlsIoBytes, &encrypted, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (encrypted.length == 0U) {
        if (out_diag != nullptr) {
            *out_diag = {};
            out_diag->severity = SL_DIAG_SEVERITY_ERROR;
            out_diag->code = SL_DIAG_NET_CONNECTION_CLOSED;
            out_diag->message = sl_str_from_cstr("TCP connection closed during TLS I/O");
            out_diag->primary_span = sl_source_span_unknown();
        }
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    if (encrypted.length > static_cast<size_t>(INT_MAX)) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }
    if (encrypted.length != 0U &&
        BIO_write(connection.tls_read_bio, encrypted.ptr, static_cast<int>(encrypted.length)) <= 0)
    {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    return sl_status_ok();
}

bool net_v8_tls_load_verify_path(SSL_CTX* context, const std::string& path)
{
    if (context == nullptr || path.empty()) {
        return true;
    }
    if (SSL_CTX_load_verify_locations(context, path.c_str(), nullptr) == 1) {
        return true;
    }
    ERR_clear_error();
    return SSL_CTX_load_verify_locations(context, nullptr, path.c_str()) == 1;
}

bool net_v8_tls_configure_context(NetV8Connection& connection, SlV8NetRequest& request)
{
    SSL_CTX* context = SSL_CTX_new(TLS_client_method());
    if (context == nullptr) {
        net_v8_set_diag(request, SL_DIAG_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE, SL_STATUS_UNSUPPORTED,
                        "HTTP client TLS backend is unavailable");
        return false;
    }
    connection.tls_context = context;

    if (!request.tls_insecure_skip_verify) {
        SSL_CTX_set_verify(context, SSL_VERIFY_PEER, nullptr);
        if (SSL_CTX_set_default_verify_paths(context) != 1) {
            ERR_clear_error();
        }
        if (!net_v8_tls_load_verify_path(context, request.tls_ca_path) ||
            !net_v8_tls_load_verify_path(context, request.tls_ca_bundle_path) ||
            !net_v8_tls_load_verify_path(context, request.tls_trust_store_path))
        {
            net_v8_set_diag(request, SL_DIAG_HTTP_CLIENT_TLS_CERTIFICATE_VALIDATION_FAILED,
                            SL_STATUS_INVALID_ARGUMENT,
                            "HTTP client TLS trust material could not be loaded");
            return false;
        }
    }
    else {
        SSL_CTX_set_verify(context, SSL_VERIFY_NONE, nullptr);
    }

    if (!request.tls_client_certificate_path.empty()) {
        if (SSL_CTX_use_certificate_file(context, request.tls_client_certificate_path.c_str(),
                                         SSL_FILETYPE_PEM) != 1)
        {
            net_v8_set_diag(request, SL_DIAG_HTTP_CLIENT_TLS_CERTIFICATE_VALIDATION_FAILED,
                            SL_STATUS_INVALID_ARGUMENT,
                            "HTTP client TLS client certificate could not be loaded");
            return false;
        }
    }
    if (!request.tls_client_private_key_path.empty()) {
        SSL_CTX_set_default_passwd_cb(context, net_v8_tls_passphrase_cb);
        SSL_CTX_set_default_passwd_cb_userdata(context, &request.tls_client_private_key_passphrase);
        if (SSL_CTX_use_PrivateKey_file(context, request.tls_client_private_key_path.c_str(),
                                        SSL_FILETYPE_PEM) != 1 ||
            SSL_CTX_check_private_key(context) != 1)
        {
            OPENSSL_cleanse(request.tls_client_private_key_passphrase.data(),
                            request.tls_client_private_key_passphrase.size());
            net_v8_set_diag(request, SL_DIAG_HTTP_CLIENT_TLS_CERTIFICATE_VALIDATION_FAILED,
                            SL_STATUS_INVALID_ARGUMENT,
                            "HTTP client TLS client private key could not be loaded");
            return false;
        }
        OPENSSL_cleanse(request.tls_client_private_key_passphrase.data(),
                        request.tls_client_private_key_passphrase.size());
    }
    return true;
}

bool net_v8_tls_attach(NetV8Connection& connection, SlV8NetRequest& request)
{
    SSL* ssl = nullptr;
    BIO* read_bio = nullptr;
    BIO* write_bio = nullptr;

    if (!net_v8_tls_configure_context(connection, request)) {
        return false;
    }
    ssl = SSL_new(connection.tls_context);
    read_bio = BIO_new(BIO_s_mem());
    write_bio = BIO_new(BIO_s_mem());
    if (ssl == nullptr || read_bio == nullptr || write_bio == nullptr) {
        if (ssl != nullptr) {
            SSL_free(ssl);
        }
        if (read_bio != nullptr) {
            BIO_free(read_bio);
        }
        if (write_bio != nullptr) {
            BIO_free(write_bio);
        }
        net_v8_set_diag(request, SL_DIAG_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE,
                        SL_STATUS_OUT_OF_MEMORY, "HTTP client TLS state allocation failed");
        return false;
    }

    SSL_set_bio(ssl, read_bio, write_bio);
    connection.tls_ssl = ssl;
    connection.tls_read_bio = read_bio;
    connection.tls_write_bio = write_bio;
    SSL_set_connect_state(ssl);
    SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
    if (!request.tls_alpn_protocols.empty() &&
        SSL_set_alpn_protos(ssl, request.tls_alpn_protocols.data(),
                            static_cast<unsigned int>(request.tls_alpn_protocols.size())) != 0)
    {
        net_v8_set_diag(request, SL_DIAG_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE,
                        SL_STATUS_INVALID_ARGUMENT, "HTTP client TLS ALPN configuration failed");
        return false;
    }
    if (!request.tls_server_name.empty()) {
        bool server_name_is_ip = false;
        if (!request.tls_insecure_skip_verify) {
            X509_VERIFY_PARAM* params = SSL_get0_param(ssl);
            if (params != nullptr &&
                X509_VERIFY_PARAM_set1_ip_asc(params, request.tls_server_name.c_str()) == 1)
            {
                server_name_is_ip = true;
            }
            else if (params == nullptr || SSL_set1_host(ssl, request.tls_server_name.c_str()) != 1)
            {
                net_v8_set_diag(request, SL_DIAG_HTTP_CLIENT_TLS_HOSTNAME_MISMATCH,
                                SL_STATUS_INVALID_ARGUMENT,
                                "HTTP client TLS hostname verifier could not be configured");
                return false;
            }
        }
        if (!server_name_is_ip) {
            (void)SSL_set_tlsext_host_name(ssl, request.tls_server_name.c_str());
        }
    }
    return true;
}

void net_v8_tls_failure_from_ssl(SlV8NetRequest& request, SSL* ssl, SlDiagCode fallback_code,
                                 const char* fallback_message)
{
    long verify = ssl == nullptr ? X509_V_ERR_UNSPECIFIED : SSL_get_verify_result(ssl);
    if (verify == X509_V_ERR_HOSTNAME_MISMATCH) {
        net_v8_set_diag(request, SL_DIAG_HTTP_CLIENT_TLS_HOSTNAME_MISMATCH,
                        SL_STATUS_INVALID_ARGUMENT, "HTTP client TLS hostname mismatch");
        return;
    }
    if (verify != X509_V_OK) {
        net_v8_set_diag(request, SL_DIAG_HTTP_CLIENT_TLS_CERTIFICATE_VALIDATION_FAILED,
                        SL_STATUS_INVALID_ARGUMENT,
                        "HTTP client TLS certificate validation failed");
        return;
    }
    net_v8_set_diag(request, fallback_code, SL_STATUS_INTERNAL, fallback_message);
}

bool net_v8_tls_handshake(NetV8Connection& connection, SlV8NetRequest& request)
{
    while (!request.cancelled.load()) {
        int rc = SSL_connect(connection.tls_ssl);
        SlStatus drain_status = net_v8_tls_drain_write_bio(connection, &request.diag);
        if (!sl_status_is_ok(drain_status)) {
            request.status = drain_status;
            return false;
        }
        if (rc == 1) {
            const unsigned char* selected = nullptr;
            unsigned int selected_length = 0U;
            SSL_get0_alpn_selected(connection.tls_ssl, &selected, &selected_length);
            if (selected != nullptr && selected_length > 0U) {
                request.tls_selected_alpn.assign(reinterpret_cast<const char*>(selected),
                                                 selected_length);
            }
            connection.tls_active = true;
            return true;
        }
        int error = SSL_get_error(connection.tls_ssl, rc);
        if (error == SSL_ERROR_WANT_READ) {
            SlStatus feed_status = net_v8_tls_feed_tcp(connection, connection.arena, &request.diag);
            if (!sl_status_is_ok(feed_status)) {
                request.status = feed_status;
                return false;
            }
            continue;
        }
        if (error == SSL_ERROR_WANT_WRITE) {
            continue;
        }
        net_v8_tls_failure_from_ssl(request, connection.tls_ssl,
                                    SL_DIAG_HTTP_CLIENT_TLS_CERTIFICATE_VALIDATION_FAILED,
                                    "HTTP client TLS handshake failed");
        return false;
    }
    net_v8_set_diag(request, SL_DIAG_HTTP_CLIENT_REQUEST_CANCELLED, SL_STATUS_CANCELLED,
                    "HTTP client TLS handshake was cancelled");
    return false;
}

SlStatus net_v8_tls_write(NetV8Connection& connection, SlBytes bytes, SlDiag* out_diag)
{
    size_t offset = 0U;
    if (connection.tls_ssl == nullptr || !connection.tls_active) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    while (offset < bytes.length) {
        size_t remaining = bytes.length - offset;
        int chunk =
            remaining > static_cast<size_t>(INT_MAX) ? INT_MAX : static_cast<int>(remaining);
        int written = SSL_write(connection.tls_ssl, bytes.ptr + offset, chunk);
        SlStatus drain_status = net_v8_tls_drain_write_bio(connection, out_diag);
        if (!sl_status_is_ok(drain_status)) {
            return drain_status;
        }
        if (written > 0) {
            offset += static_cast<size_t>(written);
            continue;
        }
        int error = SSL_get_error(connection.tls_ssl, written);
        if (error == SSL_ERROR_WANT_WRITE) {
            continue;
        }
        if (error == SSL_ERROR_WANT_READ) {
            SlStatus feed_status = net_v8_tls_feed_tcp(connection, connection.arena, out_diag);
            if (!sl_status_is_ok(feed_status)) {
                return feed_status;
            }
            continue;
        }
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    return sl_status_ok();
}

SlStatus net_v8_tls_read(NetV8Connection& connection, SlArena* arena, size_t max_bytes,
                         SlOwnedBytes* out, SlDiag* out_diag)
{
    std::vector<unsigned char> plain(max_bytes == 0U ? 8192U : max_bytes);
    if (connection.tls_ssl == nullptr || !connection.tls_active || arena == nullptr ||
        out == nullptr)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (plain.size() > static_cast<size_t>(INT_MAX)) {
        plain.resize(static_cast<size_t>(INT_MAX));
    }
    for (;;) {
        int read_count = SSL_read(connection.tls_ssl, plain.data(), static_cast<int>(plain.size()));
        SlStatus drain_status = net_v8_tls_drain_write_bio(connection, out_diag);
        if (!sl_status_is_ok(drain_status)) {
            return drain_status;
        }
        if (read_count > 0) {
            return sl_bytes_copy_to_arena(
                arena, sl_bytes_from_parts(plain.data(), static_cast<size_t>(read_count)), out);
        }
        int error = SSL_get_error(connection.tls_ssl, read_count);
        if (error == SSL_ERROR_WANT_READ) {
            SlStatus feed_status = net_v8_tls_feed_tcp(connection, *arena, out_diag);
            if (!sl_status_is_ok(feed_status)) {
                return feed_status;
            }
            continue;
        }
        if (error == SSL_ERROR_WANT_WRITE) {
            continue;
        }
        if (error == SSL_ERROR_ZERO_RETURN) {
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
}

void net_v8_tls_release(NetV8Connection& connection)
{
    if (connection.tls_ssl != nullptr) {
        SSL_free(connection.tls_ssl);
        connection.tls_ssl = nullptr;
        connection.tls_read_bio = nullptr;
        connection.tls_write_bio = nullptr;
    }
    if (connection.tls_context != nullptr) {
        SSL_CTX_free(connection.tls_context);
        connection.tls_context = nullptr;
    }
    connection.tls_active = false;
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
                if (connection->tls_active && connection->tls_ssl != nullptr) {
                    (void)SSL_shutdown(connection->tls_ssl);
                    (void)net_v8_tls_drain_write_bio(*connection, nullptr);
                }
                net_v8_tls_release(*connection);
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

void net_v8_local_resource_cleanup(void* ptr, void* user)
{
    auto* holder = static_cast<std::shared_ptr<NetV8LocalConnection>*>(ptr);
    (void)user;
    if (holder != nullptr) {
        std::shared_ptr<NetV8LocalConnection> connection = *holder;
        if (connection != nullptr) {
            std::unique_lock<std::mutex> owner_lock;
            if (connection->server_owner != nullptr) {
                owner_lock = std::unique_lock<std::mutex>(connection->server_owner->mutex);
            }
            std::lock_guard<std::mutex> lock(connection->mutex);
            if (connection->native != nullptr && !connection->closed) {
                (void)sl_local_connection_close(connection->native, nullptr);
                connection->closed = true;
            }
        }
        delete holder;
    }
}

void net_v8_local_server_resource_cleanup(void* ptr, void* user)
{
    auto* holder = static_cast<std::shared_ptr<NetV8LocalServer>*>(ptr);
    (void)user;
    if (holder != nullptr) {
        std::shared_ptr<NetV8LocalServer> server = *holder;
        if (server != nullptr) {
            std::lock_guard<std::mutex> lock(server->mutex);
            if (server->native != nullptr && !server->closed) {
                if (server->accepting) {
                    server->close_requested = true;
                    server->closed = true;
                    delete holder;
                    return;
                }
                if (sl_status_is_ok(sl_local_server_close(server->native, nullptr))) {
                    server->closed = true;
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

bool net_v8_lookup_local_connection(SlV8Engine* backend, SlResourceId id,
                                    std::shared_ptr<NetV8LocalConnection>* out)
{
    void* ptr = nullptr;
    if (backend == nullptr || out == nullptr ||
        !sl_status_is_ok(sl_resource_table_get(&backend->resources, id,
                                               SL_RESOURCE_KIND_LOCAL_CONNECTION, &ptr, nullptr)))
    {
        return false;
    }
    auto* holder = static_cast<std::shared_ptr<NetV8LocalConnection>*>(ptr);
    if (holder == nullptr || *holder == nullptr) {
        return false;
    }
    *out = *holder;
    return true;
}

bool net_v8_lookup_local_server(SlV8Engine* backend, SlResourceId id,
                                std::shared_ptr<NetV8LocalServer>* out)
{
    void* ptr = nullptr;
    if (backend == nullptr || out == nullptr ||
        !sl_status_is_ok(sl_resource_table_get(&backend->resources, id,
                                               SL_RESOURCE_KIND_LOCAL_SERVER, &ptr, nullptr)))
    {
        return false;
    }
    auto* holder = static_cast<std::shared_ptr<NetV8LocalServer>*>(ptr);
    if (holder == nullptr || *holder == nullptr) {
        return false;
    }
    *out = *holder;
    return true;
}

bool net_v8_is_local_operation(NetV8Operation operation)
{
    return operation == NetV8Operation::LocalConnect || operation == NetV8Operation::LocalListen ||
           operation == NetV8Operation::LocalAccept || operation == NetV8Operation::LocalWrite ||
           operation == NetV8Operation::LocalRead || operation == NetV8Operation::LocalReadLine ||
           operation == NetV8Operation::LocalReadUntil || operation == NetV8Operation::LocalClose ||
           operation == NetV8Operation::LocalAbort ||
           operation == NetV8Operation::LocalCloseServer ||
           operation == NetV8Operation::LocalAbortServer;
}

SlLocalEndpointBackend net_v8_default_local_backend(void)
{
#ifdef _WIN32
    return SL_LOCAL_ENDPOINT_BACKEND_NAMED_PIPE;
#else
    return SL_LOCAL_ENDPOINT_BACKEND_UNIX;
#endif
}

bool net_v8_is_runtime_path_segment_char(char ch)
{
    return ch == '.' || ch == '_' || ch == '-' || (ch >= '0' && ch <= '9') ||
           (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

bool net_v8_runtime_path_to_local_path(const std::string& logical, SlLocalEndpointBackend backend,
                                       std::string* out)
{
    constexpr const char* prefix = "runtime:/";
    if (out == nullptr || logical.rfind(prefix, 0U) != 0U) {
        return false;
    }
    std::string suffix = logical.substr(std::char_traits<char>::length(prefix));
    if (suffix.empty()) {
        return false;
    }
    if (suffix.front() == '/' || suffix.back() == '/' || suffix.find('\\') != std::string::npos) {
        return false;
    }

    std::string encoded_suffix;
    size_t segment_start = 0U;
    while (segment_start < suffix.size()) {
        size_t segment_end = suffix.find('/', segment_start);
        if (segment_end == std::string::npos) {
            segment_end = suffix.size();
        }
        if (segment_end == segment_start) {
            return false;
        }

        const std::string segment = suffix.substr(segment_start, segment_end - segment_start);
        if (segment == "." || segment == "..") {
            return false;
        }
        for (const char ch : segment) {
            if (!net_v8_is_runtime_path_segment_char(ch)) {
                return false;
            }
        }
        if (!encoded_suffix.empty()) {
            encoded_suffix += '~';
        }
        encoded_suffix += segment;
        segment_start = segment_end + 1U;
    }
    if (backend == SL_LOCAL_ENDPOINT_BACKEND_AUTO) {
        backend = net_v8_default_local_backend();
    }
    if (backend == SL_LOCAL_ENDPOINT_BACKEND_NAMED_PIPE) {
        *out = "\\\\.\\pipe\\sloppy-runtime-" + encoded_suffix;
        return true;
    }
    if (backend == SL_LOCAL_ENDPOINT_BACKEND_UNIX) {
#ifdef _WIN32
        return false;
#else
        *out = "/tmp/sloppy-runtime-" + encoded_suffix;
        return true;
#endif
    }
    return false;
}

void net_v8_worker(std::shared_ptr<SlV8NetRequest> request)
{
    if (request == nullptr || request->cancelled.load()) {
        return;
    }

    if (request->operation == NetV8Operation::Connect ||
        request->operation == NetV8Operation::ConnectTls)
    {
        auto connection = std::make_shared<NetV8Connection>();
        SlTcpConnectOptions options = sl_tcp_connect_options_default(
            sl_str_from_parts(request->host.data(), request->host.size()), request->port);
        options.has_timeout_ms = request->has_timeout_ms;
        options.timeout_ms = request->timeout_ms;
        options.no_delay = request->no_delay;
        request->status = sl_tcp_client_connect(&connection->arena, &options, &connection->native,
                                                &request->diag);
        if (sl_status_is_ok(request->status)) {
            if (request->operation == NetV8Operation::ConnectTls &&
                (!net_v8_tls_attach(*connection, *request) ||
                 !net_v8_tls_handshake(*connection, *request)))
            {
                (void)sl_tcp_connection_abort(connection->native, nullptr);
                connection->closed = true;
                net_v8_tls_release(*connection);
                return;
            }
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

    if (request->operation == NetV8Operation::LocalConnect) {
        auto connection = std::make_shared<NetV8LocalConnection>();
        std::string native_path;
        if (!net_v8_runtime_path_to_local_path(request->path, request->local_backend, &native_path))
        {
            request->status = sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            request->diag.code = SL_DIAG_NET_LOCAL_IPC_INVALID_PATH;
            request->diag.message = sl_str_from_cstr("local IPC endpoint path is invalid");
            return;
        }
        SlLocalConnectOptions options = sl_local_connect_options_default(
            sl_str_from_parts(native_path.data(), native_path.size()));
        options.backend = request->local_backend == SL_LOCAL_ENDPOINT_BACKEND_AUTO
                              ? net_v8_default_local_backend()
                              : request->local_backend;
        options.has_timeout_ms = request->has_timeout_ms;
        options.timeout_ms = request->timeout_ms;
        request->status = sl_local_endpoint_connect(&connection->arena, &options,
                                                    &connection->native, &request->diag);
        if (sl_status_is_ok(request->status)) {
            request->local_connection = connection;
        }
        return;
    }

    if (request->operation == NetV8Operation::LocalListen) {
        auto server = std::make_shared<NetV8LocalServer>();
        std::string native_path;
        if (!net_v8_runtime_path_to_local_path(request->path, request->local_backend, &native_path))
        {
            request->status = sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            request->diag.code = SL_DIAG_NET_LOCAL_IPC_INVALID_PATH;
            request->diag.message = sl_str_from_cstr("local IPC endpoint path is invalid");
            return;
        }
        SlLocalListenOptions options = sl_local_listen_options_default(
            sl_str_from_parts(native_path.data(), native_path.size()));
        options.backend = request->local_backend == SL_LOCAL_ENDPOINT_BACKEND_AUTO
                              ? net_v8_default_local_backend()
                              : request->local_backend;
        options.unlink_existing = request->unlink_existing;
        options.has_permissions = request->has_permissions;
        options.permissions = request->permissions;
        options.backlog = request->backlog;
        request->status =
            sl_local_endpoint_listen(&server->arena, &options, &server->native, &request->diag);
        if (sl_status_is_ok(request->status)) {
            request->local_server = server;
        }
        return;
    }

    if (request->operation == NetV8Operation::LocalAccept ||
        request->operation == NetV8Operation::LocalCloseServer ||
        request->operation == NetV8Operation::LocalAbortServer)
    {
        if (request->local_server == nullptr) {
            request->status = sl_status_from_code(SL_STATUS_STALE_RESOURCE);
            request->diag.code = SL_DIAG_NET_LOCAL_IPC_DISPOSED;
            request->diag.message = sl_str_from_cstr("local IPC server handle is stale");
            return;
        }
        if (request->operation == NetV8Operation::LocalAccept) {
            auto connection = std::make_shared<NetV8LocalConnection>();
            SlLocalAcceptOptions options = sl_local_accept_options_default();
            uint32_t remaining_timeout = request->timeout_ms;

            {
                std::lock_guard<std::mutex> lock(request->local_server->mutex);
                if (request->local_server->closed || request->local_server->accepting) {
                    request->status = sl_status_from_code(SL_STATUS_INVALID_STATE);
                    request->diag.code = SL_DIAG_NET_LOCAL_IPC_DISPOSED;
                    request->diag.message = sl_str_from_cstr("local IPC server is closed");
                    return;
                }
                request->local_server->accepting = true;
            }

            for (;;) {
                uint32_t chunk_timeout = 100U;
                if (request->has_timeout_ms && remaining_timeout < chunk_timeout) {
                    chunk_timeout = remaining_timeout;
                }
                if (chunk_timeout == 0U) {
                    request->status = sl_status_from_code(SL_STATUS_DEADLINE_EXCEEDED);
                    request->diag.code = SL_DIAG_NET_LOCAL_IPC_ACCEPT_CANCELLED;
                    request->diag.message =
                        sl_str_from_cstr("local IPC accept was cancelled or timed out");
                    break;
                }
                options.has_timeout_ms = true;
                options.timeout_ms = chunk_timeout;
                request->status =
                    sl_local_server_accept(request->local_server->native, &connection->arena,
                                           &options, &connection->native, &request->diag);
                if (sl_status_is_ok(request->status)) {
                    break;
                }
                {
                    std::lock_guard<std::mutex> lock(request->local_server->mutex);
                    if (request->local_server->closed || request->local_server->close_requested ||
                        request->local_server->abort_requested)
                    {
                        request->status = sl_status_from_code(SL_STATUS_INVALID_STATE);
                        request->diag.code = SL_DIAG_NET_LOCAL_IPC_DISPOSED;
                        request->diag.message = sl_str_from_cstr("local IPC server is closed");
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
                std::lock_guard<std::mutex> lock(request->local_server->mutex);
                request->local_server->accepting = false;
                if (request->local_server->close_requested ||
                    request->local_server->abort_requested)
                {
                    if (connection->native != nullptr) {
                        (void)sl_local_connection_abort(connection->native, nullptr);
                    }
                    if (request->local_server->abort_requested) {
                        (void)sl_local_server_abort(request->local_server->native, nullptr);
                    }
                    else {
                        (void)sl_local_server_close(request->local_server->native, nullptr);
                    }
                    request->local_server->closed = true;
                    request->status = sl_status_from_code(SL_STATUS_INVALID_STATE);
                    request->diag.code = SL_DIAG_NET_LOCAL_IPC_DISPOSED;
                    request->diag.message = sl_str_from_cstr("local IPC server is closed");
                }
                else if (sl_status_is_ok(request->status)) {
                    connection->server_owner = request->local_server;
                    request->local_connection = connection;
                }
            }
        }
        else if (request->operation == NetV8Operation::LocalCloseServer) {
            std::lock_guard<std::mutex> lock(request->local_server->mutex);
            if (request->local_server->closed) {
                request->status = sl_status_ok();
                return;
            }
            if (request->local_server->accepting) {
                request->local_server->close_requested = true;
                request->local_server->closed = true;
                request->status = sl_status_ok();
                return;
            }
            request->status = sl_local_server_close(request->local_server->native, &request->diag);
            if (sl_status_is_ok(request->status)) {
                request->local_server->closed = true;
            }
        }
        else if (request->operation == NetV8Operation::LocalAbortServer) {
            std::lock_guard<std::mutex> lock(request->local_server->mutex);
            if (request->local_server->closed) {
                request->status = sl_status_ok();
                return;
            }
            if (request->local_server->accepting) {
                request->local_server->abort_requested = true;
                request->local_server->closed = true;
                request->status = sl_status_ok();
                return;
            }
            request->status = sl_local_server_abort(request->local_server->native, &request->diag);
            if (sl_status_is_ok(request->status)) {
                request->local_server->closed = true;
            }
        }
        return;
    }

    if (net_v8_is_local_operation(request->operation)) {
        if (request->local_connection == nullptr) {
            request->status = sl_status_from_code(SL_STATUS_STALE_RESOURCE);
            request->diag.code = SL_DIAG_NET_LOCAL_IPC_DISPOSED;
            request->diag.message = sl_str_from_cstr("local IPC connection handle is stale");
            return;
        }

        std::unique_lock<std::mutex> owner_lock;
        if (request->local_connection->server_owner != nullptr) {
            owner_lock =
                std::unique_lock<std::mutex>(request->local_connection->server_owner->mutex);
        }
        std::lock_guard<std::mutex> lock(request->local_connection->mutex);
        if (request->local_connection->closed && request->operation != NetV8Operation::LocalClose &&
            request->operation != NetV8Operation::LocalAbort)
        {
            request->status = sl_status_from_code(SL_STATUS_INVALID_STATE);
            request->diag.code = SL_DIAG_NET_LOCAL_IPC_DISPOSED;
            request->diag.message = sl_str_from_cstr("local IPC connection is closed");
        }
        else if (request->operation == NetV8Operation::LocalWrite) {
            SlLocalIoOptions options = sl_local_io_options_default();
            options.has_timeout_ms = request->has_timeout_ms;
            options.timeout_ms = request->timeout_ms;
            request->status = sl_local_connection_write_ex(
                request->local_connection->native,
                sl_bytes_from_parts(request->bytes.data(), request->bytes.size()), &options,
                &request->diag);
        }
        else if (request->operation == NetV8Operation::LocalRead) {
            SlOwnedBytes bytes = {};
            SlLocalIoOptions options = sl_local_io_options_default();
            options.has_timeout_ms = request->has_timeout_ms;
            options.timeout_ms = request->timeout_ms;
            request->status = sl_local_connection_read_ex(
                request->local_connection->native, &request->local_connection->arena,
                request->max_bytes, &options, &bytes, &request->diag);
            if (sl_status_is_ok(request->status)) {
                request->result_bytes.assign(bytes.ptr, bytes.ptr + bytes.length);
            }
        }
        else if (request->operation == NetV8Operation::LocalReadUntil) {
            SlOwnedBytes bytes = {};
            SlLocalIoOptions options = sl_local_io_options_default();
            options.has_timeout_ms = request->has_timeout_ms;
            options.timeout_ms = request->timeout_ms;
            request->status = sl_local_connection_read_until_ex(
                request->local_connection->native, &request->local_connection->arena,
                sl_bytes_from_parts(request->delimiter.data(), request->delimiter.size()),
                request->max_bytes, &options, &bytes, &request->diag);
            if (sl_status_is_ok(request->status)) {
                request->result_bytes.assign(bytes.ptr, bytes.ptr + bytes.length);
            }
        }
        else if (request->operation == NetV8Operation::LocalReadLine) {
            SlOwnedStr line = {};
            SlLocalIoOptions options = sl_local_io_options_default();
            options.has_timeout_ms = request->has_timeout_ms;
            options.timeout_ms = request->timeout_ms;
            request->status = sl_local_connection_read_line_ex(
                request->local_connection->native, &request->local_connection->arena,
                request->max_bytes, &options, &line, &request->diag);
            if (sl_status_is_ok(request->status)) {
                request->result_text.assign(line.ptr, line.length);
            }
        }
        else if (request->operation == NetV8Operation::LocalClose) {
            request->status =
                sl_local_connection_close(request->local_connection->native, &request->diag);
            request->local_connection->closed = true;
        }
        else if (request->operation == NetV8Operation::LocalAbort) {
            request->status =
                sl_local_connection_abort(request->local_connection->native, &request->diag);
            request->local_connection->closed = true;
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
        if (request->connection->tls_active) {
            request->status = net_v8_tls_write(
                *request->connection,
                sl_bytes_from_parts(request->bytes.data(), request->bytes.size()), &request->diag);
            if (!sl_status_is_ok(request->status) && request->diag.code == SL_DIAG_NONE) {
                request->diag.code = SL_DIAG_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE;
                request->diag.message = sl_str_from_cstr("HTTP client TLS write failed");
            }
        }
        else {
            request->status = sl_tcp_connection_write(
                request->connection->native,
                sl_bytes_from_parts(request->bytes.data(), request->bytes.size()), &request->diag);
        }
    }
    else if (request->operation == NetV8Operation::Read) {
        SlOwnedBytes bytes = {};
        request->status =
            request->connection->tls_active
                ? net_v8_tls_read(*request->connection, &request->connection->arena,
                                  request->max_bytes, &bytes, &request->diag)
                : sl_tcp_connection_read(request->connection->native, &request->connection->arena,
                                         request->max_bytes, &bytes, &request->diag);
        if (!sl_status_is_ok(request->status) && request->diag.code == SL_DIAG_NONE) {
            request->diag.code = SL_DIAG_NET_CONNECTION_CLOSED;
            request->diag.message = sl_str_from_cstr("TCP connection is closed");
        }
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
        if (request->connection->tls_active && request->connection->tls_ssl != nullptr) {
            (void)SSL_shutdown(request->connection->tls_ssl);
            (void)net_v8_tls_drain_write_bio(*request->connection, nullptr);
        }
        net_v8_tls_release(*request->connection);
        request->status = sl_tcp_connection_close(request->connection->native, &request->diag);
        request->connection->closed = true;
    }
    else if (request->operation == NetV8Operation::Abort) {
        net_v8_tls_release(*request->connection);
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
             request->operation == NetV8Operation::ConnectTls ||
             request->operation == NetV8Operation::Accept ||
             request->operation == NetV8Operation::LocalConnect ||
             request->operation == NetV8Operation::LocalAccept)
    {
        if (net_v8_is_local_operation(request->operation)) {
            auto* holder =
                new (std::nothrow) std::shared_ptr<NetV8LocalConnection>(request->local_connection);
            SlResourceId id = {};
            v8::Local<v8::Object> handle;
            if (holder == nullptr ||
                !sl_status_is_ok(sl_resource_table_insert(
                    &backend->resources, SL_RESOURCE_KIND_LOCAL_CONNECTION, holder,
                    net_v8_local_resource_cleanup, nullptr, &id, nullptr)) ||
                !net_v8_resource_to_handle(isolate, context, id, &handle))
            {
                delete holder;
                ok = resolver
                         ->Reject(context,
                                  v8::Exception::Error(v8::String::NewFromUtf8Literal(
                                      isolate,
                                      "SLOPPY_E_NET_BACKEND_UNAVAILABLE: resource insert failed")))
                         .FromMaybe(false);
            }
            else {
                ok = resolver->Resolve(context, handle).FromMaybe(false);
            }
            isolate->PerformMicrotaskCheckpoint();
            return ok ? sl_status_ok() : sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
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
            if (request->operation == NetV8Operation::ConnectTls &&
                !request->tls_selected_alpn.empty() &&
                !net_v8_set_string(isolate, context, handle, "selectedProtocol",
                                   request->tls_selected_alpn))
            {
                (void)sl_resource_table_close_kind(&backend->resources, id,
                                                   SL_RESOURCE_KIND_TCP_CONNECTION, nullptr);
                ok = resolver
                         ->Reject(
                             context,
                             v8::Exception::Error(v8::String::NewFromUtf8Literal(
                                 isolate,
                                 "SLOPPY_E_NET_BACKEND_UNAVAILABLE: selected protocol set failed")))
                         .FromMaybe(false);
                isolate->PerformMicrotaskCheckpoint();
                return ok ? sl_status_ok() : sl_status_from_code(SL_STATUS_INVALID_STATE);
            }
            ok = resolver->Resolve(context, handle).FromMaybe(false);
        }
    }
    else if (request->operation == NetV8Operation::Listen ||
             request->operation == NetV8Operation::LocalListen)
    {
        if (request->operation == NetV8Operation::LocalListen) {
            auto* holder =
                new (std::nothrow) std::shared_ptr<NetV8LocalServer>(request->local_server);
            SlResourceId id = {};
            v8::Local<v8::Object> handle;
            if (holder == nullptr ||
                !sl_status_is_ok(sl_resource_table_insert(
                    &backend->resources, SL_RESOURCE_KIND_LOCAL_SERVER, holder,
                    net_v8_local_server_resource_cleanup, nullptr, &id, nullptr)) ||
                !net_v8_resource_to_handle(isolate, context, id, &handle))
            {
                delete holder;
                ok = resolver
                         ->Reject(context,
                                  v8::Exception::Error(v8::String::NewFromUtf8Literal(
                                      isolate,
                                      "SLOPPY_E_NET_BACKEND_UNAVAILABLE: resource insert failed")))
                         .FromMaybe(false);
            }
            else {
                ok = resolver->Resolve(context, handle).FromMaybe(false);
            }
            isolate->PerformMicrotaskCheckpoint();
            return ok ? sl_status_ok() : sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
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
             request->operation == NetV8Operation::ReadUntil ||
             request->operation == NetV8Operation::LocalRead ||
             request->operation == NetV8Operation::LocalReadUntil)
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
    else if (request->operation == NetV8Operation::ReadLine ||
             request->operation == NetV8Operation::LocalReadLine)
    {
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
        if ((request->operation == NetV8Operation::LocalClose ||
             request->operation == NetV8Operation::LocalAbort) &&
            sl_resource_id_is_valid(request->resource_id))
        {
            (void)sl_resource_table_close_kind(&backend->resources, request->resource_id,
                                               SL_RESOURCE_KIND_LOCAL_CONNECTION, nullptr);
        }
        if ((request->operation == NetV8Operation::CloseListener ||
             request->operation == NetV8Operation::AbortListener) &&
            sl_resource_id_is_valid(request->resource_id))
        {
            (void)sl_resource_table_close_kind(&backend->resources, request->resource_id,
                                               SL_RESOURCE_KIND_TCP_LISTENER, nullptr);
        }
        if ((request->operation == NetV8Operation::LocalCloseServer ||
             request->operation == NetV8Operation::LocalAbortServer) &&
            sl_resource_id_is_valid(request->resource_id))
        {
            (void)sl_resource_table_close_kind(&backend->resources, request->resource_id,
                                               SL_RESOURCE_KIND_LOCAL_SERVER, nullptr);
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

bool net_v8_object_get(v8::Isolate* isolate, v8::Local<v8::Context> context,
                       v8::Local<v8::Object> object, const char* key, v8::Local<v8::Value>* out);

bool net_v8_read_optional_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                 v8::Local<v8::Object> object, const char* key, std::string* out)
{
    v8::Local<v8::Value> value;
    if (out == nullptr) {
        return false;
    }
    if (!net_v8_object_get(isolate, context, object, key, &value) || value->IsUndefined()) {
        return true;
    }
    return sl_v8_std_string_from_value(isolate, value, out);
}

bool net_v8_read_optional_bool(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Object> object, const char* key, bool* out)
{
    v8::Local<v8::Value> value;
    if (out == nullptr) {
        return false;
    }
    if (!net_v8_object_get(isolate, context, object, key, &value) || value->IsUndefined()) {
        return true;
    }
    if (!value->IsBoolean()) {
        return false;
    }
    *out = value.As<v8::Boolean>()->Value();
    return true;
}

bool net_v8_parse_alpn_protocols(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                 v8::Local<v8::Object> options, SlV8NetRequest& request)
{
    v8::Local<v8::Value> value;
    if (!net_v8_object_get(isolate, context, options, "alpnProtocols", &value) ||
        value->IsUndefined())
    {
        return true;
    }
    if (!value->IsArray()) {
        return false;
    }

    v8::Local<v8::Array> protocols = value.As<v8::Array>();
    uint32_t length = protocols->Length();
    if (length == 0U || length > 8U) {
        return false;
    }
    request.tls_alpn_protocols.clear();
    for (uint32_t index = 0U; index < length; ++index) {
        v8::Local<v8::Value> item;
        std::string protocol;
        if (!protocols->Get(context, index).ToLocal(&item) ||
            !sl_v8_std_string_from_value(isolate, item, &protocol) || protocol.empty() ||
            protocol.size() > 255U || (protocol != "h2" && protocol != "http/1.1"))
        {
            return false;
        }
        request.tls_alpn_protocols.push_back(static_cast<unsigned char>(protocol.size()));
        request.tls_alpn_protocols.insert(request.tls_alpn_protocols.end(), protocol.begin(),
                                          protocol.end());
    }
    return true;
}

bool net_v8_parse_tls_options(v8::Isolate* isolate, v8::Local<v8::Context> context,
                              v8::Local<v8::Object> options, SlV8NetRequest& request)
{
    v8::Local<v8::Value> value;
    v8::Local<v8::Object> tls;
    if (!net_v8_object_get(isolate, context, options, "tls", &value) || value->IsUndefined()) {
        return true;
    }
    if (!value->IsObject()) {
        return false;
    }
    tls = value.As<v8::Object>();
    return net_v8_read_optional_string(isolate, context, tls, "caPath", &request.tls_ca_path) &&
           net_v8_read_optional_string(isolate, context, tls, "caBundlePath",
                                       &request.tls_ca_bundle_path) &&
           net_v8_read_optional_string(isolate, context, tls, "trustStorePath",
                                       &request.tls_trust_store_path) &&
           net_v8_read_optional_string(isolate, context, tls, "clientCertificatePath",
                                       &request.tls_client_certificate_path) &&
           net_v8_read_optional_string(isolate, context, tls, "clientPrivateKeyPath",
                                       &request.tls_client_private_key_path) &&
           net_v8_read_optional_string(isolate, context, tls, "clientPrivateKeyPassphrase",
                                       &request.tls_client_private_key_passphrase) &&
           net_v8_read_optional_bool(isolate, context, tls, "insecureSkipVerify",
                                     &request.tls_insecure_skip_verify);
}

void net_v8_connect_with_operation(const v8::FunctionCallbackInfo<v8::Value>& args,
                                   NetV8Operation operation)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Promise::Resolver> resolver;
    auto request = net_v8_make_request(args, operation, &resolver);
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
    if (operation == NetV8Operation::ConnectTls) {
        if (get("serverName", &value) && !value->IsUndefined() &&
            !sl_v8_std_string_from_value(isolate, value, &request->tls_server_name))
        {
            net_v8_throw_type_error(isolate, "__sloppy.net.connectTls serverName must be a string");
            return;
        }
        if (request->tls_server_name.empty()) {
            request->tls_server_name = request->host;
        }
        if (!net_v8_parse_tls_options(isolate, context, options, *request)) {
            net_v8_throw_type_error(isolate, "__sloppy.net.connectTls tls options are invalid");
            return;
        }
        if (!net_v8_parse_alpn_protocols(isolate, context, options, *request)) {
            net_v8_throw_type_error(isolate, "__sloppy.net.connectTls alpnProtocols are invalid");
            return;
        }
    }
    if (!net_v8_start_request(request)) {
        net_v8_throw_type_error(isolate, "__sloppy.net.connect could not start worker");
        return;
    }
    args.GetReturnValue().Set(resolver->GetPromise());
}

void net_v8_connect(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    net_v8_connect_with_operation(args, NetV8Operation::Connect);
}

void net_v8_connect_tls(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    net_v8_connect_with_operation(args, NetV8Operation::ConnectTls);
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

bool net_v8_object_get(v8::Isolate* isolate, v8::Local<v8::Context> context,
                       v8::Local<v8::Object> object, const char* key, v8::Local<v8::Value>* out)
{
    v8::Local<v8::String> name;
    return out != nullptr &&
           sl_status_is_ok(net_v8_to_local_string(isolate, sl_str_from_cstr(key), &name)) &&
           object->Get(context, name).ToLocal(out);
}

bool net_v8_parse_local_backend(v8::Isolate* isolate, v8::Local<v8::Value> value,
                                SlLocalEndpointBackend* out)
{
    std::string text;
    if (out == nullptr || value.IsEmpty() || value->IsUndefined()) {
        if (out != nullptr) {
            *out = SL_LOCAL_ENDPOINT_BACKEND_AUTO;
        }
        return true;
    }
    if (!sl_v8_std_string_from_value(isolate, value, &text)) {
        return false;
    }
    if (text == "unix") {
        *out = SL_LOCAL_ENDPOINT_BACKEND_UNIX;
        return true;
    }
    if (text == "namedPipe") {
        *out = SL_LOCAL_ENDPOINT_BACKEND_NAMED_PIPE;
        return true;
    }
    return false;
}

bool net_v8_parse_octal_permissions(const std::string& text, uint16_t* out)
{
    uint16_t mode = 0U;
    if (out == nullptr || text.size() != 4U || text[0] != '0') {
        return false;
    }
    for (size_t index = 1U; index < text.size(); ++index) {
        char ch = text[index];
        if (ch < '0' || ch > '7') {
            return false;
        }
        mode = static_cast<uint16_t>((mode << 3U) | static_cast<uint16_t>(ch - '0'));
    }
    *out = mode;
    return true;
}

void net_v8_local_endpoint(const v8::FunctionCallbackInfo<v8::Value>& args,
                           NetV8Operation operation)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Promise::Resolver> resolver;
    auto request = net_v8_make_request(args, operation, &resolver);
    v8::Local<v8::Object> options;
    v8::Local<v8::Value> value;
    uint32_t number = 0U;

    if (request == nullptr) {
        return;
    }
    if (args.Length() < 1 || !args[0]->IsObject()) {
        net_v8_throw_type_error(isolate, "__sloppy.net local endpoint operation requires options");
        return;
    }
    options = args[0].As<v8::Object>();
    if (!net_v8_object_get(isolate, context, options, "path", &value) ||
        !sl_v8_std_string_from_value(isolate, value, &request->path) || request->path.empty())
    {
        net_v8_throw_type_error(isolate, "__sloppy.net local endpoint path must be a string");
        return;
    }
    if (net_v8_object_get(isolate, context, options, "backend", &value) &&
        !net_v8_parse_local_backend(isolate, value, &request->local_backend))
    {
        net_v8_throw_type_error(isolate, "__sloppy.net local endpoint backend is invalid");
        return;
    }
    if (net_v8_object_get(isolate, context, options, "timeoutMs", &value) && !value->IsUndefined())
    {
        if (!net_v8_parse_uint(value, 0U, UINT32_MAX, &number)) {
            net_v8_throw_type_error(isolate, "__sloppy.net local endpoint timeoutMs is invalid");
            return;
        }
        request->has_timeout_ms = true;
        request->timeout_ms = number;
    }
    if (operation == NetV8Operation::LocalListen) {
        if (net_v8_object_get(isolate, context, options, "unlinkExisting", &value) &&
            value->IsBoolean())
        {
            request->unlink_existing = value.As<v8::Boolean>()->Value();
        }
        if (net_v8_object_get(isolate, context, options, "permissions", &value) &&
            !value->IsUndefined())
        {
            std::string permissions;
            if (!sl_v8_std_string_from_value(isolate, value, &permissions) ||
                !net_v8_parse_octal_permissions(permissions, &request->permissions))
            {
                net_v8_throw_type_error(isolate,
                                        "__sloppy.net local endpoint permissions are invalid");
                return;
            }
            request->has_permissions = true;
        }
        if (net_v8_object_get(isolate, context, options, "backlog", &value) &&
            !value->IsUndefined())
        {
            if (!net_v8_parse_uint(value, 1U, UINT32_MAX, &number)) {
                net_v8_throw_type_error(isolate, "__sloppy.net local endpoint backlog is invalid");
                return;
            }
            request->backlog = number;
        }
    }
    if (!net_v8_start_request(request)) {
        net_v8_throw_type_error(isolate,
                                "__sloppy.net local endpoint operation could not start worker");
        return;
    }
    args.GetReturnValue().Set(resolver->GetPromise());
}

void net_v8_with_local_server(const v8::FunctionCallbackInfo<v8::Value>& args,
                              NetV8Operation operation)
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
        !net_v8_lookup_local_server(request->backend, id, &request->local_server))
    {
        net_v8_throw_type_error(isolate, "__sloppy.net local server handle is stale or closed");
        return;
    }
    request->resource_id = id;
    if (operation == NetV8Operation::LocalAccept && args.Length() >= 2 && !args[1]->IsUndefined()) {
        if (!net_v8_parse_uint(args[1], 0U, UINT32_MAX, &timeout_ms)) {
            net_v8_throw_type_error(isolate, "__sloppy.net.acceptLocal timeoutMs is invalid");
            return;
        }
        request->has_timeout_ms = true;
        request->timeout_ms = timeout_ms;
    }
    if (!net_v8_start_request(request)) {
        net_v8_throw_type_error(isolate,
                                "__sloppy.net local server operation could not start worker");
        return;
    }
    args.GetReturnValue().Set(resolver->GetPromise());
}

void net_v8_with_local_connection(const v8::FunctionCallbackInfo<v8::Value>& args,
                                  NetV8Operation operation)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Promise::Resolver> resolver;
    auto request = net_v8_make_request(args, operation, &resolver);
    SlResourceId id = {};
    uint32_t number = 0U;

    if (request == nullptr) {
        return;
    }
    if (args.Length() < 1 || !net_v8_handle_to_resource(isolate, context, args[0], &id) ||
        !net_v8_lookup_local_connection(request->backend, id, &request->local_connection))
    {
        net_v8_throw_type_error(isolate, "__sloppy.net local connection handle is stale or closed");
        return;
    }
    request->resource_id = id;
    if (operation == NetV8Operation::LocalWrite) {
        if (args.Length() < 2 || !net_v8_bytes_arg(args[1], kNetMaxBufferBytes, &request->bytes)) {
            net_v8_throw_type_error(isolate, "__sloppy.net.writeLocal requires bounded bytes");
            return;
        }
        if (args.Length() >= 3 && !args[2]->IsUndefined()) {
            if (!net_v8_parse_uint(args[2], 0U, UINT32_MAX, &number)) {
                net_v8_throw_type_error(isolate, "__sloppy.net.writeLocal timeoutMs is invalid");
                return;
            }
            request->has_timeout_ms = true;
            request->timeout_ms = number;
        }
    }
    else if (operation == NetV8Operation::LocalRead || operation == NetV8Operation::LocalReadLine) {
        uint32_t max_bytes = 8192U;
        if (args.Length() >= 2 && !args[1]->IsUndefined() &&
            !net_v8_parse_uint(args[1], 1U, kNetMaxBufferBytes, &max_bytes))
        {
            net_v8_throw_type_error(isolate, "__sloppy.net local read size is invalid");
            return;
        }
        request->max_bytes = max_bytes;
        if (args.Length() >= 3 && !args[2]->IsUndefined()) {
            if (!net_v8_parse_uint(args[2], 0U, UINT32_MAX, &number)) {
                net_v8_throw_type_error(isolate, "__sloppy.net local read timeoutMs is invalid");
                return;
            }
            request->has_timeout_ms = true;
            request->timeout_ms = number;
        }
    }
    else if (operation == NetV8Operation::LocalReadUntil) {
        uint32_t max_bytes = 8192U;
        if (args.Length() < 2 ||
            !net_v8_bytes_arg(args[1], kNetMaxBufferBytes, &request->delimiter))
        {
            net_v8_throw_type_error(isolate,
                                    "__sloppy.net.readUntilLocal requires delimiter bytes");
            return;
        }
        if (args.Length() >= 3 && !args[2]->IsUndefined() &&
            !net_v8_parse_uint(args[2], 1U, kNetMaxBufferBytes, &max_bytes))
        {
            net_v8_throw_type_error(isolate, "__sloppy.net local read size is invalid");
            return;
        }
        request->max_bytes = max_bytes;
        if (args.Length() >= 4 && !args[3]->IsUndefined()) {
            if (!net_v8_parse_uint(args[3], 0U, UINT32_MAX, &number)) {
                net_v8_throw_type_error(isolate, "__sloppy.net local read timeoutMs is invalid");
                return;
            }
            request->has_timeout_ms = true;
            request->timeout_ms = number;
        }
    }
    if (!net_v8_start_request(request)) {
        net_v8_throw_type_error(isolate, "__sloppy.net local operation could not start worker");
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

void net_v8_connect_local(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    net_v8_local_endpoint(args, NetV8Operation::LocalConnect);
}

void net_v8_listen_local(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    net_v8_local_endpoint(args, NetV8Operation::LocalListen);
}

void net_v8_accept_local(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    net_v8_with_local_server(args, NetV8Operation::LocalAccept);
}

void net_v8_close_local_server(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    net_v8_with_local_server(args, NetV8Operation::LocalCloseServer);
}

void net_v8_abort_local_server(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    net_v8_with_local_server(args, NetV8Operation::LocalAbortServer);
}

void net_v8_write_local(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    net_v8_with_local_connection(args, NetV8Operation::LocalWrite);
}

void net_v8_read_local(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    net_v8_with_local_connection(args, NetV8Operation::LocalRead);
}

void net_v8_read_line_local(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    net_v8_with_local_connection(args, NetV8Operation::LocalReadLine);
}

void net_v8_read_until_local(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    net_v8_with_local_connection(args, NetV8Operation::LocalReadUntil);
}

void net_v8_close_local(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    net_v8_with_local_connection(args, NetV8Operation::LocalClose);
}

void net_v8_abort_local(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    net_v8_with_local_connection(args, NetV8Operation::LocalAbort);
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
        !net_v8_set_function(isolate, context, net, "connectTls", net_v8_connect_tls) ||
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
        !net_v8_set_function(isolate, context, net, "connectLocal", net_v8_connect_local) ||
        !net_v8_set_function(isolate, context, net, "listenLocal", net_v8_listen_local) ||
        !net_v8_set_function(isolate, context, net, "acceptLocal", net_v8_accept_local) ||
        !net_v8_set_function(isolate, context, net, "writeLocal", net_v8_write_local) ||
        !net_v8_set_function(isolate, context, net, "readLocal", net_v8_read_local) ||
        !net_v8_set_function(isolate, context, net, "readLineLocal", net_v8_read_line_local) ||
        !net_v8_set_function(isolate, context, net, "readUntilLocal", net_v8_read_until_local) ||
        !net_v8_set_function(isolate, context, net, "closeLocal", net_v8_close_local) ||
        !net_v8_set_function(isolate, context, net, "abortLocal", net_v8_abort_local) ||
        !net_v8_set_function(isolate, context, net, "closeLocalServer",
                             net_v8_close_local_server) ||
        !net_v8_set_function(isolate, context, net, "abortLocalServer",
                             net_v8_abort_local_server) ||
        !net_v8_set_bool(isolate, context, net, "tlsCaPath", true) ||
        !net_v8_set_bool(isolate, context, net, "tlsCaBundlePath", true) ||
        !net_v8_set_bool(isolate, context, net, "tlsTrustStorePath", true) ||
        !net_v8_set_bool(isolate, context, net, "tlsClientCertificate", true) ||
        !net_v8_set_bool(isolate, context, net, "tlsInsecureSkipVerify", true) ||
        !net_v8_set_bool(isolate, context, net, "tlsAlpn", true) ||
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
