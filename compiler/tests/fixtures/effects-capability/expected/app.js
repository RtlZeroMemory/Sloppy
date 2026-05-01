const __sloppyRuntime = globalThis.__sloppy_runtime;
if (__sloppyRuntime === undefined) {
  throw new Error("Sloppy bootstrap runtime was not loaded");
}
const { Results, data } = __sloppyRuntime;
function __sloppy_open_data_provider(kind, token) {
  if (kind === "sqlite") { return data.sqlite(token); }
  throw new Error(`sloppy: ${kind} provider bridge unavailable`);
}

function listUsers() {
  return db.query("select id, name from users", []);
}

globalThis.__sloppy_handler_1 = function(ctx) { const db = __sloppy_open_data_provider("sqlite", "data.main"); function listUsers() {
  return db.query("select id, name from users", []);
} try { return (() => Results.json(listUsers()))(ctx); } finally { db.close(); } };
globalThis.__sloppy_register_handler(1, globalThis.__sloppy_handler_1);
