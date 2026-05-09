import {
  Sloppy,
  Results,
  ProblemDetails,
  schema,
  Body,
  Config,
  Header,
  Query,
  RequestContext,
  Route,
  Service,
} from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
import { Sqlite } from "sloppy/providers/sqlite";
import { healthModule } from "./modules/health";

const ProjectCreate = schema.object({
  name: schema.string().min(1),
  owner: schema.string().optional(),
});

type ProjectCreateInput = {
  name: string;
  owner?: string;
};

type ProjectDto = {
  id: number;
  name: string;
};

type ClockService = {
  now: string;
};

const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
app.use(ProblemDetails.defaults({ detail: "never" }));
app.services.addSingleton("ClockService", () => ({ now: "2026-01-01T00:00:00Z" }));

const db = app.provider("sqlite:main");
const api = app.group("/api").withTags("api");
const projects = api.group("/projects").withTags("projects");
const greeting = app.config.getString("App:Greeting", "hello");

function listProjects() {
  return db.query("select id, name from projects", []);
}

projects.get("/", { name: "Projects.List", tags: ["list"] }, (ctx) => Results.ok({
  q: ctx.query.q,
  rows: listProjects(),
}));

projects.post("/", { name: "Projects.Create", tags: ["write"] }, (
  body: Body<ProjectCreateInput>,
  database: Sqlite<"main">,
  clock: Service<ClockService>,
  trace: Header<"x-trace-id">,
  message: Config<"App:Greeting">,
  ctx: RequestContext,
) => Results.created("/api/projects/1", {
  name: body.name,
  at: clock.now,
  trace,
  message,
  hasContext: ctx !== undefined,
  providerReady: database !== undefined,
}));

projects.get("/{id:int}", { name: "Projects.Get", tags: ["detail"] }, (
  id: Route<number>,
  includeDeleted: Query<boolean>,
  database: Sqlite<"main">,
  ctx: RequestContext,
) => {
  const project = database.queryOne("select id, name from projects where id = ?", [id]);
  return project ? Results.ok(project) : Results.notFound();
});

app.mapHealthChecks({
  path: "/status",
  livenessPath: "/status/live",
  readinessPath: "/status/ready",
  checks: [
    { name: "database", readiness: true, check: () => true },
    { name: "scheduler", liveness: true, readiness: false, check() { return true; } },
  ],
});

app.useModule(healthModule);

export default app;
