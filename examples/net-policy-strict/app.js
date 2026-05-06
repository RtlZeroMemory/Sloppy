import { TcpClient, TcpListener } from "sloppy/net";

export const strictNetworkPolicy = {
  mode: "strict",
  allow: [
    { access: "connect", host: "10.0.0.25", port: 6379 },
    { access: "listen", host: "127.0.0.1", port: 9000 }
  ]
};

export async function connectToAllowedCache() {
  return await TcpClient.connect({
    host: "10.0.0.25",
    port: 6379,
    timeoutMs: 1000
  });
}

export async function listenOnAllowedLoopback(signal) {
  const listener = await TcpListener.listen({
    host: "127.0.0.1",
    port: 9000,
    backlog: 16
  });

  try {
    for await (const conn of listener.accept({ signal })) {
      await conn.close();
    }
  }
  finally {
    await listener.close();
  }
}
