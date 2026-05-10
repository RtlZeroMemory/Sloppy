#include "sloppy/async_backend.h"

#include <atomic>
#include <climits>
#include <cstddef>
#include <cstring>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#define LIBUV_MAX_RECORDS 128U

typedef struct LibuvRecord
{
    int values[LIBUV_MAX_RECORDS];
    size_t count;
    size_t cleanup_count;
    size_t retain_count;
    size_t release_count;
    bool terminal;
    size_t late_count;
} LibuvRecord;

typedef struct LibuvPayload
{
    LibuvRecord* record;
    int value;
} LibuvPayload;

typedef struct LibuvIoWatchRecord
{
    size_t callback_count;
    unsigned last_events;
    SlStatusCode last_status;
    bool owner_thread;
} LibuvIoWatchRecord;

#ifdef _WIN32
typedef SOCKET LibuvSocket;
static constexpr LibuvSocket LIBUV_INVALID_SOCKET = INVALID_SOCKET;
#else
typedef int LibuvSocket;
static constexpr LibuvSocket LIBUV_INVALID_SOCKET = -1;
#endif

typedef struct LibuvSocketPair
{
    LibuvSocket watched;
    LibuvSocket peer;
} LibuvSocketPair;

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static void yield_briefly(void)
{
    std::this_thread::yield();
}

#ifdef _WIN32
static bool ensure_winsock_started(void)
{
    static std::atomic_bool initialized = false;
    static std::atomic_bool ready = false;

    if (initialized.load()) {
        return ready.load();
    }

    WSADATA data = {};
    bool expected = false;
    bool started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    if (initialized.compare_exchange_strong(expected, true)) {
        ready.store(started);
    }
    while (!initialized.load()) {
        yield_briefly();
    }
    return ready.load();
}

static int libuv_close_socket(LibuvSocket socket)
{
    return closesocket(socket);
}
#else
static bool ensure_winsock_started(void)
{
    return true;
}

static int libuv_close_socket(LibuvSocket socket)
{
    return close(socket);
}
#endif

static void close_socket_pair(LibuvSocketPair* pair)
{
    if (pair == nullptr) {
        return;
    }
    if (pair->watched != LIBUV_INVALID_SOCKET) {
        (void)libuv_close_socket(pair->watched);
        pair->watched = LIBUV_INVALID_SOCKET;
    }
    if (pair->peer != LIBUV_INVALID_SOCKET) {
        (void)libuv_close_socket(pair->peer);
        pair->peer = LIBUV_INVALID_SOCKET;
    }
}

static bool create_socket_pair(LibuvSocketPair* out_pair)
{
    if (out_pair == nullptr || !ensure_winsock_started()) {
        return false;
    }

    *out_pair = {LIBUV_INVALID_SOCKET, LIBUV_INVALID_SOCKET};

#ifdef _WIN32
    SOCKET listener = INVALID_SOCKET;
    SOCKET client = INVALID_SOCKET;
    SOCKET server = INVALID_SOCKET;
    sockaddr_in address = {};
    sockaddr_in bound = {};
    int bound_length = sizeof(bound);
    int reuse = 1;

    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        return false;
    }
    (void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listener, (const sockaddr*)&address, sizeof(address)) != 0 ||
        listen(listener, 1) != 0 || getsockname(listener, (sockaddr*)&bound, &bound_length) != 0)
    {
        (void)closesocket(listener);
        return false;
    }

    client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET || connect(client, (const sockaddr*)&bound, sizeof(bound)) != 0) {
        if (client != INVALID_SOCKET) {
            (void)closesocket(client);
        }
        (void)closesocket(listener);
        return false;
    }

    server = accept(listener, nullptr, nullptr);
    (void)closesocket(listener);
    if (server == INVALID_SOCKET) {
        (void)closesocket(client);
        return false;
    }

    out_pair->watched = server;
    out_pair->peer = client;
    return true;
#else
    int fds[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        return false;
    }
    out_pair->watched = fds[0];
    out_pair->peer = fds[1];
    return true;
#endif
}

