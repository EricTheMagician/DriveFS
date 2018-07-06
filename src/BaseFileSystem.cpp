//
// Created by eric on 31/01/18.
//

#include "BaseFileSystem.h"

int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
                             off_t off, size_t maxsize)
{
    if (off < bufsize) {
        auto status = fuse_reply_buf(req, buf + off,
                              std::min(bufsize - off, maxsize));
        while(status != 0){
            fuse_reply_buf(req, buf + off,
                           std::min(bufsize - off, maxsize));
        }
        return status;
    }
    else
        return fuse_reply_buf(req, NULL, 0);
}