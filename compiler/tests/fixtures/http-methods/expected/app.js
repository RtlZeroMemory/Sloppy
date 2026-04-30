const __sloppyRuntime = globalThis.__sloppy_runtime;
if (__sloppyRuntime === undefined) {
  throw new Error("Sloppy bootstrap runtime was not loaded");
}
const { Results } = __sloppyRuntime;

globalThis.__sloppy_handler_1 = () => Results.json([{ id: 1 }]);
globalThis.__sloppy_register_handler(1, globalThis.__sloppy_handler_1);
globalThis.__sloppy_handler_2 = () => Results.json({ id: 2 });
globalThis.__sloppy_register_handler(2, globalThis.__sloppy_handler_2);
globalThis.__sloppy_handler_3 = (ctx) => Results.json({ id: ctx.route.id, updated: true });
globalThis.__sloppy_register_handler(3, globalThis.__sloppy_handler_3);
globalThis.__sloppy_handler_4 = (ctx) => Results.json({ id: ctx.route.id, patched: true });
globalThis.__sloppy_register_handler(4, globalThis.__sloppy_handler_4);
globalThis.__sloppy_handler_5 = () => Results.noContent();
globalThis.__sloppy_register_handler(5, globalThis.__sloppy_handler_5);
