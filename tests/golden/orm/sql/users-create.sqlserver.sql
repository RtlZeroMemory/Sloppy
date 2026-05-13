create table [users] (
  [id] uniqueidentifier primary key,
  [teamId] uniqueidentifier not null,
  [email] nvarchar(450) not null unique,
  [displayName] nvarchar(max),
  [passwordHash] nvarchar(max) not null,
  [version] int not null,
  [deletedAt] datetimeoffset,
  [createdAt] datetimeoffset not null default SYSUTCDATETIME()
);
create index [ix_users_deletedAt] on [users] ([deletedAt]);

alter table [users] add constraint [fk_users_teamId_teams_id] foreign key ([teamId]) references [teams] ([id]);
