import { isPlainObject } from "./internal/validation.js";
import { copyBinaryLike } from "./internal/bytes.js";
import { Deadline } from "./time.js";

const DEFAULT_QUEUE_CAPACITY = 1024;
const DEFAULT_WORKER_POOL_CAPACITY = 64;
const MAX_RESOURCE_NAME_LENGTH = 128;
const WORKER_SERIALIZATION_MARKER = "__sloppyWorkerSerialized";

class SloppyWorkerError extends Error {
    constructor(code, message, options = undefined) {
        super(`${code}: ${message}`, options);
        this.name = "SloppyWorkerError";
        this.code = code;
    }
}

function workerError(code, message, cause = undefined) {
    return new SloppyWorkerError(code, message, cause === undefined ? undefined : { cause });
}

function toWorkerError(error, fallbackCode, fallbackMessage) {
    if (error instanceof SloppyWorkerError) {
        return error;
    }
    const code = typeof error?.code === "string" && error.code.startsWith("SLOPPY_E_")
        ? error.code
        : fallbackCode;
    const message = typeof error?.message === "string" && error.message.length > 0
        ? error.message
        : fallbackMessage;
    return workerError(code, message, error);
}

function validateName(name, subject) {
    if (
        typeof name !== "string" ||
        name.length === 0 ||
        name.length > MAX_RESOURCE_NAME_LENGTH ||
        name.trim() !== name ||
        /[\0\r\n]/u.test(name)
    ) {
        throw new TypeError(`${subject} name must be a non-empty stable string.`);
    }
    return name;
}

function positiveInteger(value, field, fallback = undefined) {
    if (value === undefined) {
        if (fallback !== undefined) {
            return fallback;
        }
        throw new TypeError(`${field} is required.`);
    }
    if (!Number.isInteger(value) || value <= 0) {
        throw new TypeError(`${field} must be a positive integer.`);
    }
    return value;
}

function normalizeDeadline(deadline) {
    if (deadline === undefined || deadline === null) {
        return undefined;
    }
    if (typeof deadline.remainingMs === "function") {
        return deadline;
    }
    if (typeof deadline === "number") {
        return Deadline.after(deadline);
    }
    throw new TypeError("worker deadline must expose remainingMs().");
}

function deadlineRemainingMs(deadline) {
    if (deadline === undefined) {
        return Infinity;
    }
    const remaining = Number(deadline.remainingMs());
    if (remaining === Infinity) {
        return Infinity;
    }
    if (!Number.isFinite(remaining)) {
        throw new TypeError("worker deadline remainingMs() must be finite or Infinity.");
    }
    return Math.max(0, remaining);
}

function isSignal(value) {
    return value !== null && typeof value === "object" &&
        (typeof value.cancelled === "boolean" || typeof value.aborted === "boolean");
}

function signalCancelled(signal) {
    return isSignal(signal) && (signal.cancelled === true || signal.aborted === true);
}

function signalReason(signal) {
    return isSignal(signal) ? signal.reason : undefined;
}

function subscribeSignal(signal, listener) {
    if (!isSignal(signal) || typeof listener !== "function") {
        return () => {};
    }
    if (signalCancelled(signal)) {
        listener(signalReason(signal));
        return () => {};
    }
    if (typeof signal._subscribe === "function") {
        return signal._subscribe(listener);
    }
    if (typeof signal.addEventListener === "function") {
        const wrapped = () => listener(signalReason(signal));
        signal.addEventListener("abort", wrapped);
        return () => signal.removeEventListener?.("abort", wrapped);
    }
    return () => {};
}

function raceSignalCancellation(promise, signal, timeoutCode = "SLOPPY_E_WORK_JOB_TIMEOUT") {
    if (!isSignal(signal)) {
        return Promise.resolve(promise);
    }
    if (signalCancelled(signal)) {
        return Promise.reject(rejectForCancellation(signalReason(signal), timeoutCode));
    }
    let cleanup = () => {};
    const cancellation = new Promise((_, reject) => {
        cleanup = subscribeSignal(signal, (reason) => reject(rejectForCancellation(reason, timeoutCode)));
    });
    return Promise.race([promise, cancellation]).finally(cleanup);
}

class WorkerCancellationSignal {
    constructor() {
        this.cancelled = false;
        this.aborted = false;
        this.reason = undefined;
        this._listeners = new Set();
        Object.seal(this);
    }

