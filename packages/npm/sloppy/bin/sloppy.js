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
        "Install the matching @rtlzeromemory/sloppy platform package generated from a tested release archive."
    );
    return 1;
  }

  const packageRoot = path.dirname(packageJsonPath);
  const platformBin = path.join(packageRoot, "bin");
  const executable = path.join(platformBin, process.platform === "win32" ? "sloppy.exe" : "sloppy");
  const sloppyc = path.join(platformBin, process.platform === "win32" ? "sloppyc.exe" : "sloppyc");
  const pathKey = Object.keys(process.env).find((key) => /^path$/i.test(key)) || "PATH";
  const currentPath = process.env[pathKey] || "";
  const childEnv = {
    ...process.env,
    [pathKey]: `${platformBin}${path.delimiter}${currentPath}`,
    SLOPPY_SLOPPYC: process.env.SLOPPY_SLOPPYC || sloppyc
  };
  const result = spawnSync(executable, process.argv.slice(2), { stdio: "inherit", env: childEnv });
  if (result.error) {
    console.error(`Failed to run packaged Sloppy binary '${executable}': ${result.error.message}`);
    return 1;
  }
  return typeof result.status === "number" ? result.status : 1;
}

process.exitCode = main();
