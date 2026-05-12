const __sloppyRuntime = globalThis.__sloppy_runtime;
if (__sloppyRuntime === undefined) {
  throw new Error("Sloppy bootstrap runtime was not loaded");
}
const { Environment, Results, data } = __sloppyRuntime;

globalThis.__sloppy_handler_1 = async () => {
  try {
  const connectionString = Environment.get("SLOPPY_POSTGRES_TEST_URL");
  if (connectionString.length === 0) {
    throw new Error("SLOPPY_POSTGRES_TEST_URL is required for the PostgreSQL live bridge lane");
  }

  const db = data.postgres.open({
    connectionString,
    capability: "data.main",
    maxConnections: 2,
  });
  try {
    await db.exec("create temp table sloppy_pg_bridge (id serial primary key, name text not null)");
    await db.exec("insert into sloppy_pg_bridge (name) values ($1)", ["Ada"]);
    await db.transaction(async (tx) => {
      await tx.exec("insert into sloppy_pg_bridge (name) values ($1)", ["Grace"]);
    });
    const users = await db.query("select id, name from sloppy_pg_bridge order by id", []);
    let postgresTimedOut = false;
    try {
      await db.query("select pg_sleep(2)", [], { timeoutMs: 50 });
    } catch (error) {
      postgresTimedOut = String(error && error.message ? error.message : error).includes("deadline was exceeded");
    }
    return Results.json({ users, postgresTimedOut });
  } finally {
    db.close();
  }
  } catch (error) {
    return Results.text(String(error && error.message ? error.message : error), { status: 500 });
  }
};
globalThis.__sloppy_register_handler(1, globalThis.__sloppy_handler_1);