    throwIfCancelled() {
        if (this.cancelled) {
            throw workerError("SLOPPY_E_WORK_JOB_CANCELLED", "worker operation was cancelled");
        }
    }

    addEventListener(type, listener) {
        if (type === "abort" && typeof listener === "function") {
            this._listeners.add(listener);
        }
    }

    removeEventListener(type, listener) {
        if (type === "abort") {
            this._listeners.delete(listener);
        }
    }

    _subscribe(listener) {
        if (this.cancelled) {
            listener(this.reason);
            return () => {};
        }
        this._listeners.add(listener);
        return () => {
            this._listeners.delete(listener);
        };
    }

    _cancel(reason) {
        if (this.cancelled) {
            return false;
        }
        this.cancelled = true;
        this.aborted = true;
        this.reason = reason;
        const listeners = Array.from(this._listeners);
        this._listeners.clear();
        for (const listener of listeners) {
            try {
                listener(reason);
            } catch {
                // Cancellation fan-out must stay deterministic even if user code throws.
            }
        }
        return true;
    }
}

class WorkerCancellationController {
    constructor() {
        this.signal = new WorkerCancellationSignal();
        Object.freeze(this);
    }

    cancel(reason = "cancelled") {
        return this.signal._cancel(reason);
    }
}

function normalizeOperationOptions(options = undefined) {
    if (options === undefined || options === null) {
        return { deadline: undefined, signal: undefined };
    }
    if (!isPlainObject(options)) {
        throw new TypeError("worker operation options must be a plain object.");
    }
    if (options.signal !== undefined && !isSignal(options.signal)) {
        throw new TypeError("worker signal must be a Sloppy cancellation signal or AbortSignal-like object.");
    }
    let timeoutDeadline = undefined;
    if (options.timeoutMs !== undefined) {
        if (!Number.isFinite(options.timeoutMs) || options.timeoutMs < 0) {
            throw new TypeError("worker timeoutMs must be finite and non-negative.");
        }
        timeoutDeadline = Deadline.after(Math.ceil(options.timeoutMs));
    }
    const explicitDeadline = normalizeDeadline(options.deadline);
    return {
        deadline: combineDeadlines(explicitDeadline, timeoutDeadline),
        signal: options.signal,
    };
}

function combineDeadlines(first, second) {
    if (first === undefined) {
        return second;
    }
    if (second === undefined) {
        return first;
    }
    return Object.freeze({
        remainingMs() {
            return Math.min(deadlineRemainingMs(first), deadlineRemainingMs(second));
        },
    });
}

function timeoutMsFromOptions(options) {
    const deadlineMs = deadlineRemainingMs(options.deadline);
    return deadlineMs === Infinity ? Infinity : Math.ceil(deadlineMs);
}

function delay(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
}

function yieldTurn() {
    return Promise.resolve();
}

function copyBytes(bytes) {
    return copyBinaryLike(bytes);
}

function validateWorkerOptions(options, operation) {
    if (options === undefined || options === null) {
        return Object.freeze({});
    }
    if (!isPlainObject(options)) {
        throw new TypeError(`${operation} options must be a plain object.`);
    }
    return Object.freeze({ ...options });
}

function serializePayload(value, seen = new Set()) {
    if (value === undefined) {
        return null;
    }
    if (value === null || typeof value === "string" || typeof value === "boolean") {
        return value;
    }
    if (typeof value === "number") {
        if (!Number.isFinite(value)) {
            throw workerError("SLOPPY_E_WORKER_UNSUPPORTED_PAYLOAD", "worker payload is not serializable");
        }
        return value;
    }
    const bytes = copyBytes(value);
    if (bytes !== undefined) {
        return bytes;
    }
    if (Array.isArray(value)) {
        if (seen.has(value)) {
            throw workerError("SLOPPY_E_WORKER_MESSAGE_SERIALIZATION_FAILED", "worker payload contains a cycle");
        }
        seen.add(value);
        const copy = value.map((item) => serializePayload(item, seen));
        seen.delete(value);
        return copy;
    }
    if (isPlainObject(value)) {
        if (seen.has(value)) {
            throw workerError("SLOPPY_E_WORKER_MESSAGE_SERIALIZATION_FAILED", "worker payload contains a cycle");
        }
        if (Object.prototype.hasOwnProperty.call(value, WORKER_SERIALIZATION_MARKER)) {
            throw workerError("SLOPPY_E_WORKER_UNSUPPORTED_PAYLOAD", "worker payload uses a reserved serialization marker");
        }
        seen.add(value);
        const copy = {};
        for (const [key, item] of Object.entries(value)) {
            if (item !== undefined) {
                copy[key] = serializePayload(item, seen);
            }
        }
        seen.delete(value);
        return copy;
    }
    throw workerError("SLOPPY_E_WORKER_UNSUPPORTED_PAYLOAD", "worker payload type is unsupported");
}

