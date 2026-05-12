#!/usr/bin/env node
// Generator for tests/fixtures/npm-compat/. Run once to populate fixtures.
// The script is committed for reproducibility; the produced files are also
// committed and tracked.
//
// Usage:
//   node tools/scripts/build-npm-compat-fixtures.mjs
//
// The script writes files only if their contents differ. It does not delete
// existing fixtures so that hand-authored examples stay intact.

import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = fileURLToPath(new URL("../..", import.meta.url));
const fixturesRoot = join(repoRoot, "tests", "fixtures", "npm-compat");

const fixtures = [
    // ---- basic shapes (remaining) ----
    {
        path: "basic-type-module/node_modules/typemod-pkg/index.js",
        content: `export default "typemod-pkg-index";\n`,
    },
    {
        path: "basic-type-commonjs/importer.js",
        content: `const value = require("typemod-cjs-pkg");\nmodule.exports = value;\n`,
    },
    {
        path: "basic-type-commonjs/node_modules/typemod-cjs-pkg/package.json",
        content: jsonText({
            name: "typemod-cjs-pkg",
            version: "1.0.0",
            type: "commonjs"
        }),
    },
    {
        path: "basic-type-commonjs/node_modules/typemod-cjs-pkg/index.js",
        content: `module.exports = "typemod-cjs-pkg-index";\n`,
    },
    {
        path: "basic-no-main-index/importer.js",
        content: `import value from "no-main-pkg";\nexport const result = value;\n`,
    },
    {
        path: "basic-no-main-index/node_modules/no-main-pkg/package.json",
        content: jsonText({ name: "no-main-pkg", version: "1.0.0", type: "module" }),
    },
    {
        path: "basic-no-main-index/node_modules/no-main-pkg/index.js",
        content: `export default "no-main-pkg-default";\n`,
    },
    {
        path: "basic-directory-index/importer.js",
        content: `import value from "dir-index-pkg";\nexport const result = value;\n`,
    },
    {
        path: "basic-directory-index/node_modules/dir-index-pkg/package.json",
        content: jsonText({
            name: "dir-index-pkg",
            version: "1.0.0",
            type: "module",
            main: "./lib"
        }),
    },
    {
        path: "basic-directory-index/node_modules/dir-index-pkg/lib/index.js",
        content: `export default "dir-index-pkg-via-directory";\n`,
    },
    {
        path: "basic-scoped/importer.js",
        content: `import { ping } from "@scope/util";\nexport const out = ping();\n`,
    },
    {
        path: "basic-scoped/node_modules/@scope/util/package.json",
        content: jsonText({
            name: "@scope/util",
            version: "1.0.0",
            type: "module",
            main: "./index.js"
        }),
    },
    {
        path: "basic-scoped/node_modules/@scope/util/index.js",
        content: `export function ping() {\n    return "scoped-ping";\n}\n`,
    },

    // ---- exports ----
    {
        path: "exports-string/importer.js",
        content: `import value from "exports-string-pkg";\nexport const result = value;\n`,
    },
    {
        path: "exports-string/node_modules/exports-string-pkg/package.json",
        content: jsonText({
            name: "exports-string-pkg",
            version: "1.0.0",
            type: "module",
            exports: "./entry.js"
        }),
    },
    {
        path: "exports-string/node_modules/exports-string-pkg/entry.js",
        content: `export default "exports-string-entry";\n`,
    },
    {
        path: "exports-object-default/importer.js",
        content: `import value from "exports-object-pkg";\nexport const result = value;\n`,
    },
    {
        path: "exports-object-default/node_modules/exports-object-pkg/package.json",
        content: jsonText({
            name: "exports-object-pkg",
            version: "1.0.0",
            type: "module",
            exports: { ".": "./entry.js" }
        }),
    },
    {
        path: "exports-object-default/node_modules/exports-object-pkg/entry.js",
        content: `export default "exports-object-default-entry";\n`,
    },
    {
        path: "exports-subpath/importer.js",
        content: `import value from "subpath-pkg/feature";\nexport const result = value;\n`,
    },
    {
        path: "exports-subpath/node_modules/subpath-pkg/package.json",
        content: jsonText({
            name: "subpath-pkg",
            version: "1.0.0",
            type: "module",
            exports: {
                ".": "./index.js",
                "./feature": "./feature.js"
            }
        }),
    },
    {
        path: "exports-subpath/node_modules/subpath-pkg/index.js",
        content: `export default "subpath-pkg-main";\n`,
    },
    {
        path: "exports-subpath/node_modules/subpath-pkg/feature.js",
        content: `export default "subpath-pkg-feature";\n`,
    },
    {
        path: "exports-subpath-extensionless/importer.js",
        content: `import value from "ext-pkg/feature";\nexport const result = value;\n`,
    },
    {
        path: "exports-subpath-extensionless/node_modules/ext-pkg/package.json",
        content: jsonText({
            name: "ext-pkg",
            version: "1.0.0",
            type: "module",
            exports: {
                ".": "./index.js",
                "./feature": "./feature"
            }
        }),
    },
    {
        path: "exports-subpath-extensionless/node_modules/ext-pkg/index.js",
        content: `export default "ext-pkg-main";\n`,
    },
    {
        path: "exports-subpath-extensionless/node_modules/ext-pkg/feature.js",
        content: `export default "ext-pkg-feature-extensionless";\n`,
    },
    {
        path: "exports-pattern/importer.js",
        content: `import a from "pattern-pkg/features/alpha";\nimport b from "pattern-pkg/features/beta";\nexport const out = [a, b];\n`,
    },
    {
        path: "exports-pattern/node_modules/pattern-pkg/package.json",
        content: jsonText({
            name: "pattern-pkg",
            version: "1.0.0",
            type: "module",
            exports: {
                ".": "./index.js",
                "./features/*": "./features/*.js"
            }
        }),
    },
    {
        path: "exports-pattern/node_modules/pattern-pkg/index.js",
        content: `export default "pattern-pkg-main";\n`,
    },
    {
        path: "exports-pattern/node_modules/pattern-pkg/features/alpha.js",
        content: `export default "pattern-pkg-alpha";\n`,
    },
    {
        path: "exports-pattern/node_modules/pattern-pkg/features/beta.js",
        content: `export default "pattern-pkg-beta";\n`,
    },
    {
        path: "exports-nested-conditions/importer.js",
        content: `import value from "nested-pkg";\nexport const result = value;\n`,
    },
    {
        path: "exports-nested-conditions/node_modules/nested-pkg/package.json",
        content: jsonText({
            name: "nested-pkg",
            version: "1.0.0",
            exports: {
                ".": {
                    import: { default: "./entry.mjs" },
                    require: "./entry.cjs"
                }
            }
        }),
    },
    {
        path: "exports-nested-conditions/node_modules/nested-pkg/entry.mjs",
        content: `export default "nested-pkg-esm";\n`,
    },
    {
        path: "exports-nested-conditions/node_modules/nested-pkg/entry.cjs",
        content: `module.exports = "nested-pkg-cjs";\n`,
    },
    {
        path: "exports-import-require/importer.js",
        content: `import value from "ir-pkg";\nexport const result = value;\n`,
    },
    {
        path: "exports-import-require/node_modules/ir-pkg/package.json",
        content: jsonText({
            name: "ir-pkg",
            version: "1.0.0",
            exports: {
                ".": {
                    import: "./entry.mjs",
                    require: "./entry.cjs",
                    default: "./entry.cjs"
                }
            }
        }),
    },
    {
        path: "exports-import-require/node_modules/ir-pkg/entry.mjs",
        content: `export default "ir-pkg-esm";\n`,
    },
    {
        path: "exports-import-require/node_modules/ir-pkg/entry.cjs",
        content: `module.exports = "ir-pkg-cjs";\n`,
    },
    {
        path: "exports-node-default/importer.js",
        content: `import value from "node-default-pkg";\nexport const result = value;\n`,
    },
    {
        path: "exports-node-default/node_modules/node-default-pkg/package.json",
        content: jsonText({
            name: "node-default-pkg",
            version: "1.0.0",
            exports: {
                ".": {
                    node: "./node-entry.js",
                    default: "./browser-entry.js"
                }
            }
        }),
    },
    {
        path: "exports-node-default/node_modules/node-default-pkg/node-entry.js",
        content: `export default "node-default-pkg-node";\n`,
    },
    {
        path: "exports-node-default/node_modules/node-default-pkg/browser-entry.js",
        content: `export default "node-default-pkg-browser";\n`,
    },
    {
        path: "exports-sloppy-condition/importer.js",
        content: `import value from "sloppy-cond-pkg";\nexport const result = value;\n`,
    },
    {
        path: "exports-sloppy-condition/node_modules/sloppy-cond-pkg/package.json",
        content: jsonText({
            name: "sloppy-cond-pkg",
            version: "1.0.0",
            exports: {
                ".": {
                    sloppy: "./sloppy-entry.js",
                    node: "./node-entry.js",
                    default: "./default-entry.js"
                }
            }
        }),
    },
    {
        path: "exports-sloppy-condition/node_modules/sloppy-cond-pkg/sloppy-entry.js",
        content: `export default "sloppy-cond-pkg-sloppy";\n`,
    },
    {
        path: "exports-sloppy-condition/node_modules/sloppy-cond-pkg/node-entry.js",
        content: `export default "sloppy-cond-pkg-node";\n`,
    },
    {
        path: "exports-sloppy-condition/node_modules/sloppy-cond-pkg/default-entry.js",
        content: `export default "sloppy-cond-pkg-default";\n`,
    },
    {
        path: "exports-unsupported-shape/importer.js",
        content: `import value from "array-exports-pkg";\nexport const result = value;\n`,
    },
    {
        path: "exports-unsupported-shape/node_modules/array-exports-pkg/package.json",
        content: jsonText({
            name: "array-exports-pkg",
            version: "1.0.0",
            exports: {
                ".": ["./entry-a.js", "./entry-b.js"]
            }
        }),
    },
    {
        path: "exports-unsupported-shape/node_modules/array-exports-pkg/entry-a.js",
        content: `export default "array-exports-pkg-a";\n`,
    },
    {
        path: "exports-unsupported-shape/node_modules/array-exports-pkg/entry-b.js",
        content: `export default "array-exports-pkg-b";\n`,
    },

    // ---- imports (#aliases) ----
    {
        path: "imports-alias/package.json",
        content: jsonText({
            name: "imports-alias-pkg",
            version: "1.0.0",
            type: "module",
            imports: {
                "#util": "./src/util.js"
            }
        }),
    },
    {
        path: "imports-alias/src/importer.js",
        content: `import util from "#util";\nexport const out = util();\n`,
    },
    {
        path: "imports-alias/src/util.js",
        content: `export default function util() {\n    return "imports-alias-util";\n}\n`,
    },
    {
        path: "imports-pattern/package.json",
        content: jsonText({
            name: "imports-pattern-pkg",
            version: "1.0.0",
            type: "module",
            imports: {
                "#lib/*": "./src/lib/*.js"
            }
        }),
    },
    {
        path: "imports-pattern/src/importer.js",
        content: `import a from "#lib/alpha";\nimport b from "#lib/beta";\nexport const out = [a, b];\n`,
    },
    {
        path: "imports-pattern/src/lib/alpha.js",
        content: `export default "imports-pattern-alpha";\n`,
    },
    {
        path: "imports-pattern/src/lib/beta.js",
        content: `export default "imports-pattern-beta";\n`,
    },
    {
        path: "imports-unsupported/package.json",
        content: jsonText({
            name: "imports-unsupported-pkg",
            version: "1.0.0",
            type: "module",
            imports: {
                "#missing": "./not-installed-target"
            }
        }),
    },
    {
        path: "imports-unsupported/src/importer.js",
        content: `import missing from "#missing";\nexport const out = missing;\n`,
    },

    // ---- self-reference ----
    {
        path: "self-reference/package.json",
        content: jsonText({
            name: "self-ref-pkg",
            version: "1.0.0",
            type: "module",
            exports: {
                ".": "./index.js",
                "./feature": "./feature.js"
            }
        }),
    },
    {
        path: "self-reference/index.js",
        content: `export const head = "self-ref-pkg-index";\n`,
    },
    {
        path: "self-reference/feature.js",
        content: `export const tail = "self-ref-pkg-feature";\n`,
    },
    {
        path: "self-reference/src/importer.js",
        content: `import { head } from "self-ref-pkg";\nimport { tail } from "self-ref-pkg/feature";\nexport const out = head + tail;\n`,
    },

    // ---- interop ----
    {
        path: "interop-cjs-requires-cjs/importer.js",
        content: `const value = require("cjs-deep-pkg");\nmodule.exports = value;\n`,
    },
    {
        path: "interop-cjs-requires-cjs/node_modules/cjs-deep-pkg/package.json",
        content: jsonText({
            name: "cjs-deep-pkg",
            version: "1.0.0",
            main: "./index.js"
        }),
    },
    {
        path: "interop-cjs-requires-cjs/node_modules/cjs-deep-pkg/index.js",
        content: `const inner = require("./inner");\nmodule.exports = "cjs-deep-pkg:" + inner;\n`,
    },
    {
        path: "interop-cjs-requires-cjs/node_modules/cjs-deep-pkg/inner.js",
        content: `module.exports = "inner-cjs-value";\n`,
    },
    {
        path: "interop-cjs-requires-json/importer.js",
        content: `const data = require("cjs-json-pkg");\nmodule.exports = data;\n`,
    },
    {
        path: "interop-cjs-requires-json/node_modules/cjs-json-pkg/package.json",
        content: jsonText({
            name: "cjs-json-pkg",
            version: "1.0.0",
            main: "./index.js"
        }),
    },
    {
        path: "interop-cjs-requires-json/node_modules/cjs-json-pkg/index.js",
        content: `const config = require("./config.json");\nmodule.exports = config;\n`,
    },
    {
        path: "interop-cjs-requires-json/node_modules/cjs-json-pkg/config.json",
        content: `{\n    "name": "cjs-json-pkg",\n    "version": 1\n}\n`,
    },
    {
        path: "interop-esm-imports-cjs/importer.js",
        content: `import cjs from "esm-of-cjs-pkg";\nexport const result = cjs.greet();\n`,
    },
    {
        path: "interop-esm-imports-cjs/node_modules/esm-of-cjs-pkg/package.json",
        content: jsonText({
            name: "esm-of-cjs-pkg",
            version: "1.0.0",
            main: "./index.cjs"
        }),
    },
    {
        path: "interop-esm-imports-cjs/node_modules/esm-of-cjs-pkg/index.cjs",
        content: `module.exports = {\n    greet() {\n        return "esm-imports-cjs-default";\n    }\n};\n`,
    },

    // ---- JSON ----
    {
        path: "json-require/importer.js",
        content: `const cfg = require("json-pkg/config");\nmodule.exports = cfg;\n`,
    },
    {
        path: "json-require/node_modules/json-pkg/package.json",
        content: jsonText({
            name: "json-pkg",
            version: "1.0.0",
            exports: {
                ".": "./index.js",
                "./config": "./config.json"
            }
        }),
    },
    {
        path: "json-require/node_modules/json-pkg/index.js",
        content: `module.exports = "json-pkg-main";\n`,
    },
    {
        path: "json-require/node_modules/json-pkg/config.json",
        content: `{\n    "feature": "json-require",\n    "enabled": true\n}\n`,
    },

    // ---- builtins ----
    {
        path: "builtins-fs-promises-buffer/importer.js",
        content: `import { read } from "buf-fs-pkg";\nexport const reader = read;\n`,
    },
    {
        path: "builtins-fs-promises-buffer/node_modules/buf-fs-pkg/package.json",
        content: jsonText({
            name: "buf-fs-pkg",
            version: "1.0.0",
            type: "module",
            main: "./index.js"
        }),
    },
    {
        path: "builtins-fs-promises-buffer/node_modules/buf-fs-pkg/index.js",
        content: `import { readFile } from "node:fs/promises";\nimport { Buffer } from "node:buffer";\nexport async function read(path) {\n    const data = await readFile(path);\n    return Buffer.isBuffer(data);\n}\n`,
    },

    // ---- assets ----
    {
        path: "assets-package-data/importer.js",
        content: `import { fingerprint } from "asset-pkg";\nexport const fp = fingerprint;\n`,
    },
    {
        path: "assets-package-data/node_modules/asset-pkg/package.json",
        content: jsonText({
            name: "asset-pkg",
            version: "1.0.0",
            type: "module",
            main: "./index.js"
        }),
    },
    {
        path: "assets-package-data/node_modules/asset-pkg/index.js",
        content: `import data from "./fingerprint.json" with { type: "json" };\nexport const fingerprint = data.fingerprint;\n`,
    },
    {
        path: "assets-package-data/node_modules/asset-pkg/fingerprint.json",
        content: `{\n    "fingerprint": "asset-pkg-fp"\n}\n`,
    },

    // ---- optional / native / unsupported ----
    {
        path: "optional-native-unused/importer.js",
        content: `import { add } from "opt-native-pkg";\nexport const sum = add(1, 2);\n`,
    },
    {
        path: "optional-native-unused/node_modules/opt-native-pkg/package.json",
        content: jsonText({
            name: "opt-native-pkg",
            version: "1.0.0",
            type: "module",
            main: "./index.js",
            optionalDependencies: {
                "fsevents": "^2.3.3"
            }
        }),
    },
    {
        path: "optional-native-unused/node_modules/opt-native-pkg/index.js",
        content: `export function add(a, b) {\n    return a + b;\n}\n`,
    },
    {
        path: "native-addon-used/importer.js",
        content: `import { run } from "native-pkg";\nexport const result = run;\n`,
    },
    {
        path: "native-addon-used/node_modules/native-pkg/package.json",
        content: jsonText({
            name: "native-pkg",
            version: "1.0.0",
            main: "./binding.node"
        }),
    },
    {
        path: "native-addon-used/node_modules/native-pkg/binding.node",
        content: "00 00 00 00\n",
    },
    {
        path: "unsupported-builtin-child_process/importer.js",
        content: `import shell from "shell-pkg";\nexport const exec = shell;\n`,
    },
    {
        path: "unsupported-builtin-child_process/node_modules/shell-pkg/package.json",
        content: jsonText({
            name: "shell-pkg",
            version: "1.0.0",
            type: "module",
            main: "./index.js"
        }),
    },
    {
        path: "unsupported-builtin-child_process/node_modules/shell-pkg/index.js",
        content: `import { spawn } from "node:child_process";\nexport default spawn;\n`,
    },

    // ---- dynamic ----
    {
        path: "dynamic-require-literal/importer.js",
        content: `const helper = require("dyn-pkg/helper");\nmodule.exports = helper;\n`,
    },
    {
        path: "dynamic-require-literal/node_modules/dyn-pkg/package.json",
        content: jsonText({
            name: "dyn-pkg",
            version: "1.0.0",
            exports: {
                "./helper": "./helper.js"
            }
        }),
    },
    {
        path: "dynamic-require-literal/node_modules/dyn-pkg/helper.js",
        content: `module.exports = "dyn-pkg-helper-literal";\n`,
    },
    {
        path: "dynamic-require-computed/importer.js",
        content: `function load(name) {\n    return require(name);\n}\nmodule.exports = load;\n`,
    },
    {
        path: "dynamic-require-computed/node_modules/dyn-c-pkg/package.json",
        content: jsonText({
            name: "dyn-c-pkg",
            version: "1.0.0",
            main: "./index.js"
        }),
    },
    {
        path: "dynamic-require-computed/node_modules/dyn-c-pkg/index.js",
        content: `module.exports = "dyn-c-pkg";\n`,
    },

    // ---- peer dependency metadata ----
    {
        path: "peer-dependency-meta/importer.js",
        content: `import { plug } from "peer-pkg";\nexport const wire = plug();\n`,
    },
    {
        path: "peer-dependency-meta/node_modules/peer-pkg/package.json",
        content: jsonText({
            name: "peer-pkg",
            version: "1.0.0",
            type: "module",
            main: "./index.js",
            peerDependencies: {
                "host-pkg": ">=1.0.0"
            },
            peerDependenciesMeta: {
                "host-pkg": { optional: true }
            }
        }),
    },
    {
        path: "peer-dependency-meta/node_modules/peer-pkg/index.js",
        content: `export function plug() {\n    return "peer-pkg-plug";\n}\n`,
    },
];

function jsonText(value) {
    return JSON.stringify(value, null, 4) + "\n";
}

let written = 0;
let skipped = 0;
for (const fixture of fixtures) {
    const fullPath = join(fixturesRoot, fixture.path);
    mkdirSync(dirname(fullPath), { recursive: true });
    let current = null;
    try {
        current = readFileSync(fullPath, "utf8");
    } catch (error) {
        if (error?.code !== "ENOENT") {
            throw error;
        }
    }
    if (current === fixture.content) {
        skipped++;
        continue;
    }
    writeFileSync(fullPath, fixture.content);
    written++;
}

console.log(`npm-compat fixtures: wrote ${written}, unchanged ${skipped}`);
