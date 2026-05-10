import { Environment, Process } from "../os.js";

const env = new Proxy(Object.create(null), {
    get(_target, key) {
        return typeof key === "string" ? Environment.get(key) : undefined;
    },
    ownKeys() {
        return Object.keys(Environment.list?.() ?? {});
    },
});

const argv = Object.freeze([]);
const cwd = () => globalThis.__sloppy_program_context?.cwd ?? ".";
const platform = globalThis.__sloppy_runtime?.System?.platform ?? "sloppy";
const arch = globalThis.__sloppy_runtime?.System?.arch ?? "unknown";
const version = "sloppy-0.1.0";
const versions = Object.freeze({ sloppy: "0.1.0" });
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
