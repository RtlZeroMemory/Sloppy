#!/usr/bin/env node
"use strict";

const { spawnSync } = require("node:child_process");
const path = require("node:path");
const { resolvePlatformPackage } = require("../lib/platform");

function main() {
  const selected = resolvePlatformPackage(process.env, process.platform, process.arch);
  if (!selected.supported) {
    console.error(selected.message);
    return 1;
  }

  let packageJsonPath;
  try {
    packageJsonPath = require.resolve(`${selected.packageName}/package.json`);
  } catch {
    console.error(
      `Sloppy runtime platform package '${selected.packageName}' is not installed. ` +
        "Install the matching @sloppy/runtime platform package generated from a tested release archive."
    );
    return 1;
  }

  const packageRoot = path.dirname(packageJsonPath);
  const executable = path.join(packageRoot, "bin", process.platform === "win32" ? "sloppy.exe" : "sloppy");
  const result = spawnSync(executable, process.argv.slice(2), { stdio: "inherit" });
  if (result.error) {
    console.error(`Failed to run packaged Sloppy binary '${executable}': ${result.error.message}`);
    return 1;
  }
  return typeof result.status === "number" ? result.status : 1;
}

process.exitCode = main();
