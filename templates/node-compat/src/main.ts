import path from "node:path";
import assert from "node:assert";
import { Buffer } from "node:buffer";
import crypto from "node:crypto";
import { EventEmitter } from "node:events";
import querystring from "node:querystring";
import { Readable } from "node:stream";
import process from "node:process";

export async function main() {
    const emitter = new EventEmitter();
    let event = "missing";
    emitter.on("ready", (value) => {
        event = value;
    });
    emitter.emit("ready", "ok");

    const query = querystring.parse("ready=true&mode=alpha");
    const encoded = Buffer.from("sloppy").toString("utf8");
    const euroCodePoint = Buffer.from([0xe2, 0x82, 0xac]).toString("utf8").codePointAt(0);
    const digest = await crypto.createHash("sha256").update(encoded).digest("hex");
    const chunks = [];
    for await (const chunk of Readable.from(["node", "-", "compat"])) {
        chunks.push(chunk);
    }
    assert.strictEqual(event, "ok");

    console.log(JSON.stringify({
        joined: path.join("src", "main.ts"),
        event,
        encoded,
        euroCodePoint,
        digestPrefix: digest.slice(0, 8),
        ready: query.ready,
        mode: query.mode,
        platform: process.platform,
        stream: chunks.join(""),
    }, null, 2));
    return 0;
}
