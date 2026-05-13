const __sloppyRuntime = globalThis.__sloppy_runtime;
if (__sloppyRuntime === undefined) {
  throw new Error("Sloppy bootstrap runtime was not loaded");
}
const { Cache, Environment, Results, data } = __sloppyRuntime;

async function sqlserverDelay(db) {
  await db.query("waitfor delay '00:00:00.05'; select 1 as value", [], { timeoutMs: 1000 });
}

async function sqlserverCacheStep(label, callback) {
  try {
    return await callback();
  } catch (error) {
    const details = error && typeof error === "object"
      ? Object.fromEntries(Object.getOwnPropertyNames(error).map((name) => [name, error[name]]))
      : {};
    throw new Error(`${label}: ${String(error && error.message ? error.message : error)} ${JSON.stringify(details)}`);
  }
}

async function sqlserverCacheEvidence(db) {
  const cache = Cache.sqlServer(db, {
    name: "main",
    namespace: "sqlserver_cache_a",
    table: "sloppy_sqlserver_cache_live",
  });
  const other = Cache.sqlServer(db, {
    name: "other",
    namespace: "sqlserver_cache_b",
    table: "sloppy_sqlserver_cache_live",
  });
  await sqlserverCacheStep("cache set alpha", () => cache.set("alpha", { value: "one" }));
    const getOk = (await sqlserverCacheStep("cache get alpha", () => cache.get("alpha"))).value === "one";
    await sqlserverCacheStep("cache remove alpha", () => cache.remove("alpha"));
    const removeOk = await sqlserverCacheStep("cache get removed alpha", () => cache.get("alpha")) === undefined;

    await sqlserverCacheStep("cache set ttl", () => cache.set("ttl", "expired", { ttlMs: 1 }));
    await sqlserverDelay(db);
    const ttlExpired = await sqlserverCacheStep("cache get ttl", () => cache.get("ttl")) === undefined;

    await sqlserverCacheStep("cache set slide", () => cache.set("slide", "ok", { slidingExpirationMs: 80 }));
    await sqlserverDelay(db);
    const slidingFirst = await sqlserverCacheStep("cache get slide first", () => cache.get("slide")) === "ok";
    await sqlserverDelay(db);
    const slidingSecond = await sqlserverCacheStep("cache get slide second", () => cache.get("slide")) === "ok";
    await db.query("waitfor delay '00:00:00.12'; select 1 as value", [], { timeoutMs: 1000 });
    const slidingExpired = await sqlserverCacheStep("cache get slide expired", () => cache.get("slide")) === undefined;

    await sqlserverCacheStep("cache set tagged", () => cache.set("tagged", "x", { tags: ["tag-a"] }));
    await sqlserverCacheStep("cache invalidate tag", () => cache.invalidateTag("tag-a"));
    const tagInvalidated = await sqlserverCacheStep("cache get tagged", () => cache.get("tagged")) === undefined;

    await sqlserverCacheStep("cache set cleanup", () => cache.set("cleanup", "x", { ttlMs: 1 }));
    await sqlserverDelay(db);
    await sqlserverCacheStep("cache cleanup", () => cache.cleanup());
    const cleanupOk = await sqlserverCacheStep("cache get cleanup", () => cache.get("cleanup")) === undefined;

    await sqlserverCacheStep("cache set shared a", () => cache.set("shared", "a"));
    await sqlserverCacheStep("cache set shared b", () => other.set("shared", "b"));
    const namespaceIsolation =
      (await sqlserverCacheStep("cache get shared a", () => cache.get("shared"))) === "a" &&
      (await sqlserverCacheStep("cache get shared b", () => other.get("shared"))) === "b";

    await sqlserverCacheStep("cache set hybrid", () => cache.set("hybrid", "sqlserver"));
    const hybrid = Cache.hybrid("hybrid", {
      memory: Cache.memory("front"),
      distributed: cache,
    });
    const hybridFirst = await sqlserverCacheStep("hybrid get", () => hybrid.get("hybrid")) === "sqlserver";
    const hybridMemory = await sqlserverCacheStep("hybrid memory get", () => hybrid.memory.get("hybrid")) === "sqlserver";
    const hybridGets = hybrid.stats().gets >= 1;

  return {
    getOk,
    removeOk,
    ttlExpired,
    slidingFirst,
    slidingSecond,
    slidingExpired,
    tagInvalidated,
    cleanupOk,
    namespaceIsolation,
    hybridFirst,
    hybridMemory,
    hybridGets,
  };
}

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
      await sqlserverCacheStep("drop bridge table", () => db.exec("if object_id(N'dbo.sloppy_sqlserver_bridge_cursor', N'U') is not null drop table dbo.sloppy_sqlserver_bridge_cursor"));
      await sqlserverCacheStep("create bridge table", () => db.exec("create table dbo.sloppy_sqlserver_bridge_cursor (id int identity(1,1) primary key, name nvarchar(64) not null, payload varbinary(16) null)"));
      await sqlserverCacheStep("insert Ada", () => db.exec("insert into dbo.sloppy_sqlserver_bridge_cursor (name, payload) values (?, ?)", ["Ada", new Uint8Array([0, 1, 255])]));
      await sqlserverCacheStep("transaction bridge rows", () => db.transaction(async (tx) => {
        await tx.exec("insert into dbo.sloppy_sqlserver_bridge_cursor (name, payload) values (?, cast(null as varbinary(16)))", ["Grace"]);
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
      }));
      const bulkRows = Array.from({ length: 150 }, (_, index) => `(N'User-${index + 1}', null)`).join(", ");
      await sqlserverCacheStep("insert bulk rows", () => db.exec(`insert into dbo.sloppy_sqlserver_bridge_cursor (name, payload) values ${bulkRows}`));

      let materializedRejected = false;
      try {
        await db.query("select name from dbo.sloppy_sqlserver_bridge_cursor order by id", []);
      } catch (error) {
        materializedRejected = String(error && error.message ? error.message : error).includes("exceeded max rows");
      }

      const cursorRows = [];
      await sqlserverCacheStep("read cursor rows", async () => {
        const cursor = await db.queryCursor("select name from dbo.sloppy_sqlserver_bridge_cursor order by id", [], { batchSize: 32 });
        try {
          for await (const row of cursor) {
            cursorRows.push(row.name);
          }
        } finally {
          await cursor.close();
        }
      });

      let rawFirst = null;
      let cursorMaxRowsRejected = false;
      await sqlserverCacheStep("read raw cursor rows", async () => {
        const rawCursor = await db.queryRawCursor("select name from dbo.sloppy_sqlserver_bridge_cursor order by id", [], { batchSize: 16, maxRows: 2 });
        try {
          rawFirst = await rawCursor.next();
          await rawCursor.next();
          await rawCursor.next();
        } catch (error) {
          cursorMaxRowsRejected = String(error && error.message ? error.message : error).includes("exceeded max rows");
        } finally {
          await rawCursor.close();
        }
      });

      let poolPinned = false;
      await sqlserverCacheStep("verify pool pinning", async () => {
        const pinnedCursor = await db.queryCursor("select name from dbo.sloppy_sqlserver_bridge_cursor order by id", [], { batchSize: 8 });
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
      });
      const afterClose = await sqlserverCacheStep("query after cursor close", () => db.queryOne("select count(*) as count from dbo.sloppy_sqlserver_bridge_cursor", []));

      let cursorTimedOut = false;
      try {
        await db.queryCursor("waitfor delay '00:00:02'; select 1 as value", [], { timeoutMs: 50 });
      } catch (error) {
        cursorTimedOut = String(error && error.message ? error.message : error).includes("deadline was exceeded");
      }

      const rows = await sqlserverCacheStep("query selected rows", () => db.query("select name from dbo.sloppy_sqlserver_bridge_cursor where name in (?, ?) order by id", ["Ada", "Grace"]));
      let sqlserverTimedOut = false;
      try {
        await db.query("waitfor delay '00:00:02'; select 1 as value", [], { timeoutMs: 50 });
      } catch (error) {
        sqlserverTimedOut = String(error && error.message ? error.message : error).includes("deadline was exceeded");
      }
      const cacheEvidence = await sqlserverCacheEvidence(db);
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
        cacheEvidence,
      });
    } finally {
      try {
        await db.exec("if object_id(N'dbo.sloppy_sqlserver_bridge_cursor', N'U') is not null drop table dbo.sloppy_sqlserver_bridge_cursor");
      } catch {
      }
      try {
        db.close();
      } catch {
      }
    }
  } catch (error) {
    return Results.text(String(error && error.message ? error.message : error), { status: 500 });
  }
};
globalThis.__sloppy_register_handler(1, globalThis.__sloppy_handler_1);
