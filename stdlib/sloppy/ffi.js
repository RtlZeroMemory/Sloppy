const TYPE_NAMES = Object.freeze([
    "void",
    "bool",
    "i8",
    "u8",
    "i16",
    "u16",
    "i32",
    "u32",
    "i64",
    "u64",
    "isize",
    "usize",
    "f32",
    "f64",
    "ptr",
    "handle",
    "hwnd",
    "hmodule",
    "ntstatus",
    "cstring",
    "lpcstr",
    "utf16",
    "lpcwstr",
    "bytes",
    "mutBytes",
]);

export const t = Object.freeze(Object.fromEntries(TYPE_NAMES.map((name) => [name, Object.freeze({ kind: "ffi.type", name })])));

function ffiBridge(operation) {
    const bridge = globalThis.__sloppy?.ffi ?? null;
    if (bridge === null) {
        throw new Error(`SLOPPY_E_FFI_RUNTIME_UNAVAILABLE: runtime feature stdlib.ffi is inactive or unavailable

Feature:
  stdlib.ffi

Operation:
  ${operation}

Reason:
  The active Sloppy Plan did not enable the __sloppy.ffi V8 intrinsic namespace.`);
    }
    return bridge;
}

function typeName(value, operation) {
    if (value === undefined) {
        return undefined;
    }
    if (value === null || typeof value !== "object" || value.kind !== "ffi.type" || typeof value.name !== "string") {
        throw new TypeError(`${operation} requires FFI types from sloppy/ffi t.`);
    }
    return value.name;
}

function fn(returnType, parameters, options = undefined) {
    if (!Array.isArray(parameters)) {
        throw new TypeError("unsafeFfi.fn parameters must be an array of FFI types.");
    }
    const descriptor = {
        kind: "ffi.fn",
        returnType: typeName(returnType, "unsafeFfi.fn"),
        parameters: parameters.map((parameter) => typeName(parameter, "unsafeFfi.fn")),
        options: options === undefined ? Object.freeze({}) : Object.freeze({ ...options }),
    };
    return Object.freeze(descriptor);
}

function library(name, functions, options = undefined) {
    if (typeof name !== "string" || name.length === 0) {
        throw new TypeError("unsafeFfi.library name must be a non-empty string.");
    }
    if (functions === null || typeof functions !== "object" || Array.isArray(functions)) {
        throw new TypeError("unsafeFfi.library functions must be an object.");
    }
    return ffiBridge("unsafeFfi.library").library(name, functions, options ?? {});
}

function ref(type, initialValue = undefined) {
    const cell = ffiBridge("unsafeFfi.ref").ref(typeName(type, "unsafeFfi.ref"), initialValue);
    Object.defineProperty(cell, "value", {
        enumerable: true,
        get() {
            return cell.get();
        },
        set(value) {
            cell.set(value);
        },
    });
    Object.defineProperty(cell, "ptr", {
        enumerable: true,
        value: cell,
    });
    return cell;
}

function buffer(byteLength) {
    const bytes = ffiBridge("unsafeFfi.buffer").buffer(byteLength);
    Object.defineProperty(bytes, "ptr", {
        enumerable: true,
        value: bytes,
    });
    return bytes;
}

function cstringBuffer(valueOrByteLength) {
    const cstring = ffiBridge("unsafeFfi.cstringBuffer").cstringBuffer(valueOrByteLength);
    Object.defineProperty(cstring, "ptr", {
        enumerable: true,
        value: cstring,
    });
    return cstring;
}

function utf16Buffer(valueOrCodeUnits) {
    const utf16 = ffiBridge("unsafeFfi.utf16Buffer").utf16Buffer(valueOrCodeUnits);
    Object.defineProperty(utf16, "ptr", {
        enumerable: true,
        value: utf16,
    });
    return utf16;
}

function struct(name, fields, options = undefined) {
    if (typeof name !== "string" || name.length === 0) {
        throw new TypeError("unsafeFfi.struct name must be a non-empty string.");
    }
    if (fields === null || typeof fields !== "object" || Array.isArray(fields)) {
        throw new TypeError("unsafeFfi.struct fields must be an object.");
    }
    const normalized = Object.fromEntries(
        Object.entries(fields).map(([field, fieldType]) => [field, typeName(fieldType, "unsafeFfi.struct")]),
    );
    const layout = ffiBridge("unsafeFfi.struct").struct(name, normalized, options ?? {});
    const alloc = layout.alloc.bind(layout);
    Object.defineProperty(layout, "alloc", {
        enumerable: true,
        value(initial = undefined) {
            const instance = alloc(initial);
            Object.defineProperty(instance, "ptr", {
                enumerable: true,
                value: instance,
            });
            for (const field of Object.keys(normalized)) {
                Object.defineProperty(instance, field, {
                    enumerable: true,
                    get() {
                        return instance.get(field);
                    },
                    set(value) {
                        instance.set(field, value);
                    },
                });
            }
            return instance;
        },
    });
    return layout;
}

export const unsafeFfi = Object.freeze({
    library,
    fn,
    ref,
    buffer,
    cstringBuffer,
    utf16Buffer,
    struct,
});
