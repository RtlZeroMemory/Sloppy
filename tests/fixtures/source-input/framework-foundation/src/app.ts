import { Sloppy, Results, RequestId, RequestLogging } from "sloppy";

class SummaryController {
  static inject = ["Clock"];

  constructor(clock) {
    this.clock = clock;
  }

  show(ctx) {
    return Results.ok({
      clock: this.clock.now,
      route: ctx.routeName,
      requestId: ctx.requestId,
    });
  }
}

function auditMiddleware(ctx, next) {
  return next();
}

const builder = Sloppy.createBuilder();
builder.services.addSingleton("Clock", () => ({ now: "2026-05-09T00:00:00Z" }));

const app = builder.build();
app.use(RequestId.defaults({ header: "x-request-id", responseHeader: true, trustIncoming: true }));
app.use(RequestLogging.defaults({ includeRoute: true, includeDuration: false, includeRequestId: true }));
app.use(auditMiddleware);
app.useCors({
  origins: ["https://app.example.com"],
  headers: ["authorization"],
  exposedHeaders: ["x-request-id"],
  credentials: true,
  maxAgeSeconds: 600,
});

const api = app.group("/api");
api.use((ctx, next) => next());
api.get("/status", () => Results.ok({ ok: true })).withName("Status.Get");

app.mapController("/api/summary", SummaryController, (mapper) => {
  mapper.get("/", "show", { tags: ["controller"] }).withName("Summary.Get");
});

export default app;