static bool socket_fits_async_backend(LibuvSocket socket)
{
#ifdef _WIN32
    return socket != LIBUV_INVALID_SOCKET && socket <= static_cast<LibuvSocket>(INT_MAX);
#else
    return socket >= 0;
#endif
}

static bool send_single_byte(LibuvSocket socket, unsigned char value)
{
    const char byte = static_cast<char>(value);
#ifdef _WIN32
    return send(socket, &byte, 1, 0) == 1;
#else
    return send(socket, &byte, 1, 0) == 1;
#endif
}

static bool read_single_byte(LibuvSocket socket, unsigned char* out_value)
{
    char byte = 0;
    int read_result = 0;

    if (out_value == nullptr) {
        return false;
    }

#ifdef _WIN32
    read_result = recv(socket, &byte, 1, 0);
#else
    read_result = (int)recv(socket, &byte, 1, 0);
#endif
    if (read_result != 1) {
        return false;
    }

    *out_value = static_cast<unsigned char>(byte);
    return true;
}

template <typename Predicate>
static int drive_loop_until(SlAsyncLoop* loop, size_t max_attempts, Predicate predicate)
{
    size_t attempts = 0U;

    if (loop == nullptr) {
        return 1;
    }

    for (attempts = 0U; attempts < max_attempts; attempts += 1U) {
        size_t ran = 0U;
        if (expect_status(sl_async_loop_drain(loop, 1U, &ran), SL_STATUS_OK) != 0) {
            return 2;
        }
        if (predicate()) {
            return 0;
        }
        if (ran == 0U) {
            yield_briefly();
        }
    }

    return 3;
}

static SlStatus retain_scope(void* scope, void* user)
{
    (void)user;
    LibuvRecord* record = static_cast<LibuvRecord*>(scope);
    if (record != nullptr) {
        record->retain_count += 1U;
    }
    return sl_status_ok();
}

static void release_scope(void* scope, void* user)
{
    LibuvRecord* record = static_cast<LibuvRecord*>(scope);

    (void)user;
    if (record != nullptr) {
        record->release_count += 1U;
    }
}

