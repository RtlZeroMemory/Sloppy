import assert from "node:assert/strict";

import {
    CancelledError,
    CancellationController,
    ConstantTime,
    Deadline,
    Directory,
    File,
    FileHandle,
    Hash,
    Hmac,
    HttpClient,
    InvalidDeadlineError,
    LocalEndpoint,
    NetworkAddress,
    NamedPipe,
    NonCryptoHash,
    Password,
    Random,
    Router,
    Results,
    Secret,
    Sloppy,
    Time,
    TimerDisposedError,
    TimeoutError,
    schema,
    UnixSocket,
} from "../../stdlib/sloppy/index.js";
import { sqlite } from "../../stdlib/sloppy/providers/sqlite.js";

function assertThrowsMessage(fn, expected) {
    assert.throws(fn, (error) => {
        assert.match(String(error.message), expected);
        return true;
    });
}

async function assertRejectsMessage(fn, expected) {
    await assert.rejects(fn, (error) => {
        assert.match(String(error.message), expected);
        return true;
    });
}

async function flushMicrotasks(count = 6) {
    for (let i = 0; i < count; i += 1) {
        await Promise.resolve();
    }
}

{
    const previousSloppy = globalThis.__sloppy;
    const encodeAscii = (value) => new Uint8Array(Array.from(value).map((char) => char.charCodeAt(0)));
    try {
        globalThis.__sloppy = {
            crypto: {
                randomBytes(length) {
                    return new Uint8Array(length);
                },
                randomUuid() {
                    return "00000000-0000-4000-8000-000000000000";
                },
                randomToken(length) {
                    return "A".repeat(length);
                },
                randomHex(length) {
                    return "00".repeat(length);
                },
                randomNumericCode(length) {
                    return "0".repeat(length);
                },
                hash(algorithm, bytes) {
                    assert.equal(algorithm, "sha256");
                    assert.deepEqual(bytes, encodeAscii("abc"));
                    return new Uint8Array([0xba, 0x78]);
                },
                hmac(algorithm, secret, bytes) {
                    assert.equal(algorithm, "sha256");
                    assert.deepEqual(secret, encodeAscii("key"));
                    assert.deepEqual(bytes, encodeAscii("abc"));
                    return new Uint8Array([1, 2, 3]);
                },
                constantTimeEquals(left, right) {
                    return left.byteLength === right.byteLength && left.every((byte, index) => byte === right[index]);
                },
                passwordHash(bytes, opsLimit, memoryLimitBytes) {
                    assert.deepEqual(bytes, encodeAscii("password"));
                    assert.equal(opsLimit, 2);
                    assert.equal(memoryLimitBytes, 67108864);
                    return Promise.resolve("$argon2id$v=19$m=65536,t=2,p=1$test$hash");
                },
                passwordVerify(bytes, encodedHash) {
                    assert.deepEqual(bytes, encodeAscii("password"));
                    assert.equal(encodedHash.startsWith("$argon2id$"), true);
                    return Promise.resolve(true);
                },
                passwordNeedsRehash(encodedHash, opsLimit, memoryLimitBytes) {
                    assert.equal(encodedHash.startsWith("$argon2id$"), true);
                    assert.equal(opsLimit, 3);
                    assert.equal(memoryLimitBytes, 67108864);
                    return Promise.resolve(true);
                },
                nonCryptoXxHash64(bytes) {
                    assert.deepEqual(bytes, encodeAscii("hello"));
                    return "26c7827d889f6da3";
                },
            },
        };

        assert.equal(Random.uuid(), "00000000-0000-4000-8000-000000000000");
        assert.equal(Random.token(4), "AAAA");
        assert.equal(Random.hex(2), "0000");
        assert.equal(Random.numericCode(3), "000");
        assert.equal(Random.bytes(2).byteLength, 2);
        assert.equal(await Hash.sha256Hex("abc"), "ba78");
        assert.deepEqual(await Hmac.sha256("key", "abc"), new Uint8Array([1, 2, 3]));
        assert.equal(await Hmac.verifySha256("key", "abc", new Uint8Array([1, 2, 3])), true);
        const encoded = await Password.hash("password");
        assert.equal(encoded.startsWith("$argon2id$"), true);
        assert.equal(await Password.verify("password", encoded), true);
        assert.equal(await Password.needsRehash(encoded, { opsLimit: 3 }), true);
        assert.equal(NonCryptoHash.xxHash64("hello"), "26c7827d889f6da3");
        assert.equal(ConstantTime.equals(new Uint8Array([1]), new Uint8Array([1])), true);
        const secret = Secret.fromUtf8("key");
        assert.deepEqual(secret.bytes(), encodeAscii("key"));
        assert.equal(String(secret), "[Secret redacted]");
        secret.dispose();
        assert.throws(() => secret.bytes(), /SLOPPY_E_CRYPTO_SECRET_DISPOSED/);
    } finally {
        if (previousSloppy === undefined) {
            delete globalThis.__sloppy;
        } else {
            globalThis.__sloppy = previousSloppy;
        }
    }
}

{
    const ipv4 = NetworkAddress.parse("127.0.0.1:6379");
    assert.equal(ipv4.host, "127.0.0.1");
    assert.equal(ipv4.port, 6379);
    assert.equal(String(ipv4), "127.0.0.1:6379");

    const ipv6 = NetworkAddress.parse("[::1]:443");
    assert.equal(ipv6.host, "::1");
    assert.equal(ipv6.port, 443);
    assert.equal(String(ipv6), "[::1]:443");

    const objectAddress = NetworkAddress.parse({ host: "localhost", port: 9000 });
    assert.equal(objectAddress.host, "localhost");
    assert.equal(objectAddress.port, 9000);
    assert.equal(NetworkAddress.parse(objectAddress), objectAddress);

    assertThrowsMessage(() => NetworkAddress.parse("::1:443"), /host:port/);
    assertThrowsMessage(() => NetworkAddress.parse("127.0.0.1"), /host:port/);
    assertThrowsMessage(() => NetworkAddress.parse("[::1]443"), /\[host\]:port/);
    assertThrowsMessage(() => NetworkAddress.parse("[::1]:"), /TCP port text/);
    assertThrowsMessage(() => NetworkAddress.parse("localhost:1e3"), /TCP port text/);
    assertThrowsMessage(() => NetworkAddress.parse("localhost:0x50"), /TCP port text/);
    assertThrowsMessage(() => NetworkAddress.parse("localhost:70000"), /TCP port/);
}

