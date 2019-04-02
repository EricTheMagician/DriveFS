-- migrate:up

create table CacheDB (
    path varchar(4096) not null unique,
    size bigint not null,
    mtime bigint not null,
    exists bool not null
);

CREATE INDEX path_index on CacheDB(path);


-- migrate:down

drop table CacheDB;
