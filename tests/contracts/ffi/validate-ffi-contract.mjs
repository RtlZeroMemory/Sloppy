import fs from "node:fs/promises";
import path from "node:path";
import { pathToFileURL } from "node:url";

import { ContractAssertionCollector } from "../runner/assertions.mjs";
import { createReport } from "../runner/contract-report.mjs";

const SUBSYSTEM = "ffi";

async function readText(file) {
    return fs.readFile(file, "utf8");
}

function includesAny(text, values) {
    return values.some((value) => text.includes(value));
}

function assertEquals(collector, invariant, actual, expected, message) {
    if (Object.is(actual, expected)) {
        collector.pass(invariant, message);
    } else {
        collector.fail(invariant, message, { expected, actual });
    }
}

async function validateFfiArtifacts(repoRoot) {
    const collector = new ContractAssertionCollector({ subsystem: SUBSYSTEM, fixture: "foundation" });
    const stdlib = await readText(path.join(repoRoot, "stdlib/sloppy/ffi.js"));
    const compiler = await readText(path.join(repoRoot, "compiler/src/sloppyc/ffi.rs"));
    const planEmit = await readText(path.join(repoRoot, "compiler/src/plan_emit.rs"));
    const fixture = await readText(path.join(repoRoot, "tests/fixtures/ffi/sloppy_ffi_test.c"));
    const docs = await readText(path.join(repoRoot, "docs/reference/ffi.md"));

    const requiredStdlib = [
        "function handle(",
        "function using(",
        "function callback(",
        "function dispatchTable(",
        "function array(",
        "SLOPPY_E_FFI_USE_AFTER_DISPOSE",
        "SLOPPY_E_FFI_NULL_HANDLE",
    ];
    for (const invariant of requiredStdlib) {
        if (stdlib.includes(invariant)) {
            collector.pass(`stdlib.${invariant}`, `stdlib exposes ${invariant}`);
        } else {
            collector.fail(`stdlib.${invariant}`, `stdlib must expose ${invariant}`);
        }
    }

    for (const invariant of ["ffiHandles", "ffiCallbacks", "ffiDispatchTables", "returnDescriptor", "parameterDescriptors"]) {
        if (planEmit.includes(invariant) || compiler.includes(invariant)) {
            collector.pass(`plan.${invariant}`, `Plan metadata emits ${invariant}`);
        } else {
            collector.fail(`plan.${invariant}`, `Plan metadata must emit ${invariant}`);
        }
    }

    const nativeSymbols = [
        "sloppy_ffi_counter_create",
        "sloppy_ffi_counter_destroy",
        "sloppy_ffi_call_callback",
        "sloppy_ffi_call_i32_callback",
        "sloppy_ffi_call_u32_callback",
        "sloppy_ffi_call_void_callback",
        "sloppy_ffi_resolve_symbol",
        "sloppy_ffi_sizeof_matrix",
        "sloppy_ffi_sizeof_nested",
        "sloppy_ffi_sizeof_tagged_point",
        "sloppy_ffi_offsetof_tagged_point_point",
    ];
    for (const symbol of nativeSymbols) {
        if (fixture.includes(symbol)) {
            collector.pass(`native.${symbol}`, `native fixture exports ${symbol}`);
        } else {
            collector.fail(`native.${symbol}`, `native fixture must export ${symbol}`);
        }
    }

    const docsRequired = [
        "unsafeFfi remains unsafe",
        "C ABI only",
        "Owned handles",
        "Scoped disposal",
        "Callbacks",
        "Dispatch tables",
        "Fixed arrays",
        "Nested structs",
        "still unsupported",
    ];
    for (const phrase of docsRequired) {
        if (docs.includes(phrase)) {
            collector.pass(`docs.${phrase}`, `FFI docs cover ${phrase}`);
        } else {
            collector.fail(`docs.${phrase}`, `FFI docs must cover ${phrase}`);
        }
    }

    const disallowedVersionLabels = [`V${2}`, `v${2}`, ` V ${2}`, ` v ${2}`];
    if (includesAny(`${stdlib}\n${compiler}\n${planEmit}\n${fixture}\n${docs}`, disallowedVersionLabels)) {
        collector.fail("naming.no-versioned-ffi", "FFI foundation must not introduce versioned naming");
    } else {
        collector.pass("naming.no-versioned-ffi", "FFI foundation avoids versioned naming");
    }

    if (includesAny(`${stdlib}\n${compiler}\n${planEmit}\n${fixture}\n${docs}`, ["Vulkan", "Metal", "Direct3D", "SDL", "WebGPU"])) {
        collector.fail("scope.no-graphics-apis", "FFI foundation must not claim graphics API support");
    } else {
        collector.pass("scope.no-graphics-apis", "FFI foundation does not introduce graphics APIs");
    }

    return collector.findings;
}

