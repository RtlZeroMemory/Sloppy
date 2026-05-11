const fallbackTimeOrigin = Date.now() - (
    typeof process !== "undefined" && typeof process.uptime === "function"
        ? process.uptime() * 1000
        : 0
);
const performance = globalThis.performance ?? Object.freeze({
    now: () => Date.now() - fallbackTimeOrigin,
    timeOrigin: fallbackTimeOrigin,
});

export { performance };
export default { performance };
