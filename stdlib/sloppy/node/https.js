function unsupported(name) {
    return () => {
        throw new Error(`SLOPPY_E_NODE_HTTPS_UNSUPPORTED: node:https.${name} is not implemented by Sloppy's Node compatibility shim.`);
    };
}

const request = unsupported("request");
const get = unsupported("get");

export { get, request };
export default { get, request };
