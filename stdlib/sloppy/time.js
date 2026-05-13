class SloppyTimeError extends Error {
    constructor(name, message, options = undefined) {
        super(message, options);
        this.name = name;
        if (options && Object.prototype.hasOwnProperty.call(options, "reason")) {
            this.reason = options.reason;
        }
    }
}

class TimeoutError extends SloppyTimeError {
    constructor(message = "Sloppy time operation exceeded its deadline.", options) {
        super("TimeoutError", message, options);
    }
}

class CancelledError extends SloppyTimeError {
    constructor(message = "Sloppy time operation was cancelled.", options) {
        super("CancelledError", message, options);
    }
}

class InvalidDeadlineError extends SloppyTimeError {
    constructor(message = "Sloppy deadline is invalid.", options) {
        super("InvalidDeadlineError", message, options);
    }
}

class TimerDisposedError extends SloppyTimeError {
    constructor(message = "Sloppy timer resource was disposed.", options) {
        super("TimerDisposedError", message, options);
    }
}

const MAX_DELAY_MS = 0xffffffff;
const NATIVE_TIMER_DISPOSED_MESSAGE = "Sloppy timer was disposed before completion";
const INTERVAL_UNITS_MS = Object.freeze({
    ms: 1,
    s: 1000,
    m: 60 * 1000,
    h: 60 * 60 * 1000,
});

function unavailable(operation) {
    throw new Error(`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE: runtime feature stdlib.time is inactive or unavailable

Feature:
  stdlib.time

Operation:
  ${operation}

Reason:
  Runtime scheduling requires the native time bridge to be registered in the active V8 lane.`);
}

function nativeTime(operation) {
    const bridge = globalThis.__sloppy?.time;
    if (bridge === undefined || bridge === null) {
        unavailable(operation);
    }
    return bridge;
}

function validateDelayMs(ms, operation) {
    if (typeof ms !== "number" || !Number.isFinite(ms) || ms < 0 || ms > MAX_DELAY_MS) {
        throw new InvalidDeadlineError(
            `${operation} requires a finite non-negative millisecond delay no greater than ${MAX_DELAY_MS}.`,
        );
    }
    return Math.ceil(ms);
}

function monotonicNowMs() {
    const bridge = globalThis.__sloppy?.time;
    return bridge && typeof bridge.monotonicMs === "function" ? bridge.monotonicMs() : Date.now();
}

function timeoutError(reason = undefined) {
    return reason instanceof TimeoutError
        ? reason
        : new TimeoutError("Sloppy time operation exceeded its deadline.", { reason });
}

function cancelledError(reason = undefined) {
    return reason instanceof CancelledError
        ? reason
        : new CancelledError("Sloppy time operation was cancelled.", { reason });
}

function normalizeNativeTimerError(error) {
    if (error instanceof TimerDisposedError) {
        throw error;
    }
    if (error instanceof Error && error.message === NATIVE_TIMER_DISPOSED_MESSAGE) {
        throw new TimerDisposedError(error.message, { reason: error });
    }
    throw error;
}

function timerDisposedError(reason = undefined) {
    return reason instanceof TimerDisposedError
        ? reason
        : new TimerDisposedError("Sloppy timer resource was disposed.", { reason });
}

class CancellationSignal {
    constructor() {
        this.aborted = false;
        this.reason = undefined;
        this._listeners = new Set();
        Object.seal(this);
    }

    throwIfCancelled() {
        if (this.aborted) {
            throw cancelledError(this.reason);
        }
    }

    addEventListener(type, listener) {
        if (type !== "abort" || typeof listener !== "function") {
            return;
        }
        this._listeners.add(listener);
    }

    removeEventListener(type, listener) {
        if (type === "abort") {
            this._listeners.delete(listener);
        }
    }

    _subscribe(listener) {
        if (typeof listener !== "function") {
            return () => {};
        }
        if (this.aborted) {
            listener(this.reason);
            return () => {};
        }
        this._listeners.add(listener);
        return () => {
            this._listeners.delete(listener);
        };
    }

