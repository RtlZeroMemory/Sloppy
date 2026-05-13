import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import net from "node:net";
import { once } from "node:events";

import { Cache, Redis, TestServices } from "../../stdlib/sloppy/index.js";

function installNodeBridge() {
    const handles = new Map();
    let nextId = 1;

    function handleFor(id) {
        const state = handles.get(id);
        if (state === undefined) {
            throw new Error("test TCP handle is closed");
        }
        return state;
    }

    globalThis.__sloppy = {
        ...(globalThis.__sloppy ?? {}),
        net: {
            async connect(options) {
                const socket = net.createConnection({
                    host: options.host,
                    port: options.port,
                    noDelay: options.noDelay === true,
                });
                socket.setMaxListeners(0);
                const chunks = [];
                let ended = false;
                socket.on("data", (chunk) => chunks.push(new Uint8Array(chunk)));
                socket.on("end", () => {
                    ended = true;
                });
                socket.on("close", () => {
                    ended = true;
                });
                await once(socket, "connect");
                const id = nextId++;
                handles.set(id, { socket, chunks, ended });
                return id;
            },
            async write(id, bytes) {
                const { socket } = handleFor(id);
                await new Promise((resolve, reject) => {
                    socket.write(Buffer.from(bytes), (error) => {
                        if (error) {
                            reject(error);
                        } else {
                            resolve();
                        }
                    });
                });
            },
            async read(id, maxBytes) {
                const state = handleFor(id);
                while (state.chunks.length === 0) {
                    if (state.ended) {
                        return new Uint8Array();
                    }
                    await Promise.race([
                        once(state.socket, "data"),
                        once(state.socket, "end"),
                        once(state.socket, "close"),
                    ]);
                }
                const chunk = state.chunks.shift();
                if (chunk.byteLength <= maxBytes) {
                    return chunk;
                }
                state.chunks.unshift(chunk.subarray(maxBytes));
                return chunk.subarray(0, maxBytes);
            },
            async close(id) {
                const state = handles.get(id);
                handles.delete(id);
                state?.socket.end();
            },
            async abort(id) {
                const state = handles.get(id);
                handles.delete(id);
                state?.socket.destroy();
            },
        },
        os: {
            async processRun(command, args, options = {}) {
                return await new Promise((resolve, reject) => {
                    const child = spawn(command, args, {
                        cwd: options.cwd,
                        env: options.env === undefined ? process.env : { ...process.env, ...options.env },
                        shell: false,
                        windowsHide: true,
                    });
                    const stdout = [];
                    const stderr = [];
                    let timedOut = false;
                    const timer = options.timeoutMs === undefined
                        ? undefined
                        : setTimeout(() => {
                            timedOut = true;
                            child.kill();
                        }, options.timeoutMs);
                    child.stdout.on("data", (chunk) => stdout.push(chunk));
                    child.stderr.on("data", (chunk) => stderr.push(chunk));
                    child.on("error", reject);
                    child.on("close", (code) => {
                        if (timer !== undefined) {
                            clearTimeout(timer);
                        }
                        resolve({
                            exitCode: code ?? 1,
                            stdout: new Uint8Array(Buffer.concat(stdout)),
                            stderr: new Uint8Array(Buffer.concat(stderr)),
                            timedOut,
                        });
                    });
                });
            },
        },
    };
}

if (process.env.SLOPPY_REDIS_LIVE !== "1") {
    console.log("SKIPPED: set SLOPPY_REDIS_LIVE=1 to run live Redis TestServices coverage");
    process.exit(0);
}

installNodeBridge();

const docker = await TestServices.docker.available();
if (!docker.ok) {
    console.log(`SKIPPED: Docker unavailable: ${docker.reason}`);
    process.exit(0);
}

const service = await TestServices.redis({
    startupTimeoutMs: 30000,
    readinessTimeoutMs: 30000,
});

try {
    const redis = service.client("live");
    const cache = Cache.redis(redis, { name: "live", ttlMs: 5000 });
    try {
        assert.equal(await redis.ping(), "PONG");
        await redis.set("live:user", { id: 1, name: "Ada" }, { ttlMs: 5000 });
        assert.deepEqual(await redis.get("live:user"), { id: 1, name: "Ada" });

        await cache.set("settings", { enabled: true }, { tags: ["settings"] });
        assert.deepEqual(await cache.get("settings"), { enabled: true });
        assert.equal(await cache.invalidateTag("settings"), 1);

        const locks = Redis.locks(redis, { prefix: "live:locks:" });
        const lease = await locks.acquire("job", { ttlMs: 5000 });
        assert.equal(await lease.release(), true);

        await service.reset();
        assert.match(service.env().REDIS_URL, /^redis:\/\/127\.0\.0\.1:\d+\/0$/u);
        assert.equal(JSON.stringify(service.diagnostics()).includes("password"), false);
        console.log("PASS: live Redis TestServices coverage");
    } finally {
        await cache.dispose();
        await redis.dispose();
    }
} finally {
    await service.dispose();
}
