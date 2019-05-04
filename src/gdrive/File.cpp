//
// Created by eric on 27/01/18.
//

#include "gdrive/File.h"
#include "gdrive/FileIO.h"
#include <string_view>
#include "adaptive_time_parser.h"
#define ONLY_C_LOCALE 1
#include "date.h"
#undef ONLY_C_LOCALE
#include <ctime>
#include <easylogging++.h>
#include "Database.h"

constexpr std::string_view google_folder_type = "application/vnd.google-apps.folder";


static adaptive::datetime::adaptive_parser parser { adaptive::datetime::adaptive_parser::full_match, {
    "%Y-%m-%dT%H:%M:%SZ"
//    "%Y-%m-%dT%H:%M:%SZ
} };


struct timespec getTimeFromRFC3339String(std::string_view str_date){
    date::sys_time<std::chrono::milliseconds> tp;
    std::stringstream ss;
    ss << str_date;
//    ss >> date::parse("%FT%TZ", tp);
    ss >> date::parse("%F %T", tp);
    int64_t epoch = tp.time_since_epoch().count();
    return {epoch/1000, epoch % 1000};

}

std::string getRFC3339StringFromTime(const struct timespec &time){
    char date[100];
    struct tm *timeinfo = localtime (&time.tv_sec);
    strftime(&date[0], 100, "%FT%T", timeinfo);

    std::string s(date);
    uint16_t ms = (time.tv_nsec / 1000000) % 1000; // extra safety to ensure thaat the number is between [0,999]
    if(ms < 10){
        s += ".00" + std::to_string(ms) + "Z";
    }else if(ms < 100){
        s += ".0" + std::to_string(ms) + "Z";
    }else {
        s += "." + std::to_string(ms) + "Z";
    }
    return s;
}

namespace DriveFS{


    _Object::_Object():File(), isUploaded(false), version(1){
    }

    _Object::_Object(ino_t ino, const std::string &id, const char *name, mode_t mode, bool isFile):
            File(name),
            isFolder(!isFile),
            isTrashable(true), canRename(true),
            trashed(false),
            version(1)
    {
        memset(&attribute, 0, sizeof(struct stat));
        if(isFile) {
            attribute.st_size = 0;
            attribute.st_nlink = 0;
            attribute.st_mode = mode | S_IFREG;
        }else {
            attribute.st_size = 4096;
            attribute.st_nlink = 1;
            attribute.st_mode = mode | S_IFDIR;
        }
        attribute.st_ino = ino;
        isUploaded = false;

        struct timespec now{time(nullptr),0};
        attribute.st_ctim = now;
        attribute.st_mtim = now;
        attribute.st_atim = now;

        attribute.st_uid = executing_uid;
        attribute.st_gid = executing_gid;
        isUploaded = false;

        m_id = id;

    }

    _Object::_Object(const DriveFS::_Object& that):File() {
        lookupCount = that.lookupCount.load(std::memory_order_acquire);
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
//        if (that.m_buffers != nullptr) {
//            m_buffers = new std::vector<WeakBuffer>(*(that.m_buffers));
//        }else{
//            m_buffers = nullptr;
//        }
//        if(that.heap_handles != nullptr){
//            heap_handles = new std::vector<heap_handle>(*(that.heap_handles));
//        }else{
//            heap_handles = nullptr;
//        }
        isUploaded = that.isUploaded;
    }

    _Object::_Object(DriveFS::_Object&& that): File(){
        lookupCount = that.lookupCount.load(std::memory_order_acquire);
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
//        m_buffers = that.m_buffers;
//        that.m_buffers = nullptr;
//        heap_handles = that.heap_handles;
//        that.heap_handles = nullptr;
        isUploaded = that.isUploaded;
    }
    _Object::_Object(ino_t inode, std::string const &id, std::string &&name,
                     std::string &&modifiedDate, std::string &&createdDate,
            std::string &&mimeType, size_t &&size, std::string &&md5,
            int uid, int gid, int mode, bool trashed): File(name.c_str()),
            isFolder(mimeType==google_folder_type),
            m_id(std::move(id)),
            trashed(trashed),
            version(1)

            {
        struct stat &attribute = this->attribute;
        if(this->isFolder){
            attribute.st_mode = S_IFDIR | S_IXUSR | S_IXGRP | S_IXOTH;
            attribute.st_size = 4096;
            attribute.st_blocks = 0;
            this->isUploaded = true;
        }else{
            attribute.st_mode = S_IFREG | S_IXUSR | S_IXGRP | S_IXOTH;
            attribute.st_size = size;
            attribute.st_blocks = attribute.st_size / S_BLKSIZE + std::min<decltype(attribute.st_size)>(attribute.st_size % S_BLKSIZE,1);
            if(!md5.empty()){
                this->isUploaded = true;
                this->md5Checksum = std::move(md5);
            }else if(attribute.st_size == 0){
                this->isUploaded = true;
            }else{
                this->isUploaded = false;
            }
        }

        attribute.st_ino = inode;
        attribute.st_mtim = getTimeFromRFC3339String(modifiedDate);
        attribute.st_ctim = getTimeFromRFC3339String(createdDate);
        attribute.st_atim = attribute.st_mtim;
        attribute.st_nlink = 1;
        attribute.st_uid = uid == -1 ? executing_uid:uid;
        attribute.st_gid = gid == -1 ? executing_gid:gid;
        attribute.st_gid = gid == -1 ? executing_gid:gid;

    }



