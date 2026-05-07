const __sloppyRuntime = globalThis.__sloppy_runtime;
if (__sloppyRuntime === undefined) {
  throw new Error("Sloppy bootstrap runtime was not loaded");
}
const { Environment, Results, data } = __sloppyRuntime;

globalThis.__sloppy_handler_1 = async () => {
  try {
    const connectionString = Environment.get("SLOPPY_SQLSERVER_TEST_CONNECTION_STRING");
    if (connectionString.length === 0) {
      throw new Error("SLOPPY_SQLSERVER_TEST_CONNECTION_STRING is required for the SQL Server live bridge lane");
    }

    const db = data.sqlserver.open({
      connectionString,
      capability: "data.main",
      maxConnections: 1,
    });
    try {
      await db.exec("create table #sloppy_sqlserver_bridge (id int identity(1,1) primary key, name nvarchar(64) not null, payload varbinary(16) null)");
      await db.exec("insert into #sloppy_sqlserver_bridge (name, payload) values (?, convert(varbinary(16), ?))", ["Ada", new Uint8Array([0, 1, 255])]);
      await db.transaction(async (tx) => {
        await tx.exec("insert into #sloppy_sqlserver_bridge (name, payload) values (?, convert(varbinary(16), ?))", ["Grace", null]);
      });
      const rows = await db.query("select name from #sloppy_sqlserver_bridge order by id", []);
      return Results.json(rows);
    } finally {
      db.close();
    }
  } catch (error) {
    return Results.text(String(error && error.message ? error.message : error), { status: 500 });
  }
};
globalThis.__sloppy_register_handler(1, globalThis.__sloppy_handler_1);
