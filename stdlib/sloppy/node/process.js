import { Environment, Process } from "../os.js";
import { EventEmitter } from "./events.js";

const env = new Proxy(Object.create(null), {
    get(target, key) {
        if (typeof key !== "string") {
            return undefined;
        }
        return Object.prototype.hasOwnProperty.call(target, key) ? target[key] : Environment.get(key);
    },
    has(target, key) {
        return typeof key === "string" && (Object.prototype.hasOwnProperty.call(target, key) || Environment.has(key));
    },
    set(target, key, value) {
        if (typeof key !== "string") {
            return false;
        }
        target[key] = String(value);
        return true;
    },
    ownKeys(target) {
        return [...new Set([...Object.keys(target), ...(Environment.list?.() ?? [])])];
    },
    getOwnPropertyDescriptor(target, key) {
        const value = typeof key === "string"
            ? (Object.prototype.hasOwnProperty.call(target, key) ? target[key] : Environment.get(key))
            : undefined;
        return value === undefined ? undefined : {
            configurable: true,
            enumerable: true,
            value,
        };
    },
});

const monotonicMs = () => {
    const bridge = globalThis.__sloppy?.time;
    if (bridge && typeof bridge.monotonicMs === "function") {
        return bridge.monotonicMs();
    }
    if (globalThis.performance && typeof globalThis.performance.now === "function") {
        return globalThis.performance.now();
    }
    return Date.now();
};
const startedAt = monotonicMs();
const emitter = new EventEmitter();
const on = emitter.on.bind(emitter);
const off = emitter.off.bind(emitter);
const addListener = emitter.addListener.bind(emitter);
const removeListener = emitter.removeListener.bind(emitter);
const emit = emitter.emit.bind(emitter);
const once = emitter.once.bind(emitter);
const argv = Object.freeze(globalThis.__sloppy_program_args?.slice?.() ?? []);
const cwd = () => globalThis.__sloppy_program_context?.cwd ?? ".";
const platform = globalThis.__sloppy_runtime?.System?.platform ?? "sloppy";
const arch = globalThis.__sloppy_runtime?.System?.arch ?? "unknown";
const sloppyVersion = String(globalThis.__sloppy_runtime?.System?.version ?? "0.1.0");
const version = sloppyVersion.startsWith("sloppy-") ? sloppyVersion : `sloppy-${sloppyVersion}`;
const versions = Object.freeze({ sloppy: version.replace(/^sloppy-/, "") });
const release = Object.freeze({ name: "sloppy", sourceUrl: "https://github.com/RtlZeroMemory/Slop" });
let exitCode = undefined;
const nextTick = (fn, ...args) => Promise.resolve().then(() => fn(...args));
const exit = (code = 0) => {
    throw new Error(`SLOPPY_E_PROCESS_EXIT_UNSUPPORTED: process.exit(${code}) is not supported.`);
};
const hrtimeNanos = () => BigInt(Math.max(0, Math.floor(monotonicMs() * 1000000)));
const uptime = () => Math.max(0, (monotonicMs() - startedAt) / 1000);
const hrtime = (previous = undefined) => {
    const now = hrtimeNanos();
    const seconds = Number(now / 1000000000n);
    const nanos = Number(now % 1000000000n);
    if (previous === undefined) {
        return [seconds, nanos];
    }
    const previousNanos = BigInt(previous[0]) * 1000000000n + BigInt(previous[1]);
    const diff = now - previousNanos;
    return [Number(diff / 1000000000n), Number(diff % 1000000000n)];
};
hrtime.bigint = hrtimeNanos;
const memoryUsage = () => Object.freeze({
    arrayBuffers: 0,
    external: 0,
    heapTotal: 0,
    heapUsed: 0,
    rss: 0,
});
const stream = (name) => Object.freeze({
    isTTY: false,
    write() {
        throw new Error(`SLOPPY_E_PROCESS_STREAM_WRITE_UNSUPPORTED: process.${name}.write is not available in Sloppy's Node compatibility shim.`);
    },
});

const process = {
    arch,
    argv,
    browser: false,
    cwd,
    env,
    emit,
    exit,
    get exitCode() {
        return exitCode;
    },
    set exitCode(value) {
        exitCode = value;
    },
    hrtime,
    memoryUsage,
    nextTick,
    off,
    removeListener,
    on,
    addListener,
    once,
    platform,
    release,
    stderr: stream("stderr"),
    stdin: Object.freeze({ isTTY: false }),
    stdout: stream("stdout"),
    uptime,
    version,
    versions,
    Process,
};

export { addListener, arch, argv, cwd, emit, env, exit, hrtime, memoryUsage, nextTick, off, on, once, platform, Process, release, removeListener, uptime, version, versions };
export default process;
