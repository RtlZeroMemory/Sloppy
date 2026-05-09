#!/usr/bin/env node
"use strict";

const fs = require("node:fs");

const [manifestPath, packageRoot] = process.argv.slice(2);
if (!manifestPath || !packageRoot) {
  console.error("usage: validate-npm-release-manifest.mjs MANIFEST PACKAGE_ROOT");
  process.exit(2);
}

const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8"));
function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

assert(manifest.manifestSchema === "sloppy.release-artifact.v1", "Archive manifest is not a release artifact manifest.");
assert(manifest.releaseKind === "dry-run", "Archive manifest must declare releaseKind=dry-run for npm staging.");
assert(manifest.publicReleaseCreated === false, "Archive manifest must declare publicReleaseCreated=false for npm staging.");
assert(manifest.canonicalDistribution === "github-release-archive", "Archive manifest must declare GitHub Release archives as canonical.");
assert(String(manifest.packageRoot) === packageRoot, "Archive manifest packageRoot does not match extracted archive root.");
