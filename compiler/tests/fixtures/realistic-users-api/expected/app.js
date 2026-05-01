const __sloppyRuntime = globalThis.__sloppy_runtime;
if (__sloppyRuntime === undefined) {
  throw new Error("Sloppy bootstrap runtime was not loaded");
}
const { Results, data } = __sloppyRuntime;
function __sloppy_open_data_provider(kind, token) {
  if (kind === "sqlite") { return data.sqlite(token); }
  throw new Error(`sloppy: ${kind} provider bridge unavailable`);
}

globalThis.__sloppy_handler_1 = function(ctx) { const __sloppy_opened_providers = []; let db; try { db = __sloppy_open_data_provider("sqlite", "data.main"); __sloppy_opened_providers.push(db); function createUser(body) {
  db.exec("insert into users (name, email) values (?, ?)", [body.name, body.email]);
  return db.queryOne("select id, name, email from users where id = last_insert_rowid()", []);
} function listUsers() {
  return db.query("select id, name, email from users", []);
} return ((ctx) => Results.json({
  q: ctx.query.q,
  users: listUsers()
}))(ctx); } finally { while (__sloppy_opened_providers.length > 0) { const __sloppy_provider = __sloppy_opened_providers.pop(); try { __sloppy_provider.close(); } catch (_) {} } } };
globalThis.__sloppy_register_handler(1, globalThis.__sloppy_handler_1);
globalThis.__sloppy_handler_2 = function(ctx) { const __sloppy_opened_providers = []; let db; try { db = __sloppy_open_data_provider("sqlite", "data.main"); __sloppy_opened_providers.push(db); function createUser(body) {
  db.exec("insert into users (name, email) values (?, ?)", [body.name, body.email]);
  return db.queryOne("select id, name, email from users where id = last_insert_rowid()", []);
} function listUsers() {
  return db.query("select id, name, email from users", []);
} return ((ctx) => Results.created("/api/users/1", {
  user: createUser(ctx.body.json(undefined))
}))(ctx); } finally { while (__sloppy_opened_providers.length > 0) { const __sloppy_provider = __sloppy_opened_providers.pop(); try { __sloppy_provider.close(); } catch (_) {} } } };
globalThis.__sloppy_register_handler(2, globalThis.__sloppy_handler_2);
globalThis.__sloppy_handler_3 = function(ctx) { const __sloppy_opened_providers = []; let db; try { db = __sloppy_open_data_provider("sqlite", "data.main"); __sloppy_opened_providers.push(db); function createUser(body) {
  db.exec("insert into users (name, email) values (?, ?)", [body.name, body.email]);
  return db.queryOne("select id, name, email from users where id = last_insert_rowid()", []);
} function listUsers() {
  return db.query("select id, name, email from users", []);
} return ((ctx) => Results.json({
  id: ctx.route.id,
  user: db.queryOne("select id, name, email from users where id = ?", [ctx.route.id])
}))(ctx); } finally { while (__sloppy_opened_providers.length > 0) { const __sloppy_provider = __sloppy_opened_providers.pop(); try { __sloppy_provider.close(); } catch (_) {} } } };
globalThis.__sloppy_register_handler(3, globalThis.__sloppy_handler_3);
globalThis.__sloppy_handler_4 = () => Results.text("ok");
globalThis.__sloppy_register_handler(4, globalThis.__sloppy_handler_4);
