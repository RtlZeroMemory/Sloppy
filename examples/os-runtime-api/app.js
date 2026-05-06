import { Environment, Process, Signals, System } from "sloppy/os";
import { Deadline } from "sloppy/time";

export function describeHost() {
  return {
    platform: System.platform,
    arch: System.arch,
    cpuCount: System.cpuCount,
    tempDirectory: System.tempDirectory,
    setting: Environment.get("MY_APP_SETTING")
  };
}

export async function gitStatus(repo) {
  return Process.run("git", ["status", "--short"], {
    cwd: repo,
    timeoutMs: 5000,
    capture: "text"
  });
}

export async function probeProcess(command, args) {
  const proc = await Process.start(command, args, {
    stdout: "pipe",
    stderr: "pipe",
    deadline: Deadline.after(5000)
  });

  const lines = [];
  for await (const line of proc.stdout.readLines()) {
    lines.push(line);
  }

  const exit = await proc.wait();
  return { exit, lines };
}

Signals.onShutdown(async (ctx) => {
  await Promise.resolve({ signal: ctx.signal, forced: ctx.forced });
});