{
    const previousSloppy = globalThis.__sloppy;
    let connectLocalCalls = 0;
    const delayedConnectHandles = [];
    const delayedAcceptedHandles = [];
    try {
        globalThis.__sloppy = {
            net: {
                connectLocal(options) {
                    connectLocalCalls += 1;
                    if (options.path === "runtime:/late.sock") {
                        return new Promise((resolve) => {
                            setTimeout(() => {
                                const handle = { kind: "late-local", options };
                                delayedConnectHandles.push(handle);
                                resolve(handle);
                            }, 20);
                        });
                    }
                    return Promise.resolve({ kind: "local", options });
                },
                listenLocal(options) {
                    return Promise.resolve({ kind: "server", options });
                },
                acceptLocal(server, timeoutMs) {
                    if (timeoutMs === 1) {
                        return new Promise((resolve) => {
                            setTimeout(() => {
                                const handle = { kind: "late-accepted", server, timeoutMs };
                                delayedAcceptedHandles.push(handle);
                                resolve(handle);
                            }, 20);
                        });
                    }
                    return Promise.resolve({ kind: "accepted", server, timeoutMs });
                },
                writeLocal(handle, bytes, timeoutMs) {
                    if (timeoutMs === 1) {
                        return new Promise((resolve) => {
                            setTimeout(() => {
                                handle.lastWrite = { bytes, timeoutMs };
                                resolve();
                            }, 20);
                        });
                    }
                    handle.lastWrite = { bytes, timeoutMs };
                    return Promise.resolve();
                },
                readLocal(handle, maxBytes, timeoutMs) {
                    if (timeoutMs === 1) {
                        return new Promise((resolve) => {
                            setTimeout(() => {
                                handle.delayedReadResolved = true;
                                resolve(new Uint8Array([maxBytes & 0xff, timeoutMs ?? 0]));
                            }, 20);
                        });
                    }
                    return Promise.resolve(new Uint8Array([maxBytes & 0xff, timeoutMs ?? 0]));
                },
                readLineLocal(handle, maxBytes, timeoutMs) {
                    if (timeoutMs === 1) {
                        return new Promise((resolve) => {
                            setTimeout(() => {
                                handle.delayedReadLineResolved = true;
                                resolve(`${maxBytes}:${timeoutMs ?? "none"}`);
                            }, 20);
                        });
                    }
                    return Promise.resolve(`${maxBytes}:${timeoutMs ?? "none"}`);
                },
                readUntilLocal(handle, delimiter, maxBytes, timeoutMs) {
                    if (timeoutMs === 1) {
                        return new Promise((resolve) => {
                            setTimeout(() => {
                                handle.delayedReadUntilResolved = true;
                                resolve(new Uint8Array([delimiter.byteLength, maxBytes, timeoutMs ?? 0]));
                            }, 20);
                        });
                    }
                    return Promise.resolve(new Uint8Array([delimiter.byteLength, maxBytes, timeoutMs ?? 0]));
                },
                closeLocal() {
                    return Promise.resolve();
                },
                abortLocal(handle) {
                    handle.aborted = true;
                    return Promise.resolve();
                },
                closeLocalServer() {
                    return Promise.resolve();
                },
                abortLocalServer() {
                    return Promise.resolve();
                },
            },
        };

        const conn = await LocalEndpoint.connect({ path: "runtime:/my-app.sock", timeoutMs: 1 });
        assert.equal(conn.closed, false);
        assert.deepEqual(conn._handle.options, { path: "runtime:/my-app.sock", timeoutMs: 1 });
        await assert.rejects(conn.read({ maxBytes: 1, timeoutMs: 1 }), /timed out/);
        assert.equal(conn.closed, false);
        await assert.rejects(conn.readLine({ maxBytes: 1, timeoutMs: 1 }), /timed out/);
        assert.equal(conn.closed, false);
        await assert.rejects(
            conn.readUntil(new Uint8Array([10]), { maxBytes: 1, timeoutMs: 1 }),
            /timed out/,
        );
        assert.equal(conn.closed, false);
        await assert.rejects(conn.write(new Uint8Array([9]), { timeoutMs: 1 }), /timed out/);
        assert.equal(conn.closed, false);
        await new Promise((resolve) => setTimeout(resolve, 30));
        assert.equal(conn.closed, false);
        assert.equal(conn._handle.delayedReadResolved, true);
        assert.equal(conn._handle.delayedReadLineResolved, true);
        assert.equal(conn._handle.delayedReadUntilResolved, true);
        await conn.write(new Uint8Array([0, 1, 2]), { timeoutMs: 2 });
        assert.deepEqual(Array.from(conn._handle.lastWrite.bytes), [0, 1, 2]);
        assert.equal(conn._handle.lastWrite.timeoutMs, 2);
        assert.deepEqual(Array.from(await conn.read({ maxBytes: 7, timeoutMs: 3 })), [7, 3]);
        assert.equal(await conn.readLine({ maxBytes: 8, timeoutMs: 4 }), "8:4");
        assert.deepEqual(Array.from(await conn.readUntil(new Uint8Array([10]), { maxBytes: 9, timeoutMs: 5 })), [
            1,
            9,
            5,
        ]);
        await conn.close();
        assert.equal(conn.closed, true);
        await assert.rejects(conn.readUntil(new Uint8Array([10])), /closed/);
        await assert.rejects(conn.readLine(), /closed/);

        const neverDeadlineConn = await LocalEndpoint.connect({
            path: "runtime:/deadline-never.sock",
            deadline: Deadline.never(),
        });
        assert.deepEqual(neverDeadlineConn._handle.options, { path: "runtime:/deadline-never.sock" });
        await neverDeadlineConn.close();

        const cancelledConnect = new CancellationController();
        cancelledConnect.cancel("caller stopped");
        const callsBeforeCancelledConnect = connectLocalCalls;
        await assert.rejects(
            LocalEndpoint.connect({ path: "runtime:/cancelled.sock", signal: cancelledConnect.signal }),
            /cancelled/,
        );
        assert.equal(connectLocalCalls, callsBeforeCancelledConnect);

        await assert.rejects(
            LocalEndpoint.connect({ path: "runtime:/expired.sock", deadline: Deadline.after(0) }),
            /timed out/,
        );
        assert.equal(connectLocalCalls, callsBeforeCancelledConnect);

        await assert.rejects(LocalEndpoint.connect({ path: "runtime:/late.sock", timeoutMs: 1 }), /timed out/);
        await new Promise((resolve) => setTimeout(resolve, 30));
        assert.equal(delayedConnectHandles.length, 1);
        assert.equal(delayedConnectHandles[0].aborted, true);

        const server = await LocalEndpoint.listen({
            path: "runtime:/my-app.sock",
            unlinkExisting: true,
            permissions: "0600",
            backlog: 4,
        });
        assert.equal(server.closed, false);
        assert.deepEqual(server._handle.options, {
            path: "runtime:/my-app.sock",
            unlinkExisting: true,
            permissions: "0600",
            backlog: 4,
        });
        const accepted = await server.accept({ timeoutMs: 6 });
        assert.equal(accepted._handle.server, server._handle);
        assert.equal(accepted._handle.timeoutMs, 6);
        assert.equal(typeof server.accept({ timeoutMs: 7 })[Symbol.asyncIterator], "function");
        const acceptIterator = server.accept({ timeoutMs: 7 })[Symbol.asyncIterator]();
        const iterated = await acceptIterator.next();
        assert.equal(iterated.done, false);
        assert.equal(iterated.value._handle.timeoutMs, 7);
        await iterated.value.close();
        await accepted.close();
        await server.close();
        assert.equal(server.closed, true);

        const retryServer = await LocalEndpoint.listen({ path: "runtime:/retry.sock" });
        await assert.rejects(retryServer.accept({ timeoutMs: 1 }), /timed out/);
        assert.equal(retryServer.closed, false);
        const retriedAccept = await retryServer.accept({ timeoutMs: 6 });
        assert.equal(retriedAccept._handle.timeoutMs, 6);
        await new Promise((resolve) => setTimeout(resolve, 30));
        assert.equal(delayedAcceptedHandles.length, 1);
        assert.equal(delayedAcceptedHandles[0].aborted, true);
        await retriedAccept.close();
        await retryServer.close();

        const unix = await UnixSocket.connect({ path: "runtime:/svc.sock" });
        assert.equal(unix._handle.options.backend, "unix");
        const pipe = await NamedPipe.listen({ path: "runtime:/svc.sock" });
        assert.equal(pipe._handle.options.backend, "namedPipe");

        await assert.rejects(LocalEndpoint.connect({ path: "../bad.sock" }), /named-root/);
        await assert.rejects(LocalEndpoint.connect({ path: "runtime:/../bad.sock" }), /named root/);
        await assert.rejects(LocalEndpoint.connect({ path: "runtime:/bad//sock" }), /named root/);
        await assert.rejects(LocalEndpoint.connect({ path: "runtime:/bad/." }), /named root/);
        await assert.rejects(LocalEndpoint.connect({ path: "runtime:/bad name.sock" }), /named root/);
        await assert.rejects(
            LocalEndpoint.listen({ path: "runtime:/ok.sock", permissions: "600" }),
            /octal/,
        );
        await assert.rejects(LocalEndpoint.connect({ path: "runtime:/ok.sock", backend: "bad" }), /backend/);

        globalThis.__sloppy = { net: {} };
        await assert.rejects(
            LocalEndpoint.connect({ path: "runtime:/future.sock" }),
            /SLOPPY_E_NET_LOCAL_IPC_FEATURE_UNAVAILABLE/,
        );
    } finally {
        if (previousSloppy === undefined) {
            delete globalThis.__sloppy;
        } else {
            globalThis.__sloppy = previousSloppy;
        }
    }
}

