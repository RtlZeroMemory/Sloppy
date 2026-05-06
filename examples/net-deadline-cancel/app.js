import { TcpClient } from "sloppy/net";
import { CancellationController, Deadline } from "sloppy/time";

export async function pingWithDeadline(appSignal) {
  const deadline = Deadline.after(500);
  const conn = await TcpClient.connect({
    host: "127.0.0.1",
    port: 6379,
    timeoutMs: 500,
    deadline,
    signal: appSignal
  });

  try {
    await conn.writeText("PING\r\n", { deadline, signal: appSignal });
    return await conn.readLine({ deadline, signal: appSignal });
  }
  finally {
    await conn.close();
  }
}

export async function cancelPendingConnect() {
  const controller = new CancellationController();
  const pending = TcpClient.connect({
    host: "127.0.0.1",
    port: 6379,
    timeoutMs: 5000,
    signal: controller.signal
  });

  controller.cancel("request cancelled");
  return await pending;
}