function makeContext(options, extra = {}) {
    const controller = new WorkerCancellationController();
    const parentCleanup = subscribeSignal(options.signal, (reason) => controller.cancel(reason));
    const timeoutMs = timeoutMsFromOptions(options);
    let timer = undefined;
    if (timeoutMs === 0) {
        controller.cancel("deadline");
    } else if (timeoutMs !== Infinity) {
        timer = setTimeout(() => controller.cancel("deadline"), timeoutMs);
    }
    return {
        ctx: Object.freeze({
            signal: controller.signal,
            deadline: options.deadline,
            ...extra,
        }),
        controller,
        cleanup() {
            parentCleanup();
            if (timer !== undefined) {
                clearTimeout(timer);
            }
        },
    };
}

function isDeadlineReason(reason) {
    return reason === "deadline" || reason === "timeout";
}

function rejectForCancellation(reason, timeoutCode = "SLOPPY_E_WORK_JOB_TIMEOUT") {
    if (isDeadlineReason(reason)) {
        return workerError(timeoutCode, "worker operation timed out");
    }
    return workerError("SLOPPY_E_WORK_JOB_CANCELLED", "worker operation was cancelled");
}

function normalizeRetry(options = undefined) {
    const retry = options?.retry ?? {};
    const attempts = positiveInteger(retry.maxAttempts ?? options?.maxAttempts, "retry.maxAttempts", 1);
    const backoff = retry.backoffMs ?? options?.backoffMs ?? 0;
    if (typeof backoff !== "function" && (!Number.isFinite(backoff) || backoff < 0)) {
        throw new TypeError("retry backoff must be a non-negative millisecond value or function.");
    }
    return { attempts, backoff };
}

function retryDelayMs(backoff, attempt, error) {
    const value = typeof backoff === "function" ? backoff(attempt, error) : backoff;
    if (!Number.isFinite(value) || value < 0) {
        throw new TypeError("retry backoff must return a non-negative millisecond value.");
    }
    return Math.ceil(value);
}

class WorkQueueHandle {
    constructor(name, options = undefined) {
        options = validateWorkerOptions(options, "WorkQueue.create");
        this.name = validateName(name, "WorkQueue");
        this.maxQueued = positiveInteger(options?.maxQueued, "WorkQueue.maxQueued", DEFAULT_QUEUE_CAPACITY);
        this.concurrency = positiveInteger(options?.concurrency, "WorkQueue.concurrency", 1);
        this.overflow = options?.overflow ?? "reject";
        if (!["reject", "backpressure"].includes(this.overflow)) {
            throw new TypeError("WorkQueue overflow must be \"reject\" or \"backpressure\".");
        }
        this.maxBackpressureWaiters = positiveInteger(
            options?.maxBackpressureWaiters,
            "WorkQueue.maxBackpressureWaiters",
            this.maxQueued,
        );
        this.retry = normalizeRetry(options);
        this._handler = undefined;
        this._queue = [];
        this._waiters = [];
        this._active = 0;
        this._stopped = false;
        this._nextJobId = 1;
        this._stopPromise = undefined;
        this._resolveStop = undefined;
        this._stats = {
            enqueued: 0,
            completed: 0,
            failed: 0,
            cancelled: 0,
            timedOut: 0,
            retryExhausted: 0,
            overflow: 0,
        };
        Object.seal(this);
    }

    get state() {
        return Object.freeze({
            name: this.name,
            stopped: this._stopped,
            queued: this._queue.length,
            active: this._active,
            maxQueued: this.maxQueued,
            concurrency: this.concurrency,
            overflow: this.overflow,
            stats: Object.freeze({ ...this._stats }),
        });
    }