    _cancel(reason) {
        if (this.aborted) {
            return false;
        }
        this.aborted = true;
        this.reason = reason;
        const listeners = Array.from(this._listeners);
        this._listeners.clear();
        const errors = [];
        for (const listener of listeners) {
            try {
                listener(reason);
            } catch (error) {
                errors.push(error);
            }
        }
        if (errors.length > 0) {
            throw new AggregateError(errors, "one or more abort listeners threw");
        }
        return true;
    }
}

function isCancellationSignal(value) {
    return (
        value instanceof CancellationSignal ||
        (value !== null &&
            typeof value === "object" &&
            typeof value.aborted === "boolean" &&
            ("reason" in value || typeof value.addEventListener === "function"))
    );
}

function subscribeCancellation(signal, listener) {
    if (!isCancellationSignal(signal)) {
        return () => {};
    }
    if (signal.aborted) {
        listener(signal.reason);
        return () => {};
    }
    if (typeof signal._subscribe === "function") {
        return signal._subscribe(listener);
    }
    if (typeof signal.addEventListener === "function") {
        const wrapped = () => listener(signal.reason);
        signal.addEventListener("abort", wrapped);
        return () => signal.removeEventListener?.("abort", wrapped);
    }
    return () => {};
}

class DeadlineValue {
    constructor(expiresAtMonotonicMs, kind) {
        this.expiresAtMonotonicMs = expiresAtMonotonicMs;
        this.kind = kind;
        Object.freeze(this);
    }

    remainingMs() {
        if (this.expiresAtMonotonicMs === Infinity) {
            return Infinity;
        }
        return Math.max(0, this.expiresAtMonotonicMs - monotonicNowMs());
    }

    get expired() {
        return this.remainingMs() <= 0;
    }
}

const Deadline = Object.freeze({
    after(ms) {
        return new DeadlineValue(monotonicNowMs() + validateDelayMs(ms, "Deadline.after"), "after");
    },

    at(dateOrUnixMs) {
        const unixMs =
            dateOrUnixMs instanceof Date ? dateOrUnixMs.getTime() : Number(dateOrUnixMs);
        if (!Number.isFinite(unixMs)) {
            throw new InvalidDeadlineError("Deadline.at requires a finite Date or Unix millisecond value.");
        }
        return Deadline.after(Math.max(0, unixMs - Date.now()));
    },

    never() {
        return new DeadlineValue(Infinity, "never");
    },
});

function deadlineDelayMs(deadline) {
    if (deadline === undefined || deadline === null) {
        return Infinity;
    }
    if (typeof deadline.remainingMs !== "function") {
        throw new InvalidDeadlineError("Time operation deadline must come from Deadline.after, Deadline.at, or Deadline.never.");
    }
    return deadline.remainingMs();
}

function registerCancellationTimer(controller, delayMs, reasonFactory) {
    const timer = setTimeout(() => {
        if (!controller._disposed) {
            try {
                controller.cancel(reasonFactory());
            } catch {
                // Timer-triggered cancellation should not crash the host when listeners throw.
            }
        }
    }, Math.ceil(delayMs));
    controller._cleanups.push(() => clearTimeout(timer));
}

class CancellationController {
    constructor(options = undefined) {
        this.signal = new CancellationSignal();
        this._disposed = false;
        this._cleanups = [];
        Object.seal(this);

        const linked = options?.signal ?? options?.signals;
        const signals = Array.isArray(linked) ? linked : linked === undefined ? [] : [linked];
        for (const signal of signals) {
            this._cleanups.push(subscribeCancellation(signal, (reason) => this.cancel(reason)));
        }

        if (options?.timeoutMs !== undefined) {
            const timeoutMs = validateDelayMs(options.timeoutMs, "CancellationController timeout");
            registerCancellationTimer(this, timeoutMs, () => timeoutError());
        }
        if (options?.deadline !== undefined) {
            const remaining = deadlineDelayMs(options.deadline);
            if (remaining <= 0) {
                this.cancel(timeoutError(options.deadline));
            } else if (remaining !== Infinity) {
                registerCancellationTimer(this, remaining, () => timeoutError(options.deadline));
            }
        }
    }

    cancel(reason = "cancelled") {
        if (this._disposed) {
            throw new TimerDisposedError("CancellationController was disposed.");
        }
        return this.signal._cancel(reason);
    }