{
    assert.equal(typeof HttpClient.create, "function");
    assert.equal(typeof HttpClient.get, "function");
    assert.equal(typeof HttpClient.getJson, "function");
    assert.equal(typeof HttpClient.postJson, "function");
    assert.equal(typeof HttpClient.text, "function");
    assert.equal(typeof HttpClient.json, "function");
    assert.equal(typeof HttpClient.bytes, "function");
    const client = HttpClient.create({ baseUrl: "http://api.example.test" });
    assert.equal(typeof client.getJson, "function");
    assert.equal(typeof client.postJson, "function");
    await assertRejectsMessage(
        () => HttpClient.get("http://api.example.test/health"),
        /SLOPPY_E_HTTP_CLIENT_FEATURE_UNAVAILABLE/,
    );
    await assertRejectsMessage(
        () => client.postJson("/items", { name: "test" }),
        /SLOPPY_E_HTTP_CLIENT_FEATURE_UNAVAILABLE/,
    );
}

{
    const deadline = Deadline.after(50);
    assert.equal(deadline.kind, "after");
    assert.equal(deadline.expired, false);
    assert.equal(typeof deadline.remainingMs(), "number");
    assert.equal(Deadline.never().remainingMs(), Infinity);

    const controller = new CancellationController();
    let observedReason = undefined;
    controller.signal.addEventListener("abort", () => {
        observedReason = controller.signal.reason;
    });
    assert.equal(controller.cancel("done"), true);
    assert.equal(controller.cancel("again"), false);
    assert.equal(controller.signal.aborted, true);
    assert.equal(controller.signal.reason, "done");
    assert.equal(observedReason, "done");
    assert.throws(() => controller.signal.throwIfCancelled(), CancelledError);

    const fanoutController = new CancellationController();
    let fanoutObserved = false;
    fanoutController.signal.addEventListener("abort", () => {
        throw new Error("listener failed");
    });
    fanoutController.signal.addEventListener("abort", () => {
        fanoutObserved = true;
    });
    assert.throws(() => fanoutController.cancel("fanout"), AggregateError);
    assert.equal(fanoutObserved, true);

    await assert.rejects(Time.delay(10, { signal: controller.signal }), CancelledError);
    let cancelledTimeoutInvoked = false;
    await assert.rejects(
        Time.timeout(
            () => {
                cancelledTimeoutInvoked = true;
            },
            { afterMs: 10, signal: controller.signal },
        ),
        CancelledError,
    );
    assert.equal(cancelledTimeoutInvoked, false);

    let expiredTimeoutInvoked = false;
    await assert.rejects(
        Time.timeout(
            () => {
                expiredTimeoutInvoked = true;
            },
            { afterMs: 0 },
        ),
        TimeoutError,
    );
    assert.equal(expiredTimeoutInvoked, false);

    assert.throws(() => Time.delay(-1), InvalidDeadlineError);
    assert.throws(() => Time.delay(0x100000000), InvalidDeadlineError);

    const previousSloppy = globalThis.__sloppy;
    try {
        globalThis.__sloppy = {
            time: {
                delay() {
                    return Promise.reject(new Error("Sloppy timer was disposed before completion"));
                },
                monotonicMs() {
                    return Date.now();
                },
            },
        };
        await assert.rejects(Time.delay(1), TimerDisposedError);
    } finally {
        if (previousSloppy === undefined) {
            delete globalThis.__sloppy;
        } else {
            globalThis.__sloppy = previousSloppy;
        }
    }

    const clock = Time.fakeClock({ now: new Date("2026-01-01T00:00:00.000Z") });
    assert.equal(clock.kind, "fake");
    assert.equal(clock.now().toISOString(), "2026-01-01T00:00:00.000Z");

    let delayed = false;
    const delayPromise = Time.delay(1000, { clock }).then(() => {
        delayed = true;
    });
    clock.advanceBy(999);
    await flushMicrotasks();
    assert.equal(delayed, false);
    clock.advanceBy(1);
    await delayPromise;
    assert.equal(delayed, true);

    const timeoutPromise = Time.timeout(new Promise(() => {}), { afterMs: 500, clock });
    clock.advanceBy(500);
    await assert.rejects(timeoutPromise, TimeoutError);

    const promiseTimeoutClock = Time.fakeClock();
    assert.equal(
        await Time.timeout(Promise.resolve("fast"), { afterMs: 1000, clock: promiseTimeoutClock }),
        "fast",
    );
    assert.equal(promiseTimeoutClock._timers.length, 0);

    const functionTimeoutClock = Time.fakeClock();
    assert.equal(
        await Time.timeout(() => "fast", { afterMs: 1000, clock: functionTimeoutClock }),
        "fast",
    );
    assert.equal(functionTimeoutClock._timers.length, 0);

    assert.throws(
        () => Time.delay(100, { clock: Time.fakeClock(), deadline: Deadline.after(100) }),
        InvalidDeadlineError,
    );
    assert.throws(
        () => Time.timeout(Promise.resolve("x"), { clock: Time.fakeClock(), deadline: Deadline.after(100) }),
        InvalidDeadlineError,
    );

    const orderedClock = Time.fakeClock();
    const timerOrder = [];
    const laterTimer = Time.delay(200, { clock: orderedClock }).then(() => {
        timerOrder.push("later");
    });
    const earlierTimer = Time.delay(100, { clock: orderedClock }).then(() => {
        timerOrder.push("earlier");
    });
    orderedClock.advanceBy(200);
    await Promise.all([laterTimer, earlierTimer]);
    assert.deepEqual(timerOrder, ["earlier", "later"]);

    const immediateInterval = Time.interval(1000, { clock, immediate: true, maxTicks: 1 });
    assert.equal((await immediateInterval.next()).value.index, 1);
    assert.equal((await immediateInterval.next()).done, true);

    const guardedInterval = Time.interval(1000, { clock, maxTicks: 1 });
    const guardedTick = guardedInterval.next();
    await assert.rejects(guardedInterval.next(), /overlapping next\(\) calls/);
    clock.advanceBy(1000);
    assert.equal((await guardedTick).value.index, 1);

    const boundedIntervalController = new CancellationController();
    const boundedInterval = Time.interval(1000, {
        clock,
        maxTicks: 1,
        signal: boundedIntervalController.signal,
    });
    const boundedTick = boundedInterval.next();
    clock.advanceBy(1000);
    assert.equal((await boundedTick).value.index, 1);
    assert.equal(boundedIntervalController.signal._listeners.size, 0);

    const interval = Time.interval("1s", { clock, maxTicks: 2 });
    const firstTick = interval.next();
    clock.advanceBy(1000);
    assert.equal((await firstTick).value.index, 1);
    const secondTick = interval.next();
    clock.advanceBy(1000);
    assert.equal((await secondTick).value.index, 2);
    assert.equal((await interval.next()).done, true);

    let scheduledRuns = 0;
    const job = Time.every(
        "1s",
        () => {
            scheduledRuns += 1;
        },
        { clock },
    );
    clock.advanceBy(1000);
    await flushMicrotasks();
    assert.equal(scheduledRuns, 1);
    job.pause();
    clock.advanceBy(2000);
    await flushMicrotasks();
    assert.equal(scheduledRuns, 1);
    job.resume();
    clock.advanceBy(1000);
    await flushMicrotasks();
    assert.equal(scheduledRuns, 2);
    await job.stop();
    assert.equal(job.stopped, true);

    let immediateJobRuns = 0;
    const immediateJob = Time.every(
        1000,
        () => {
            immediateJobRuns += 1;
        },
        { clock, immediate: true, maxRuns: 1 },
    );
    await flushMicrotasks();
    assert.equal(immediateJobRuns, 1);
    assert.equal(immediateJob.stopped, true);

    let releaseJob = undefined;
    let noOverlapRuns = 0;
    const noOverlapJob = Time.every(
        1000,
        async () => {
            noOverlapRuns += 1;
            await new Promise((resolve) => {
                releaseJob = resolve;
            });
        },
        { clock },
    );
    clock.advanceBy(1000);
    await flushMicrotasks();
    assert.equal(noOverlapRuns, 1);
    clock.advanceBy(3000);
    await flushMicrotasks();
    assert.equal(noOverlapRuns, 1);
    assert.equal(noOverlapJob.skippedRuns, 3);
    releaseJob();
    await flushMicrotasks();
    await noOverlapJob.stop();

    const cancelJobClock = Time.fakeClock();
    const cancelJobController = new CancellationController();
    const cancelJob = Time.every(1000, () => {}, {
        clock: cancelJobClock,
        signal: cancelJobController.signal,
    });
    cancelJobController.cancel("done");
    await flushMicrotasks();
    assert.equal(cancelJob.stopped, true);
    assert.equal(cancelJob.nextRun, null);

    const disposedJobClock = Time.fakeClock();
    const disposedJob = Time.every(1000, () => {}, { clock: disposedJobClock });
    disposedJobClock.dispose();
    await flushMicrotasks();
    assert.equal(disposedJob.stopped, true);
    assert.equal(disposedJob.nextRun, null);

    const disposedClock = Time.fakeClock();
    const disposedDelay = Time.delay(100, { clock: disposedClock });
    disposedClock.dispose();
    await assert.rejects(disposedDelay, TimerDisposedError);
    assert.throws(() => disposedClock.advanceBy(1), TimerDisposedError);
}

