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

namespace DriveFS {

    class _Object;
    typedef std::shared_ptr<_Object> GDriveObject;
    class _Object : public ::File{

    public:
        static GDriveObject buildRoot(bsoncxx::document::view document);
        _Object();
        _Object(ino_t ino, bsoncxx::document::view document);
    public:
        static std::map<ino_t, GDriveObject> inodeToObject;
        static std::map<std::string, GDriveObject> idToObject;
        inline std::string getName() const{return m_name;}
        inline std::string getId() const{return m_id;}
        inline void addParent(GDriveObject parent){addRelationship(std::move(parent), parents);};
        inline void addChild(GDriveObject child){addRelationship(std::move(child), children);};
    private:
        void addRelationship(GDriveObject other, std::vector<GDriveObject> &relationship);
        std::vector<GDriveObject> parents, children;
        std::string m_name;
        bool trashed, starred;
        std::string m_id, mime_type, selflink, md5Checksum;
        uint_fast64_t version;
        bool isFolder, isTrashable, canRename;

    };

}

#endif //DRIVEFS_FILE_H
