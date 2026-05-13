const __sloppyRuntime = globalThis.__sloppy_runtime;
if (__sloppyRuntime === undefined) {
  throw new Error("Sloppy bootstrap runtime was not loaded");
}
const { DockerCliBackend, Results, TestServices } = __sloppyRuntime;

function assertContract(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

function createFakeDockerBackend(commands) {
  return Object.freeze({
    async run(args) {
      commands.push(args.slice());
      if (args[0] === "version") {
        return Object.freeze({
          exitCode: 0,
          stdout: "{\"Client\":{\"Version\":\"test\"},\"Server\":{\"Version\":\"test\"}}",
          stderr: "",
          timedOut: false,
        });
      }
      throw new Error(`unexpected fake docker command: ${args.join(" ")}`);
    },
  });
}

function createUnavailableDockerBackend() {
  return Object.freeze({
    async run() {
      return Object.freeze({
        exitCode: 1,
        stdout: "",
        stderr: "docker daemon unavailable",
        timedOut: false,
      });
    },
  });
}

globalThis.__sloppy_handler_1 = async () => {
  try {
    assertContract(typeof DockerCliBackend === "function", "DockerCliBackend runtime export is missing");
    assertContract(typeof TestServices?.docker?.available === "function", "TestServices.docker.available is missing");
    assertContract(typeof TestServices?.docker?.require === "function", "TestServices.docker.require is missing");
    assertContract(typeof TestServices?.postgres === "function", "TestServices.postgres is missing");
    assertContract(typeof TestServices?.sqlServer === "function", "TestServices.sqlServer is missing");

    const commands = [];
    const fakeDocker = createFakeDockerBackend(commands);
    const docker = await TestServices.docker.available({ dockerBackend: fakeDocker });
    assertContract(docker.ok === true, "fake Docker backend should report available");

    const realDocker = await TestServices.docker.available({ timeoutMs: 5000 });
    assertContract(typeof realDocker.ok === "boolean", "real Docker probe should return a status object");

    let unavailableCode = "";
    try {
      await TestServices.docker.require({ dockerBackend: createUnavailableDockerBackend() });
    } catch (error) {
      unavailableCode = error.code;
    }
    assertContract(
      unavailableCode === "SLOPPY_E_TESTSERVICES_DOCKER_UNAVAILABLE",
      "docker.require should fail with the TestServices Docker unavailable code",
    );

    let providerCode = "";
    try {
      await TestServices.postgres({
        containerName: "sloppy-testservices-runtime-contract",
        dockerBackend: fakeDocker,
        password: "runtime-contract-secret",
        startupTimeoutMs: 1,
      });
    } catch (error) {
      providerCode = error.code;
      assertContract(
        !String(error.message).includes("runtime-contract-secret"),
        "provider unavailable diagnostics must not print the supplied password",
      );
    }
    assertContract(
      providerCode === "SLOPPY_E_TESTSERVICES_PROVIDER_UNAVAILABLE",
      "postgres should fail before container create when the provider bridge is unavailable",
    );
    assertContract(commands.some((args) => args[0] === "create") === false, "provider-unavailable path must not create a container");

    return Results.json({
      ok: true,
      dockerProbe: realDocker.ok ? "available" : "unavailable",
      fakeCommands: commands.length,
    });
  } catch (error) {
    return Results.json({
      ok: false,
      message: String(error && error.message ? error.message : error),
    }, { status: 500 });
  }
};
globalThis.__sloppy_register_handler(1, globalThis.__sloppy_handler_1);
