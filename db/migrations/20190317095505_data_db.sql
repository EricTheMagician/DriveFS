-- migrate:up

create table GDriveData (
  id varchar(258) not null unique,
  name varchar(512) not null,
  size bigint,
  trashed bool not null,
  mimeType varchar(255) not null,
  md5Checksum varchar(32),
  version int,
  isTrashable bool not null,
  canRename bool not null,
  canDownload bool,
  modifiedTime timestamp not null,
  createdTime timestamp not null,
  parents varchar(257) ARRAY,
  inode bigint not null unique,
  mode int,
  uid int,
  gid int,
  uploadUrl varchar(512)


);

CREATE INDEX id_index on GDriveData(id);
CREATE INDEX inode_index on GDriveData(inode);

-- migrate:down
drop table GDriveData;
