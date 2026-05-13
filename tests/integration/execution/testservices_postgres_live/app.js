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

function createCleanupFailureBackend(commands) {
  return Object.freeze({
    async run(args) {
      commands.push(args.slice());
      if (args[0] === "version") {
        return Object.freeze({ exitCode: 0, stdout: "{}", stderr: "", timedOut: false });
      }
      if (args[0] === "image" && args[1] === "inspect") {
        return Object.freeze({ exitCode: 0, stdout: "[]", stderr: "", timedOut: false });
      }
      if (args[0] === "create") {
        return Object.freeze({ exitCode: 0, stdout: "fake-container\n", stderr: "", timedOut: false });
      }
      if (args[0] === "start") {
        return Object.freeze({ exitCode: 0, stdout: "fake-container\n", stderr: "", timedOut: false });
      }
      if (args[0] === "inspect") {
        return Object.freeze({
          exitCode: 0,
          stdout: JSON.stringify([{ NetworkSettings: { Ports: {} } }]),
          stderr: "",
          timedOut: false,
        });
      }
      if (args[0] === "logs") {
        return Object.freeze({ exitCode: 0, stdout: "secret runtime-contract-secret", stderr: "", timedOut: false });
      }
      if (args[0] === "stop") {
        return Object.freeze({ exitCode: 1, stdout: "", stderr: "stop failed", timedOut: false });
      }
      if (args[0] === "rm") {
        return Object.freeze({ exitCode: 1, stdout: "", stderr: "rm failed runtime-contract-secret", timedOut: false });
      }
      throw new Error(`unexpected docker command: ${args.join(" ")}`);
    },
  });
}

async function assertStartupCleanupFailureIsVisible() {
  const commands = [];
  let message = "";
  try {
    await TestServices.postgres({
      containerName: "sloppy-testservices-cleanup-failure-contract",
      dockerBackend: createCleanupFailureBackend(commands),
      password: "runtime-contract-secret",
      startupTimeoutMs: 1,
    });
  } catch (error) {
    message = String(error && error.message ? error.message : error);
  }
  assertContract(commands.some((args) => args[0] === "rm"), "startup failure should attempt docker rm");
  assertContract(message.includes("Cleanup failures:"), "startup cleanup failure should be visible");
  assertContract(message.includes("rm failed"), "startup cleanup failure should include rm failure details");
  assertContract(!message.includes("runtime-contract-secret"), "cleanup diagnostics must redact known secrets");
}

async function assertRemoved(containerId) {
  const docker = new DockerCliBackend();
  const inspect = await docker.run(["inspect", containerId], {
    timeoutMs: 15000,
    maxStdoutBytes: 64 * 1024,
    maxStderrBytes: 64 * 1024,
  });
  assertContract(inspect.exitCode !== 0, "disposed PostgreSQL TestServices container should be removed");
}

globalThis.__sloppy_handler_1 = async () => {
  let pg;
  let containerId;
  let step = "start";
  try {
    step = "fake cleanup failure";
    await assertStartupCleanupFailureIsVisible();

    step = "start postgres";
    pg = await TestServices.postgres({
      database: "app_test",
      password: "sloppy",
      startupTimeoutMs: 120000,
    });
    containerId = pg.id;

    step = "select 1";
    const selectOne = await pg.provider().queryOne("select 1::int as ok", []);
    assertContract(selectOne.ok === 1, "provider() should run select 1");

    step = "exec";
    await pg.exec("create table if not exists ts_probe (id int)", []);
    await pg.exec("drop table ts_probe", []);
    step = "migrate";
    await pg.migrate(fixturePath("tests/integration/execution/testservices_postgres_live/001-create.sql"));
    step = "seed";
    await pg.seed((db) =>
      db.exec("insert into ts_users (email) values ($1)", ["ada@example.com"]));

    step = "verify seed";
    const seeded = await pg.provider().queryOne("select count(*)::int as count from ts_users", []);
    assertContract(seeded.count === 1, "seed should insert one PostgreSQL row");

    step = "reset";
    await pg.reset({ migrate: true });
    step = "verify reset";
    const afterReset = await pg.provider().queryOne("select count(*)::int as count from ts_users", []);
    assertContract(afterReset.count === 0, "reset({ migrate: true }) should recreate an empty migrated PostgreSQL schema");

    step = "seed after reset";
    await pg.seed((db) =>
      db.exec("insert into ts_users (email) values ($1)", ["grace@example.com"]));
    step = "verify after reset seed";
    const afterSeed = await pg.provider().queryOne("select email from ts_users", []);
    assertContract(afterSeed.email === "grace@example.com", "provider() should work after reset and seed");

    step = "dispose";
    await pg.dispose();
    step = "verify removed";
    await assertRemoved(containerId);
    return Results.json({ ok: true, provider: "postgres", cleanup: "removed" });
  } catch (error) {
    try {
      await pg?.dispose();
    } catch {
    }
    return Results.text(`${step}: ${errorText(error)}`, { status: 500 });
  }
};
globalThis.__sloppy_register_handler(1, globalThis.__sloppy_handler_1);
