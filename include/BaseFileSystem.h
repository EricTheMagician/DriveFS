//
// Created by eric on 31/01/18.
//

#pragma once
#include <fuse3/fuse_lowlevel.h>
#include <future>
#include <functional>
#include <type_traits>
#include <easylogging++.h>

template< class Function, class... Args>
std::future<typename std::result_of<Function(Args...)>::type> SFAsync( Function&& f, Args&&... args )
{
//  http://stackoverflow.com/questions/16296284/workaround-for-blocking-async
    typedef typename std::result_of<Function(Args...)>::type R;
    auto bound_task = std::bind(std::forward<Function>(f), std::forward<Args>(args)...);
    std::packaged_task<R()> task(std::move(bound_task));
    auto ret = task.get_future();
    std::thread t(std::move(task));
    t.detach();
    return ret;

}
int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
                             off_t off, size_t maxsize);

class BaseFileSystem {
public:
    virtual struct fuse_lowlevel_ops get_ops() = 0;
};



