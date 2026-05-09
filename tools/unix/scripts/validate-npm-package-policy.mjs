#!/usr/bin/env node
"use strict";

import fs from "node:fs";

const [packageJsonPath] = process.argv.slice(2);
if (!packageJsonPath) {
  console.error("usage: validate-npm-package-policy.mjs PACKAGE_JSON");
  process.exit(2);
}

const packageJson = JSON.parse(fs.readFileSync(packageJsonPath, "utf8"));
function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

for (const [name, value] of Object.entries(packageJson.scripts || {})) {
  assert(!/^(preinstall|install|postinstall|prepare)$/.test(name), `${packageJsonPath} contains forbidden lifecycle script '${name}'.`);
  assert(!/node-gyp|cmake|cargo|vcpkg|fetch-v8|build-v8|postinstall/.test(String(value)), `${packageJsonPath} script '${name}' contains native build/download command.`);
}

assert(packageJson.publishConfig && packageJson.publishConfig.tag === "alpha", `${packageJsonPath} must publish with alpha dist-tag.`);