{
    const previousSloppy = globalThis.__sloppy;
    const previousDateNow = Date.now;
    let readCalls = 0;
    let handleReadCalls = 0;
    let readLinkCalls = 0;
    let directoryListCalls = 0;
    let fakeNowMs = 1000;
    try {
        Date.now = () => fakeNowMs;
        globalThis.__sloppy = {
            fs: {
                readText() {
                    readCalls += 1;
                    return Promise.resolve("ok");
                },
                stat() {
                    readCalls += 1;
                    return Promise.resolve({ exists: true, kind: "directory" });
                },
                directoryList() {
                    directoryListCalls += 1;
                    return Promise.resolve([{ name: "child", kind: "directory" }]);
                },
                readLink() {
                    readLinkCalls += 1;
                    return Promise.resolve("data:/target");
                },
                handleRead() {
                    handleReadCalls += 1;
                    return Promise.resolve(new Uint8Array([handleReadCalls === 1 ? 65 : 0]));
                },
            },
            time: {
                delay() {
                    return new Promise(() => {});
                },
                monotonicMs() {
                    return fakeNowMs;
                },
            },
        };

        const cancelledController = new CancellationController();
        cancelledController.cancel("caller stopped");
        await assert.rejects(
            File.readText("data:/users.json", { signal: cancelledController.signal }),
            CancelledError,
        );
        assert.equal(readCalls, 0);

        await assert.rejects(
            File.readText("data:/users.json", { deadline: Deadline.after(0) }),
            TimeoutError,
        );
        assert.equal(readCalls, 0);

        await assert.rejects(
            File.readText("data:/users.json", { timeoutMs: 0 }),
            TimeoutError,
        );
        assert.equal(readCalls, 0);

        assert.throws(
            () => File.readText("data:/users.json", { timeoutMs: -1 }),
            InvalidDeadlineError,
        );
        assert.equal(readCalls, 0);

        assert.throws(
            () => File.readText("data:/users.json", { timeoutMs: 0x100000000 }),
            InvalidDeadlineError,
        );
        assert.equal(readCalls, 0);

        assert.equal(await File.readText("data:/users.json", { deadline: Deadline.never() }), "ok");
        assert.equal(await Directory.exists("data:/", { deadline: Deadline.never() }), true);
        assert.equal(readCalls, 2);

        const pendingController = new CancellationController();
        const pendingRead = File.readText("data:/later.txt", { signal: pendingController.signal });
        pendingController.cancel("not needed");
        await assert.rejects(pendingRead, CancelledError);
        assert.equal(readCalls, 3);

        const walk = Directory.walk("data:/root", { timeoutMs: 5 });
        assert.deepEqual(await walk.next(), {
            done: false,
            value: { name: "child", kind: "directory" },
        });
        fakeNowMs += 6;
        await assert.rejects(walk.next(), TimeoutError);
        assert.equal(directoryListCalls, 1);
        assert.equal(readLinkCalls, 0);

        fakeNowMs = 2000;
        const handle = new FileHandle({ slot: 1, generation: 1 });
        const chunks = handle.readChunks({ chunkSize: 1, timeoutMs: 5 });
        assert.deepEqual(await chunks.next(), {
            done: false,
            value: new Uint8Array([65]),
        });
        fakeNowMs += 6;
        await assert.rejects(chunks.next(), TimeoutError);
        assert.equal(handleReadCalls, 1);
    } finally {
        Date.now = previousDateNow;
        if (previousSloppy === undefined) {
            delete globalThis.__sloppy;
        } else {
            globalThis.__sloppy = previousSloppy;
        }
    }
}

