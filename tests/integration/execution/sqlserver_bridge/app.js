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
      await db.exec("if object_id(N'dbo.sloppy_sqlserver_bridge_cursor', N'U') is not null drop table dbo.sloppy_sqlserver_bridge_cursor");
      await db.exec("create table dbo.sloppy_sqlserver_bridge_cursor (id int identity(1,1) primary key, name nvarchar(64) not null, payload varbinary(16) null)");
      await db.exec("insert into dbo.sloppy_sqlserver_bridge_cursor (name, payload) values (?, convert(varbinary(16), ?))", ["Ada", new Uint8Array([0, 1, 255])]);
      await db.transaction(async (tx) => {
        await tx.exec("insert into dbo.sloppy_sqlserver_bridge_cursor (name, payload) values (?, convert(varbinary(16), ?))", ["Grace", null]);
        const txCursor = await tx.queryCursor("select name from dbo.sloppy_sqlserver_bridge_cursor order by id", [], { batchSize: 1 });
        try {
          const txNames = [];
          for await (const row of txCursor) {
            txNames.push(row.name);
          }
          if (txNames.length !== 2) {
            throw new Error("sqlserver transaction cursor did not read expected rows");
          }
        } finally {
          await txCursor.close();
        }
      });
      const bulkRows = Array.from({ length: 150 }, (_, index) => `(N'User-${index + 1}', null)`).join(", ");
      await db.exec(`insert into dbo.sloppy_sqlserver_bridge_cursor (name, payload) values ${bulkRows}`);

      let materializedRejected = false;
      try {
        await db.query("select name from dbo.sloppy_sqlserver_bridge_cursor order by id", []);
      } catch (error) {
        materializedRejected = String(error && error.message ? error.message : error).includes("exceeded max rows");
      }

      const cursorRows = [];
      const cursor = await db.queryCursor("select name from dbo.sloppy_sqlserver_bridge_cursor order by id", [], { batchSize: 32 });
      try {
        for await (const row of cursor) {
          cursorRows.push(row.name);
        }
      } finally {
        await cursor.close();
      }

      const rawCursor = await db.queryRawCursor("select name from dbo.sloppy_sqlserver_bridge_cursor order by id", [], { batchSize: 16, maxRows: 2 });
      let rawFirst = null;
      let cursorMaxRowsRejected = false;
      try {
        rawFirst = await rawCursor.next();
        await rawCursor.next();
        await rawCursor.next();
      } catch (error) {
        cursorMaxRowsRejected = String(error && error.message ? error.message : error).includes("exceeded max rows");
      } finally {
        await rawCursor.close();
      }

      const pinnedCursor = await db.queryCursor("select name from dbo.sloppy_sqlserver_bridge_cursor order by id", [], { batchSize: 8 });
      let poolPinned = false;
      try {
        await pinnedCursor.next();
        try {
          await db.queryOne("select 1 as ok", []);
        } catch (error) {
          poolPinned = String(error && error.message ? error.message : error).includes("pool is exhausted");
        }
      } finally {
        await pinnedCursor.close();
      }
      const afterClose = await db.queryOne("select count(*) as count from dbo.sloppy_sqlserver_bridge_cursor", []);

      let cursorTimedOut = false;
      try {
        await db.queryCursor("waitfor delay '00:00:02'; select 1 as value", [], { timeoutMs: 50 });
      } catch (error) {
        cursorTimedOut = String(error && error.message ? error.message : error).includes("deadline was exceeded");
      }

      const rows = await db.query("select name from dbo.sloppy_sqlserver_bridge_cursor where name in (?, ?) order by id", ["Ada", "Grace"]);
      let sqlserverTimedOut = false;
      try {
        await db.query("waitfor delay '00:00:02'; select 1 as value", [], { timeoutMs: 50 });
      } catch (error) {
        sqlserverTimedOut = String(error && error.message ? error.message : error).includes("deadline was exceeded");
      }
      return Results.json({
        rows,
        sqlserverTimedOut,
        materializedRejected,
        cursorCount: cursorRows.length,
        cursorFirst: cursorRows[0],
        cursorLast: cursorRows[cursorRows.length - 1],
        rawFirst,
        cursorMaxRowsRejected,
        poolPinned,
        afterCloseCount: afterClose.count,
        cursorTimedOut,
      });
    } finally {
      try {
        await db.exec("if object_id(N'dbo.sloppy_sqlserver_bridge_cursor', N'U') is not null drop table dbo.sloppy_sqlserver_bridge_cursor");
      } catch {
      }
      db.close();
    }
  } catch (error) {
    return Results.text(String(error && error.message ? error.message : error), { status: 500 });
  }
};
globalThis.__sloppy_register_handler(1, globalThis.__sloppy_handler_1);