    dispose() {
        if (this._disposed) {
            return;
        }
        this._disposed = true;
        for (const cleanup of this._cleanups.splice(0)) {
            cleanup();
        }
    }

    static linked(...signals) {
        return new CancellationController({ signals });
    }

    static timeout(timeoutMs, options = undefined) {
        return new CancellationController({ ...options, timeoutMs });
    }
}

function raceCancellation(promise, signal) {
    if (!isCancellationSignal(signal)) {
        return promise;
    }
    if (signal.aborted) {
        return Promise.reject(cancelledError(signal.reason));
    }
    return new Promise((resolve, reject) => {
        const cleanup = subscribeCancellation(signal, (reason) => {
            cleanup();
            reject(cancelledError(reason));
        });
        promise.then(
            (value) => {
                cleanup();
                resolve(value);
            },
            (error) => {
                cleanup();
                reject(error);
            },
        );
    });
}

function cancelAndDisposeController(controller, reason) {
    try {
        controller.cancel(reason);
    } catch (_) {
        // Cleanup paths must not be derailed by user abort listeners.
    }
    controller.dispose();
}

function parseIntervalMs(value, operation) {
    if (typeof value === "number") {
        const ms = validateDelayMs(value, operation);
        if (ms <= 0) {
            throw new InvalidDeadlineError(`${operation} requires an interval greater than 0.`);
        }
        return ms;
    }
    if (typeof value === "string") {
        const match = /^(\d+(?:\.\d+)?)(ms|s|m|h)$/.exec(value.trim());
        if (match === null) {
            throw new InvalidDeadlineError(
                `${operation} requires a millisecond number or interval string such as "500ms", "5s", "5m", or "1h".`,
            );
        }
        return parseIntervalMs(Number(match[1]) * INTERVAL_UNITS_MS[match[2]], operation);
    }
    throw new InvalidDeadlineError(`${operation} requires a millisecond number or interval string.`);
}

function validateMaxTicks(maxTicks, operation) {
    if (maxTicks === undefined) {
        return Infinity;
    }
    if (!Number.isInteger(maxTicks) || maxTicks < 0) {
        throw new InvalidDeadlineError(`${operation} maxTicks must be a non-negative integer.`);
    }
    return maxTicks;
}

function clockNow(clock) {
    return clock && typeof clock.now === "function" ? clock.now() : new Date();
}

function clockMonotonicNowMs(clock) {
    return clock && typeof clock.monotonicNowMs === "function"
        ? clock.monotonicNowMs()
        : monotonicNowMs();
}

function validateClockDeadlineOptions(options, operation) {
    if (options?.clock !== undefined && options.clock !== null && options?.deadline !== undefined) {
        throw new InvalidDeadlineError(
            `${operation} does not support deadline with an injected clock; use a duration option with that clock.`,
        );
    }
}

function delayWithDeadline(ms, options = undefined, operation = "Time.delay") {
    const delayMs = validateDelayMs(ms, operation);
    if (isCancellationSignal(options?.signal) && options.signal.aborted) {
        return Promise.reject(cancelledError(options.signal.reason));
    }
    validateClockDeadlineOptions(options, operation);
    const remaining = deadlineDelayMs(options?.deadline);
    if (remaining <= 0) {
        return Promise.reject(timeoutError(options?.deadline));
    }

    const actualDelay = Math.min(delayMs, remaining);
    if (options?.clock !== undefined && options.clock !== null) {
        if (typeof options.clock.delay !== "function") {
            return Promise.reject(
                new InvalidDeadlineError("Time clock providers must expose delay(ms, options)."),
            );
        }
        return options.clock.delay(actualDelay, { signal: options?.signal }).then(() => {
            if (actualDelay < delayMs) {
                throw timeoutError(options?.deadline);
            }
        });
    }

    const promise = nativeTime("Time.delay")
        .delay(actualDelay)
        .then(() => {
            if (actualDelay < delayMs) {
                throw timeoutError(options?.deadline);
            }
        })
        .catch(normalizeNativeTimerError);
    return raceCancellation(promise, options?.signal);
}

