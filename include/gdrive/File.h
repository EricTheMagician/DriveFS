//
// Created by eric on 27/01/18.
//

#ifndef DRIVEFS_FILE_H
#define DRIVEFS_FILE_H

#include "BaseFile.h"
#include <string_view>
#include <vector>
#include <map>
#include <memory>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/document/value.hpp>
#include <atomic>

#include "DownloadBuffer.h"
#include <autoresetevent.h>

namespace DriveFS {

    class FileSystem;
    class _Object;
    typedef std::shared_ptr<_Object> GDriveObject;
    class _Object : public ::File{

    public:
        static GDriveObject buildRoot(bsoncxx::document::view document);
        static void updateInode(ino_t ino, bsoncxx::document::view document);
        _Object();
        _Object(ino_t ino, bsoncxx::document::view document);
        _Object(const DriveFS::_Object&);
        _Object(DriveFS::_Object&&);
        ~_Object();
        static PriorityCache<GDriveObject> cache;
    public:
        static std::map<ino_t, GDriveObject> inodeToObject;
        static std::map<std::string, GDriveObject> idToObject;
        static GDriveObject buildTeamDriveHolder(ino_t ino, GDriveObject root);
        static GDriveObject buildTeamDrive(ino_t ino, bsoncxx::document::view document, GDriveObject parent);
        inline std::string getName() const{return m_name;}
        inline std::string getId() const{return m_id;}
        inline bool getIsFolder() const {return isFolder;};
        inline void addParent(GDriveObject parent){addRelationship(std::move(parent), parents);};
        inline void addChild(GDriveObject child){attribute.st_nlink++;addRelationship(std::move(child), children);};
        inline size_t getFileSize() const { return attribute.st_size; }

        void createVectorsForBuffers();
        void updatLastAccessToCache(uint64_t chunkNumber);

    public:
        std::atomic_uint64_t lookupCount; // for filesystem lookup count
        std::vector<GDriveObject> parents, children;
        std::vector<WeakBuffer> *m_buffers; // a vector pointing to possible download m_buffers
        std::vector<heap_handle> *heap_handles;
        AutoResetEvent m_event;

    protected:
        std::string m_name;
        void addRelationship(GDriveObject other, std::vector<GDriveObject> &relationship);
        bool trashed, starred;
        std::string m_id, mime_type, selflink, md5Checksum;
        uint_fast64_t version;
        bool isFolder, isTrashable, canRename;


    };

}

#endif //DRIVEFS_FILE_H
