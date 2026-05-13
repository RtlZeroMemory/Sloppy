create table "users" (
  "id" uuid primary key,
  "teamId" uuid not null,
  "email" text not null unique,
  "displayName" text,
  "passwordHash" text not null,
  "version" integer not null,
  "deletedAt" timestamptz,
  "createdAt" timestamptz not null default CURRENT_TIMESTAMP
);
create index "ix_users_deletedAt" on "users" ("deletedAt");

alter table "users" add constraint "fk_users_teamId_teams_id" foreign key ("teamId") references "teams" ("id");
