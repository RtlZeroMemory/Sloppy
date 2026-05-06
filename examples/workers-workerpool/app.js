import { Sloppy, Results } from "sloppy";
import { Deadline } from "sloppy/time";
import { WorkerPool } from "sloppy/workers";

const app = Sloppy.create();

const pool = WorkerPool.create("image-processing", {
  workers: 4,
  maxQueued: 100,
});

app.use(pool);
app.mapPost("/images/resize", async () => {
  const result = await pool.run(async (ctx) => {
    return { bytes: ctx.input.length, resized: true };
  }, {
    input: new Uint8Array([1, 2, 3, 4]),
    deadline: Deadline.after(5000),
  });
  return Results.json(result);
});

export default app;