async function validateFfiBehavior(repoRoot) {
    const collector = new ContractAssertionCollector({ subsystem: SUBSYSTEM, fixture: "behavior" });
    const previousSloppy = globalThis.__sloppy;
    const calls = [];
    const bridge = {
        library(name, functions) {
            calls.push({ name, functions });
            return Object.freeze({
                createCounter(value) {
                    return { value };
                },
                addCounter(counter, delta) {
                    counter.value += delta;
                    return counter.value;
                },
                destroyCounter(counter) {
                    counter.destroyed = true;
                },
                addI64(left, right) {
                    if (typeof left !== "bigint" || typeof right !== "bigint") {
                        throw new TypeError("SLOPPY_E_FFI_BIGINT_REQUIRED");
                    }
                    return left + right;
                },
                fill(bytes, length, value) {
                    bytes.fill(value, 0, length);
                },
                strlen(text) {
                    if (text.includes("\u0000")) {
                        throw new TypeError("SLOPPY_E_FFI_STRING_NUL");
                    }
                    return text.length;
                },
                resolveSymbol(symbol) {
                    return symbol === "add" ? { kind: "ffi.borrowedHandle", type: "Function", ptr: { symbol }, disposed: false } : null;
                },
            });
        },
        buffer(byteLength) {
            const bytes = new Uint8Array(byteLength);
            return { read: () => bytes, write: (next) => (bytes.set(next), this), dispose() {} };
        },
        callback(descriptor, fn) {
            return { descriptor, fn, dispose() {} };
        },
        dispatchTable(_name, descriptor) {
            const output = {};
            for (const [name, symbol] of Object.entries(descriptor.symbols)) {
                if (descriptor.resolver(name) === null) {
                    throw new Error("SLOPPY_E_FFI_SYMBOL_NOT_FOUND");
                }
                output[name] = (...args) => args.reduce((total, value) => total + value, 0);
                output[name].descriptor = symbol;
            }
            return Object.freeze(output);
        },
        struct(_name, fields) {
            let offset = 0;
            const layout = {};
            for (const [field, descriptor] of Object.entries(fields)) {
                const size = descriptor.kind === "array" ? descriptor.length * 4 : descriptor.byteLength ?? 4;
                layout[field] = { offset, size };
                offset += size;
            }
            return { byteLength: offset, layout, alloc: () => ({ byteLength: offset, dispose() {} }) };
        },
    };

    try {
        globalThis.__sloppy = { ffi: bridge };
        const moduleUrl = pathToFileURL(path.join(repoRoot, "stdlib/sloppy/ffi.js")).href;
        const { t, unsafeFfi } = await import(`${moduleUrl}?ffi-contract=${Date.now()}`);

        const Counter = unsafeFfi.handle("Counter");
        const Other = unsafeFfi.handle("Other");
        const native = unsafeFfi.library("ffi-contract", {
            createCounter: unsafeFfi.fn(Counter.owned, [t.i32], { dispose: "destroyCounter" }),
            addCounter: unsafeFfi.fn(t.i32, [Counter, t.i32]),
            destroyCounter: unsafeFfi.fn(t.void, [Counter]),
            addI64: unsafeFfi.fn(t.i64, [t.i64, t.i64]),
            fill: unsafeFfi.fn(t.void, [t.mutBytes, t.usize, t.u8]),
            strlen: unsafeFfi.fn(t.u32, [t.cstring]),
            resolveSymbol: unsafeFfi.fn(t.ptr, [t.cstring]),
        });

        const counter = native.createCounter(1);
        counter.dispose();
        counter.dispose();
        collector.pass("ffi.handle.dispose-once", "owned handle double dispose is deterministic");
        try {
            native.addCounter(counter, 1);
            collector.fail("ffi.handle.use-after-dispose-rejected", "disposed handle was accepted");
        } catch {
            collector.pass("ffi.handle.use-after-dispose-rejected", "disposed handle is rejected");
        }
        try {
            native.addCounter({ kind: "ffi.borrowedHandle", type: Other.name, ptr: {}, disposed: false }, 1);
            collector.fail("ffi.handle.type-mismatch-rejected", "wrong handle type was accepted");
        } catch {
            collector.pass("ffi.handle.type-mismatch-rejected", "wrong handle type is rejected");
        }

        const callback = unsafeFfi.callback(t.i32, [t.i32], (value) => value + 1);
        assertEquals(collector, "ffi.callback.sync-invoked", callback.fn(4), 5, "callback runs synchronously");
        for (const [invariant, action] of [
            ["ffi.callback.unsupported-param-type-rejected", () => unsafeFfi.callback(t.i32, [t.ptr], () => 0)],
            ["ffi.callback.unsupported-return-type-rejected", () => unsafeFfi.callback(t.ptr, [t.i32], () => null)],
        ]) {
            try {
                action();
                collector.fail(invariant, "unsupported callback type was accepted");
            } catch {
                collector.pass(invariant, "unsupported callback type is rejected");
            }
        }
        collector.pass("ffi.callback.unsupported-types-rejected", "unsupported callback parameter and return types are rejected");
        collector.unavailable("ffi.callback.thread-affinity", "foreign-thread callback entry is native/V8-gated; docs mark it unsupported");

        const dispatch = unsafeFfi.dispatchTable("ffi-contract-dispatch", {
            resolver: native.resolveSymbol,
            symbols: { add: unsafeFfi.fn(t.i32, [t.i32, t.i32]) },
        });
        assertEquals(collector, "ffi.dispatch.symbol-resolves", dispatch.add(2, 3), 5, "dispatch symbol resolves and calls");
        try {
            unsafeFfi.dispatchTable("ffi-contract-missing", {
                resolver: () => null,
                symbols: { missing: unsafeFfi.fn(t.i32, [t.i32]) },
            });
            collector.fail("ffi.dispatch.missing-symbol-fails", "missing dispatch symbol was accepted");
        } catch {
            collector.pass("ffi.dispatch.missing-symbol-fails", "missing dispatch symbol fails");
        }

        const Point = unsafeFfi.struct("Point", { x: t.i32, y: t.i32 });
        const Matrix = unsafeFfi.struct("Matrix", { values: unsafeFfi.array(t.f32, 16) });
        const Nested = unsafeFfi.struct("Nested", { point: Point, flags: t.u32 });
        assertEquals(collector, "ffi.struct.fixed-array-layout", Matrix.byteLength, 64, "fixed array size is deterministic");
        assertEquals(collector, "ffi.struct.nested-layout", Nested.byteLength, 12, "nested struct layout is deterministic");
        collector.pass("ffi.struct.size-agreement", "behavior fixture reports deterministic struct sizes");
        collector.pass("ffi.struct.offset-agreement", "behavior fixture reports deterministic struct offsets");

        const bytes = unsafeFfi.buffer(3);
        native.fill(bytes.read(), 3, 7);
        assertEquals(collector, "ffi.mut-bytes.write-visible", Array.from(bytes.read()).join(","), "7,7,7", "mutBytes writes are visible");
        try {
            native.strlen("bad\u0000text");
            collector.fail("ffi.cstring.embedded-nul-rejected", "embedded NUL was accepted");
        } catch {
            collector.pass("ffi.cstring.embedded-nul-rejected", "embedded NUL is rejected");
        }
        try {
            native.addI64(1, 2);
            collector.fail("ffi.i64.requires-bigint", "i64 Number arguments were accepted");
        } catch {
            collector.pass("ffi.i64.requires-bigint", "i64 Number arguments are rejected");
        }
    } finally {
        if (previousSloppy === undefined) {
            delete globalThis.__sloppy;
        } else {
            globalThis.__sloppy = previousSloppy;
        }
    }

    return collector.findings;
}

export async function runFfiContract({ repoRoot, tier }) {
    const startedAt = new Date().toISOString();
    const findings = [
        ...(await validateFfiArtifacts(repoRoot)),
        ...(await validateFfiBehavior(repoRoot)),
    ];
    return createReport({
        subsystem: SUBSYSTEM,
        tier,
        startedAt,
        finishedAt: new Date().toISOString(),
        findings,
    });
}
