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

namespace DriveFS {

    class FileSystem;
    class _Object;
    typedef std::shared_ptr<_Object> GDriveObject;
    class _Object : public ::File{

    public:
        static GDriveObject buildRoot(bsoncxx::document::view document);
        static void updateInode(ino_t ino, bsoncxx::document::view document);
        void updateInode(bsoncxx::document::view document);
        _Object(ino_t ino, bsoncxx::document::view document); // default object creation
        _Object(ino_t ino, const std::string &id, const char *name, mode_t mode, bool isFile);
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
        bool removeChild(GDriveObject child);
        bool removeParent(GDriveObject child);
        inline void addChild(GDriveObject child){
            if(child->trashed) return;
            attribute.st_nlink++;
            addRelationship(std::move(child), children);
        };

        void createVectorsForBuffers();
        void updatLastAccessToCache(uint64_t chunkNumber);
        inline bool getIsUploaded() const{return isUploaded;}
        void trash();
        std::string md5() const { return md5Checksum; };
        GDriveObject findChildByName( const char *name) const ;
        bsoncxx::document::value to_bson() const;
        bsoncxx::document::value to_rename_bson() const;
        std::string getCreatedTimeAsString() const;
        void setName(const char *name){
            m_name = name;
        }

        /*
         * member function called for decreasing lookup counts
         */
        void forget(uint64_t nLookup);


    public:
        std::vector<GDriveObject> parents, children;

    protected:
        _Object(); // used for creating a default object when creating root folders
        void updateProperties(bsoncxx::document::view document);
        bool addRelationship(GDriveObject other, std::vector<GDriveObject> &relationship);
        bool trashed, starred;
        std::string m_id, mime_type, selflink, md5Checksum;
        uint_fast64_t version;
        bool isFolder, isTrashable, canRename;
        bool isUploaded;


    };

}

#endif //DRIVEFS_FILE_H
