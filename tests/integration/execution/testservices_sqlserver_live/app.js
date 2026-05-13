const __sloppyRuntime = globalThis.__sloppy_runtime;
if (__sloppyRuntime === undefined) {
  throw new Error("Sloppy bootstrap runtime was not loaded");
}
const { DockerCliBackend, Process, Results, TestServices } = __sloppyRuntime;

function assertContract(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

function errorText(error) {
  if (error === null || error === undefined) {
    return String(error);
  }
  const stack = error.stack;
  if (typeof stack === "string" && stack.length > 0) {
    return stack;
  }
  const message = error.message;
  if (typeof message === "string" && message.length > 0) {
    return `${error.name ?? "Error"}: ${message}`;
  }
  return String(error);
}

function fixturePath(relativePath) {
  return `${Process.info().cwd.replaceAll("\\", "/")}/${relativePath}`;
}

async function assertRemoved(containerId) {
  const docker = new DockerCliBackend();
  const inspect = await docker.run(["inspect", containerId], {
    timeoutMs: 15000,
    maxStdoutBytes: 64 * 1024,
    maxStderrBytes: 64 * 1024,
  });
  assertContract(inspect.exitCode !== 0, "disposed SQL Server TestServices container should be removed");
}

globalThis.__sloppy_handler_1 = async () => {
  let sqlServer;
  let containerId;
  let step = "start";
  try {
    step = "username rejection";
    let usernameError = "";
    try {
      await TestServices.sqlServer({
        username: "sloppy",
        password: "Strong_test_password_123!",
      });
    } catch (error) {
      usernameError = String(error && error.message ? error.message : error);
    }
    assertContract(
      usernameError.includes('currently supports only username "sa"'),
      "sqlServer should reject non-sa usernames until custom login provisioning exists",
    );

    step = "start sqlserver";
    sqlServer = await TestServices.sqlServer({
      database: "app_test",
      password: " Aa1;{Odd}}Pass_2026! ",
      startupTimeoutMs: 180000,
    });
    containerId = sqlServer.id;
    step = "assert escaped connection string";
    assertContract(sqlServer.connectionString.includes("PWD={ Aa1;{Odd}}}}Pass_2026! }"), "SQL Server password should be ODBC-escaped");
    assertContract(sqlServer.connectionString.includes("Database=app_test"), "SQL Server database should be present");

    step = "select 1";
    const selectOne = await sqlServer.provider().queryOne("select 1 as ok", []);
    assertContract(selectOne.ok === 1, "provider() should run select 1");

    step = "exec";
    await sqlServer.exec("create table dbo.ts_probe (id int)", []);
    await sqlServer.exec("drop table dbo.ts_probe", []);
    step = "migrate";
    await sqlServer.migrate([
      fixturePath("tests/integration/execution/testservices_sqlserver_live/001-create.sql"),
      fixturePath("tests/integration/execution/testservices_sqlserver_live/002-view.sql"),
      fixturePath("tests/integration/execution/testservices_sqlserver_live/003-procedure.sql"),
    ]);
    step = "seed";
    await sqlServer.seed((db) =>
      db.exec("exec dbo.ts_insert_user ?", ["ada@example.com"]));

    step = "verify seed";
    const seeded = await sqlServer.provider().queryOne("select count(*) as count from dbo.ts_users", []);
    assertContract(seeded.count === 1, "seed should insert one SQL Server row");

    step = "verify view";
    await sqlServer.exec("select email from dbo.ts_users_view", []);
    step = "reset";
    await sqlServer.reset({ migrate: true });

    step = "verify reset";
    const afterReset = await sqlServer.provider().queryOne("select count(*) as count from dbo.ts_users", []);
    assertContract(afterReset.count === 0, "reset({ migrate: true }) should recreate an empty migrated SQL Server database");

    step = "seed after reset";
    await sqlServer.seed((db) =>
      db.exec("exec dbo.ts_insert_user ?", ["grace@example.com"]));
    step = "verify after reset seed";
    const afterSeed = await sqlServer.provider().queryOne("select email from dbo.ts_users", []);
    assertContract(afterSeed.email === "grace@example.com", "provider() should work after SQL Server reset and seed");

    step = "dispose";
    await sqlServer.dispose();
    step = "verify removed";
    await assertRemoved(containerId);
    return Results.json({ ok: true, provider: "sqlserver", cleanup: "removed" });
  } catch (error) {
    try {
      await sqlServer?.dispose();
    } catch {
    }
    return Results.text(`${step}: ${errorText(error)}`, { status: 500 });
  }
};
globalThis.__sloppy_register_handler(1, globalThis.__sloppy_handler_1);
