import { Sloppy, Results } from "sloppy";
import { Deadline } from "sloppy/time";
import { Worker } from "sloppy/workers";

const app = Sloppy.create();

app.mapPost("/parse", async () => {
  const worker = await Worker.start("./workers/parser.ts", { memoryLimitMb: 128 });
  try {
    const parsed = await worker.invoke("parse", { text: "one two three" }, {
      deadline: Deadline.after(1000),
    });
    return Results.json(parsed);
  } finally {
    await worker.stop();
  }
});

export default app;
