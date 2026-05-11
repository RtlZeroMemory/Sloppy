#!/usr/bin/env node
// Adds program-mode entries to the representative npm-compat fixtures so
// that `sloppy build` and `compile_file` can drive them end-to-end. Idempotent.

import { mkdirSync, writeFileSync, existsSync, readFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";

const repoRoot = resolve(new URL("../..", import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, "$1"));
const fixturesRoot = join(repoRoot, "tests", "fixtures", "npm-compat");

const sloppyJson = (entry) => JSON.stringify({
    kind: "program",
    entry,
    outDir: ".sloppy",
    environment: "Development"
}, null, 4) + "\n";

const entries = [
    {
        fixture: "basic-main-cjs",
        relative: "src/main.ts",
        content: `import greet from "greet-cjs";\n\nexport async function main() {\n    const message = greet("npm-compat");\n    console.log(message);\n    return 0;\n}\n`
    },
    {
        fixture: "basic-main-esm",
        relative: "src/main.ts",
        content: `import { greet } from "greet-esm";\n\nexport async function main() {\n    console.log(greet("npm-compat"));\n    return 0;\n}\n`
    },
    {
        fixture: "exports-nested-conditions",
        relative: "src/main.ts",
        content: `import value from "nested-pkg";\n\nexport async function main() {\n    console.log(value);\n    return 0;\n}\n`
    },
    {
        fixture: "imports-alias",
        relative: "src/main.ts",
        content: `import util from "#util";\n\nexport async function main() {\n    console.log(util());\n    return 0;\n}\n`,
        sloppyEntry: "src/main.ts"
    },
    {
        fixture: "self-reference",
        relative: "src/main.ts",
        content: `import { head } from "self-ref-pkg";\nimport { tail } from "self-ref-pkg/feature";\n\nexport async function main() {\n    console.log(head + tail);\n    return 0;\n}\n`,
        sloppyEntry: "src/main.ts"
    },
    {
        fixture: "interop-cjs-requires-json",
        relative: "src/main.ts",
        content: `import data from "cjs-json-pkg";\n\nexport async function main() {\n    console.log(JSON.stringify(data));\n    return 0;\n}\n`
    },
    {
        fixture: "builtins-fs-promises-buffer",
        relative: "src/main.ts",
        content: `import { read } from "buf-fs-pkg";\n\nexport async function main(args) {\n    const target = args[0] ?? "missing";\n    console.log(typeof read, target);\n    return 0;\n}\n`
    },
    {
        fixture: "optional-native-unused",
        relative: "src/main.ts",
        content: `import { add } from "opt-native-pkg";\n\nexport async function main() {\n    console.log(add(2, 3));\n    return 0;\n}\n`
    },
    {
        fixture: "dynamic-require-literal",
        relative: "src/main.ts",
        content: `import helper from "dyn-pkg/helper";\n\nexport async function main() {\n    console.log(helper);\n    return 0;\n}\n`
    }
];

let written = 0;
let skipped = 0;
for (const entry of entries) {
    const fixtureDir = join(fixturesRoot, entry.fixture);
    const entryPath = join(fixtureDir, entry.relative);
    mkdirSync(dirname(entryPath), { recursive: true });
    if (existsSync(entryPath) && readFileSync(entryPath, "utf8") === entry.content) {
        skipped++;
    } else {
        writeFileSync(entryPath, entry.content);
        written++;
    }

    const sloppyJsonPath = join(fixtureDir, "sloppy.json");
    const desired = sloppyJson(entry.sloppyEntry ?? entry.relative);
    if (existsSync(sloppyJsonPath) && readFileSync(sloppyJsonPath, "utf8") === desired) {
        skipped++;
    } else {
        writeFileSync(sloppyJsonPath, desired);
        written++;
    }
}

console.log(`npm-compat program entries: wrote ${written}, unchanged ${skipped}`);
