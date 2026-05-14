const TYPE_NAMES = Object.freeze([
    "void",
    "bool",
    "bool32",
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

const STRUCT_RESERVED_FIELDS = new Set(["ptr", "get", "set"]);
const HANDLE_RESERVED_NAMES = new Set(["", "ptr"]);

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

function typeDescriptor(value, operation) {
    if (value === undefined) {
        return undefined;
    }
    if (value !== null && typeof value === "object") {
        if (value.kind === "ffi.handle" && typeof value.name === "string") {
            return { kind: "handle", name: value.name, type: "ptr", owned: false };
        }
        if (value.kind === "ffi.handle.owned" && typeof value.name === "string") {
            return { kind: "handle", name: value.name, type: "ptr", owned: true };
        }
        if (value.kind === "ffi.callback.type") {
            return {
                kind: "callback",
                returnType: typeName(value.returnType, operation),
                parameters: value.parameters.map((parameter) => typeName(parameter, operation)),
                type: "ptr",
            };
        }
    }
    return { kind: "type", type: typeName(value, operation) };
}

function typeName(value, operation) {
    const descriptor =
        value !== undefined && value !== null && typeof value === "object" && value.kind !== "ffi.type"
            ? typeDescriptor(value, operation)
            : undefined;
    if (descriptor !== undefined) {
        return descriptor.type;
    }
    if (
        value === null ||
        typeof value !== "object" ||
        value.kind !== "ffi.type" ||
        typeof value.name !== "string" ||
        t[value.name] !== value
    ) {
        throw new TypeError(`${operation} requires FFI types from sloppy/ffi t.`);
    }
    return value.name;
}

function fn(returnType, parameters, options = undefined) {
    if (!Array.isArray(parameters)) {
        throw new TypeError("unsafeFfi.fn parameters must be an array of FFI types.");
    }
    const normalizedReturn = typeDescriptor(returnType, "unsafeFfi.fn");
    const normalizedParameters = parameters.map((parameter) => typeDescriptor(parameter, "unsafeFfi.fn"));
    const descriptor = {
        kind: "ffi.fn",
        returnType: normalizedReturn.type,
        parameters: normalizedParameters.map((parameter) => parameter.type),
        options: options === undefined ? Object.freeze({}) : Object.freeze({ ...options }),
    };
    if (normalizedReturn.kind !== "type") {
        descriptor.returnDescriptor = Object.freeze(normalizedReturn);
    }
    if (normalizedParameters.some((parameter) => parameter.kind !== "type")) {
        descriptor.parameterDescriptors = Object.freeze(normalizedParameters.map((parameter) => Object.freeze(parameter)));
    }
    return Object.freeze(descriptor);
}

function library(name, functions, options = undefined) {
    if (typeof name !== "string" || name.length === 0) {
        throw new TypeError("unsafeFfi.library name must be a non-empty string.");
    }
    if (functions === null || typeof functions !== "object" || Array.isArray(functions)) {
        throw new TypeError("unsafeFfi.library functions must be an object.");
    }
    const bound = ffiBridge("unsafeFfi.library").library(name, functions, options ?? {});
    return wrapLibrary(bound, functions);
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
        enumerable: false,
        value: cell,
    });
    return cell;
}

function buffer(byteLength) {
    const bytes = ffiBridge("unsafeFfi.buffer").buffer(byteLength);
    Object.defineProperty(bytes, "ptr", {
        enumerable: false,
        value: bytes,
    });
    return bytes;
}

function cstringBuffer(valueOrByteLength) {
    const cstring = ffiBridge("unsafeFfi.cstringBuffer").cstringBuffer(valueOrByteLength);
    Object.defineProperty(cstring, "ptr", {
        enumerable: false,
        value: cstring,
    });
    return cstring;
}

