import path from "node:path";
import { EventEmitter } from "node:events";

export function main() {
    const bus = new EventEmitter();
    let seen = "";

    bus.on("ready", (value) => {
        seen = path.join("plugins", value);
    });

    bus.emit("ready", "alpha.js");
    console.log(seen);
    return 0;
}