{
    const builder = Sloppy.createBuilder();

    builder.config.addObject({
        "app.name": "first",
        "server.port": 3000,
    });
    builder.config.addObject({
        "app.name": "second",
        Auth: {
            JwtSecret: "opaque-value",
            TokenTtlMinutes: "60",
            Issuer: "sloppy-tests",
            Audience: "api",
        },
        Feature: {
            Mode: "strict",
            Timeout: "2s",
            UploadLimit: "4KiB",
            Hosts: ["localhost"],
            Metadata: { tier: "dev" },
        },
    });

    assert.equal(builder.config.get("app.name"), "second");
    assert.equal(builder.config.get("APP.NAME"), "second");
    assert.equal(builder.config.get("missing", "fallback"), "fallback");
    assert.equal(builder.config.has("server.port"), true);
    assert.equal(builder.config.require("server.port"), 3000);
    assert.equal(builder.config.getInt("server.port"), 3000);
    assert.equal(builder.config.getString("Sloppy:Server:Host", "127.0.0.1"), "127.0.0.1");
    assert.equal(builder.config.getBool("Feature:X", false), false);
    assert.equal(builder.config.getBoolean("Feature:X", false), false);
    assert.equal(builder.config.getNumber("Feature:Limit", 1.5), 1.5);
    assert.equal(builder.config.getDuration("Feature:Timeout"), 2000);
    assert.equal(builder.config.getBytes("Feature:UploadLimit"), 4096);
    assert.deepEqual(builder.config.getArray("Feature:Hosts"), ["localhost"]);
    assert.deepEqual(builder.config.getObject("Feature:Metadata"), { tier: "dev" });
    const secret = builder.config.getSecret("Auth:JwtSecret");
    assert.equal(secret.value(), "opaque-value");
    assert.equal(String(secret), "[Secret redacted]");
    assert.equal(JSON.stringify({ secret }), "{\"secret\":\"[Secret redacted]\"}");
    const auth = builder.config.bind("Auth", {
        jwtSecret: "secret",
        tokenTtlMinutes: {
            type: "number",
            default: 60,
            min: 1,
            max: 1440,
        },
        issuer: {
            type: "string",
            required: true,
        },
        audience: {
            type: "string",
            enum: ["api", "admin"],
        },
    });
    assert.equal(auth.jwtSecret.value(), "opaque-value");
    assert.equal(String(auth.jwtSecret), "[Secret redacted]");
    assert.equal(auth.tokenTtlMinutes, 60);
    assert.equal(auth.issuer, "sloppy-tests");
    assert.equal(auth.audience, "api");
    assert.deepEqual(builder.config.bind("Optional", {
        enabled: {
            type: "boolean",
            required: false,
        },
    }), {});
    assertThrowsMessage(() => builder.config.bind("Auth", { missing: { type: "string", required: true } }), /required/);
    assertThrowsMessage(() => builder.config.bind("Auth", { jwtSecret: { type: "secret", default: "unsafe" } }), /literal default/);
    assertThrowsMessage(() => builder.config.bind("Feature", { mode: { type: "string", enum: ["safe"] } }), /declared values/);
    builder.config.addObject({
        Unsafe: {
            constructor: "blocked",
        },
    });
    assertThrowsMessage(() => builder.config.bind("Unsafe"), /not supported/);
    assertThrowsMessage(() => builder.config.getObject("Unsafe"), /not supported/);
    assertThrowsMessage(() => builder.config.getInt("app.name"), /number/);
    assertThrowsMessage(() => builder.config.require("missing"), /required/);
    assertThrowsMessage(() => builder.config.get(""), /non-empty string/);
    assertThrowsMessage(() => builder.config.get("A::B"), /empty segments/);

    const memorySink = builder.logging.addMemorySink();
    builder.logging.setMinimumLevel("info");
    assertThrowsMessage(() => builder.logging.setMinimumLevel("verbose"), /log level/);

    let singletonCalls = 0;
    let transientCalls = 0;
    let scopedCalls = 0;
    let scopedDisposals = 0;
    let transientDisposals = 0;
    let singletonDisposals = 0;
    builder.services.addSingleton("message", () => {
        singletonCalls += 1;
        return "Hello from Sloppy";
    });
    builder.services.addSingleton("disposable-singleton", () => ({
        dispose() {
            singletonDisposals += 1;
        },
    }));
    builder.services.addScoped("request-id", () => {
        scopedCalls += 1;
        return {
            value: scopedCalls,
            dispose() {
                scopedDisposals += 1;
            },
        };
    });
    builder.services.addTransient("clock", () => {
        transientCalls += 1;
        return {
            now: () => transientCalls,
            dispose() {
                transientDisposals += 1;
            },
        };
    });

    assertThrowsMessage(
        () => builder.services.addSingleton("message", "duplicate"),
        /already registered/,
    );
    assertThrowsMessage(() => builder.services.addTransient("", () => "bad"), /non-empty string/);
    assertThrowsMessage(() => builder.services.addTransient("bad", 123), /factory/);
    assertThrowsMessage(() => builder.services.addScoped("bad-scoped", 123), /factory/);

    const app = builder.build();

    assertThrowsMessage(() => builder.config.addObject({ later: true }), /builder is frozen/);
    assertThrowsMessage(() => builder.logging.addMemorySink(), /builder is frozen/);
    assertThrowsMessage(() => builder.services.addSingleton("later", "value"), /builder is frozen/);
    assertThrowsMessage(() => builder.build(), /builder is frozen/);

    assert.equal(app.config.getInt("server.port"), 3000);

    const scope = app.services.createScope();
    assert.equal(scope.get("message"), "Hello from Sloppy");
    assert.equal(scope.get("message"), "Hello from Sloppy");
    assert.equal(singletonCalls, 1);
    assert.equal(scope.get("request-id").value, 1);
    assert.equal(scope.get("request-id").value, 1);
    assert.equal(scopedCalls, 1);
    assert.equal(scope.get("clock").now(), 1);
    assert.equal(scope.get("clock").now(), 2);
    assert.equal(transientCalls, 2);
    assertThrowsMessage(() => scope.get("missing"), /not registered/);
    scope.dispose();
    scope.dispose();
    assert.equal(scopedDisposals, 1);
    assert.equal(transientDisposals, 2);
    assertThrowsMessage(() => scope.get("message"), /scope is disposed/);
    assert.equal(app.services.createScope().get("request-id").value, 2);
    assert.equal(app.services.get("message"), "Hello from Sloppy");
    assertThrowsMessage(() => app.services.get("request-id"), /root service resolution/);
    app.services.get("disposable-singleton");

    const fields = { route: "/" };
    app.log.debug("filtered", fields);
    app.log.info("hello", fields);
    fields.route = "/mutated";
    assert.equal(memorySink.entries().length, 1);
    assert.deepEqual(memorySink.entries()[0], {
        level: "info",
        message: "hello",
        fields: { route: "/" },
    });
    assert.notEqual(memorySink.entries()[0].fields, fields);
    assert.equal(Object.isFrozen(memorySink.entries()[0].fields), true);

    app.mapGet("/", ({ config, log, services }) => {
        log.info("handler", { route: "/" });
        return Results.text(`${config.require("app.name")}: ${services.get("message")}`);
    }).withName("Hello.Index");

    const querySchema = schema.object({
        q: schema.string().min(1),
    });

    const users = app
        .mapGroup("/users/")
        .withTags("Users")
        .withName("Users");

    assertThrowsMessage(() => app.mapGroup("users"), /starting with/);

    users.mapGet("{id:int}", { query: querySchema }, ({ route }) => {
        return Results.ok({ id: route.id ?? "demo" });
    }).withName("Users.Get");

    const beforeFreeze = app.__getRoutes();
    assert.equal(beforeFreeze.length, 2);
    assert.equal(beforeFreeze[0].method, "GET");
    assert.equal(beforeFreeze[0].pattern, "/");
    assert.equal(beforeFreeze[0].name, "Hello.Index");
    assert.equal(beforeFreeze[0].handler().body, "second: Hello from Sloppy");
    assert.equal(beforeFreeze[1].method, "GET");
    assert.equal(beforeFreeze[1].pattern, "/users/{id:int}");
    assert.equal(beforeFreeze[1].name, "Users.Get");
    assert.deepEqual(beforeFreeze[1].metadata.tags, ["Users"]);
    assert.equal(beforeFreeze[1].metadata.groupName, "Users");
    assert.equal(beforeFreeze[1].metadata.groupPrefix, "/users");
    assert.equal(beforeFreeze[1].metadata.query, querySchema);
    assert.deepEqual(beforeFreeze[1].handler().body, { id: "demo" });

    assert.equal(app.isFrozen(), false);
    assert.equal(app.freeze(), app);
    assert.equal(app.freeze(), app);
    assert.equal(app.isFrozen(), true);

    const afterFreeze = app.__getRoutes();
    assert.equal(afterFreeze.length, 2);
    assert.equal(afterFreeze[0].pattern, beforeFreeze[0].pattern);
    assert.equal(afterFreeze[0].name, beforeFreeze[0].name);
    assertThrowsMessage(() => app.mapGet("/late", () => Results.text("late")), /app is frozen/);
    assertThrowsMessage(() => users.mapGet("/late", () => Results.text("late")), /app is frozen/);

    app.services.dispose();
    app.services.dispose();
    assert.equal(singletonDisposals, 1);
    assertThrowsMessage(() => app.services.get("message"), /provider is disposed/);
}