function utf16Buffer(valueOrCodeUnits) {
    const utf16 = ffiBridge("unsafeFfi.utf16Buffer").utf16Buffer(valueOrCodeUnits);
    Object.defineProperty(utf16, "ptr", {
        enumerable: false,
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
        Object.entries(fields).map(([field, fieldType]) => {
            if (STRUCT_RESERVED_FIELDS.has(field)) {
                throw new TypeError(`unsafeFfi.struct field '${field}' is reserved.`);
            }
            return [field, normalizeStructField(fieldType)];
        }),
    );
    const layout = ffiBridge("unsafeFfi.struct").struct(name, normalized, options ?? {});
    Object.defineProperty(layout, "kind", {
        enumerable: true,
        value: "ffi.struct.layout",
    });
    Object.defineProperty(layout, "name", {
        enumerable: true,
        value: name,
    });
    const alloc = layout.alloc.bind(layout);
    Object.defineProperty(layout, "alloc", {
        enumerable: true,
        value(initial = undefined) {
            const instance = alloc(initial);
            Object.defineProperty(instance, "ptr", {
                enumerable: false,
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

function normalizeStructField(fieldType) {
    if (fieldType !== null && typeof fieldType === "object") {
        if (fieldType.kind === "ffi.array") {
            return Object.freeze({
                kind: "array",
                element: typeName(fieldType.element, "unsafeFfi.array"),
                length: fieldType.length,
            });
        }
        if (fieldType.kind === "ffi.struct.layout" && typeof fieldType.name === "string") {
            return fieldType;
        }
    }
    return typeName(fieldType, "unsafeFfi.struct");
}

function array(element, length) {
    if (!Number.isInteger(length) || length <= 0) {
        throw new TypeError("unsafeFfi.array length must be a positive integer.");
    }
    return Object.freeze({
        kind: "ffi.array",
        element,
        length,
    });
}

function handle(name) {
    if (typeof name !== "string" || HANDLE_RESERVED_NAMES.has(name)) {
        throw new TypeError("unsafeFfi.handle name must be a non-empty string.");
    }
    const descriptor = {
        kind: "ffi.handle",
        name,
    };
    Object.defineProperty(descriptor, "owned", {
        enumerable: true,
        value: Object.freeze({
            kind: "ffi.handle.owned",
            name,
        }),
    });
    return Object.freeze(descriptor);
}

function wrapLibrary(bound, descriptors) {
    const wrapped = {};
    for (const [name, callable] of Object.entries(bound)) {
        const descriptor = descriptors[name];
        if (typeof callable !== "function" || descriptor?.kind !== "ffi.fn") {
            wrapped[name] = callable;
            continue;
        }
        wrapped[name] = function (...args) {
            const translated = translateHandleArguments(descriptor, args);
            const result = callable(...translated);
            return wrapOwnedHandleResult(result, descriptor, wrapped);
        };
    }
    return Object.freeze(wrapped);
}

function translateHandleArguments(descriptor, args) {
    const parameterDescriptors = descriptor.parameterDescriptors ?? [];
    return args.map((arg, index) => {
        const parameter = parameterDescriptors[index];
        if (parameter?.kind !== "handle") {
            return arg;
        }
        if (arg === null) {
            throw new TypeError("SLOPPY_E_FFI_NULL_HANDLE: FFI handle argument cannot be null.");
        }
        if (arg?.kind !== "ffi.ownedHandle" && arg?.kind !== "ffi.borrowedHandle") {
            throw new TypeError("SLOPPY_E_FFI_INVALID_HANDLE: FFI handle argument is not a typed handle.");
        }
        if (arg.disposed) {
            throw new TypeError("SLOPPY_E_FFI_USE_AFTER_DISPOSE: FFI handle is disposed.");
        }
        if (arg.type !== parameter.name) {
            throw new TypeError("SLOPPY_E_FFI_HANDLE_TYPE_MISMATCH: FFI handle argument type does not match parameter.");
        }
        return arg.ptr;
    });
}

function wrapOwnedHandleResult(result, descriptor, libraryObject) {
    const returnDescriptor = descriptor.returnDescriptor;
    if (returnDescriptor?.kind !== "handle" || !returnDescriptor.owned) {
        return result;
    }
    if (result === null) {
        throw new Error("SLOPPY_E_FFI_NULL_HANDLE: owned FFI handle factory returned null.");
    }
    const disposeName = descriptor.options?.dispose;
    if (typeof disposeName !== "string" || typeof libraryObject[disposeName] !== "function") {
        throw new Error("SLOPPY_E_FFI_MISSING_DISPOSER: owned FFI handle requires a static disposer.");
    }
    let disposed = false;
    const handleObject = {
        kind: "ffi.ownedHandle",
        type: returnDescriptor.name,
        get disposed() {
            return disposed;
        },
        get ptr() {
            if (disposed) {
                throw new TypeError("SLOPPY_E_FFI_USE_AFTER_DISPOSE: FFI handle is disposed.");
            }
            return result;
        },
        dispose() {
            if (disposed) {
                return undefined;
            }
            disposed = true;
            libraryObject[disposeName]({ kind: "ffi.borrowedHandle", type: returnDescriptor.name, ptr: result, disposed: false });
            return undefined;
        },
    };
    return Object.freeze(handleObject);
}

function using(resource, callback) {
    if (resource === null || typeof resource !== "object" || typeof resource.dispose !== "function") {
        throw new TypeError("unsafeFfi.using requires a disposable FFI resource.");
    }
    if (typeof callback !== "function") {
        throw new TypeError("unsafeFfi.using requires a callback.");
    }
    let result;
    try {
        result = callback(resource);
    } catch (error) {
        resource.dispose();
        throw error;
    }
    if (result !== null && typeof result === "object" && typeof result.then === "function") {
        return result.finally(() => resource.dispose());
    }
    resource.dispose();
    return result;
}

function callbackType(returnType, parameters) {
    if (!Array.isArray(parameters)) {
        throw new TypeError("unsafeFfi.callbackType parameters must be an array.");
    }
    const returnName = typeName(returnType, "unsafeFfi.callbackType");
    if (!["void", "i32", "u32"].includes(returnName)) {
        throw new TypeError("SLOPPY_E_FFI_UNSUPPORTED_CALLBACK: callback return type unsupported.");
    }
    const parameterNames = parameters.map((parameter) => typeName(parameter, "unsafeFfi.callbackType"));
    if (parameterNames.some((parameter) => parameter !== "i32" && parameter !== "u32")) {
        throw new TypeError("SLOPPY_E_FFI_UNSUPPORTED_CALLBACK: callback parameter type unsupported.");
    }
    return Object.freeze({
        kind: "ffi.callback.type",
        returnType,
        parameters,
    });
}

function callback(returnTypeOrDescriptor, parameters, fnValue, options = undefined) {
    let descriptor;
    if (returnTypeOrDescriptor !== null && typeof returnTypeOrDescriptor === "object" && !Array.isArray(returnTypeOrDescriptor) && returnTypeOrDescriptor.returns !== undefined) {
        descriptor = returnTypeOrDescriptor;
    } else {
        descriptor = { returns: returnTypeOrDescriptor, parameters, fn: fnValue, ...(options ?? {}) };
    }
    if (typeof descriptor.fn !== "function") {
        throw new TypeError("unsafeFfi.callback requires a JavaScript function.");
    }
    const callbackDescriptor = {
        returnType: typeName(descriptor.returns, "unsafeFfi.callback"),
        parameters: descriptor.parameters.map((parameter) => typeName(parameter, "unsafeFfi.callback")),
        thread: descriptor.thread ?? "runtime",
    };
    if (!["void", "i32", "u32"].includes(callbackDescriptor.returnType)) {
        throw new TypeError("SLOPPY_E_FFI_UNSUPPORTED_CALLBACK: callback return type unsupported.");
    }
    for (const parameter of callbackDescriptor.parameters) {
        if (parameter !== "i32" && parameter !== "u32") {
            throw new TypeError("SLOPPY_E_FFI_UNSUPPORTED_CALLBACK: callback parameter type unsupported.");
        }
    }
    return ffiBridge("unsafeFfi.callback").callback(callbackDescriptor, descriptor.fn);
}

function dispatchTable(name, descriptor) {
    if (typeof name !== "string" || name.length === 0) {
        throw new TypeError("unsafeFfi.dispatchTable name must be a non-empty string.");
    }
    if (descriptor === null || typeof descriptor !== "object" || typeof descriptor.resolver !== "function") {
        throw new TypeError("unsafeFfi.dispatchTable requires a typed resolver function.");
    }
    if (descriptor.symbols === null || typeof descriptor.symbols !== "object" || Array.isArray(descriptor.symbols)) {
        throw new TypeError("unsafeFfi.dispatchTable symbols must be an object.");
    }
    return ffiBridge("unsafeFfi.dispatchTable").dispatchTable(name, descriptor);
}

export const unsafeFfi = Object.freeze({
    library,
    fn,
    handle,
    array,
    ref,
    buffer,
    cstringBuffer,
    utf16Buffer,
    struct,
    callback,
    callbackType,
    dispatchTable,
    using,
});
