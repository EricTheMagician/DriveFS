//
// Created by eric on 31/01/18.
//

#pragma once
#include <vector>
#include <cstdint>
#include "BaseFileSystem.h"

class FolderIO {

public:
    FolderIO(fuse_req_t _req, uint64_t nChildren):
        req(_req),
        totalSize(256*nChildren),
        buffer(new std::vector<char>(256*nChildren)){
    }
    ~FolderIO(){
        delete buffer;
    }

    inline void addDirEntry(const char* name, const struct stat &attribute){
        size_t sz = fuse_add_direntry(req, nullptr, 0, name, nullptr, 0);
        if(totalSize < (accumulated_size + sz)){
            do{
                totalSize *= 1.5;
            }
            while(totalSize <= (accumulated_size + sz) );
            buffer->resize(totalSize);
        }

        fuse_add_direntry(req, buffer->data() + accumulated_size,
                          totalSize-accumulated_size,
                          name, &attribute, accumulated_size + sz);
        accumulated_size += sz;
    }

    inline void done(){
        buffer->resize(accumulated_size);
    }
    std::vector<char> *buffer;
    size_t accumulated_size = 0;
private:
    uint64_t totalSize;
    fuse_req_t req;
};



