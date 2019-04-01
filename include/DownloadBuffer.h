//
// Created by eric on 04/02/18.
//

#ifndef DRIVEFS_DOWNLOADBUFFER_H
#define DRIVEFS_DOWNLOADBUFFER_H

#include <memory>
#include <future>
#include <atomic>
#include <easylogging++.h>
#include <atomic>
#include <boost/compute/detail/lru_cache.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include "autoresetevent.h"

//using __no_collision_download__ = std::vector<unsigned char>;
struct __no_collision_download__;
using DownloadItem = std::shared_ptr< __no_collision_download__>;
using WeakBuffer   = std::weak_ptr< __no_collision_download__>;

//
struct __no_collision_download__{

    __no_collision_download__(): buffer(nullptr)
    {
////        created++;
//        LOG(INFO) << (created++) + 1<< " collision downloads created available";
    }

//    __no_collision_download__(__no_collision_download__ &that){
//        this->buffer = new std::vector(*that.buffer);
//        this->size = that.size;
//        this->name = that.name;
//        this->last_access = that.last_access;
//        this->isInvalid  = that.isInvalid;
//        LOG(INFO) << (created++) + 1<< " collision downloads copied";
//    }
    __no_collision_download__(__no_collision_download__ &&that){
        this->buffer = that.buffer;
        that.buffer = nullptr;

    }
    ~__no_collision_download__(){
        if(buffer != nullptr){
            delete buffer;
        }
    }
    std::vector<unsigned char>* buffer;
    AutoResetEvent event;


};


class PriorityCache{
public:
    PriorityCache(size_t block_download_size, size_t max_cache_size):
            idToDownload( std::ceil(max_cache_size * 1.2 / block_download_size )),
            m_block_download_size(block_download_size){
    }
    void insert(std::string const &id, DownloadItem const &item){
        std::lock_guard lock(_access);
        idToDownload.insert(id, item);
    }

    void remove(std::string const &id){
        std::lock_guard lock(_access);
        idToDownload.insert(id, nullptr);
    }

    DownloadItem get(std::string const &id){
        std::lock_guard lock(_access);
        boost::optional<DownloadItem> maybeDownload = idToDownload.get(id);
        if(maybeDownload){
            return *maybeDownload;
        }
        return nullptr;
    }

public:
    size_t m_block_download_size;

private:
    boost::compute::detail::lru_cache<std::string, DownloadItem> idToDownload; // queue storing cached data by last access
    boost::recursive_mutex  _access;
//    friend
};

#endif //DRIVEFS_DOWNLOADBUFFER_H
