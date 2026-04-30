const __sloppyRuntime = globalThis.__sloppy_runtime;
if (__sloppyRuntime === undefined) {
  throw new Error("Sloppy bootstrap runtime was not loaded");
}
const { Results, data } = __sloppyRuntime;

globalThis.__sloppy_handler_1 = async () => {
  const db = data.sqlite("main");
  try {
    db.exec("create table users (id integer primary key, name text not null)");
    db.exec("insert into users (name) values (?)", ["Ada"]);
    await db.transaction(tx => {
      tx.exec("insert into users (name) values (?)", ["Grace"]);
    });
    const users = await db.query("select id, name from users order by id", []);
    return Results.json(users);
  } finally {
    db.close();
  }
};
globalThis.__sloppy_register_handler(1, globalThis.__sloppy_handler_1);
