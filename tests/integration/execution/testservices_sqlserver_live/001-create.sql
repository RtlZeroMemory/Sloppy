create sequence dbo.ts_seq as int start with 1 increment by 1;
create table dbo.ts_users (
    id int not null primary key default(next value for dbo.ts_seq),
    email nvarchar(128) not null unique
);
