import { Sloppy, Results } from "sloppy";

const policy = { origins: ["https://app.example.com"] };
const app = Sloppy.create();
app.useCors(policy);
app.get("/", () => Results.ok({ ok: true }));

export default app;
