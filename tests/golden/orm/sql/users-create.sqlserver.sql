create table [users] (
  [id] uniqueidentifier primary key,
  [teamId] uniqueidentifier not null references [teams] ([id]),
  [email] nvarchar(max) not null unique,
  [displayName] nvarchar(max),
  [passwordHash] nvarchar(max) not null,
  [version] int not null,
  [deletedAt] datetimeoffset,
  [createdAt] datetimeoffset not null default SYSUTCDATETIME()
);
create index [ix_users_deletedAt] on [users] ([deletedAt]);