    /*
    void _Object::updateInode(bsoncxx::document::view document) {
        attribute.st_blksize = 1;

        auto f = document["mimeType"];
        if(f.get_utf8().value.compare("application/vnd.google-apps.folder") == 0) {
            isFolder = true;
            attribute.st_size = 4096;
            attribute.st_blocks = 0;
            isUploaded = true;
        }else{
            isFolder = false;
            attribute.st_mode = S_IFREG | 0755;
            auto sz = document["size"];
            if(sz){
                attribute.st_size = std::strtoll(std::string(sz.get_utf8().value).c_str(), nullptr, 10);
            }else{
                sz = document["quotaBytesUsed"];
                if(sz){
                    attribute.st_size = std::strtoll(std::string(sz.get_utf8().value).c_str(), nullptr, 10);
                }else {
                    attribute.st_size = 0;
                }

            }
            attribute.st_blocks = attribute.st_size / S_BLKSIZE + std::min<int64_t>(attribute.st_size % S_BLKSIZE,1);
            auto md5 = document["md5Checksum"];
            if(md5){
                isUploaded = true;
                md5Checksum = md5.get_utf8().value;
            }else if(attribute.st_size == 0){
                isUploaded = true;
            }else{
                isUploaded = false;
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
        trashed = document["trashed"].get_bool().value;
        m_name = document["name"].get_utf8().value;
        attribute.st_mtim = getTimeFromRFC3339String(document["modifiedTime"].get_utf8().value);
        attribute.st_ctim = getTimeFromRFC3339String(document["createdTime"].get_utf8().value);
        updateProperties(document);
    }

    void _Object::updateProperties(bsoncxx::document::view document){
        bool uid_found=false, gid_found=false;
        auto maybeProperties = document["appProperties"];
        if(maybeProperties){
            auto appProperties = maybeProperties.get_document().value;
            auto maybeProperty = appProperties[APP_UID];
            if(maybeProperty){
                uid_found = true;
                if(maybeProperty.type() == bsoncxx::type::k_int32) {
                    attribute.st_uid = maybeProperty.get_int32();
                }else if(maybeProperty.type() == bsoncxx::type::k_int64){
                    attribute.st_uid = maybeProperty.get_int64();
                }else if(maybeProperty.type() == bsoncxx::type::k_utf8){
                    attribute.st_uid = std::strtoul(std::string(maybeProperty.get_utf8().value).c_str(), nullptr, 10);
                }else{
                    LOG(INFO)<< "type is " << (uint8_t) maybeProperty.type();
//                    attribute.st_uid = maybeProperty.get_int64();
                }
            }

            maybeProperty = appProperties[APP_GID];
            if(maybeProperty){
                gid_found = true;
                if(maybeProperty.type() == bsoncxx::type::k_int32) {
                    attribute.st_gid = maybeProperty.get_int32();
                }else if(maybeProperty.type() == bsoncxx::type::k_int64){
                    attribute.st_gid = maybeProperty.get_int64();
                }else if(maybeProperty.type() == bsoncxx::type::k_utf8){
                    attribute.st_gid = std::strtoul(std::string(maybeProperty.get_utf8().value).c_str(), nullptr, 10);
                }else{
 //                   attribute.st_gid = maybeProperty.get_int32();
                }
            }

            maybeProperty = appProperties[APP_MODE];
            if(maybeProperty){
                if(maybeProperty.type() == bsoncxx::type::k_int32) {
                    attribute.st_mode = maybeProperty.get_int32();
                }else if(maybeProperty.type() == bsoncxx::type::k_int64){
                    attribute.st_mode = maybeProperty.get_int64();
                }else if(maybeProperty.type() == bsoncxx::type::k_utf8){
                    attribute.st_mode = std::strtoul(std::string(maybeProperty.get_utf8().value).c_str(), nullptr, 10);
                }else{
   ///                 attribute.st_mode = maybeProperty.get_int32();
                }
            }
        }

        if(!uid_found) attribute.st_uid = executing_uid;
        if(!gid_found) attribute.st_gid = executing_gid;

    }
*/

    void _Object::trash(){
        trashed = true;
    }

    void _Object::trash(GDriveObject file){
        if(!file)
            return;
        file->trash();
    }

//    GDriveObject _Object::findChildByName(const char *name) const {
//#warning bsoncxx
//        for (auto child: children) {
//            if (child->getName().compare(name) == 0) {
//                return child;
//            }
//        }
//        return nullptr;
//    }

    /*
    bsoncxx::document::value _Object::to_bson(bool includeId) const
    {

        bsoncxx::builder::stream::document doc;
        if(includeId) {
            doc << "id" << m_id;
        }
        if(isFolder) {
            doc << "mimeType" << "application/vnd.google-apps.folder";
        }else{
            if(mime_type.empty()) {
                doc << "mimeType" << "octet-stream";
            }else{
                doc << "mimeType" << mime_type;
            }
            doc << "size" << std::to_string(attribute.st_size);
        }

        doc << "name" << m_name ;
        doc << "modifiedTime" << getRFC3339StringFromTime(attribute.st_mtim);
        doc << "createdTime" << getRFC3339StringFromTime(attribute.st_ctim);
        doc << "trashed" << trashed;
        bsoncxx::builder::stream::array array;
        for(auto parent: parents){
            array << parent->getId();
        }
        doc << "parents" << array;
        doc << "appProperties"
            << open_document << APP_MODE << (int) attribute.st_mode
                             << APP_UID  << (int) attribute.st_uid
                             << APP_GID  << (int) attribute.st_gid
            << close_document;

        return doc.extract();
    }
*/


    std::string _Object::getCreatedTimeAsString() const noexcept{
        return getRFC3339StringFromTime(attribute.st_ctim);
    }

    std::string _Object::getModifiedTimeAsString() const noexcept{
        return getRFC3339StringFromTime(attribute.st_mtim);
    }



}
