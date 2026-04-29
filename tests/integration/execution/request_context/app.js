const __sloppyRuntime = globalThis.__sloppy_runtime;
if (__sloppyRuntime === undefined) {
  throw new Error("Sloppy bootstrap runtime was not loaded");
}
const { Results } = __sloppyRuntime;

globalThis.__sloppy_register_handler(1, ({ route, query, request }) =>
  Results.json({
    id: route.id,
    q: query.q,
    path: request.path,
    rawTarget: request.rawTarget,
    method: request.method,
  }));
