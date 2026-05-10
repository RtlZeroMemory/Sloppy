import { Time } from "../time.js";

function setTimeout(handler, delay = 0, ...args) {
    const timer = { cancelled: false };
    Time.delay(delay).then(() => {
        if (!timer.cancelled) {
            handler(...args);
        }
    });
    return timer;
}

function clearTimeout(timer) {
    if (timer !== null && typeof timer === "object") {
        timer.cancelled = true;
    }
}

function setImmediate(handler, ...args) {
    return setTimeout(handler, 0, ...args);
}

const clearImmediate = clearTimeout;

export { clearImmediate, clearTimeout, setImmediate, setTimeout };
export default { clearImmediate, clearTimeout, setImmediate, setTimeout };
