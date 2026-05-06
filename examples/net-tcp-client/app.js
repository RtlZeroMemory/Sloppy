import { TcpClient, NetworkAddress } from "sloppy/net";
import { Deadline } from "sloppy/time";

export async function pingRedisLikeService() {
  const target = NetworkAddress.parse("127.0.0.1:6379");
  const deadline = Deadline.after(1000);
  const conn = await TcpClient.connect({
    host: target.host,
    port: target.port,
    timeoutMs: 1000,
    noDelay: true,
    keepAlive: { enabled: true, delayMs: 30000 },
    deadline
  });

  try {
    await conn.writeText("PING\r\n");
    return await conn.readLine({ deadline });
  }
  finally {
    await conn.close();
  }
}