static SlStatus record_completion(SlAsyncLoop* loop, const SlAsyncCompletion* completion,
                                  void* user)
{
    LibuvRecord* record = static_cast<LibuvRecord*>(user);
    LibuvPayload* payload =
        completion == nullptr ? nullptr : static_cast<LibuvPayload*>(completion->payload);
    size_t index = 0U;

    if (loop == nullptr || !sl_async_loop_is_owner_thread(loop) || record == nullptr ||
        payload == nullptr || payload->record != record || record->count >= LIBUV_MAX_RECORDS)
    {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    index = record->count;
    record->values[index] = payload->value;
    record->count += 1U;
    return sl_status_ok();
}

static void cleanup_completion(const SlAsyncCompletion* completion, void* user)
{
    LibuvRecord* record = static_cast<LibuvRecord*>(user);

    (void)completion;
    if (record != nullptr) {
        record->cleanup_count += 1U;
    }
}

static bool completion_scope_is_terminal(const SlAsyncCompletion* completion, void* user)
{
    LibuvRecord* record = static_cast<LibuvRecord*>(user);

    (void)completion;
    return record != nullptr && record->terminal;
}

static void record_late_completion(const SlAsyncCompletion* completion, void* user)
{
    LibuvRecord* record = static_cast<LibuvRecord*>(user);

    (void)completion;
    if (record != nullptr) {
        record->late_count += 1U;
    }
}

static void record_io_watch(SlAsyncLoop* loop, SlAsyncIoWatch* watch, unsigned events,
                            SlStatus status, void* user)
{
    LibuvIoWatchRecord* record = static_cast<LibuvIoWatchRecord*>(user);

    (void)watch;
    if (record == nullptr) {
        return;
    }

    record->callback_count += 1U;
    record->last_events = events;
    record->last_status = sl_status_code(status);
    record->owner_thread = sl_async_loop_is_owner_thread(loop);
}

static SlAsyncCompletion make_completion(LibuvRecord* record, LibuvPayload* payload, int value)
{
    payload->record = record;
    payload->value = value;

    SlAsyncCompletion completion = {};
    completion.kind = SL_ASYNC_COMPLETION_TEST;
    completion.status = sl_status_ok();
    completion.payload = payload;
    completion.scope.scope = record;
    completion.scope.retain = retain_scope;
    completion.scope.release = release_scope;
    completion.dispatch = record_completion;
    completion.dispatch_user = record;
    completion.cleanup = cleanup_completion;
    completion.cleanup_user = record;
    return completion;
}

static int test_libuv_create_post_drain_dispose(void)
{
    unsigned char arena_storage[4096];
    SlArena arena = {};
    SlAsyncCompletion storage[2];
    SlAsyncLoop* loop = nullptr;
    LibuvRecord record = {{0}, 0U, 0U, 0U, 0U, false, 0U};
    LibuvPayload payload;
    SlAsyncCompletion completion;
    size_t ran = 0U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_LIBUV, &arena, storage, 2U, &loop),
                      SL_STATUS_OK) != 0 ||
        loop == nullptr)
    {
        return 1;
    }

    if (sl_async_loop_backend_kind(loop) != SL_ASYNC_BACKEND_LIBUV ||
        sl_async_loop_capacity(loop) != 2U || !sl_async_loop_is_owner_thread(loop))
    {
        sl_async_loop_dispose(loop);
        return 2;
    }

    completion = make_completion(&record, &payload, 31);
    if (expect_status(sl_async_loop_post(loop, &completion), SL_STATUS_OK) != 0 ||
        expect_status(sl_async_loop_drain(loop, 0U, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        record.count != 1U || record.values[0] != 31 || record.cleanup_count != 1U ||
        record.release_count != 1U)
    {
        sl_async_loop_dispose(loop);
        return 3;
    }

    sl_async_loop_dispose(loop);
    if (!sl_async_loop_is_disposed(loop) ||
        expect_status(sl_async_loop_post(loop, &completion), SL_STATUS_INVALID_STATE) != 0)
    {
        return 4;
    }

    return 0;
}

static int test_libuv_cross_thread_post_owner_thread_dispatch(void)
{
    unsigned char arena_storage[4096];
    SlArena arena = {};
    SlAsyncCompletion storage[2];
    SlAsyncLoop* loop = nullptr;
    LibuvRecord record = {{0}, 0U, 0U, 0U, 0U, false, 0U};
    LibuvPayload payload;
    SlAsyncCompletion completion;
    SlStatus worker_status = sl_status_ok();
    bool worker_saw_owner = true;
    size_t ran = 0U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_LIBUV, &arena, storage, 2U, &loop),
                      SL_STATUS_OK) != 0)
    {
        return 10;
    }

    completion = make_completion(&record, &payload, 44);
    std::thread worker([&]() {
        worker_saw_owner = sl_async_loop_is_owner_thread(loop);
        worker_status = sl_async_loop_post(loop, &completion);
    });
    worker.join();

    if (worker_saw_owner || expect_status(worker_status, SL_STATUS_OK) != 0 || record.count != 0U ||
        sl_async_loop_pending_count(loop) != 1U)
    {
        sl_async_loop_dispose(loop);
        return 11;
    }

    if (expect_status(sl_async_loop_drain(loop, 0U, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        record.count != 1U || record.values[0] != 44 || record.cleanup_count != 1U ||
        record.release_count != 1U)
    {
        sl_async_loop_dispose(loop);
        return 12;
    }

    sl_async_loop_dispose(loop);
    return 0;
}

static int test_libuv_capacity_overflow_is_deterministic(void)
{
    unsigned char arena_storage[4096];
    SlArena arena = {};
    SlAsyncCompletion storage[1];
    SlAsyncLoop* loop = nullptr;
    LibuvRecord record = {{0}, 0U, 0U, 0U, 0U, false, 0U};
    LibuvPayload first;
    LibuvPayload second;
    SlAsyncCompletion first_completion;
    SlAsyncCompletion second_completion;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_LIBUV, &arena, storage, 1U, &loop),
                      SL_STATUS_OK) != 0)
    {
        return 20;
    }

    first_completion = make_completion(&record, &first, 1);
    second_completion = make_completion(&record, &second, 2);
    if (expect_status(sl_async_loop_post(loop, &first_completion), SL_STATUS_OK) != 0 ||
        expect_status(sl_async_loop_post(loop, &second_completion), SL_STATUS_CAPACITY_EXCEEDED) !=
            0 ||
        sl_async_loop_pending_count(loop) != 1U || record.cleanup_count != 0U ||
        record.release_count != 0U)
    {
        sl_async_loop_dispose(loop);
        return 21;
    }

    sl_async_loop_dispose(loop);
    if (record.cleanup_count != 1U || record.release_count != 1U) {
        return 22;
    }

    return 0;
}

