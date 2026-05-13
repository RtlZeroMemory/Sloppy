export function copyArrayBuffer(value) {
    if (value instanceof ArrayBuffer) {
        return value.slice(0);
    }
    if (ArrayBuffer.isView(value)) {
        return value.buffer.slice(value.byteOffset, value.byteOffset + value.byteLength);
    }
    return undefined;
}

export function copyUint8Array(value) {
    if (value instanceof Uint8Array) {
        return new Uint8Array(value);
    }
    const buffer = copyArrayBuffer(value);
    return buffer === undefined ? undefined : new Uint8Array(buffer);
}

export function copyBinaryLike(value) {
    if (value instanceof ArrayBuffer) {
        return value.slice(0);
    }
    const buffer = copyArrayBuffer(value);
    if (buffer === undefined) {
        return undefined;
    }
    if (value instanceof DataView) {
        return new DataView(buffer);
    }
    return new value.constructor(buffer);
}
