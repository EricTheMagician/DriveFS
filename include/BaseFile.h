//
// Created by eric on 27/01/18.
//

#pragma once

#if FUSE_USE_VERSION >= 30
#include <fuse3/fuse_lowlevel.h>
#else
#include <fuse/fuse_lowlevel.h>
#endif
#include <autoresetevent.h>
#include "DownloadBuffer.h"
#include <unistd.h>
#include <sys/stat.h>
#include <atomic>
#include "lockable.h"

class File: public Lockable{

public:
    static struct fuse_session *session;
    static uid_t executing_uid;
    static gid_t executing_gid;
    struct stat attribute;
    File(): m_handle_creation_event(1), lookupCount(0){
        memset(&attribute, 0, sizeof(struct stat));
    }
    File(const char *name): m_handle_creation_event(1), lookupCount(0), m_name(name){
        memset(&attribute, 0, sizeof(struct stat));
    }

    virtual ~File() = default;
    inline size_t getFileSize() const { return attribute.st_size; }
    inline const __ino_t & getInode() const { return attribute.st_ino; }
//    void create_heap_handles(size_t write_buffer_size);
public:
    std::atomic<int64_t> lookupCount; // for filesystem lookup count
protected:
    std::string m_name;

private:
    AutoResetEvent m_handle_creation_event;


};



