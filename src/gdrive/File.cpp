//
// Created by eric on 27/01/18.
//

#include "gdrive/File.h"
#include "adaptive_time_parser.h"
#include <ctime>

static adaptive::datetime::adaptive_parser parser { adaptive::datetime::adaptive_parser::full_match, {
    "%Y %dth %B %H:%M %p",
    "%Y %dth %B %I:%M %p",
} };


namespace DriveFS{
    std::map<ino_t, GDriveObject> _Object::inodeToObject;
    std::map<std::string, GDriveObject> _Object::idToObject;

    _Object::_Object(){
    }

    _Object::_Object(ino_t ino, bsoncxx::document::view document){
        attribute.st_ino = ino;
        m_id = document["id"].get_utf8().value.to_string();

        auto f = document["mimeType"];
        if(f.get_utf8().value == "application/vnd.google-apps.folder"){
                isFolder = true;
                attribute.st_mode = S_IFDIR | S_IXUSR | S_IXGRP | S_IXOTH;
        }else{
                isFolder = false;
                attribute.st_mode = S_IFREG | S_IXUSR | S_IXGRP | S_IXOTH;
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

    }
    GDriveObject _Object::buildRoot(bsoncxx::document::view document){
            _Object f;

            f.attribute.st_ino = 1;
            f.attribute.st_size = 0;
            struct timespec now{time(nullptr),0};
            f.attribute.st_atim = now;
            f.attribute.st_mtim = now;
            f.attribute.st_ctim = now;
            f.attribute.st_mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;
            f.isFolder = true;
            std::string id(document["id"].get_utf8().value.to_string());
            f.m_id = id;
            auto sf = std::make_shared<_Object>(f);
            _Object::idToObject["id"] = sf;
            _Object::inodeToObject[1] = sf;
            return sf;
    }

    void _Object::addRelationship(GDriveObject other, std::vector<GDriveObject> &relationship){
        if( std::find(relationship.begin(), relationship.end(), other) == relationship.end() ){
            relationship.emplace_back(other);
        }
    }



}