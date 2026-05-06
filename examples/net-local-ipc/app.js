import { LocalEndpoint, NamedPipe, UnixSocket } from "sloppy/net";

export async function runLocalEcho() {
  const server = await LocalEndpoint.listen({
    path: "runtime:/local-echo.sock",
    unlinkExisting: true,
    permissions: "0600",
    backlog: 8
  });

  const accepted = (async () => {
    const conn = await server.accept({ timeoutMs: 1000 });
    try {
      const payload = await conn.readUntil(new Uint8Array([0]), { maxBytes: 65536, timeoutMs: 1000 });
      await conn.write(payload, { timeoutMs: 1000 });
    }
    finally {
      await conn.close();
    }
  })();

  const client = await LocalEndpoint.connect({ path: "runtime:/local-echo.sock", timeoutMs: 1000 });
  try {
    await client.write(new Uint8Array([1, 2, 0]), { timeoutMs: 1000 });
    return await client.read({ maxBytes: 3, timeoutMs: 1000 });
  }
  finally {
    await client.close();
    await accepted;
    await server.close();
  }
}

export async function connectPlatformSpecific() {
  const unix = UnixSocket.connect({ path: "runtime:/daemon.sock" });
  const pipe = NamedPipe.connect({ path: "runtime:/daemon.sock" });
  return { unix, pipe };
}
