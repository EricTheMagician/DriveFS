//
// Created by eric on 27/01/18.
//

#pragma once

#include <sys/stat.h>
#include <fuse_lowlevel.h>
#include <autoresetevent.h>
#include "DownloadBuffer.h"
class File{

public:
    static struct fuse_session *session;
    struct stat attribute;
    File():m_buffers(nullptr), heap_handles(nullptr), m_event(1), lookupCount(0){}
    File(const char *name):m_event(1), lookupCount(0), m_name(name), m_buffers(nullptr){}
    inline size_t getFileSize() const { return attribute.st_size; }
    void create_heap_handles(size_t write_buffer_size);
public:
    std::vector<WeakBuffer> *m_buffers; // a vector pointing to possible download m_buffers
    std::vector<heap_handle> *heap_handles;
    std::atomic_uint64_t lookupCount; // for filesystem lookup count

    AutoResetEvent m_event;
protected:
    std::string m_name;


};



