import { TcpClient, TcpListener } from "sloppy/net";

export async function startEcho(signal) {
  const listener = await TcpListener.listen({
    host: "127.0.0.1",
    port: 0,
    backlog: 32
  });

  runEchoAcceptLoop(listener, signal);
  return listener;
}

async function runEchoAcceptLoop(listener, signal) {
  for await (const conn of listener.accept({ signal })) {
    echoConnection(conn);
  }
}

async function echoConnection(conn) {
  try {
    for await (const chunk of conn.readChunks({ maxBytes: 65536 })) {
      await conn.write(chunk);
    }
  }
  finally {
    await conn.close();
  }
}

export async function echoOnce(listener, payload) {
  const endpoint = listener.localAddress;
  const conn = await TcpClient.connect({
    host: endpoint.host,
    port: endpoint.port
  });

  try {
    await conn.write(payload);
    return await conn.read({ maxBytes: payload.length });
  }
  finally {
    await conn.close();
  }
}
