//
// Created by eric on 27/01/18.
//

#pragma once

#include <sys/stat.h>
#include <fuse_lowlevel.h>

class File{

public:
    static struct fuse_session *session;
    struct stat attribute;
};



