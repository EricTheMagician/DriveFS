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

namespace DriveFS {

    class _File;
    typedef std::shared_ptr<_File> DriveFile;
    class _File : ::File{

        static std::map<std::string_view, DriveFile> idToFile;

    private:
        std::vector<DriveFile> parents;
        bool trashed, starred;
        std::string_view id, mime_type, selflink,title, md5Checksum;
        uint_fast64_t version;

    };

}

#endif //DRIVEFS_FILE_H
