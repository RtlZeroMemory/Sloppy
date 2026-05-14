import { Agent, ClientRequest, IncomingMessage } from "./http.js";

const globalAgent = new Agent({ keepAlive: false, protocol: "https:" });

function request(input, options = undefined, callback = undefined) {
    if (typeof options === "function") {
        callback = options;
        options = undefined;
    }
    const mergedOptions = typeof options === "object" && options !== null
        ? { ...options, protocol: "https:" }
        : { protocol: "https:" };
    return new ClientRequest("https:", input, mergedOptions, callback);
}

function get(input, options = undefined, callback = undefined) {
    const req = request(input, options, callback);
    req.end();
    return req;
}

export { Agent, ClientRequest, IncomingMessage, get, globalAgent, request };
export default { Agent, ClientRequest, IncomingMessage, get, globalAgent, request };
