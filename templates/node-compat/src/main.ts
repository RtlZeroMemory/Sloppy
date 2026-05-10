import path from "node:path";
import { Buffer } from "node:buffer";
import { EventEmitter } from "node:events";
import querystring from "node:querystring";

export function main() {
    const emitter = new EventEmitter();
    let event = "missing";
    emitter.on("ready", (value) => {
        event = value;
    });
    emitter.emit("ready", "ok");

    const query = querystring.parse("ready=true&mode=alpha");
    const encoded = Buffer.from("sloppy").toString("utf8");

    console.log(JSON.stringify({
        joined: path.join("src", "main.ts"),
        event,
        encoded,
        ready: query.ready,
        mode: query.mode,
    }, null, 2));
    return 0;
}