{
    const builder = Sloppy.createBuilder();
    const disposed = [];
    builder.services.addScoped("async-cleanup", () => ({
        async dispose() {
            await Promise.resolve();
            disposed.push("async-cleanup");
        },
    }));
    const app = builder.build();
    const scope = app.services.createScope();
    scope.get("async-cleanup");
    await scope.dispose();
    assert.deepEqual(disposed, ["async-cleanup"]);
}

{
    const builder = Sloppy.createBuilder();
    let secondDisposed = false;
    builder.services.addScoped("throws-on-dispose", () => ({
        dispose() {
            throw new Error("first disposal failed");
        },
    }));
    builder.services.addScoped("still-disposes", () => ({
        dispose() {
            secondDisposed = true;
        },
    }));
    const app = builder.build();
    const scope = app.services.createScope();
    scope.get("throws-on-dispose");
    scope.get("still-disposes");
    assertThrowsMessage(() => scope.dispose(), /first disposal failed/);
    assert.equal(secondDisposed, true);
}

{
    const builder = Sloppy.createBuilder();
    let singletonContext;
    let singletonScope;
    let scopedScope;
    builder.services.addSingleton("root", (scope) => {
        singletonScope = scope;
        singletonContext = scope.context;
        return "root";
    });
    builder.services.addScoped("scoped", (scope) => {
        scopedScope = scope;
        return "scoped";
    });
    const app = builder.build();
    const requestScope = app.services.createScope();
    assert.equal(requestScope.get("root"), "root");
    assert.equal(requestScope.get("scoped"), "scoped");
    assert.equal(singletonContext, undefined);
    assert.notEqual(singletonScope, requestScope);
    assert.notEqual(singletonScope, scopedScope);
    assert.equal(Object.hasOwn(singletonScope, "context"), false);
}