static int test_libuv_wrong_thread_run_once_and_drain_rejected_before_callbacks(void)
{
    unsigned char arena_storage[4096];
    SlArena arena = {};
    SlAsyncCompletion storage[1];
    SlAsyncLoop* loop = nullptr;
    LibuvRecord record = {{0}, 0U, 0U, 0U, 0U, false, 0U};
    LibuvPayload payload;
    SlAsyncCompletion completion;
    SlStatus worker_run_once_status = sl_status_ok();
    SlStatus worker_drain_status = sl_status_ok();
    bool worker_saw_owner = true;
    size_t worker_run_once_ran = 99U;
    size_t worker_drain_ran = 99U;
    size_t ran = 0U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_LIBUV, &arena, storage, 1U, &loop),
                      SL_STATUS_OK) != 0)
    {
        return 30;
    }

    completion = make_completion(&record, &payload, 71);
    if (expect_status(sl_async_loop_post(loop, &completion), SL_STATUS_OK) != 0 ||
        record.retain_count != 1U || sl_async_loop_pending_count(loop) != 1U)
    {
        sl_async_loop_dispose(loop);
        return 31;
    }

    std::thread worker([&]() {
        worker_saw_owner = sl_async_loop_is_owner_thread(loop);
        worker_run_once_status = sl_async_loop_run_once(loop, &worker_run_once_ran);
        worker_drain_status = sl_async_loop_drain(loop, 0U, &worker_drain_ran);
    });
    worker.join();

    if (worker_saw_owner || expect_status(worker_run_once_status, SL_STATUS_INVALID_STATE) != 0 ||
        expect_status(worker_drain_status, SL_STATUS_INVALID_STATE) != 0 ||
        worker_run_once_ran != 0U || worker_drain_ran != 0U || record.count != 0U ||
        record.cleanup_count != 0U || record.release_count != 0U || record.late_count != 0U ||
        sl_async_loop_pending_count(loop) != 1U)
    {
        sl_async_loop_dispose(loop);
        return 32;
    }

    if (expect_status(sl_async_loop_drain(loop, 0U, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        record.count != 1U || record.values[0] != 71 || record.cleanup_count != 1U ||
        record.release_count != 1U || record.late_count != 0U ||
        sl_async_loop_pending_count(loop) != 0U)
    {
        sl_async_loop_dispose(loop);
        return 33;
    }

    sl_async_loop_dispose(loop);
    return 0;
}

static int test_libuv_completion_terminal_after_enqueue_is_cleanup_only(void)
{
    unsigned char arena_storage[4096];
    SlArena arena = {};
    SlAsyncCompletion storage[1];
    SlAsyncLoop* loop = nullptr;
    LibuvRecord record = {{0}, 0U, 0U, 0U, 0U, false, 0U};
    LibuvPayload payload;
    SlAsyncCompletion completion;
    size_t ran = 0U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_LIBUV, &arena, storage, 1U, &loop),
                      SL_STATUS_OK) != 0)
    {
        return 40;
    }

    completion = make_completion(&record, &payload, 123);
    completion.terminal_check = completion_scope_is_terminal;
    completion.terminal_check_user = &record;
    completion.late = record_late_completion;
    completion.late_user = &record;

    if (expect_status(sl_async_loop_post(loop, &completion), SL_STATUS_OK) != 0 ||
        record.retain_count != 1U)
    {
        sl_async_loop_dispose(loop);
        return 41;
    }

    record.terminal = true;
    if (expect_status(sl_async_loop_drain(loop, 0U, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
        record.count != 0U || record.cleanup_count != 1U || record.release_count != 1U ||
        record.late_count != 1U || sl_async_loop_pending_count(loop) != 0U)
    {
        sl_async_loop_dispose(loop);
        return 42;
    }

    sl_async_loop_dispose(loop);
    if (record.cleanup_count != 1U || record.release_count != 1U || record.late_count != 1U) {
        return 43;
    }

    return 0;
}

static int test_libuv_dispose_pending_completion_cleans_once(void)
{
    unsigned char arena_storage[4096];
    SlArena arena = {};
    SlAsyncCompletion storage[3];
    SlAsyncLoop* loop = nullptr;
    LibuvRecord record = {{0}, 0U, 0U, 0U, 0U, false, 0U};
    LibuvPayload payloads[3];
    SlAsyncCompletion completions[3];

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_LIBUV, &arena, storage, 3U, &loop),
                      SL_STATUS_OK) != 0)
    {
        return 50;
    }

    for (size_t index = 0U; index < 3U; index += 1U) {
        completions[index] = make_completion(&record, &payloads[index], (int)(index + 1U));
        if (expect_status(sl_async_loop_post(loop, &completions[index]), SL_STATUS_OK) != 0) {
            sl_async_loop_dispose(loop);
            return 51;
        }
    }

    if (record.retain_count != 3U || sl_async_loop_pending_count(loop) != 3U ||
        record.cleanup_count != 0U || record.release_count != 0U)
    {
        sl_async_loop_dispose(loop);
        return 52;
    }

    sl_async_loop_dispose(loop);
    sl_async_loop_dispose(loop);
    if (!sl_async_loop_is_disposed(loop) || record.count != 0U || record.cleanup_count != 3U ||
        record.release_count != 3U || record.late_count != 0U ||
        sl_async_loop_pending_count(loop) != 0U)
    {
        return 53;
    }

    if (expect_status(sl_async_loop_post(loop, &completions[0]), SL_STATUS_INVALID_STATE) != 0) {
        return 54;
    }

    return 0;
}

