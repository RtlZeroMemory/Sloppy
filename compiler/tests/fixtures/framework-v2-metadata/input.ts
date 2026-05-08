import {
  Sloppy,
  Results,
  Email,
  NonEmptyString,
  PasswordString,
  SecretString,
  Uuid,
  PositiveInt,
  DateTime,
  Instant,
  RequestContext,
  Body,
  Header,
  Query,
  Route,
} from "sloppy";
import { sql } from "sloppy/data";
import { Postgres } from "sloppy/providers/postgres";
import { Sqlite } from "sloppy/providers/sqlite";
import { SqlServer } from "sloppy/providers/sqlserver";
import { WorkQueue } from "sloppy/workers";

// Fixture invariant: semantic schema extraction and redaction coverage.
type UserCreate = {
  email: Email;
  name: NonEmptyString;
  password: PasswordString<8>;
  secretNote: SecretString;
  tenantId: Uuid;
  loginCount: PositiveInt;
  createdAt: DateTime;
  lastSeenAt: Instant | null;
  profile?: {
    displayName: string;
    tags: string[];
  };
  role: "admin" | "user";
};

type UserDto = {
  id: number;
  email: string;
  name: string;
};

const app = Sloppy.create();

app.post("/users", async (
  input: UserCreate,
  db: Postgres<"main">,
  audit: Sqlite<"audit">,
  search: SqlServer<"search">,
  emails: WorkQueue<"emails">,
  ctx: RequestContext,
) => {
  const user = await db.queryOne<UserDto>(
    sql`select id, email, name from users where id = ${1}`,
    { signal: ctx.signal, deadline: ctx.deadline }
  );

  return Results.created(`/users/${user.id}`, user);
});

app.get("/users/:id", async (
  id: Route<number>,
  trace: Header<"x-trace-id">,
  includeDeleted: Query<boolean>,
  input: Body<UserCreate>,
  db: Postgres<"main">,
  ctx: RequestContext,
) => {
  const user = await db.queryOne<UserDto>(
    sql`select id, email, name from users where id = ${id}`,
    { signal: ctx.signal, deadline: ctx.deadline }
  );

  return user ? Results.ok(user) : Results.notFound();
});

export default app;