{
    const builder = Sloppy.createBuilder();
    const disposalOrder = [];
    builder.services.addTransient("transient", () => ({
        dispose() {
            disposalOrder.push("transient");
        },
    }));
    builder.services.addScoped("scoped", (scope) => {
        scope.get("transient");
        return {
            dispose() {
                disposalOrder.push("scoped");
            },
        };
    });
    const app = builder.build();
    const scope = app.services.createScope();
    scope.get("scoped");
    scope.dispose();
    assert.deepEqual(disposalOrder, ["scoped", "transient"]);
}

{
    const builder = Sloppy.createBuilder();
    let disposedScoped = 0;
    let actionSawServices = false;

    builder.services.addScoped("GreetingService", () => ({
        greet(id) {
            return `hello-${id}`;
        },
        dispose() {
            disposedScoped += 1;
        },
    }));

    const app = builder.build();

    class UsersController {
        static inject = ["GreetingService"];

        constructor(greeting) {
            this.greeting = greeting;
        }

        get({ route, services }) {
            actionSawServices = services !== undefined;
            return Results.ok({ message: this.greeting.greet(route.id ?? "demo") });
        }
    }

    app.mapController("/users", UsersController, (users) => {
        users.get("/{id:int}", "get").withName("Users.Get");
    });

    const route = app.__getRoutes()[0];
    assert.equal(route.method, "GET");
    assert.equal(route.pattern, "/users/{id:int}");
    assert.equal(route.name, "Users.Get");
    assert.equal(route.metadata.controller, "UsersController");
    assert.equal(route.metadata.action, "get");
    assert.deepEqual(route.handler({ route: { id: 42 }, services: app.services.createScope() }).body, {
        message: "hello-42",
    });
    assert.deepEqual(route.handler().body, { message: "hello-demo" });
    assert.equal(disposedScoped, 1);
    assert.equal(actionSawServices, true);

    assertThrowsMessage(() => app.mapController("/bad", UsersController, (bad) => {
        bad.get("/", "missing");
    }), /prototype method/);

    app.mapController("/api/", UsersController, (api) => {
        api.get("/status", "get");
    });
    assert.equal(app.__getRoutes()[1].pattern, "/api/status");
}

{
    const builder = Sloppy.createBuilder();
    let disposedScoped = 0;
    builder.services.addScoped("GreetingService", () => ({
        dispose() {
            disposedScoped += 1;
        },
    }));
    const app = builder.build();

    class FailingController {
        static inject = ["GreetingService", "MissingService"];

        get() {
            return Results.ok({});
        }
    }

    app.mapController("/failing", FailingController, (routes) => {
        routes.get("/", "get");
    });
    assertThrowsMessage(() => app.__getRoutes()[0].handler(), /not registered/);
    assert.equal(disposedScoped, 1);
}

{
    const builder = Sloppy.createBuilder();
    let disposedScoped = 0;
    builder.services.addScoped("GreetingService", () => ({
        dispose() {
            disposedScoped += 1;
        },
    }));
    const app = builder.build();

    class AsyncController {
        static inject = ["GreetingService"];

        constructor(greeting) {
            this.greeting = greeting;
        }

        async ok({ services }) {
            assert.equal(services.get("GreetingService"), this.greeting);
            await Promise.resolve();
            return Results.ok({ ok: true });
        }

        async fail() {
            await Promise.resolve();
            throw new Error("controller failed");
        }
    }

    app.mapController("/async", AsyncController, (routes) => {
        routes.get("/ok", "ok");
        routes.get("/fail", "fail");
    });
    const routes = app.__getRoutes();
    assert.deepEqual((await routes[0].handler()).body, { ok: true });
    assert.equal(disposedScoped, 1);
    await assert.rejects(routes[1].handler(), /controller failed/);
    assert.equal(disposedScoped, 2);
}

{
    const app = Sloppy.create();
    function usersModule(moduleApp) {
        const api = moduleApp.group("/api");
        api.group("/users").get("/{id:int}", ({ route }) => Results.ok({ id: route.id ?? "demo" }));
    }

    app.useModule(usersModule);
    assert.equal(app.__getRoutes()[0].pattern, "/api/users/{id:int}");
    assert.equal(app.__getRoutes()[0].metadata.module, "usersModule");
    assert.deepEqual(app.__getRoutes()[0].handler().body, { id: "demo" });
    assertThrowsMessage(() => app.useModule(usersModule), /already registered/);
    assertThrowsMessage(() => app.get("/api/users/{id:int}", () => Results.ok({})), /already registered/);

    const reports = Sloppy.module("reports").routes((moduleApp) => {
        moduleApp.get("/reports", () => Results.ok({ ok: true }));
    });
    app.useModule(reports);
    assert.equal(app.__getRoutes()[1].metadata.module, "reports");

    assertThrowsMessage(
        () => app.useModule(Sloppy.module("data").services(() => {})),
        /route-only modules/,
    );

    app.useModule(Router.group("/admin", (admin) => {
        admin.get("/health", () => Results.text("ok"));
    }));
    assert.equal(app.__getRoutes()[2].pattern, "/admin/health");
    assert.equal(app.__getRoutes()[2].metadata.module, "router:/admin");
}

{
    const builder = Sloppy.createBuilder();
    builder.services.addSingleton("root", (scope) => scope.get("request"));
    builder.services.addScoped("request", () => "request");
    const app = builder.build();
    assertThrowsMessage(() => app.services.get("root"), /singleton service cannot depend on scoped service/);
}

{
    const builder = Sloppy.createBuilder();
    builder.services.addTransient("a", (scope) => scope.get("b"));
    builder.services.addTransient("b", (scope) => scope.get("a"));
    const app = builder.build();
    const scope = app.services.createScope();
    assertThrowsMessage(() => scope.get("a"), /circular dependency/);
}

