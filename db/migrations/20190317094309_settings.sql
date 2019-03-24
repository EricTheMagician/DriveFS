-- migrate:up
create table drive_settings (
  name varchar(128) not null,
  value varchar(255) not null,
  id varchar(128)
);

-- migrate:down

drop table drive_settings;

