#pragma once

#include "File.h"
#include "autoresetevent.h"

namespace DriveFS::FileManager{
//    extern boost::compute::detail::lru_cache<ino_t, GDriveObject> inodeToObject;
//    extern boost::compute::detail::lru_cache<std::string, GDriveObject> idToObject;
    extern PriorityCache DownloadCache;

    extern bool hasId(std::string const & id, bool removeFromCache);
    extern GDriveObject fromId(std::string const &id);
    extern GDriveObject fromInode(ino_t inode);
    extern GDriveObject fromParentIdAndName(std::string const &id, char const* name, bool logSqlFailure=true);
    extern std::vector<GDriveObject> getChildren(const std::string &parent_id);
    extern std::vector<GDriveObject> getParents(const std::string &child_id);
    extern std::vector<std::string> getParentIds(const std::string &child_id);
    extern GDriveObject insertObjectToMemoryMap(const GDriveObject &object);

    extern std::string asJSONForRename(std::string const &id);
    extern std::string asJSONForRename(ino_t inode);
    extern std::string asJSONForRename(GDriveObject const & obj);
    extern bool removeFileWithIDFromDB(std::string const &id);
    extern void cleanUpOnExit();
};

#define APP_UID "driveFS_uid"
#define APP_GID "driveFS_gid"
#define APP_MODE "driveFS_mode"
#define GOOGLE_FOLDER "application/vnd.google-apps.folder"
