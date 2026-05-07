const __sloppyRuntime = globalThis.__sloppy_runtime;
if (__sloppyRuntime === undefined) {
  throw new Error("Sloppy bootstrap runtime was not loaded");
}
const { Results } = __sloppyRuntime;

globalThis.__sloppy_handler_1 = () => Results.problem({ status: 501, title: "Framework v2 runtime dispatch is deferred" });
globalThis.__sloppy_register_handler(1, globalThis.__sloppy_handler_1);
globalThis.__sloppy_handler_2 = () => Results.problem({ status: 501, title: "Framework v2 runtime dispatch is deferred" });
globalThis.__sloppy_register_handler(2, globalThis.__sloppy_handler_2);
