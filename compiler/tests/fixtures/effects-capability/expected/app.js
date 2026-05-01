const __sloppyRuntime = globalThis.__sloppy_runtime;
if (__sloppyRuntime === undefined) {
  throw new Error("Sloppy bootstrap runtime was not loaded");
}
const { Results, data } = __sloppyRuntime;
function __sloppy_open_data_provider(kind, token) {
  if (kind === "sqlite") { return data.sqlite(token); }
  throw new Error(`sloppy: ${kind} provider bridge unavailable`);
}

globalThis.__sloppy_handler_1 = function(ctx) { const __sloppy_opened_providers = []; let db; try { db = __sloppy_open_data_provider("sqlite", "data.main"); __sloppy_opened_providers.push(db); function listUsers() {
  return db.query("select id, name from users", []);
} return (() => Results.json(listUsers()))(ctx); } finally { while (__sloppy_opened_providers.length > 0) { const __sloppy_provider = __sloppy_opened_providers.pop(); try { __sloppy_provider.close(); } catch (_) {} } } };
globalThis.__sloppy_register_handler(1, globalThis.__sloppy_handler_1);
