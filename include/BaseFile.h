//
// Created by eric on 27/01/18.
//

#pragma once

#include <sys/stat.h>
#include <fuse_lowlevel.h>
#include <autoresetevent.h>

class File{

public:
    static struct fuse_session *session;
    struct stat attribute;
    File():m_event(1), lookupCount(0){}
    File(const char *name):m_event(1), lookupCount(0), m_name(name){}
    std::atomic_uint64_t lookupCount; // for filesystem lookup count
    AutoResetEvent m_event;
protected:
    std::string m_name;

};



