import { Random } from "../crypto.js";
import { TcpListener } from "../net.js";

const LOOPBACK_PORT_MIN = 49152;
const LOOPBACK_PORT_MAX = 65535;

export function validateLoopbackPort(port) {
    if (!Number.isInteger(port) || port < 1 || port > 65535) {
        throw new TypeError("Sloppy TestHost loopback port must be an integer from 1 to 65535.");
    }
    return port;
}

function randomLoopbackPort() {
    const bytes = Random.bytes(2);
    const value = (bytes[0] << 8) | bytes[1];
    return LOOPBACK_PORT_MIN + (value % (LOOPBACK_PORT_MAX - LOOPBACK_PORT_MIN + 1));
}

export async function reserveLoopbackPort(host, options = {}) {
    if (options.port !== undefined) {
        const port = validateLoopbackPort(options.port);
        const listener = await TcpListener.listen({ host, port, backlog: 1 });
        return { port, listener };
    }
    const attempts = options.portReservationAttempts ?? 64;
    for (let attempt = 0; attempt < attempts; attempt += 1) {
        const port = randomLoopbackPort();
        try {
            const listener = await TcpListener.listen({ host, port, backlog: 1 });
            return { port, listener };
        } catch {
            // A different process can own the sampled port; try another reserved candidate.
        }
    }
    throw new Error("Sloppy TestHost could not reserve an available loopback port.");
}

export async function releaseLoopbackReservation(reservation) {
    if (reservation?.listener === undefined) {
        return;
    }
    await reservation.listener.close().catch(async () => {
        await reservation.listener.abort().catch(() => {});
    });
}

export function loopbackAuthority(host, port) {
    return `${String(host).includes(":") ? `[${host}]` : host}:${port}`;
}