function systemDelay(ms, options = undefined) {
    return delayWithDeadline(ms, { ...options, clock: undefined });
}

class TimeInterval {
    constructor(ms, options = undefined) {
        this._intervalMs = parseIntervalMs(ms, "Time.interval");
        this._clock = options?.clock;
        this._signal = options?.signal;
        this._immediate = options?.immediate === true;
        this._maxTicks = validateMaxTicks(options?.maxTicks, "Time.interval");
        this._ticks = 0;
        this._stopped = false;
        this._nextInFlight = null;
        this._cleanup = subscribeCancellation(this._signal, () => {
            this._stopped = true;
        });
    }

    [Symbol.asyncIterator]() {
        return this;
    }

    next() {
        if (this._nextInFlight !== null) {
            return Promise.reject(new Error("Time.interval does not support overlapping next() calls."));
        }
        this._nextInFlight = this._nextImpl();
        return this._nextInFlight.finally(() => {
            this._nextInFlight = null;
        });
    }

    async _nextImpl() {
        if (this._stopped || this._ticks >= this._maxTicks) {
            this._cleanup();
            return { done: true, value: undefined };
        }
        if (!(this._immediate && this._ticks === 0)) {
            try {
                await Time.delay(this._intervalMs, {
                    clock: this._clock,
                    signal: this._signal,
                });
            } catch (error) {
                if (error instanceof CancelledError) {
                    this._stopped = true;
                    this._cleanup();
                    return { done: true, value: undefined };
                }
                throw error;
            }
        }
        if (this._stopped || this._ticks >= this._maxTicks) {
            this._cleanup();
            return { done: true, value: undefined };
        }

        this._ticks += 1;
        if (this._ticks >= this._maxTicks) {
            this._cleanup();
        }
        return {
            done: false,
            value: Object.freeze({
                index: this._ticks,
                at: clockNow(this._clock),
                scheduledAt: clockNow(this._clock),
            }),
        };
    }

    async return() {
        this.stop();
        return { done: true, value: undefined };
    }

    stop() {
        this._stopped = true;
        this._cleanup();
    }
}

class ScheduledJob {
    constructor(interval, handler, options = undefined) {
        if (typeof handler !== "function") {
            throw new TypeError("Time.every requires an async job handler.");
        }
        const missedRunPolicy = options?.missedRunPolicy ?? "skip";
        if (missedRunPolicy !== "skip") {
            throw new InvalidDeadlineError('Time.every only supports missedRunPolicy: "skip".');
        }
        this._intervalMs = parseIntervalMs(interval, "Time.every");
        this._handler = handler;
        this._clock = options?.clock;
        this._controller = new CancellationController({ signal: options?.signal });
        this._noOverlap = options?.noOverlap !== false;
        this._maxRuns = validateMaxTicks(options?.maxRuns, "Time.every");
        this._paused = false;
        this._stopped = false;
        this._running = false;
        this._runCount = 0;
        this._skippedRuns = 0;
        this._lastError = undefined;
        this._nextRunMs =
            clockMonotonicNowMs(this._clock) + (options?.immediate === true ? 0 : this._intervalMs);
        this._loopPromise = this._runLoop();
    }

    get running() {
        return this._running;
    }

    get stopped() {
        return this._stopped;
    }

    get skippedRuns() {
        return this._skippedRuns;
    }

    get lastError() {
        return this._lastError;
    }

    get nextRun() {
        if (this._stopped) {
            return null;
        }
        const remaining = Math.max(0, this._nextRunMs - clockMonotonicNowMs(this._clock));
        return new Date(clockNow(this._clock).getTime() + remaining);
    }

    pause() {
        if (this._stopped) {
            throw timerDisposedError();
        }
        this._paused = true;
    }

    resume() {
        if (this._stopped) {
            throw timerDisposedError();
        }
        this._paused = false;
    }

    stop(reason = "scheduled job stopped") {
        if (this._stopped) {
            return this._loopPromise;
        }
        this._finishStopped(reason, true);
        return this._loopPromise;
    }

