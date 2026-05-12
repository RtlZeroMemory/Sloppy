import assert from "node:assert/strict";
import { mkdtemp, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { pathToFileURL } from "node:url";

import {
    BackgroundService,
    Deadline,
    Sloppy,
    SloppyWorkerError,
    WorkQueue,
    Worker,
    WorkerCancellationController,
    WorkerPool,
} from "../../stdlib/sloppy/index.js";

function assertWorkerCode(error, code) {
    assert(error instanceof SloppyWorkerError);
    assert.equal(error.code, code);
}

async function assertRejectsWorkerCode(promise, code) {
    await assert.rejects(promise, (error) => {
        assertWorkerCode(error, code);
        return true;
    });
}

async function flush(count = 5) {
    for (let index = 0; index < count; index += 1) {
        await Promise.resolve();
    }
}

function installWorkerBridge() {
    const previous = globalThis.__sloppy;
    globalThis.__sloppy = {
        ...previous,
        workers: {
            async runPool(_name, fn, input, ctx) {
                return fn(Object.freeze({
                    input,
                    signal: ctx.signal,
                    deadline: ctx.deadline,
                }));
            },
            async startWorker(modulePath) {
                const moduleNamespace = await import(modulePath);
                let stopped = false;
                return {
                    async invoke(exportName, payload = null, ctx = undefined) {
                        if (stopped) {
                            throw new SloppyWorkerError("SLOPPY_E_WORKER_STALE_HANDLE", "worker handle has been stopped");
                        }
                        const fn = moduleNamespace[exportName];
                        if (typeof fn !== "function") {
                            throw new SloppyWorkerError("SLOPPY_E_WORKER_CRASHED", "worker export is not callable");
                        }
                        return fn(payload, ctx);
                    },
                    async post(payload = null, ctx = undefined) {
                        return this.invoke("onMessage", payload, ctx);
                    },
                    onMessage(callback) {
                        return callback;
                    },
                    async stop() {
                        stopped = true;
                    },
                };
            },
        },
    };
    return () => {
        globalThis.__sloppy = previous;
    };
}

{
    assert.throws(() => BackgroundService.create("bad-options", async () => {}, "bad"), TypeError);
    assert.throws(() => WorkQueue.create("bad-options", "bad"), TypeError);
    assert.throws(() => WorkerPool.create("bad-options", "bad"), TypeError);
    await assert.rejects(Worker.start("bad\0worker", {}), TypeError);
    await assert.rejects(Worker.start("file:///worker.mjs", "bad"), TypeError);
}

{
    let started = 0;
    let observedCancel = false;
    const service = BackgroundService.create("cleanup", async (ctx) => {
        started += 1;
        while (!ctx.signal.cancelled) {
            await Promise.resolve();
        }
        observedCancel = true;
    });
    const app = Sloppy.create();
    assert.equal(app.use(service), service);
    await flush();
    assert.equal(started, 1);
    assert.equal(app.__getPlanContributions().workers[0].kind, "backgroundService");
    await service.stop();
    assert.equal(observedCancel, true);
    assert.equal(service.state, "stopped");
}

{
    const queue = WorkQueue.create("emails", { maxQueued: 4, concurrency: 2, overflow: "reject" });
    let active = 0;
    let maxActive = 0;
    const order = [];
    queue.process(async (job) => {
        active += 1;
        maxActive = Math.max(maxActive, active);
        order.push(job.data.id);
        await Promise.resolve();
        active -= 1;
        return job.data.id * 2;
    });

    const results = await Promise.all([
        queue.enqueue({ id: 1 }),
        queue.enqueue({ id: 2 }),
        queue.enqueue({ id: 3 }),
        queue.enqueue({ id: 4 }),
    ]);
    assert.deepEqual(results, [2, 4, 6, 8]);
    assert.deepEqual(order, [1, 2, 3, 4]);
    assert.equal(maxActive, 2);
}

{
    const queue = WorkQueue.create("overflow", { maxQueued: 1, concurrency: 1, overflow: "reject" });
    let release;
    queue.process((job) => {
        if (job.data === "queued") {
            return "queued-ok";
        }
        return new Promise((resolve) => {
            release = resolve;
        });
    });
    const first = queue.enqueue("active");
    const second = queue.enqueue("queued");
    await assertRejectsWorkerCode(queue.enqueue("rejected"), "SLOPPY_E_WORK_QUEUE_FULL");
    release("ok");
    await first;
    assert.equal(await second, "queued-ok");
}

{
    const queue = WorkQueue.create("retry", {
        maxQueued: 4,
        concurrency: 1,
        retry: { maxAttempts: 3, backoffMs: 0 },
    });
    let attempts = 0;
    queue.process(async () => {
        attempts += 1;
        if (attempts < 3) {
            throw new Error("planned failure");
        }
        return "sent";
    });
    assert.equal(await queue.enqueue({ template: "welcome" }), "sent");
    assert.equal(attempts, 3);
}

{
    const queue = WorkQueue.create("timeout", { maxQueued: 1, concurrency: 1 });
    queue.process(() => new Promise(() => {}));
    await assertRejectsWorkerCode(
        queue.enqueue("late", { deadline: Deadline.after(1) }),
        "SLOPPY_E_WORK_JOB_TIMEOUT",
    );
}

{
    const queue = WorkQueue.create("backpressure-shutdown", {
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
    const waiting = queue.enqueue("waiting");
    const stopped = queue.stop({ drain: false });
    await assertRejectsWorkerCode(queued, "SLOPPY_E_WORKER_SHUTDOWN_CANCELLED");
    await assertRejectsWorkerCode(waiting, "SLOPPY_E_WORKER_SHUTDOWN_CANCELLED");
    releaseActive();
    assert.equal(await active, "active-ok");
    await stopped;
}

{
    const controller = new WorkerCancellationController();
    controller.cancel("caller");
    const queue = WorkQueue.create("cancel", { maxQueued: 1, concurrency: 1 });
    queue.process(async () => "never");
    await assertRejectsWorkerCode(
        queue.enqueue("cancelled", { signal: controller.signal }),
        "SLOPPY_E_WORK_JOB_CANCELLED",
    );
}

{
    await assertRejectsWorkerCode(
        WorkerPool.create("missing-bridge", { workers: 1 }).run(async () => "never"),
        "SLOPPY_E_WORKER_BRIDGE_UNAVAILABLE",
    );
}

{
    const restoreBridge = installWorkerBridge();
    const pool = WorkerPool.create("image-processing", { workers: 2, maxQueued: 4 });
    try {
        const values = await Promise.all([
            pool.run(async (ctx) => ctx.input + 1, { input: 1 }),
            pool.run(async (ctx) => ctx.input + 1, { input: 2 }),
        ]);
        assert.deepEqual(values, [2, 3]);
        assert.equal(await pool.run(async (ctx) => ctx.input === null), true);
        assert.equal(pool.state.workers, 2);
        await pool.stop();
        await assertRejectsWorkerCode(pool.run(async () => "late"), "SLOPPY_E_WORK_QUEUE_STOPPED");
    } finally {
        restoreBridge();
    }
}

{
    await assertRejectsWorkerCode(
        WorkQueue.create("unsupported").process(async () => undefined).enqueue(() => {}),
        "SLOPPY_E_WORKER_UNSUPPORTED_PAYLOAD",
    );
}

{
    await assertRejectsWorkerCode(
        Worker.start("file:///missing-worker.mjs", { memoryLimitMb: 128 }),
        "SLOPPY_E_WORKER_BRIDGE_UNAVAILABLE",
    );
}

{
    const restoreBridge = installWorkerBridge();
    const directory = await mkdtemp(join(tmpdir(), "sloppy-worker-"));
    const workerPath = join(directory, "parser.mjs");
    try {
        await writeFile(
            workerPath,
            "export async function parse(payload) { return { tokens: payload.text.split(/\\s+/u).length }; }\n"
                + "export function ping(payload) { return payload === null ? 'pong' : 'unexpected'; }\n"
                + "export function onMessage(payload) { return payload === null ? 'posted-null' : 'unexpected'; }\n",
            "utf8",
        );
        const worker = await Worker.start(pathToFileURL(workerPath).href, { memoryLimitMb: 128 });
        assert.deepEqual(await worker.invoke("parse", { text: "one two three" }), { tokens: 3 });
        assert.equal(await worker.invoke("ping"), "pong");
        assert.equal(await worker.post(), "posted-null");
        assert.equal(typeof worker.onMessage(() => {}), "function");
        await worker.stop();
        await assertRejectsWorkerCode(worker.invoke("parse", { text: "late" }), "SLOPPY_E_WORKER_STALE_HANDLE");
    } finally {
        restoreBridge();
    }
}