    process(handler) {
        if (typeof handler !== "function") {
            throw new TypeError("WorkQueue.process requires a job handler.");
        }
        if (this._handler !== undefined) {
            throw new TypeError("WorkQueue.process may only be called once.");
        }
        this._handler = handler;
        this._pump();
        return this;
    }

    enqueue(data, options = undefined) {
        if (this._stopped) {
            return Promise.reject(workerError("SLOPPY_E_WORK_QUEUE_STOPPED", "work queue is stopped"));
        }
        if (this._handler === undefined) {
            return Promise.reject(workerError("SLOPPY_E_WORK_QUEUE_STOPPED", "work queue has no processor"));
        }
        let payload;
        try {
            payload = serializePayload(data);
        } catch (error) {
            return Promise.reject(error);
        }
        return this._enqueuePayload(payload, options);
    }

    _enqueuePayload(payload, options = undefined) {
        if (this._stopped) {
            return Promise.reject(workerError("SLOPPY_E_WORK_QUEUE_STOPPED", "work queue is stopped"));
        }
        let normalizedOptions;
        try {
            normalizedOptions = normalizeOperationOptions(options);
        } catch (error) {
            return Promise.reject(error);
        }
        try {
            if (signalCancelled(normalizedOptions.signal)) {
                return Promise.reject(rejectForCancellation(signalReason(normalizedOptions.signal)));
            }
            if (deadlineRemainingMs(normalizedOptions.deadline) <= 0) {
                return Promise.reject(rejectForCancellation("deadline"));
            }
        } catch (error) {
            return Promise.reject(error);
        }
        const submit = () => new Promise((resolve, reject) => {
            if (this._stopped) {
                reject(workerError("SLOPPY_E_WORK_QUEUE_STOPPED", "work queue is stopped"));
                return;
            }
            const job = {
                id: this._nextJobId++,
                data: payload,
                attempt: 0,
                options: normalizedOptions,
                resolve,
                reject,
            };
            this._queue.push(job);
            this._stats.enqueued += 1;
            this._pump();
        });

        if (this._queue.length >= this.maxQueued && this._active >= this.concurrency) {
            this._stats.overflow += 1;
            if (this.overflow === "reject") {
                return Promise.reject(workerError("SLOPPY_E_WORK_QUEUE_FULL", "work queue is full"));
            }
            if (this._waiters.length >= this.maxBackpressureWaiters) {
                return Promise.reject(workerError("SLOPPY_E_WORK_QUEUE_FULL", "work queue backpressure waiters are full"));
            }
            return new Promise((resolve, reject) => {
                const waiter = this._createBackpressureWaiter(normalizedOptions, resolve, reject, submit);
                this._waiters.push(waiter);
            });
        }
        return submit();
    }

    _createBackpressureWaiter(options, resolve, reject, submit) {
        let finished = false;
        let timeoutId = undefined;
        let cleanupSignal = () => {};
        const remove = (waiter) => {
            const index = this._waiters.indexOf(waiter);
            if (index >= 0) {
                this._waiters.splice(index, 1);
            }
        };
        const finish = (waiter, callback) => {
            if (finished) {
                return;
            }
            finished = true;
            cleanupSignal();
            if (timeoutId !== undefined) {
                clearTimeout(timeoutId);
            }
            callback();
        };
        const waiter = {
            cleanup: () => {
                cleanupSignal();
                if (timeoutId !== undefined) {
                    clearTimeout(timeoutId);
                }
            },
            reject: (error) => finish(waiter, () => reject(error)),
            resume: () => finish(waiter, () => {
                try {
                    if (signalCancelled(options.signal)) {
                        this._stats.cancelled += 1;
                        reject(rejectForCancellation(signalReason(options.signal)));
                        return;
                    }
                    if (deadlineRemainingMs(options.deadline) <= 0) {
                        this._stats.timedOut += 1;
                        reject(rejectForCancellation("deadline"));
                        return;
                    }
                } catch (error) {
                    reject(error);
                    return;
                }
                submit().then(resolve, reject);
            }),
        };

        cleanupSignal = subscribeSignal(options.signal, (reason) => {
            remove(waiter);
            this._stats.cancelled += 1;
            waiter.reject(rejectForCancellation(reason));
        });
        const remaining = deadlineRemainingMs(options.deadline);
        if (remaining !== Infinity) {
            timeoutId = setTimeout(() => {
                remove(waiter);
                this._stats.timedOut += 1;
                waiter.reject(rejectForCancellation("deadline"));
            }, Math.ceil(remaining));
        }
        return waiter;
    }