    async _runLoop() {
        while (!this._stopped) {
            if (this._runCount >= this._maxRuns) {
                this._finishStopped("scheduled job completed");
                break;
            }
            const waitMs = Math.max(0, this._nextRunMs - clockMonotonicNowMs(this._clock));
            try {
                await Time.delay(waitMs, {
                    clock: this._clock,
                    signal: this._controller.signal,
                });
            } catch (error) {
                if (error instanceof CancelledError || error instanceof TimerDisposedError) {
                    this._finishStopped(error);
                    break;
                }
                this._lastError = error;
                this._finishStopped(error);
                break;
            }

            if (this._stopped) {
                break;
            }
            if (this._paused) {
                this._nextRunMs += this._intervalMs;
                continue;
            }
            if (this._running && this._noOverlap) {
                this._skippedRuns += 1;
                this._nextRunMs += this._intervalMs;
                continue;
            }

            this._startRun();
            this._nextRunMs += this._intervalMs;
        }
    }

    _startRun() {
        this._running = true;
        this._runCount += 1;
        const context = Object.freeze({
            signal: this._controller.signal,
            scheduledAt: this.nextRun,
            startedAt: clockNow(this._clock),
            run: this._runCount,
            skippedRuns: this._skippedRuns,
        });
        Promise.resolve()
            .then(() => this._handler(context))
            .catch((error) => {
                this._lastError = error;
            })
            .finally(() => {
                this._running = false;
            });
    }

    _finishStopped(reason, cancel = false) {
        if (this._stopped) {
            this._controller.dispose();
            return;
        }
        this._stopped = true;
        if (cancel) {
            cancelAndDisposeController(this._controller, reason);
            return;
        }
        this._controller.dispose();
    }
}

class FakeClock {
    constructor(options = undefined) {
        const wallMs =
            options?.now instanceof Date
                ? options.now.getTime()
                : options?.now === undefined
                  ? 0
                  : Number(options.now);
        if (!Number.isFinite(wallMs)) {
            throw new InvalidDeadlineError("Time.fakeClock now must be a finite Date or Unix millisecond value.");
        }
        this.kind = "fake";
        this._wallMs = wallMs;
        this._nowMs = 0;
        this._disposed = false;
        this._timers = [];
        this._timerSeq = 0;
    }

    now() {
        this._throwIfDisposed();
        return new Date(this._wallMs);
    }

    monotonicNowMs() {
        this._throwIfDisposed();
        return this._nowMs;
    }

    delay(ms, options = undefined) {
        const delayMs = validateDelayMs(ms, "FakeClock.delay");
        if (this._disposed) {
            return Promise.reject(timerDisposedError());
        }
        if (isCancellationSignal(options?.signal) && options.signal.aborted) {
            return Promise.reject(cancelledError(options.signal.reason));
        }
        const remaining = deadlineDelayMs(options?.deadline);
        if (remaining <= 0) {
            return Promise.reject(timeoutError(options?.deadline));
        }

        const actualDelay = Math.min(delayMs, remaining);
        if (actualDelay === 0) {
            return actualDelay < delayMs ? Promise.reject(timeoutError(options?.deadline)) : Promise.resolve();
        }

        return new Promise((resolve, reject) => {
            const timer = {
                dueMs: this._nowMs + actualDelay,
                seq: this._timerSeq += 1,
                reject,
                resolve: () => {
                    cleanup();
                    if (actualDelay < delayMs) {
                        reject(timeoutError(options?.deadline));
                        return;
                    }
                    resolve();
                },
            };
            const cleanup = subscribeCancellation(options?.signal, (reason) => {
                this._removeTimer(timer);
                reject(cancelledError(reason));
            });
            timer.cleanup = cleanup;
            this._timers.push(timer);
            this._flushDueTimers();
        });
    }

    set(dateOrUnixMs) {
        this._throwIfDisposed();
        const nextWallMs =
            dateOrUnixMs instanceof Date ? dateOrUnixMs.getTime() : Number(dateOrUnixMs);
        if (!Number.isFinite(nextWallMs)) {
            throw new InvalidDeadlineError("FakeClock.set requires a finite Date or Unix millisecond value.");
        }
        const delta = nextWallMs - this._wallMs;
        if (delta > 0) {
            this.advanceBy(delta);
            return;
        }
        this._wallMs = nextWallMs;
    }

