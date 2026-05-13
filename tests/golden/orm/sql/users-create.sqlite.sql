create table "users" (
  "id" text primary key,
  "teamId" text not null references "teams" ("id"),
  "email" text not null unique,
  "displayName" text,
  "passwordHash" text not null,
  "version" integer not null,
  "deletedAt" text,
  "createdAt" text not null default CURRENT_TIMESTAMP
);
create index "ix_users_deletedAt" on "users" ("deletedAt");
