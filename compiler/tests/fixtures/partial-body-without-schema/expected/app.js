const __sloppyRuntime = globalThis.__sloppy_runtime;
if (__sloppyRuntime === undefined) {
  throw new Error("Sloppy bootstrap runtime was not loaded");
}
const { Results } = __sloppyRuntime;

globalThis.__sloppy_handler_1 = (ctx) => Results.json({
  body: ctx.body.json()
});
globalThis.__sloppy_register_handler(1, globalThis.__sloppy_handler_1);
