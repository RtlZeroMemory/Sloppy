import { Environment, Process } from "../os.js";

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

const argv = Object.freeze([]);
const cwd = () => globalThis.__sloppy_program_context?.cwd ?? ".";
const platform = globalThis.__sloppy_runtime?.System?.platform ?? "sloppy";
const arch = globalThis.__sloppy_runtime?.System?.arch ?? "unknown";
const sloppyVersion = String(globalThis.__sloppy_runtime?.System?.version ?? "0.1.0");
const version = sloppyVersion.startsWith("sloppy-") ? sloppyVersion : `sloppy-${sloppyVersion}`;
const versions = Object.freeze({ sloppy: version.replace(/^sloppy-/, "") });
let exitCode = undefined;
const nextTick = (fn, ...args) => Promise.resolve().then(() => fn(...args));
const exit = (code = 0) => {
    throw new Error(`SLOPPY_E_PROCESS_EXIT_UNSUPPORTED: process.exit(${code}) is not supported.`);
};

const process = {
    arch,
    argv,
    cwd,
    env,
    exit,
    get exitCode() {
        return exitCode;
    },
    set exitCode(value) {
        exitCode = value;
    },
    nextTick,
    platform,
    version,
    versions,
    Process,
};

export { arch, argv, cwd, env, exit, nextTick, platform, Process, version, versions };
export default process;