static int test_libuv_many_cross_thread_posts_drain_without_loss(void)
{
    constexpr size_t thread_count = 4U;
    constexpr size_t posts_per_thread = 16U;
    constexpr size_t total_posts = thread_count * posts_per_thread;
    unsigned char arena_storage[8192];
    SlArena arena = {};
    SlAsyncCompletion storage[total_posts];
    SlAsyncLoop* loop = nullptr;
    LibuvRecord record = {{0}, 0U, 0U, 0U, 0U, false, 0U};
    LibuvPayload payloads[total_posts];
    SlAsyncCompletion completions[total_posts];
    SlStatus statuses[total_posts];
    bool seen[total_posts] = {};
    size_t ran = 0U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(
            sl_async_loop_create(SL_ASYNC_BACKEND_LIBUV, &arena, storage, total_posts, &loop),
            SL_STATUS_OK) != 0)
    {
        return 30;
    }

    for (size_t index = 0U; index < total_posts; index += 1U) {
        completions[index] = make_completion(&record, &payloads[index], (int)(index + 1U));
        statuses[index] = sl_status_from_code(SL_STATUS_INTERNAL);
    }

    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (size_t thread_index = 0U; thread_index < thread_count; thread_index += 1U) {
        threads.emplace_back([&, thread_index]() {
            size_t base = thread_index * posts_per_thread;
            for (size_t offset = 0U; offset < posts_per_thread; offset += 1U) {
                size_t index = base + offset;
                statuses[index] = sl_async_loop_post(loop, &completions[index]);
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    for (size_t index = 0U; index < total_posts; index += 1U) {
        if (expect_status(statuses[index], SL_STATUS_OK) != 0) {
            sl_async_loop_dispose(loop);
            return 31;
        }
    }

    if (sl_async_loop_pending_count(loop) != total_posts || record.retain_count != total_posts ||
        record.release_count != 0U || record.cleanup_count != 0U)
    {
        sl_async_loop_dispose(loop);
        return 32;
    }

    if (expect_status(sl_async_loop_drain(loop, 0U, &ran), SL_STATUS_OK) != 0 ||
        ran != total_posts || record.count != total_posts || record.cleanup_count != total_posts ||
        record.release_count != total_posts || sl_async_loop_pending_count(loop) != 0U)
    {
        sl_async_loop_dispose(loop);
        return 33;
    }

    for (size_t index = 0U; index < record.count; index += 1U) {
        int value = record.values[index];

        if (value < 1 || value > (int)total_posts || seen[(size_t)(value - 1)] ||
            record.late_count != 0U)
        {
            sl_async_loop_dispose(loop);
            return 34;
        }
        seen[(size_t)(value - 1)] = true;
    }

    for (size_t index = 0U; index < total_posts; index += 1U) {
        if (!seen[index]) {
            sl_async_loop_dispose(loop);
            return 35;
        }
    }

    sl_async_loop_dispose(loop);
    if (!sl_async_loop_is_disposed(loop)) {
        return 36;
    }

    return 0;
}

static int test_libuv_io_watch_start_update_stop_and_dispose_lifecycle(void)
{
    unsigned char arena_storage[8192];
    SlArena arena = {};
    SlAsyncCompletion storage[2];
    SlAsyncLoop* loop = nullptr;
    LibuvSocketPair pair = {LIBUV_INVALID_SOCKET, LIBUV_INVALID_SOCKET};
    LibuvIoWatchRecord record = {0U, 0U, SL_STATUS_OK, false};
    SlAsyncIoWatch* watch = nullptr;
    SlAsyncIoWatch* dispose_watch = nullptr;
    unsigned char byte = 0U;
    size_t callback_count_before = 0U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_LIBUV, &arena, storage, 2U, &loop),
                      SL_STATUS_OK) != 0 ||
        !create_socket_pair(&pair) || !socket_fits_async_backend(pair.watched))
    {
        close_socket_pair(&pair);
        return 60;
    }

    if (expect_status(sl_async_io_watch_start(loop, &arena, static_cast<int>(pair.watched),
                                              SL_ASYNC_IO_EVENT_READABLE, record_io_watch, &record,
                                              &watch),
                      SL_STATUS_OK) != 0 ||
        watch == nullptr)
    {
        close_socket_pair(&pair);
        sl_async_loop_dispose(loop);
        return 61;
    }

    if (!send_single_byte(pair.peer, 0x44U) ||
        drive_loop_until(loop, 4096U, [&record]() { return record.callback_count >= 1U; }) != 0 ||
        record.last_status != SL_STATUS_OK || !record.owner_thread ||
        (record.last_events & SL_ASYNC_IO_EVENT_READABLE) == 0U ||
        !read_single_byte(pair.watched, &byte) || byte != 0x44U)
    {
        close_socket_pair(&pair);
        sl_async_loop_dispose(loop);
        return 62;
    }

    if (expect_status(sl_async_io_watch_update(watch, SL_ASYNC_IO_EVENT_WRITABLE), SL_STATUS_OK) !=
            0 ||
        drive_loop_until(loop, 4096U, [&record]() { return record.callback_count >= 2U; }) != 0 ||
        record.last_status != SL_STATUS_OK ||
        (record.last_events & SL_ASYNC_IO_EVENT_WRITABLE) == 0U)
    {
        close_socket_pair(&pair);
        sl_async_loop_dispose(loop);
        return 63;
    }

    sl_async_io_watch_stop(watch);
    sl_async_io_watch_stop(watch);
    if (expect_status(sl_async_io_watch_update(watch, SL_ASYNC_IO_EVENT_READABLE),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        close_socket_pair(&pair);
        sl_async_loop_dispose(loop);
        return 64;
    }

    callback_count_before = record.callback_count;
    if (!send_single_byte(pair.peer, 0x55U) ||
        drive_loop_until(loop, 128U,
                         [&record, callback_count_before]() {
                             return record.callback_count == callback_count_before;
                         }) != 0 ||
        record.callback_count != callback_count_before)
    {
        close_socket_pair(&pair);
        sl_async_loop_dispose(loop);
        return 65;
    }
    (void)read_single_byte(pair.watched, &byte);

    if (expect_status(sl_async_io_watch_start(loop, &arena, static_cast<int>(pair.watched),
                                              SL_ASYNC_IO_EVENT_READABLE, record_io_watch, &record,
                                              &dispose_watch),
                      SL_STATUS_OK) != 0 ||
        dispose_watch == nullptr)
    {
        close_socket_pair(&pair);
        sl_async_loop_dispose(loop);
        return 66;
    }

    sl_async_loop_dispose(loop);
    if (!sl_async_loop_is_disposed(loop) ||
        expect_status(sl_async_io_watch_start(loop, &arena, static_cast<int>(pair.watched),
                                              SL_ASYNC_IO_EVENT_READABLE, record_io_watch, &record,
                                              &dispose_watch),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        close_socket_pair(&pair);
        return 67;
    }

    close_socket_pair(&pair);
    return 0;
}

static int test_libuv_dispose_racing_cross_thread_post_cleans_once(void)
{
    unsigned char arena_storage[4096];
    SlArena arena = {};
    SlAsyncCompletion storage[1];
    SlAsyncLoop* loop = nullptr;
    LibuvRecord record = {{0}, 0U, 0U, 0U, 0U, false, 0U};
    LibuvPayload payload;
    SlAsyncCompletion completion;
    std::atomic_bool trigger(false);
    SlStatus worker_status = sl_status_ok();

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_LIBUV, &arena, storage, 1U, &loop),
                      SL_STATUS_OK) != 0)
    {
        return 68;
    }

    completion = make_completion(&record, &payload, 200);
    std::thread worker([&]() {
        while (!trigger.load()) {
            yield_briefly();
        }
        worker_status = sl_async_loop_post(loop, &completion);
    });

    trigger.store(true);
    sl_async_loop_dispose(loop);
    worker.join();

    if (!sl_async_loop_is_disposed(loop) ||
        (expect_status(worker_status, SL_STATUS_OK) != 0 &&
         expect_status(worker_status, SL_STATUS_INVALID_STATE) != 0))
    {
        return 69;
    }

    if (sl_status_code(worker_status) == SL_STATUS_OK) {
        if (record.retain_count != 1U || record.cleanup_count != 1U || record.release_count != 1U ||
            sl_async_loop_pending_count(loop) != 0U)
        {
            return 70;
        }
    }
    else if (record.retain_count != 0U || record.cleanup_count != 0U || record.release_count != 0U)
    {
        return 71;
    }

    return 0;
}

int main(void)
{
    int result = test_libuv_create_post_drain_dispose();

    if (result != 0) {
        return result;
    }

    result = test_libuv_cross_thread_post_owner_thread_dispatch();
    if (result != 0) {
        return result;
    }

    result = test_libuv_capacity_overflow_is_deterministic();
    if (result != 0) {
        return result;
    }

    result = test_libuv_wrong_thread_run_once_and_drain_rejected_before_callbacks();
    if (result != 0) {
        return result;
    }

    result = test_libuv_completion_terminal_after_enqueue_is_cleanup_only();
    if (result != 0) {
        return result;
    }

    result = test_libuv_dispose_pending_completion_cleans_once();
    if (result != 0) {
        return result;
    }

    result = test_libuv_many_cross_thread_posts_drain_without_loss();
    if (result != 0) {
        return result;
    }

    result = test_libuv_io_watch_start_update_stop_and_dispose_lifecycle();
    if (result != 0) {
        return result;
    }

    return test_libuv_dispose_racing_cross_thread_post_cleans_once();
}
