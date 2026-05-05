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

function unavailable(operation) {
    throw new Error(`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE: runtime feature stdlib.time is inactive or unavailable

Feature:
  stdlib.time

Operation:
  ${operation}

Reason:
  CORE-TIME-01.C/D/G requires the V8 native time bridge for runtime scheduling.`);
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
            Time.delay(timeoutMs)
                .then(() => this.cancel(timeoutError()))
                .catch(() => {});
        }
        if (options?.deadline !== undefined) {
            const remaining = deadlineDelayMs(options.deadline);
            if (remaining <= 0) {
                this.cancel(timeoutError(options.deadline));
            } else if (remaining !== Infinity) {
                Time.delay(remaining)
                    .then(() => this.cancel(timeoutError(options.deadline)))
                    .catch(() => {});
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

function delayWithDeadline(ms, options = undefined) {
    const delayMs = validateDelayMs(ms, "Time.delay");
    if (isCancellationSignal(options?.signal) && options.signal.aborted) {
        return Promise.reject(cancelledError(options.signal.reason));
    }
    const remaining = deadlineDelayMs(options?.deadline);
    if (remaining <= 0) {
        return Promise.reject(timeoutError(options?.deadline));
    }

    const actualDelay = Math.min(delayMs, remaining);
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

const Time = Object.freeze({
    delay(ms, options = undefined) {
        return delayWithDeadline(ms, options);
    },

    timeout(operationOrPromise, options = undefined) {
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
            const timeoutPromise = Time.delay(timeoutMs).then(() => {
                const error = timeoutError(options?.deadline);
                controller.cancel(error);
                throw error;
            });
            const operationPromise = Promise.resolve().then(() => operationOrPromise(controller.signal));
            const race = [operationPromise, timeoutPromise];
            if (isCancellationSignal(options?.signal)) {
                race.push(raceCancellation(new Promise(() => {}), options.signal));
            }
            return Promise.race(race).finally(() => controller.dispose());
        }

        return Promise.race([
            raceCancellation(Promise.resolve(operationOrPromise), options?.signal),
            Time.delay(timeoutMs, { signal: options?.signal }).then(() => {
                throw timeoutError(options?.deadline);
            }),
        ]);
    },

    interval() {
        return unavailable("Time.interval");
    },

    every() {
        return unavailable("Time.every");
    },

    yield(options = undefined) {
        return delayWithDeadline(0, options);
    },

    systemClock() {
        return Object.freeze({
            kind: "system",
            now: () => new Date(),
            monotonicNowMs,
            delay: Time.delay,
        });
    },

    fakeClock() {
        return unavailable("Time.fakeClock");
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