    _rejectWaiters(error, options = undefined) {
        const countCancelled = options?.countCancelled !== false;
        while (this._waiters.length > 0) {
            const waiter = this._waiters.shift();
            waiter.cleanup?.();
            waiter.reject(error);
            if (countCancelled) {
                this._stats.cancelled += 1;
            }
        }
    }

    async drain() {
        if (this._queue.length === 0 && this._active === 0) {
            return;
        }
        if (this._stopPromise === undefined) {
            this._stopPromise = new Promise((resolve) => {
                this._resolveStop = resolve;
            });
        }
        await this._stopPromise;
    }

    async stop(options = undefined) {
        const drain = options?.drain !== false;
        this._stopped = true;
        if (!drain) {
            while (this._queue.length > 0) {
                const job = this._queue.shift();
                job.reject(workerError("SLOPPY_E_WORKER_SHUTDOWN_CANCELLED", "queued job was cancelled by shutdown"));
                this._stats.cancelled += 1;
            }
        }
        if (drain) {
            this._rejectWaiters(workerError("SLOPPY_E_WORK_QUEUE_STOPPED", "work queue is stopped"), {
                countCancelled: false,
            });
        } else {
            this._rejectWaiters(workerError("SLOPPY_E_WORKER_SHUTDOWN_CANCELLED", "queued job was cancelled by shutdown"));
        }
        await this.drain();
    }

    _pump() {
        while (!this._stopped && this._waiters.length > 0 && this._queue.length < this.maxQueued) {
            this._waiters.shift().resume();
        }
        while (this._handler !== undefined && this._active < this.concurrency && this._queue.length > 0) {
            const job = this._queue.shift();
            this._active += 1;
            this._runJob(job);
        }
        if (this._queue.length === 0 && this._active === 0 && this._resolveStop !== undefined) {
            this._resolveStop();
            this._resolveStop = undefined;
            this._stopPromise = undefined;
        }
    }

    async _runJob(job) {
        let settled = false;
        const attemptLimit = this.retry.attempts;
        try {
            for (;;) {
                job.attempt += 1;
                const owned = makeContext(job.options, { attempt: job.attempt });
                try {
                    if (signalCancelled(owned.ctx.signal)) {
                        throw rejectForCancellation(signalReason(owned.ctx.signal));
                    }
                    const result = await raceSignalCancellation(
                        this._handler(Object.freeze({ id: job.id, data: job.data, attempt: job.attempt }), owned.ctx),
                        owned.ctx.signal,
                    );
                    if (!settled) {
                        settled = true;
                        this._stats.completed += 1;
                        job.resolve(result);
                    }
                    break;
                } catch (error) {
                    if (error instanceof SloppyWorkerError &&
                        (error.code === "SLOPPY_E_WORK_JOB_CANCELLED" || error.code === "SLOPPY_E_WORK_JOB_TIMEOUT"))
                    {
                        settled = true;
                        if (error.code === "SLOPPY_E_WORK_JOB_TIMEOUT") {
                            this._stats.timedOut += 1;
                        } else {
                            this._stats.cancelled += 1;
                        }
                        job.reject(error);
                        break;
                    }
                    if (job.attempt >= attemptLimit) {
                        settled = true;
                        this._stats.failed += 1;
                        this._stats.retryExhausted += attemptLimit > 1 ? 1 : 0;
                        job.reject(attemptLimit === 1 && error instanceof SloppyWorkerError
                            ? error
                            : workerError("SLOPPY_E_WORK_RETRY_EXHAUSTED", "work queue retry attempts were exhausted", error));
                        break;
                    }
                    const ms = retryDelayMs(this.retry.backoff, job.attempt, error);
                    if (ms > 0) {
                        await delay(ms);
                    } else {
                        await yieldTurn();
                    }
                } finally {
                    owned.cleanup();
                }
            }
        } finally {
            this._active -= 1;
            this._pump();
        }
    }

    __sloppyPlanMetadata() {
        return Object.freeze({
            kind: "workQueue",
            name: this.name,
            maxQueued: this.maxQueued,
            concurrency: this.concurrency,
            overflow: this.overflow,
        });
    }
}

