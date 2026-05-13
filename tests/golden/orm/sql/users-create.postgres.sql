create table "users" (
  "id" uuid primary key,
  "teamId" uuid not null references "teams" ("id"),
  "email" text not null unique,
  "displayName" text,
  "passwordHash" text not null,
  "version" integer not null,
  "deletedAt" timestamptz,
  "createdAt" timestamptz not null default CURRENT_TIMESTAMP
);
create index "ix_users_deletedAt" on "users" ("deletedAt");
