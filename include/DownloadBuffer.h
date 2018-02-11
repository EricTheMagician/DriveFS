//
// Created by eric on 04/02/18.
//

#ifndef DRIVEFS_DOWNLOADBUFFER_H
#define DRIVEFS_DOWNLOADBUFFER_H

#include <memory>
#include <boost/heap/fibonacci_heap.hpp>
#include <future>
#include <string_view>
#include <atomic>
#include <autoresetevent.h>

class __no_collision_download__;
typedef std::shared_ptr< __no_collision_download__> DownloadItem;
typedef std::weak_ptr< __no_collision_download__> WeakBuffer;
typedef typename boost::heap::fibonacci_heap<DownloadItem>::handle_type heap_handle;

class __no_collision_download__{
public:
    std::vector<unsigned char>* buffer;
    size_t size = 0;
    std::string_view name;
    std::shared_future<void> future;

    int64_t last_access=0;
    heap_handle handle;
    bool isInvalid=false;
    AutoResetEvent event;

    __no_collision_download__(): buffer(nullptr){}

    ~__no_collision_download__(){
        if(buffer != nullptr){
            delete buffer;
        }
    }

    bool operator()(const __no_collision_download__& n1, const __no_collision_download__& n2) const
    {
        return n1.last_access > n2.last_access;
    }
};

template <class file_t>
class PriorityCache{
    const size_t maxCacheSize;

public:
    PriorityCache(size_t size):cacheSize(0), maxCacheSize(size), cache(new boost::heap::fibonacci_heap<DownloadItem>), sema(1){
    }
    void insert(file_t file, uint64_t chunkNumber, size_t size, DownloadItem item){
        sema.wait();
        auto handle = cache->push(item);
        (*file->heap_handles)[chunkNumber] = handle;
        auto newCacheSize = cacheSize.fetch_add(size, std::memory_order_release) + size;
        sema.signal();
    }

    void updateAccessTime(file_t file, uint64_t chunkNumber, DownloadItem item){
        sema.wait();
        item->last_access = time(NULL);
        cache->update(file->heap_handles->at(chunkNumber), item);
        sema.signal();
    }

private:
    boost::heap::fibonacci_heap<DownloadItem> *cache; // queue storing cached data by last access
    std::atomic<size_t> cacheSize; // total bytes of data in cache
    AutoResetEvent sema;
};

#endif //DRIVEFS_DOWNLOADBUFFER_H
