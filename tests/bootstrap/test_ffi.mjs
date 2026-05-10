import assert from "node:assert/strict";

import { t, unsafeFfi } from "../../stdlib/sloppy/ffi.js";

const calls = [];
const previousSloppy = globalThis.__sloppy;

try {
globalThis.__sloppy = {
    ffi: {
        library(name, functions, options) {
            calls.push({ name, functions, options });
            return Object.freeze({
                add(left, right) {
                    return left + right;
                },
            });
        },
        ref(type, initialValue) {
            let value = initialValue;
            return {
                byteLength: 4,
                get() {
                    return value;
                },
                set(next) {
                    value = next;
                    return this;
                },
                dispose() {},
            };
        },
        buffer(byteLength) {
            return {
                byteLength,
                read() {
                    return new Uint8Array(byteLength);
                },
                write() {
                    return this;
                },
                dispose() {},
            };
        },
        cstringBuffer(valueOrByteLength) {
            return {
                byteLength: typeof valueOrByteLength === "string" ? valueOrByteLength.length + 1 : valueOrByteLength,
                readString() {
                    return typeof valueOrByteLength === "string" ? valueOrByteLength : "";
                },
                writeString() {
                    return this;
                },
                dispose() {},
            };
        },
        utf16Buffer(valueOrCodeUnits) {
            return {
                byteLength: (typeof valueOrCodeUnits === "string" ? valueOrCodeUnits.length + 1 : valueOrCodeUnits) * 2,
                readString() {
                    return typeof valueOrCodeUnits === "string" ? valueOrCodeUnits : "";
                },
                writeString() {
                    return this;
                },
                dispose() {},
            };
        },
        struct(name, fields, options) {
            calls.push({ name, fields, options });
            return {
                alloc(initial = {}) {
                    const values = { ...initial };
                    return {
                        byteLength: 8,
                        get(field) {
                            return values[field];
                        },
                        set(field, value) {
                            values[field] = value;
                            return this;
                        },
                        dispose() {},
                    };
                },
            };
        },
    },
};

assert.equal(t.i32.kind, "ffi.type");
assert.equal(t.ntstatus.name, "ntstatus");
assert.equal(t.bool.name, "bool");
assert.equal(t.bool32.name, "bool32");

const add = unsafeFfi.fn(t.i32, [t.i32, t.i32], { symbol: "sloppy_ffi_add_i32" });
assert.equal(add.kind, "ffi.fn");
assert.equal(add.returnType, "i32");
assert.deepEqual(add.parameters, ["i32", "i32"]);
assert.equal(add.options?.symbol, "sloppy_ffi_add_i32");

const native = unsafeFfi.library("ffi-test", { add }, { convention: "system" });
assert.equal(native.add(40, 2), 42);
const ffiTestCall = calls.find((entry) => entry.name === "ffi-test");
assert.ok(ffiTestCall, "ffi-test library should be registered");
assert.equal(ffiTestCall.functions.add, add);

const cell = unsafeFfi.ref(t.u32, 1);
assert.equal(cell.ptr, cell);
assert.equal(Object.prototype.propertyIsEnumerable.call(cell, "ptr"), false);
assert.equal(cell.value, 1);
cell.value = 7;
assert.equal(cell.value, 7);

const buffer = unsafeFfi.buffer(3);
assert.equal(buffer.ptr, buffer);
assert.equal(Object.prototype.propertyIsEnumerable.call(buffer, "ptr"), false);
assert.equal(buffer.read().byteLength, 3);

assert.equal(unsafeFfi.cstringBuffer("abc").readString(), "abc");
assert.equal(unsafeFfi.utf16Buffer("abc").readString(), "abc");
assert.throws(() => unsafeFfi.fn({ kind: "ffi.type", name: "i32" }, []), /requires FFI types/);
assert.throws(() => unsafeFfi.struct("Bad", { ptr: t.i32 }), /field 'ptr' is reserved/);
assert.throws(() => unsafeFfi.struct("Bad", { get: t.i32 }), /field 'get' is reserved/);
assert.throws(() => unsafeFfi.struct("Bad", { set: t.i32 }), /field 'set' is reserved/);

const Point = unsafeFfi.struct("Point", { x: t.i32, y: t.i32 }, { pack: 4 });
const pointStructCall = calls.find((entry) => entry.name === "Point");
assert.ok(pointStructCall, "Point struct should be registered");
assert.deepEqual(pointStructCall.fields, { x: "i32", y: "i32" });
assert.deepEqual(pointStructCall.options, { pack: 4 });
const point = Point.alloc({ x: 19, y: 23 });
assert.equal(point.ptr, point);
assert.equal(Object.prototype.propertyIsEnumerable.call(point, "ptr"), false);
assert.equal(point.x, 19);
point.y = 24;
assert.equal(point.y, 24);
} finally {
    if (previousSloppy === undefined) {
        delete globalThis.__sloppy;
    } else {
        globalThis.__sloppy = previousSloppy;
    }
}
