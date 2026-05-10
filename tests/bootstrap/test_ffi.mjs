import assert from "node:assert/strict";

import { t, unsafeFfi } from "../../stdlib/sloppy/ffi.js";

const calls = [];

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

const add = unsafeFfi.fn(t.i32, [t.i32, t.i32], { symbol: "sloppy_ffi_add_i32" });
assert.deepEqual(add, {
    kind: "ffi.fn",
    returnType: "i32",
    parameters: ["i32", "i32"],
    options: { symbol: "sloppy_ffi_add_i32" },
});

const native = unsafeFfi.library("ffi-test", { add }, { convention: "system" });
assert.equal(native.add(40, 2), 42);
assert.equal(calls[0].name, "ffi-test");
assert.equal(calls[0].functions.add, add);

const cell = unsafeFfi.ref(t.u32, 1);
assert.equal(cell.ptr, cell);
assert.equal(cell.value, 1);
cell.value = 7;
assert.equal(cell.value, 7);

const buffer = unsafeFfi.buffer(3);
assert.equal(buffer.ptr, buffer);
assert.equal(buffer.read().byteLength, 3);

assert.equal(unsafeFfi.cstringBuffer("abc").readString(), "abc");
assert.equal(unsafeFfi.utf16Buffer("abc").readString(), "abc");

const Point = unsafeFfi.struct("Point", { x: t.i32, y: t.i32 }, { pack: 4 });
const point = Point.alloc({ x: 19, y: 23 });
assert.equal(point.ptr, point);
assert.equal(point.x, 19);
point.y = 24;
assert.equal(point.y, 24);
