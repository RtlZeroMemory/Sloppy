class SloppyTimeError extends Error {
    constructor(name, message, options) {
        super(message, options);
        this.name = name;
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

function unavailable(operation) {
    throw new Error(`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE: runtime feature stdlib.time is inactive or unavailable

Feature:
  stdlib.time

Operation:
  ${operation}

Reason:
  CORE-TIME-01.A/B defines the sloppy/time contract. Native timer scheduling lands in the CORE-TIME-01.C/D/G implementation slice.`);
}

const Deadline = Object.freeze({
    after() {
        return unavailable("Deadline.after");
    },

    at() {
        return unavailable("Deadline.at");
    },

    never() {
        return unavailable("Deadline.never");
    },
});

class CancellationController {
    constructor() {
        unavailable("CancellationController");
    }
}

const Time = Object.freeze({
    delay() {
        return unavailable("Time.delay");
    },

    timeout() {
        return unavailable("Time.timeout");
    },

    interval() {
        return unavailable("Time.interval");
    },

    every() {
        return unavailable("Time.every");
    },

    yield() {
        return unavailable("Time.yield");
    },

    systemClock() {
        return unavailable("Time.systemClock");
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
