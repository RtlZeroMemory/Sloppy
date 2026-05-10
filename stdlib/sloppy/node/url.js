const URLCtor = globalThis.URL;
const URLSearchParamsCtor = globalThis.URLSearchParams;

function pathToFileURL(path) {
    return new URLCtor(`file://${String(path).replace(/\\/g, "/")}`);
}

function fileURLToPath(url) {
    const parsed = url instanceof URLCtor ? url : new URLCtor(String(url));
    if (parsed.protocol !== "file:") {
        throw new TypeError("fileURLToPath requires a file: URL.");
    }
    return decodeURIComponent(parsed.pathname);
}

export { fileURLToPath, pathToFileURL, URLCtor as URL, URLSearchParamsCtor as URLSearchParams };
export default { URL: URLCtor, URLSearchParams: URLSearchParamsCtor, fileURLToPath, pathToFileURL };
