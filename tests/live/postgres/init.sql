create table if not exists sloppy_live_probe (
    id integer generated always as identity primary key,
    label text not null,
    created_at timestamptz not null default now()
);
