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
#include <atomic>
#include <boost/compute/detail/lru_cache.hpp>

#include "DownloadBuffer.h"
std::string getRFC3339StringFromTime(const struct timespec &time);
struct timespec getTimeFromRFC3339String(std::string_view str_date);


namespace DriveFS {

    class FileSystem;
    class _Object;
    typedef std::shared_ptr<_Object> GDriveObject;

    class _Object : public ::File{
    public:

//        static GDriveObject buildRoot(bsoncxx::document::view document);
//        static void updateInode(ino_t ino, bsoncxx::document::view document);
//        void updateInode(bsoncxx::document::view document);
//        _Object(ino_t ino, bsoncxx::document::view document); // default object creation
        _Object(ino_t inode, const std::string &id, std::string &&name,
                std::string &&modifiedDate, std::string &&createdDate,
                std::string &&mimeType, size_t &&size, std::string &&md5,
                int uid, int gid, int mode, bool trashed);
        _Object(ino_t ino, const std::string &id, const char *name, mode_t mode, bool isFile);
        _Object(const DriveFS::_Object&);
        _Object(DriveFS::_Object&&);
        virtual ~_Object();
    public:
        static AutoResetEvent insertEvent;

        inline const std::string& getName() const{return m_name;}
        inline const std::string& getId() const{return m_id;}
        inline bool getIsFolder() const {return isFolder;};
//        bool removeChild(GDriveObject child);
//        bool removeParent(GDriveObject child);

        inline bool getIsUploaded() const{return isUploaded;}
        inline void setIsUploaded(bool status) {isUploaded = status;}
        inline bool getIsTrashed() const {return trashed; }
        void trash();
        static void trash(GDriveObject file);
        std::string md5() const { return md5Checksum; };
//        GDriveObject findChildByName( const char *name) const ;
//        bsoncxx::document::value to_bson(bool includeId=true) const;
//        bsoncxx::document::value to_rename_bson() const;
        std::string getCreatedTimeAsString() const;
        void setName(const char *name){
            m_name = name;
        }

        /*
         * member function called for decreasing lookup counts
         */
        void forget(uint64_t nLookup);

    protected:
        _Object(); // used for creating a default object when creating root folders
//        void updateProperties(bsoncxx::document::view document);
        bool trashed, starred;
        std::string m_id, mime_type, selflink, md5Checksum;
        uint_fast64_t version;
        bool isFolder, isTrashable, canRename;
        bool isUploaded;


    };

}

#endif //DRIVEFS_FILE_H