class BackgroundServiceHandle {
    constructor(name, handler, options = undefined) {
        options = validateWorkerOptions(options, "BackgroundService.create");
        this.name = validateName(name, "BackgroundService");
        if (typeof handler !== "function") {
            throw new TypeError("BackgroundService.create requires a function.");
        }
        this._handler = handler;
        this._options = options ?? {};
        this._controller = undefined;
        this._promise = undefined;
        this._state = "created";
        this._failure = undefined;
        this.__sloppyWorkerResource = "backgroundService";
        Object.seal(this);
    }

    get state() {
        return this._state;
    }

    get failure() {
        return this._failure;
    }

    start() {
        if (this._state === "running") {
            return this;
        }
        if (this._state === "stopped") {
            throw workerError("SLOPPY_E_WORKER_STALE_HANDLE", "background service has been stopped");
        }
        this._controller = new WorkerCancellationController();
        this._state = "running";
        const ctx = Object.freeze({ name: this.name, signal: this._controller.signal });
        this._promise = Promise.resolve()
            .then(() => this._handler(ctx))
            .then(
                () => {
                    if (this._state === "running") {
                        this._state = "completed";
                    }
                },
                (error) => {
                    if (this._state === "running") {
                        this._state = "failed";
                        this._failure = workerError("SLOPPY_E_BACKGROUND_SERVICE_FAILED", "background service failed", error);
                    }
                },
            );
        return this;
    }

    async stop(reason = "app shutdown") {
        if (this._state === "created") {
            this._state = "stopped";
            return;
        }
        if (this._state === "stopped") {
            return;
        }
        this._controller?.cancel(reason);
        await this._promise;
        this._state = "stopped";
    }

    __sloppyStartForApp() {
        return this.start();
    }

    __sloppyStopForApp(reason) {
        return this.stop(reason);
    }

    __sloppyPlanMetadata() {
        return Object.freeze({
            kind: "backgroundService",
            name: this.name,
            start: this._options.start ?? "app",
            shutdown: this._options.shutdown ?? "cancel-and-drain",
        });
    }
}

class WorkerPoolHandle {
    constructor(name, options = undefined) {
        options = validateWorkerOptions(options, "WorkerPool.create");
        this.name = validateName(name, "WorkerPool");
        this.workers = positiveInteger(options?.workers, "WorkerPool.workers", 1);
        this.maxQueued = positiveInteger(options?.maxQueued, "WorkerPool.maxQueued", DEFAULT_WORKER_POOL_CAPACITY);
        this._queue = new WorkQueueHandle(`${name}:pool`, {
            maxQueued: this.maxQueued,
            concurrency: this.workers,
            overflow: options?.overflow ?? "reject",
        });
        this._queue.process(async (job, ctx) => {
            const bridge = globalThis.__sloppy?.workers;
            if (bridge === undefined || typeof bridge.runPool !== "function") {
                throw workerError(
                    "SLOPPY_E_WORKER_BRIDGE_UNAVAILABLE",
                    "worker pool bridge is unavailable",
                );
            }
            try {
                return await bridge.runPool(this.name, job.data.fn, job.data.input, ctx);
            } catch (error) {
                throw toWorkerError(error, "SLOPPY_E_WORKER_CRASHED", "worker pool operation failed");
            }
        });
        this.__sloppyWorkerResource = "workerPool";
        Object.seal(this);
    }

    get state() {
        return Object.freeze({
            name: this.name,
            workers: this.workers,
            maxQueued: this.maxQueued,
            queue: this._queue.state,
        });
    }

    run(fn, options = undefined) {
        if (typeof fn !== "function") {
            return Promise.reject(new TypeError("WorkerPool.run requires a function."));
        }
        let input;
        try {
            input = serializePayload(options?.input);
        } catch (error) {
            return Promise.reject(error);
        }
        return this._queue._enqueuePayload({ fn, input }, options);
    }

    drain() {
        return this._queue.drain();
    }

    stop(options = undefined) {
        return this._queue.stop(options);
    }

    __sloppyPlanMetadata() {
        return Object.freeze({
            kind: "workerPool",
            name: this.name,
            workers: this.workers,
            maxQueued: this.maxQueued,
        });
    }
}

class NativeJsWorkerHandle {
    constructor(modulePath, nativeHandle, options) {
        this.modulePath = modulePath;
        this._native = nativeHandle;
        this._stopped = false;
        this._memoryLimitMb = options.memoryLimitMb;
        this.__sloppyWorkerResource = "jsWorker";
        Object.seal(this);
    }