{
    class SqliteOptions {
        constructor(values) {
            this.database = values.database;
            this.queueCapacity = values.queueCapacity;
        }
    }

    const builder = Sloppy.createBuilder();
    builder.config.addObject({
        Sloppy: {
            Server: {
                MaxRequestBodyBytes: 16384,
                RequestTimeoutMs: 15000,
            },
            Providers: {
                sqlite: {
                    main: {
                        database: "./app.db",
                        queueCapacity: 8,
                    },
                },
            },
        },
    });
    const app = builder.build();
    const options = app.config.bind("sqlite:main", SqliteOptions);
    assert.equal(options.database, "./app.db");
    assert.equal(options.queueCapacity, 8);
    const serverOptions = app.config.bind("Sloppy:Server");
    assert.equal(serverOptions.maxRequestBodyBytes, 16384);
    assert.equal(serverOptions.requestTimeoutMs, 15000);

    const provider = app.use(sqlite("main", { database: ":memory:" }));
    assert.equal(provider.kind, "sqlite");
    assert.equal(provider.name, "main");
    assert.equal(provider.options.database, ":memory:");
    assert.equal(provider.options.queueCapacity, 8);
}

{
    const app = Sloppy.create();

    assertThrowsMessage(() => app.use(sqlite("missing")), /database option/);
    assertThrowsMessage(() => sqlite("bad:name"), /provider name/);
    assertThrowsMessage(() => sqlite(" main "), /provider name/);

    const provider = app.use({
        __sloppyProvider: true,
        kind: "sqlite",
        name: "main",
        token: "wrong.token",
        options: { database: ":memory:" },
    });
    assert.equal(provider.token, "data.main");
}

{
    const app = Sloppy.create();

    assert.equal(typeof app.config.get, "function");
    assert.equal(typeof app.log.info, "function");
    assert.equal(typeof app.services.createScope, "function");

    app.mapGet("/tiny", () => Results.text("tiny"));
    app.mapPost("/tiny", () => Results.json({ method: "POST" }));
    app.mapPut("/tiny", () => Results.json({ method: "PUT" }));
    app.mapPatch("/tiny", () => Results.json({ method: "PATCH" }));
    app.mapDelete("/tiny", () => Results.noContent());
    app.mapGroup("/health").withTags("Health").mapGet("/", () => Results.noContent());
    app.mapGroup("/jobs").withTags("Jobs").mapPost("/", () => Results.accepted());
    assert.equal(app.__getRoutes()[0].handler().body, "tiny");
    assert.equal(app.__getRoutes()[1].method, "POST");
    assert.equal(app.__getRoutes()[2].method, "PUT");
    assert.equal(app.__getRoutes()[3].method, "PATCH");
    assert.equal(app.__getRoutes()[4].method, "DELETE");
    assert.equal(app.__getRoutes()[5].pattern, "/health");
    assert.equal(app.__getRoutes()[6].method, "POST");
    assert.equal(app.__getRoutes()[6].pattern, "/jobs");
}

{
    assert.deepEqual(Results.ok({ ok: true }), {
        __sloppyResult: true,
        kind: "json",
        status: 200,
        body: { ok: true },
        contentType: "application/json; charset=utf-8",
        headers: undefined,
    });
    assert.equal(Results.created("/users/1", { id: 1 }).status, 201);
    assert.equal(Results.created("/users/1", { id: 1 }).location, "/users/1");
    assert.equal(Results.accepted({ queued: true }).status, 202);
    assert.equal(Results.noContent().status, 204);
    assert.equal(Object.prototype.hasOwnProperty.call(Results.noContent(), "body"), false);
    assert.equal(Results.notFound().status, 404);
    assert.equal(Results.badRequest({ error: "bad" }).status, 400);
    assert.deepEqual(Results.status(202, { accepted: true }).body, { accepted: true });
    assert.equal(Results.status(204).kind, "empty");
    assert.equal(Results.problem("broken").kind, "problem");
    assert.equal(Results.problem("broken").body.status, 500);
    assert.equal(Results.html("<p>ok</p>").contentType, "text/html; charset=utf-8");
    const bytesSource = new Uint8Array([0, 65, 255]);
    const bytesResult = Results.bytes(bytesSource, { contentType: "application/x-test" });
    bytesSource[1] = 66;
    assert.equal(bytesResult.kind, "bytes");
    assert.equal(bytesResult.contentType, "application/x-test");
    assert.deepEqual(Array.from(bytesResult.body), [0, 65, 255]);
    assert.deepEqual(Results.json({ ok: true }, { headers: { "x-test": "1" } }).headers, {
        "x-test": "1",
    });
    assertThrowsMessage(() => Results.ok("bad", { status: 99 }), /status/);
    assertThrowsMessage(() => Results.ok("bad", { headers: new Map() }), /plain object/);
    assertThrowsMessage(() => Results.bytes([1, 2, 3]), /binary data or a typed array view/);
    assertThrowsMessage(() => Results.bytes(new Uint8Array([1]), { contentType: "" }), /contentType/);
    assertThrowsMessage(() => Results.bytes(new Uint8Array([1]), { contentType: "text/plain\r\nx: y" }), /control characters/);
    assertThrowsMessage(() => Results.bytes(new Uint8Array([1]), { contentType: "text/plain\0" }), /control characters/);
    assertThrowsMessage(() => Results.bytes(new Uint8Array([1]), { contentType: "text/plain\x1F" }), /control characters/);
    assertThrowsMessage(() => Results.bytes(new Uint8Array([1]), { contentType: "text/plain\x7F" }), /control characters/);
}

{
    const Email = schema.string().min(3).email();
    assert.equal(Email.kind, "string");
    assert.deepEqual(Email.validate("a@example.com"), {
        ok: true,
        value: "a@example.com",
    });

    const invalidEmail = Email.validate("no");
    assert.equal(invalidEmail.ok, false);
    assert.deepEqual(invalidEmail.issues.map((current) => current.code), [
        "string.min",
        "string.email",
    ]);
    assert.deepEqual(Email.metadata.rules.map((rule) => rule.kind), ["min", "email"]);

    const User = schema.object({
        name: schema.string().min(1),
        age: schema.number(),
        active: schema.boolean(),
    });

    assert.equal(User.kind, "object");
    assert.deepEqual(User.validate({
        name: "Ada",
        age: 37,
        active: true,
    }).ok, true);

    const invalidUser = User.validate({
        name: "",
        age: Number.NaN,
        active: "yes",
    });

    assert.equal(invalidUser.ok, false);
    assert.deepEqual(invalidUser.issues.map((current) => current.path.join(".")), [
        "name",
        "age",
        "active",
    ]);
    assert.equal(User.metadata.shape.name.kind, "string");
    assertThrowsMessage(() => schema.object({ bad: {} }), /must be a schema/);
    assertThrowsMessage(
        () => schema.object({ bad: { validate() { return { ok: true }; } } }),
        /must be a schema/,
    );
}
