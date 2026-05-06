import { LocalEndpoint, NamedPipe, UnixSocket } from "sloppy/net";
import { CancellationController, Deadline } from "sloppy/time";

export async function runLocalEcho() {
  const controller = new CancellationController();
  const signal = controller.signal;
  const deadline = Deadline.after(1000);
  const server = await LocalEndpoint.listen({
    path: "runtime:/local-echo.sock",
    unlinkExisting: true,
    permissions: "0600",
    backlog: 8
  });

  const accepted = (async () => {
    for await (const conn of server.accept({ signal, deadline })) {
      try {
        const payload = await conn.readUntil(new Uint8Array([0]), { maxBytes: 65536, deadline, signal });
        await conn.write(payload, { deadline, signal });
      }
      finally {
        await conn.close();
      }
      break;
    }
  })();

  const client = await LocalEndpoint.connect({ path: "runtime:/local-echo.sock", deadline, signal });
  try {
    await client.write(new Uint8Array([1, 2, 0]), { deadline, signal });
    return await client.read({ maxBytes: 3, deadline, signal });
  }
  finally {
    await client.close();
    controller.cancel("local echo complete");
    await accepted;
    await server.close();
    controller.dispose();
  }
}

export async function connectPlatformSpecific() {
  const unix = UnixSocket.connect({ path: "runtime:/daemon.sock" });
  const pipe = NamedPipe.connect({ path: "runtime:/daemon.sock" });
  return { unix, pipe };
}
