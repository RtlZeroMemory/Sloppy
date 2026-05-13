create procedure dbo.ts_insert_user
    @Email nvarchar(128)
as
begin
    insert into dbo.ts_users (email) values (@Email);
end;
