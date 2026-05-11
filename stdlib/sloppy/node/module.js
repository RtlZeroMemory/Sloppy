const builtinModules = Object.freeze([
    "assert",
    "assert/strict",
    "buffer",
    "console",
    "constants",
    "crypto",
    "diagnostics_channel",
    "events",
    "fs",
    "fs/promises",
    "http",
    "https",
    "module",
    "os",
    "path",
    "perf_hooks",
    "process",
    "querystring",
    "stream",
    "stream/promises",
    "string_decoder",
    "timers",
    "tty",
    "url",
    "util",
    "zlib",
]);

function createRequire(source) {
    if (typeof globalThis.__sloppy_program_create_require === "function") {
        return globalThis.__sloppy_program_create_require(source);
    }
    throw new Error("SLOPPY_E_MODULE_REQUIRE_UNAVAILABLE: createRequire is only available in bundled Sloppy programs.");
}

const Module = Object.freeze({ builtinModules, createRequire });

export { builtinModules, createRequire, Module };
export default Module;