    async invoke(exportName, payload = undefined, options = undefined) {
        if (this._stopped) {
            throw workerError("SLOPPY_E_WORKER_STALE_HANDLE", "worker handle has been stopped");
        }
        if (typeof exportName !== "string" || exportName.length === 0) {
            throw new TypeError("Worker.invoke export name must be a non-empty string.");
        }
        const input = serializePayload(payload);
        const owned = makeContext(normalizeOperationOptions(options));
        try {
            return await raceSignalCancellation(
                this._native.invoke(exportName, input, options),
                owned.ctx.signal,
                "SLOPPY_E_WORK_JOB_TIMEOUT",
            );
        } catch (error) {
            throw toWorkerError(error, "SLOPPY_E_WORKER_CRASHED", "worker crashed");
        } finally {
            owned.cleanup();
        }
    }

    async post(message, options = undefined) {
        if (this._stopped) {
            throw workerError("SLOPPY_E_WORKER_STALE_HANDLE", "worker handle has been stopped");
        }
        const input = serializePayload(message);
        const owned = makeContext(normalizeOperationOptions(options));
        try {
            return await raceSignalCancellation(
                this._native.post(input, options),
                owned.ctx.signal,
                "SLOPPY_E_WORK_JOB_TIMEOUT",
            );
        } catch (error) {
            throw toWorkerError(error, "SLOPPY_E_WORKER_CRASHED", "worker crashed");
        } finally {
            owned.cleanup();
        }
    }

    onMessage(callback) {
        if (this._stopped) {
            throw workerError("SLOPPY_E_WORKER_STALE_HANDLE", "worker handle has been stopped");
        }
        if (typeof callback !== "function") {
            throw new TypeError("Worker.onMessage requires a callback.");
        }
        const subscribe = this._native.onMessage ?? this._native.addMessageListener;
        if (typeof subscribe !== "function") {
            throw workerError(
                "SLOPPY_E_WORKER_BRIDGE_UNAVAILABLE",
                "worker receive bridge is unavailable",
            );
        }
        return subscribe.call(this._native, callback);
    }

    async stop() {
        if (this._stopped) {
            return;
        }
        this._stopped = true;
        try {
            await this._native.stop();
        } catch (error) {
            throw toWorkerError(error, "SLOPPY_E_WORKER_STALE_HANDLE", "worker handle has been stopped");
        }
    }

    __sloppyPlanMetadata() {
        return Object.freeze({
            kind: "jsWorker",
            path: this.modulePath,
            memoryLimitMb: this._memoryLimitMb,
        });
    }
}

const BackgroundService = Object.freeze({
    create(name, handler, options = undefined) {
        return new BackgroundServiceHandle(name, handler, options);
    },
});

const WorkQueue = Object.freeze({
    create(name, options = undefined) {
        return new WorkQueueHandle(name, options);
    },
});

const WorkerPool = Object.freeze({
    create(name, options = undefined) {
        return new WorkerPoolHandle(name, options);
    },
});

const Worker = Object.freeze({
    async start(modulePath, options = undefined) {
        options = validateWorkerOptions(options, "Worker.start");
        if (typeof modulePath !== "string" || modulePath.length === 0 || modulePath.includes("\0")) {
            throw new TypeError("Worker.start module path must be a non-empty string without NUL.");
        }
        const memoryLimitMb = positiveInteger(options?.memoryLimitMb, "Worker.memoryLimitMb", 128);
        const bridge = globalThis.__sloppy?.workers;
        if (bridge !== undefined && typeof bridge.startWorker === "function") {
            try {
                const nativeHandle = await bridge.startWorker(modulePath, { memoryLimitMb });
                return new NativeJsWorkerHandle(modulePath, nativeHandle, { memoryLimitMb });
            } catch (error) {
                throw toWorkerError(error, "SLOPPY_E_WORKER_ISOLATE_STARTUP_FAILED", "worker isolate startup failed");
            }
        }
        throw workerError(
            "SLOPPY_E_WORKER_BRIDGE_UNAVAILABLE",
            "worker bridge is unavailable",
        );
    },
});

export {
    BackgroundService,
    SloppyWorkerError,
    WorkQueue,
    Worker,
    WorkerCancellationController,
    WorkerCancellationSignal,
    WorkerPool,
};