    advanceBy(ms) {
        this._throwIfDisposed();
        const delta = validateDelayMs(ms, "FakeClock.advanceBy");
        this._nowMs += delta;
        this._wallMs += delta;
        this._flushDueTimers();
    }

    dispose() {
        if (this._disposed) {
            return;
        }
        this._disposed = true;
        const timers = this._timers.splice(0);
        for (const timer of timers) {
            timer.cleanup?.();
            timer.reject(timerDisposedError());
        }
    }

    _throwIfDisposed() {
        if (this._disposed) {
            throw timerDisposedError();
        }
    }

    _removeTimer(timer) {
        const index = this._timers.indexOf(timer);
        if (index >= 0) {
            this._timers.splice(index, 1);
        }
    }

    _flushDueTimers() {
        if (this._disposed) {
            return;
        }
        const due = [];
        this._timers = this._timers.filter((timer) => {
            if (timer.dueMs <= this._nowMs) {
                due.push(timer);
                return false;
            }
            return true;
        });
        due.sort((left, right) => left.dueMs - right.dueMs || left.seq - right.seq);
        for (const timer of due) {
            timer.resolve();
        }
    }
}

const Time = Object.freeze({
    delay(ms, options = undefined) {
        return delayWithDeadline(ms, options);
    },

    timeout(operationOrPromise, options = undefined) {
        validateClockDeadlineOptions(options, "Time.timeout");
        const afterMs =
            options?.afterMs !== undefined ? validateDelayMs(options.afterMs, "Time.timeout") : Infinity;
        const deadlineMs = deadlineDelayMs(options?.deadline);
        const timeoutMs = Math.min(afterMs, deadlineMs);
        if (timeoutMs === Infinity) {
            throw new InvalidDeadlineError("Time.timeout requires afterMs or deadline.");
        }
        if (isCancellationSignal(options?.signal) && options.signal.aborted) {
            return Promise.reject(cancelledError(options.signal.reason));
        }
        if (timeoutMs <= 0) {
            return Promise.reject(timeoutError(options?.deadline));
        }

        if (typeof operationOrPromise === "function") {
            const controller = new CancellationController({ signal: options?.signal });
            const timeoutController = new CancellationController({ signal: options?.signal });
            const timeoutPromise = Time.delay(timeoutMs, {
                clock: options?.clock,
                signal: timeoutController.signal,
            }).then(() => {
                const error = timeoutError(options?.deadline);
                try {
                    controller.cancel(error);
                } catch (_) {
                    // Timeout remains authoritative even when user abort listeners throw.
                }
                throw error;
            });
            const operationPromise = Promise.resolve().then(() => operationOrPromise(controller.signal));
            const race = [operationPromise, timeoutPromise];
            if (isCancellationSignal(options?.signal)) {
                race.push(raceCancellation(new Promise(() => {}), options.signal));
            }
            return Promise.race(race).finally(() => {
                cancelAndDisposeController(timeoutController, "timeout race settled");
                controller.dispose();
            });
        }

        const timeoutController = new CancellationController({ signal: options?.signal });
        return Promise.race([
            raceCancellation(Promise.resolve(operationOrPromise), options?.signal),
            Time.delay(timeoutMs, {
                clock: options?.clock,
                signal: timeoutController.signal,
            }).then(() => {
                throw timeoutError(options?.deadline);
            }),
        ]).finally(() => {
            cancelAndDisposeController(timeoutController, "timeout race settled");
        });
    },

    interval(ms, options = undefined) {
        return new TimeInterval(ms, options);
    },

    every(interval, handler, options = undefined) {
        return new ScheduledJob(interval, handler, options);
    },

    yield(options = undefined) {
        return delayWithDeadline(0, options);
    },

    systemClock() {
        return Object.freeze({
            kind: "system",
            now: () => new Date(),
            monotonicNowMs,
            delay: systemDelay,
        });
    },

    fakeClock(options = undefined) {
        return new FakeClock(options);
    },
});

export {
    CancelledError,
    CancellationController,
    Deadline,
    InvalidDeadlineError,
    Time,
    TimeoutError,
    TimerDisposedError,
};
