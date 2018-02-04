//
// Created by eric on 27/01/18.
//

#include <gdrive/File.h>
#include "adaptive_time_parser.h"
#include "date.h"
#include <ctime>
#include <easylogging++.h>

static adaptive::datetime::adaptive_parser parser { adaptive::datetime::adaptive_parser::full_match, {
    "%Y-%m-%dT%H:%M:%SZ"
//    "%Y-%m-%dT%H:%M:%SZ
} };

struct timespec getTimeFromRFC3339String(const std::string &str_date){
    date::sys_time<std::chrono::milliseconds> tp;
    std::stringstream ss( str_date );
    ss >> date::parse("%FT%TZ", tp);
    int64_t epoch = tp.time_since_epoch().count();
    return {epoch/1000, epoch % 1000};

}

namespace DriveFS{
    std::map<ino_t, GDriveObject> _Object::inodeToObject;
    std::map<std::string, GDriveObject> _Object::idToObject;

    _Object::_Object():lookupCount(0){
    }
    _Object::_Object(const DriveFS::_Object& that){
        lookupCount = that.lookupCount.load(std::memory_order_acquire);
        parents = that.parents;
        children = that.children;
        m_name = that.m_name;
        trashed = that.trashed;
        starred = that.starred;
        m_id = that.m_id;
        mime_type = that.mime_type;
        selflink = that.selflink;
        md5Checksum = that.md5Checksum;
        version = that.version;
        isFolder = that.isFolder;
        isTrashable = that.isTrashable;
        canRename = that.canRename;
        attribute = that.attribute;
    }
    _Object::_Object(DriveFS::_Object&& that){
        lookupCount = that.lookupCount.load(std::memory_order_acquire);
        parents = std::move(that.parents);
        children = std::move(that.children);
        m_name = std::move(that.m_name);
        trashed = that.trashed;
        starred = that.starred;
        m_id = std::move(that.m_id);
        mime_type = std::move(that.mime_type);
        selflink = std::move(that.selflink);
        md5Checksum = std::move(that.md5Checksum);
        version = that.version;
        isFolder = that.isFolder;
        isTrashable = that.isTrashable;
        canRename = that.canRename;
        attribute = std::move(that.attribute);

    }

    _Object::_Object(ino_t ino, bsoncxx::document::view document):lookupCount(0){
        attribute.st_ino = ino;
        m_id = document["id"].get_utf8().value.to_string();

        auto f = document["mimeType"];
        if(f.get_utf8().value == "application/vnd.google-apps.folder"){
            isFolder = true;
            attribute.st_mode = S_IFDIR | S_IXUSR | S_IXGRP | S_IXOTH;
            attribute.st_size = 4096;
        }else{
            isFolder = false;
            attribute.st_mode = S_IFREG | S_IXUSR | S_IXGRP | S_IXOTH;
            auto sz = document["size"];
            if(sz){
                attribute.st_size = std::atoll(sz.get_utf8().value.to_string().c_str());
            }else{
                sz = document["quotaBytesUsed"];
                if(sz){
                    attribute.st_size = std::atoll(sz.get_utf8().value.to_string().c_str());
                }else {
                    attribute.st_size = 0;
                }
            }
        }

        auto maybeCapabilities = document["capabilities"];
        if(maybeCapabilities){
                auto capabilities = maybeCapabilities.get_document().value;
                isTrashable = capabilities["canTrash"].get_bool();
                canRename = capabilities["canRename"].get_bool();
                if(capabilities["canDownload"].get_bool()){
                        attribute.st_mode |= S_IRUSR | S_IRGRP | S_IROTH;
                }
                if(capabilities["canEdit"].get_bool()){
                        attribute.st_mode |= S_IWUSR | S_IWGRP | S_IWOTH;
                }
        }

        m_name = document["name"].get_utf8().value.to_string();
        attribute.st_mtim = getTimeFromRFC3339String(document["modifiedTime"].get_utf8().value.to_string());
        attribute.st_ctim = getTimeFromRFC3339String(document["createdTime"].get_utf8().value.to_string());
        attribute.st_atim = attribute.st_mtim;
        attribute.st_nlink = 1;
        attribute.st_uid = 65534; // nobody
        attribute.st_gid = 65534;
    }
    GDriveObject _Object::buildRoot(bsoncxx::document::view document){
        _Object f;

        f.attribute.st_ino = 1;
        f.attribute.st_size = 0;
        struct timespec now{time(nullptr),0};
        f.attribute.st_atim = now;
        f.attribute.st_mtim = now;
        f.attribute.st_ctim = now;
        f.attribute.st_mode = S_IFDIR | 0x777;//S_IRWXU | S_IRWXG | S_IRWXO;
        f.attribute.st_nlink = 1;
        f.attribute.st_uid = 65534; // nobody
        f.attribute.st_gid = 65534;

        f.isFolder = true;
        std::string id(document["id"].get_utf8().value.to_string());
        f.m_id = id;
        auto sf = std::make_shared<_Object>(f);
        _Object::idToObject[id] = sf;
        _Object::inodeToObject[1] = sf;


        return sf;
    }

    GDriveObject _Object::buildTeamDriveHolder(ino_t ino, GDriveObject root){
        _Object f;

        f.attribute.st_ino = ino;
        f.attribute.st_size = 0;
        struct timespec now{time(nullptr),0};
        f.attribute.st_atim = now;
        f.attribute.st_mtim = now;
        f.attribute.st_ctim = now;
        f.attribute.st_mode = S_IFDIR | 0x777;//S_IRWXU | S_IRWXG | S_IRWXO;
        f.attribute.st_nlink = 1;
        f.attribute.st_uid = 65534; // nobody
        f.attribute.st_gid = 65534;


        f.isFolder = true;
        std::string id("teamDriveHolder");
        f.m_id = id;
        f.m_name = "Team Drives";
        auto sf = std::make_shared<_Object>(f);
        _Object::idToObject[id] = sf;
        _Object::inodeToObject[ino] = sf;
        sf->addParent(root);
        root->addChild(sf);

        return sf;
    }

    GDriveObject _Object::buildTeamDrive(ino_t ino, bsoncxx::document::view document, GDriveObject parent){
        _Object f;

        f.attribute.st_ino = ino;
        f.attribute.st_size = 0;
        struct timespec now{time(nullptr),0};
        f.attribute.st_atim = now;
        f.attribute.st_mtim = now;
        f.attribute.st_ctim = now;
        f.attribute.st_mode = S_IFDIR | 0x777;//S_IRWXU | S_IRWXG | S_IRWXO;
        f.attribute.st_nlink = 1;
        f.attribute.st_uid = 65534; // nobody
        f.attribute.st_gid = 65534;
        f.m_name = document["name"].get_utf8().value.to_string();

        f.isFolder = true;
        std::string id(document["id"].get_utf8().value.to_string());
        f.m_id = id;
        auto sf = std::make_shared<_Object>(f);
        _Object::idToObject[id] = sf;
        _Object::inodeToObject[ino] = sf;

        sf->addParent(parent);
        parent->addChild(sf);

        return sf;
    }




    void _Object::addRelationship(GDriveObject other, std::vector<GDriveObject> &relationship){
        if( std::find(relationship.begin(), relationship.end(), other) == relationship.end() ){
            relationship.emplace_back(other);
        }
    }



}