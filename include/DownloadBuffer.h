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
#include <easylogging++.h>

class __no_collision_download__;
struct compare_download_items;
typedef std::shared_ptr< __no_collision_download__> DownloadItem;
typedef std::weak_ptr< __no_collision_download__> WeakBuffer;

class __no_collision_download__{
public:
    std::vector<unsigned char>* buffer=nullptr;
    size_t size = 0;
    std::string name;
    std::shared_future<void> future;

    int64_t last_access=0;
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

    bool operator<(const __no_collision_download__ &that){
        return this->last_access < that.last_access;
    }

    bool operator<=(const __no_collision_download__ &that){
        return this->last_access <= that.last_access;
    }


};

struct compare_download_items{
    bool operator()(const DownloadItem &lhs, const DownloadItem &rhs) const{
        return lhs->last_access > rhs->last_access;
    }
};

typedef typename boost::heap::fibonacci_heap<DownloadItem, boost::heap::compare<compare_download_items>> fibHeap;
typedef fibHeap::handle_type heap_handle;
template <class file_t>
class PriorityCache{
public:
    PriorityCache(size_t block_download_size, size_t max_cache_size):
            cacheSize(0), maxCacheSize(max_cache_size),
            cache( new fibHeap), sema(1),
    m_block_download_size(block_download_size){
    }
    void insert(file_t file, uint64_t chunkNumber, size_t size, DownloadItem item){
        sema.wait();
        auto handle = cache->push(item);
        file->create_heap_handles(m_block_download_size);
        *(file->heap_handles->data()+chunkNumber) = handle;
        auto newCacheSize = cacheSize.fetch_add(size, std::memory_order_release) + size;
        while(newCacheSize > maxCacheSize){
            auto &head = cache->top();
            auto sz = head->buffer == nullptr ? head->size : head->buffer->size();
            newCacheSize = cacheSize.fetch_sub(sz, std::memory_order_relaxed) - sz;
            VLOG(10) << "Deleting cache item "<< head->name <<" of size " << sz;
            cache->pop();


        }
        VLOG(10) << "Inserting " << std::to_string(chunkNumber);
        sema.signal();
    }

    void updateAccessTime(file_t file, uint64_t chunkNumber, DownloadItem item){
        sema.wait();
        item->last_access = time(NULL);
        cache->update(file->heap_handles->at(chunkNumber), item);
        sema.signal();
    }

    void updateAccessTime(heap_handle handle, const DownloadItem &item){
        sema.wait();
        item->last_access = time(NULL);
        cache->update(handle);
        sema.signal();
    }
public:
    size_t maxCacheSize;
    size_t m_block_download_size;

private:
    fibHeap *cache; // queue storing cached data by last access
    std::atomic<size_t> cacheSize; // total bytes of data in cache

    AutoResetEvent sema;

};

#endif //DRIVEFS_DOWNLOADBUFFER_H
