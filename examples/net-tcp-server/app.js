import { TcpListener } from "sloppy/net";

export async function serveLines(appSignal) {
  const listener = await TcpListener.listen({
    host: "127.0.0.1",
    port: 9000,
    backlog: 128
  });

  try {
    for await (const conn of listener.accept({ signal: appSignal })) {
      handleConnection(conn);
    }
  }
  finally {
    await listener.close();
  }
}

async function handleConnection(conn) {
  try {
    const line = await conn.readLine();
    await conn.writeText(line);
  }
  finally {
    await conn.close();
  }
}
