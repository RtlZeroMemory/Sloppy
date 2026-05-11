import { Compression, Text } from "../codec.js";

function toBytes(value) {
    if (typeof value === "string") {
        return Text.utf8.encode(value);
    }
    if (value instanceof Uint8Array) {
        return value;
    }
    if (value instanceof ArrayBuffer) {
        return new Uint8Array(value);
    }
    if (ArrayBuffer.isView(value)) {
        return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
    }
    throw new TypeError("Sloppy node:zlib input must be a string or bytes.");
}

function callbackify(promiseFactory, callback) {
    if (typeof callback !== "function") {
        throw new TypeError("Sloppy node:zlib callback API requires a callback.");
    }
    promiseFactory().then((value) => callback(null, value), (error) => callback(error));
}

function gzip(input, options, callback) {
    if (typeof options === "function") {
        callback = options;
        options = undefined;
    }
    callbackify(() => Compression.gzip(toBytes(input), options), callback);
}

function gunzip(input, options, callback) {
    if (typeof options === "function") {
        callback = options;
        options = undefined;
    }
    callbackify(() => Compression.gunzip(toBytes(input), options), callback);
}

function unsupportedAsync(name, input, options, callback) {
    if (typeof options === "function") {
        callback = options;
        options = undefined;
    }
    callbackify(
        () => Promise.reject(new Error(`SLOPPY_E_NODE_ZLIB_UNSUPPORTED: node:zlib.${name} is not implemented by Sloppy's Node compatibility shim.`)),
        callback,
    );
}

function deflate(input, options, callback) {
    unsupportedAsync("deflate", input, options, callback);
}

function inflate(input, options, callback) {
    unsupportedAsync("inflate", input, options, callback);
}

function unsupportedSync(name) {
    return () => {
        throw new Error(`SLOPPY_E_NODE_ZLIB_SYNC_UNSUPPORTED: node:zlib.${name} is not available because Sloppy compression is async.`);
    };
}

export { deflate, gzip, gunzip, inflate };
export const gzipSync = unsupportedSync("gzipSync");
export const gunzipSync = unsupportedSync("gunzipSync");
export const deflateSync = unsupportedSync("deflateSync");
export const inflateSync = unsupportedSync("inflateSync");
export default { deflate, deflateSync, gzip, gzipSync, gunzip, gunzipSync, inflate, inflateSync };
