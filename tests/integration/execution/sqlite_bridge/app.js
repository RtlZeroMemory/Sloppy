const __sloppyRuntime = globalThis.__sloppy_runtime;
if (__sloppyRuntime === undefined) {
  throw new Error("Sloppy bootstrap runtime was not loaded");
}
const { Results, data } = __sloppyRuntime;

globalThis.__sloppy_handler_1 = () => {
  const db = data.sqlite.open(":memory:");
  try {
    db.exec("create table users (id integer primary key, name text not null)");
    db.exec("insert into users (name) values (?)", ["Ada"]);
    const row = db.queryOne("select name from users where id = ?", [1]);
    return Results.json(row);
  } finally {
    db.close();
  }
};
globalThis.__sloppy_register_handler(1, globalThis.__sloppy_handler_1);
