//
// Created by eric on 27/01/18.
//

#include "gdrive/File.h"
#include "gdrive/FileIO.h"
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

std::string getRFC3339StringFromTime(const struct timespec &time){
    char date[100];
    struct tm *timeinfo = localtime (&time.tv_sec);
    strftime(&date[0], 100, "%FT%T", timeinfo);

    std::string s(date);
    s += "." + std::to_string(time.tv_nsec) + "Z";
    return s;
}

namespace DriveFS{

    std::map<ino_t, GDriveObject> _Object::inodeToObject;
    std::map<std::string, GDriveObject> _Object::idToObject;
    PriorityCache<GDriveObject>_Object::cache = PriorityCache<GDriveObject>(512*1024*1024);

    _Object::_Object():File(), m_buffers(nullptr), heap_handles(nullptr), isUploaded(false){
    }

    _Object::_Object(ino_t ino, const std::string &id, const char *name, mode_t mode, bool isFile):
            File(name),
            isFolder(!isFile),
            isTrashable(true), canRename(true),
            m_buffers(nullptr),
            heap_handles(nullptr)
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

        struct timespec now{time(nullptr),0};
        attribute.st_ctim = now;
        attribute.st_mtim = now;
        attribute.st_atim = now;

        attribute.st_uid = 65534; // nobody
        attribute.st_gid = 65534;
        isUploaded = false;

        m_id = id;
    }

    _Object::_Object(const DriveFS::_Object& that):File() {
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
        if (that.m_buffers != nullptr) {
            m_buffers = new std::vector<WeakBuffer>(*(that.m_buffers));
        }else{
            m_buffers = nullptr;
        }
        if(that.heap_handles != nullptr){
            heap_handles = new std::vector<heap_handle>(*(that.heap_handles));
        }else{
            heap_handles = nullptr;
        }
        isUploaded = that.isUploaded;
    }
    _Object::_Object(DriveFS::_Object&& that): File(){
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
        m_buffers = that.m_buffers;
        that.m_buffers = nullptr;
        heap_handles = that.heap_handles;
        that.heap_handles = nullptr;
        isUploaded = that.isUploaded;
    }

    _Object::_Object(ino_t ino, bsoncxx::document::view document):File(), m_buffers(nullptr), heap_handles(nullptr),
        isUploaded(true)
    {
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
            createVectorsForBuffers();

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
        f.isUploaded = false;

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

        f.isUploaded = false;
        f.canRename = false;

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
        m_event.wait();
        if( std::find(relationship.begin(), relationship.end(), other) == relationship.end() ){
            relationship.emplace_back(other);
        }
        m_event.signal();
    }

    void _Object::updateInode(ino_t ino, bsoncxx::document::view document) {

    }

    _Object::~_Object(){
        if(m_buffers != nullptr){
            delete m_buffers;
        }

        if(heap_handles != nullptr){
            delete heap_handles;
        }
    }

    void _Object::createVectorsForBuffers(){
        auto size = getFileSize();
        if(m_buffers != nullptr){
            delete m_buffers;
        }
        auto nChunks = size/FileIO::write_buffer_size + 1;
        m_buffers = new std::vector<WeakBuffer>(nChunks);
        heap_handles = new std::vector<heap_handle>(nChunks);

    }

    void _Object::updatLastAccessToCache(uint64_t chunkNumber){
        DownloadItem item = m_buffers->at(chunkNumber).lock();
        if(item) {
            cache.updateAccessTime((*heap_handles)[chunkNumber], item);
        }
    }

    void _Object::trash(){
        if(lookupCount.load(std::memory_order_acquire) == 0){
            _Object::inodeToObject.erase(attribute.st_ino);
            _Object::idToObject.erase(m_id);
        }
    }

    GDriveObject _Object::findChildByName(const char *name) const {
        for (auto child: children) {
            if (child->getName().compare(name) == 0) {
                return child;
            }
        }
        return nullptr;
    }

    bsoncxx::document::value _Object::to_bson()
    {

        bsoncxx::builder::stream::document doc;
        doc << "id" << m_id;
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

        return doc.extract();
    }

    bool _Object::removeChild(GDriveObject child){
        m_event.wait();
        children.erase(std::find(children.begin(),children.end(), child));
        m_event.signal();
    }

    bool _Object::removeParent(GDriveObject parent){
        m_event.wait();
        parents.erase(std::find(parents.begin(),parents.end(), parent));
        m_event.signal();
    }

}