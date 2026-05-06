import assert from "node:assert/strict";

import {
    Base64,
    Deadline,
    FileHandle,
    Hash,
    Hex,
    HttpClient,
    Process,
    Secret,
    Text,
    WorkQueue,
} from "../../stdlib/sloppy/index.js";

function assertBytes(actual, expected) {
    assert.deepEqual(Array.from(actual), Array.from(expected));
}

async function assertRejectsCode(promise, code) {
    await assert.rejects(promise, (error) => {
        assert.equal(error.code, code);
        return true;
    });
}

const previousSloppy = globalThis.__sloppy;

try {
    const digestBytes = new Uint8Array([0x00, 0x0f, 0x10, 0xff]);
    globalThis.__sloppy = {
        ...(previousSloppy ?? {}),
        crypto: {
            hash() {
                return digestBytes.slice();
            },
        },
    };

    assert.equal(await Hash.sha256Hex("A\0B"), Hex.encode(digestBytes));
    assert.equal(await Hash.sha256Base64("A\0B"), Base64.encode(digestBytes));
    assertBytes(Secret.fromUtf8("A\0B").bytes(), Text.utf8.encode("A\0B"));

    let readCount = 0;
    globalThis.__sloppy = {
        ...(globalThis.__sloppy ?? {}),
        fs: {
            handleRead(_id, _maxBytes) {
                readCount += 1;
                return readCount === 1 ? Text.utf8.encode("a\nb\nc\n") : new Uint8Array(0);
            },
        },
    };
    const lines = [];
    for await (const line of new FileHandle({ slot: 1, generation: 1 }).readLines({
        chunkSize: 64,
        maxLineLength: 1,
    })) {
        lines.push(line);
    }
    assert.deepEqual(lines, ["a", "b", "c"]);

    readCount = 0;
    globalThis.__sloppy.fs.handleRead = () => {
        readCount += 1;
        return readCount === 1 ? Text.utf8.encode("aa") : new Uint8Array(0);
    };
    await assert.rejects(async () => {
        for await (const _line of new FileHandle({ slot: 1, generation: 1 }).readLines({
            chunkSize: 64,
            maxLineLength: 1,
        })) {
            // unreachable
        }
    }, /SLOPPY_E_LIMIT_EXCEEDED/);

    readCount = 0;
    globalThis.__sloppy.fs.handleRead = () => {
        readCount += 1;
        return readCount === 1
            ? Text.utf8.encode("abc\r")
            : readCount === 2
              ? Text.utf8.encode("\n")
              : new Uint8Array(0);
    };
    const splitDelimiterLines = [];
    for await (const line of new FileHandle({ slot: 1, generation: 1 }).readLines({
        chunkSize: 64,
        maxLineLength: 3,
        newline: "\r\n",
    })) {
        splitDelimiterLines.push(line);
    }
    assert.deepEqual(splitDelimiterLines, ["abc"]);

    readCount = 0;
    globalThis.__sloppy.fs.handleRead = () => {
        readCount += 1;
        return readCount === 1 ? Text.utf8.encode("abc\r") : new Uint8Array(0);
    };
    const trailingCarriageReturnLines = [];
    for await (const line of new FileHandle({ slot: 1, generation: 1 }).readLines({
        chunkSize: 64,
        maxLineLength: 3,
        newline: "\r\n",
    })) {
        trailingCarriageReturnLines.push(line);
    }
    assert.deepEqual(trailingCarriageReturnLines, ["abc"]);

    readCount = 0;
    globalThis.__sloppy.fs.handleRead = () => {
        readCount += 1;
        return readCount === 1 ? Text.utf8.encode("abc\r--") : new Uint8Array(0);
    };
    const customDelimiterLines = [];
    for await (const line of new FileHandle({ slot: 1, generation: 1 }).readLines({
        chunkSize: 64,
        maxLineLength: 4,
        newline: "--",
    })) {
        customDelimiterLines.push(line);
    }
    assert.deepEqual(customDelimiterLines, ["abc\r"]);

    globalThis.__sloppy = {
        ...(globalThis.__sloppy ?? {}),
        os: {
            processStart() {
                return Object.freeze({ slot: 9, generation: 1 });
            },
            processReadStdout() {
                return Text.utf8.encode("x\0y\n");
            },
        },
    };
    const proc = await Process.start("tool", [], { stdout: "pipe" });
    assert.equal(await proc.stdout.readText(), "x\0y\n");

    function oneShotStream(value) {
        let streamReturned = 0;
        const stream = {
            [Symbol.asyncIterator]() {
                let step = 0;
                return {
                    async next() {
                        step += 1;
                        if (step === 1) {
                            return { done: false, value };
                        }
                        return { done: true, value: undefined };
                    },
                    async return() {
                        streamReturned += 1;
                        return { done: true, value: undefined };
                    },
                };
            },
        };
        return { stream, returned: () => streamReturned };
    }

    function pendingStream() {
        let streamReturned = 0;
        const stream = {
            [Symbol.asyncIterator]() {
                return {
                    async next() {
                        return new Promise(() => {});
                    },
                    async return() {
                        streamReturned += 1;
                        return { done: true, value: undefined };
                    },
                };
            },
        };
        return { stream, returned: () => streamReturned };
    }

    let streamReturned = 0;
    const tooLargeStream = {
        [Symbol.asyncIterator]() {
            let step = 0;
            return {
                async next() {
                    step += 1;
                    return {
                        done: false,
                        value: step === 1 ? new Uint8Array([1, 2]) : new Uint8Array([3, 4]),
                    };
                },
                async return() {
                    streamReturned += 1;
                    return { done: true, value: undefined };
                },
            };
        },
    };
    await assertRejectsCode(
        HttpClient.request({
            url: "http://127.0.0.1/",
            stream: tooLargeStream,
            maxRequestBytes: 3,
        }),
        "SLOPPY_E_HTTP_CLIENT_REQUEST_BODY_LIMIT",
    );
    assert.equal(streamReturned, 1);

    let delayedCleanupFinished = false;
    const delayedCleanupStream = {
        [Symbol.asyncIterator]() {
            let step = 0;
            return {
                async next() {
                    step += 1;
                    return {
                        done: false,
                        value: step === 1 ? new Uint8Array([1, 2]) : new Uint8Array([3, 4]),
                    };
                },
                async return() {
                    await new Promise((resolve) => setTimeout(resolve, 5));
                    delayedCleanupFinished = true;
                    return { done: true, value: undefined };
                },
            };
        },
    };
    await assertRejectsCode(
        HttpClient.request({
            url: "http://127.0.0.1/",
            stream: delayedCleanupStream,
            maxRequestBytes: 3,
        }),
        "SLOPPY_E_HTTP_CLIENT_REQUEST_BODY_LIMIT",
    );
    assert.equal(delayedCleanupFinished, true);

    const invalidChunk = oneShotStream("not-bytes");
    await assertRejectsCode(
        HttpClient.request({
            url: "http://127.0.0.1/",
            stream: invalidChunk.stream,
        }),
        "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
    );
    assert.equal(invalidChunk.returned(), 1);

    const timedOutStream = pendingStream();
    await assertRejectsCode(
        HttpClient.request({
            url: "http://127.0.0.1/",
            stream: timedOutStream.stream,
            timeoutMs: 1,
        }),
        "SLOPPY_E_HTTP_CLIENT_REQUEST_TIMEOUT",
    );
    assert.equal(timedOutStream.returned(), 1);

    const cancelledStream = pendingStream();
    const streamController = new AbortController();
    const cancelledRequest = HttpClient.request({
        url: "http://127.0.0.1/",
        stream: cancelledStream.stream,
        signal: streamController.signal,
    });
    streamController.abort("caller");
    await assertRejectsCode(cancelledRequest, "SLOPPY_E_HTTP_CLIENT_REQUEST_CANCELLED");
    assert.equal(cancelledStream.returned(), 1);

    await assertRejectsCode(
        HttpClient.get("https://127.0.0.1/"),
        "SLOPPY_E_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE",
    );

    const queue = WorkQueue.create("core-integration-backpressure", {
        maxQueued: 1,
        concurrency: 1,
        overflow: "backpressure",
        maxBackpressureWaiters: 1,
    });
    let releaseActive;
    queue.process((job) => {
        if (job.data === "active") {
            return new Promise((resolve) => {
                releaseActive = () => resolve("active-ok");
            });
        }
        return `${job.data}-ok`;
    });
    const active = queue.enqueue("active");
    const queued = queue.enqueue("queued");
    const waiting = queue.enqueue("waiting", { deadline: Deadline.after(1) });
    await assertRejectsCode(waiting, "SLOPPY_E_WORK_JOB_TIMEOUT");
    releaseActive();
    assert.equal(await active, "active-ok");
    assert.equal(await queued, "queued-ok");

    const cancelQueue = WorkQueue.create("core-integration-backpressure-cancel", {
        maxQueued: 1,
        concurrency: 1,
        overflow: "backpressure",
        maxBackpressureWaiters: 1,
    });
    let releaseCancelActive;
    cancelQueue.process((job) => {
        if (job.data === "active") {
            return new Promise((resolve) => {
                releaseCancelActive = () => resolve("cancel-active-ok");
            });
        }
        return `${job.data}-ok`;
    });
    const cancelActive = cancelQueue.enqueue("active");
    const cancelQueued = cancelQueue.enqueue("queued");
    const waitController = new AbortController();
    const cancelWaiting = cancelQueue.enqueue("waiting", { signal: waitController.signal });
    waitController.abort("caller");
    await assertRejectsCode(cancelWaiting, "SLOPPY_E_WORK_JOB_CANCELLED");
    releaseCancelActive();
    assert.equal(await cancelActive, "cancel-active-ok");
    assert.equal(await cancelQueued, "queued-ok");
} finally {
    if (previousSloppy === undefined) {
        delete globalThis.__sloppy;
    } else {
        globalThis.__sloppy = previousSloppy;
    }
}
