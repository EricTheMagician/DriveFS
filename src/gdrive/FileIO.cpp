//
// Created by eric on 04/02/18.
//

#include <thread>
#include <cpprest/http_client.h>
#include <easylogging++.h>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio.hpp>
#include <boost/range/iterator_range.hpp>
#include <mutex>
#include <sys/sendfile.h>
#include <atomic>
#include <cstdint>
#include <inttypes.h>
#include <cpprest/rawptrstream.h>

#if FUSE_USE_VERSION >= 30
#include <fuse3/fuse.h>
#else
#include <fuse/fuse.h>
#endif
#include "gdrive/Account.h"
#include "gdrive/FileIO.h"
#include "gdrive/FileManager.h"
#include "Database.h"

#define INVALID_CACHE_DELETED 4



static std::mutex deleteCacheMutex;
#define DBCACHENAME "CacheDB"
using namespace web::http::client;          // HTTP client features
using Object = DriveFS::_Object;

inline uint64_t getChunkStart(uint64_t start, uint64_t buffer_size){
    uint64_t chunkNumber = start / buffer_size;
    return chunkNumber * buffer_size;
}

inline uint64_t getChunkNumber(uint64_t start, uint64_t buffer_size){
    return  start / buffer_size;
}

static boost::asio::thread_pool *DownloadPool, *ReadPool;
static boost::asio::thread_pool *UploadPool;
static boost::asio::thread_pool WritePool(1);


/*
 * reply to the fuse request with the data from item if the data does not span multiple chunks.
 * if it does span multiple chunks, copy it to buf. the first part will have spillOverPreCopy as non null.
 */
void handleReplyData(fuse_req_t req, __no_collision_download__ *item, std::vector<uint8_t> *buf, size_t size, off_t off, bool isSpillOver, size_t  * spillOverPreCopy){
    const uint64_t start = off % DriveFS::FileIO::block_download_size;
//    const uint64_t size2 = isSpillOver ? item->buffer->size() - start: size;
    LOG_IF((start+size) > item->buffer->size(),
           ERROR)  << "About to fail assertion\n"
//                   << "ID: " << m_file->getId()
//                   << "\nfileSize: " << m_file->getFileSize()
                   << "\nbufferSize: " << item->buffer->size()
                   << "\nstart: " << start
                   << "\n(off,size)  (" << off << ", " << size  << ")";

    assert((start+size) <= item->buffer->size());
    if(isSpillOver){
        memcpy(buf->data() + (*spillOverPreCopy), item->buffer->data()+start, size);
        if(*spillOverPreCopy == 0){
            *spillOverPreCopy = size;
        }else{
            assert( (size + (*spillOverPreCopy)) == buf->size());
//            struct fuse_bufvec fbuf = FUSE_BUFVEC_INIT(buf->size());
//            fbuf.buf[0].mem = buf->data();
//            fbuf.buf[0].size = buf->size();
//            fuse_reply_data(req, &fbuf, FUSE_BUF_NO_SPLICE);
            fuse_reply_buf(req, reinterpret_cast<char *>(item->buffer->data()+start),  size);

            delete buf;
        }
    }else{
//        struct fuse_bufvec buf = FUSE_BUFVEC_INIT(size);
//        buf.buf[0].mem = item->buffer->data()+start;
//        buf.buf[0].size = item->buffer->size()-start;
//        fuse_reply_data(req, &buf, FUSE_BUF_NO_SPLICE);
        fuse_reply_buf(req, reinterpret_cast<char *>(item->buffer->data()+start),  size);
    }

}

namespace DriveFS{
    using FileManager::DownloadCache;
    bool FileIO::finishedInitialCheck = false;

    uint_fast32_t FileIO::write_buffer_size = 32*1024*1024, //64mb
            FileIO::block_read_ahead_start = UINT32_MAX,
            FileIO::block_read_ahead_end = UINT32_MAX,
            FileIO::block_download_size=1024*1024*2;
    uint_fast8_t FileIO::number_of_blocks_to_read_ahead = 0;
    bool FileIO::download_last_chunk_at_the_beginning = false;
    bool FileIO::move_files_to_download_on_finish_upload = true;
    std::atomic<int64_t> FileIO::cacheSize(0);
    Account* FileIO::m_account = nullptr;
    int64_t FileIO::maxCacheOnDisk = 0;


    fs::path FileIO::cachePath = "/tmp/DriveFS";
    fs::path FileIO::downloadPath = "/tmp/DriveFS/download";
    fs::path FileIO::uploadPath = "/tmp/DriveFS/upload";

    FileIO::FileIO(GDriveObject object, int flag): shared_obj(),
            m_file(std::move(object)),
            b_is_uploading(false),
            b_is_cached(false),
            b_needs_uploading(false),
            m_readable( (flag & 0x8000) || (flag & O_RDONLY) || (flag & O_RDWR)),
            m_writeable((flag & O_WRONLY) || (flag & O_RDWR)),
            write_buffer(nullptr),
            write_buffer2(nullptr),
            last_write_to_buffer(0),
            first_write_to_buffer(0),
            m_event(1),
            m_fp(nullptr)
    {
        setFileName();
        assert(m_readable || m_writeable || flag == 0);
    }

    FileIO::~FileIO(){
        if(write_buffer != nullptr){
            delete write_buffer;
        }
        if(write_buffer2 != nullptr){
            delete write_buffer2;
        }

        if(isOpen()){
            VLOG(9) << "Closing file " << f_name << ".released. fd is " << m_fd << ". This is " << reinterpret_cast<uintptr_t>(this);
//            LOG(TRACE) << "Closing file " << f_name << ".released. fd is " << m_fd << ". This is " << (uintptr_t ) this;
            fclose(m_fp);
            m_fp = nullptr;
        }
    }

    void FileIO::read(fuse_req_t req, const size_t &size, const off_t &off) {
        if( (! m_file->getIsUploaded()) || (b_is_cached && isOpen()) ){
            bool once = false;
            while(!isOpen() && (!m_file->getIsUploaded() || !once) ){
                open();
                LOG_IF(once, ERROR) << "There was an error opening file " << this->m_file->getId() <<". " << strerror(errno);
                once = true;
                sleep(5);
            }
            if(isOpen()) {

                struct fuse_bufvec buf = FUSE_BUFVEC_INIT(size);
                buf.buf[0].flags = (fuse_buf_flags)(FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK);
                buf.buf[0].fd = fileno(m_fp);
                buf.buf[0].pos = off;

                fuse_reply_data(req, &buf, FUSE_BUF_SPLICE_MOVE);
                return;


            }
        }
        try {
            getFromCloud(req, size, off);
            return;
        }catch(std::exception &e){
            LOG(ERROR) << e.what();
            LOG(ERROR) << "Failed to download " << m_file->getInode() << "\noff: " << off << " size: " << size << "\n";
            fuse_reply_err(req, EIO);
        }catch(...){
            LOG(ERROR) << "Failed to download " << m_file->getInode() << "\noff: " << off << " size: " << size << "\n";
            fuse_reply_err(req, EIO);
        }

    }

    bool FileIO::download(fuse_req_t fuseReq, _Object* file, __no_collision_download__ *cache, std::string cacheName, uint64_t start, uint64_t end) {
        struct VecBuffer{

            /*
             * RAII struct for deleting the buffer if it wasn't used
             */
            VecBuffer(size_t size): buffer(new std::vector<uint8_t>(size)){}
            std::vector<uint8_t> *buffer;
            void deleteBuffer(){
                if(buffer!= nullptr)
                    delete buffer;
                buffer = nullptr;
            }
            ~VecBuffer(){
                if(buffer != nullptr)
                    delete buffer;
            }
        };

        uint_fast8_t backoff = 0;
        VecBuffer vecBuffer(end-start+1);
        uri_builder builder( std::string("files/") +  file->getId());
        builder.append_query("alt", "media");
        builder.append_query("supportsTeamDrives", "true");
        char range[100];
        snprintf( range, 100, "bytes=%lu-%lu", start, end);

        while( backoff < 7 ){
            VLOG(9) << "Downloading " << file->getName() <<"\t" << start;
            Concurrency::streams::rawptr_buffer<uint8_t> buf(vecBuffer.buffer->data(),vecBuffer.buffer->size());
            m_account->refresh_token();
            http_client client = m_account->getClient(30); // timeout after 30s

            http_request req;
            auto &headers = req.headers();

            req.set_method(methods::GET);
            req.set_request_uri(builder.to_uri());
            req.set_response_stream(buf);

            headers.add("Range", range);

            VLOG(9) << req.headers()["Range"];

            http_response resp;
            try {
                resp = client.request(req).get();
            }catch(std::exception &e){
                LOG(ERROR) << "There was an error while trying to download chunk: " << e.what();
                LOG(DEBUG) << "Chunk-range" << start << " - " << end;
                if(file) {
                    LOG(DEBUG) << "File ID " << file->getId();
                    LOG(DEBUG) << "File Size " << file->getFileSize();
                }
                LOG(DEBUG) << "Url " << builder.to_string();
                const unsigned int sleep_time = std::pow(2, backoff++);
                LOG(INFO) << "Sleeping for " << sleep_time << " seconds before retrying";
                sleep(sleep_time);
                continue;
            };

            if(resp.status_code() == 404){
                LOG(ERROR) << "Giving up for id " << file->getId();
#warning todo remove from database
                if(!fuseReq){
                    fuse_reply_err(fuseReq, ENOENT);
                }
                DownloadCache.remove(cacheName);
                cache->buffer = new std::vector<uint8_t>(0);
                cache->event.signal();
                return false;
            }

            if(resp.status_code() != 206 && resp.status_code() != 200){
                LOG(ERROR) << "Failed to get file fragment : " << resp.reason_phrase();
                try {
                    if(resp.status_code() < 500) {
                        resp.content_ready().get();
                        LOG(ERROR) << reinterpret_cast<const char *>(vecBuffer.buffer->data());
                    }else{
                        LOG(ERROR) << "status code is "<< resp.status_code();
                    }
                }catch(std::exception &e){
                    LOG(ERROR) << e.what() << " - " << resp.status_code();
                };

                const unsigned int sleep_time = std::pow(2, backoff++);
                LOG(INFO) << "Sleeping for " << sleep_time << " seconds before retrying";
                sleep(sleep_time);
                continue;
             }

            try {
                resp.content_ready().get();
                auto size = buf.size();
                if(size == (end-start+1)){
                    std::swap(cache->buffer,vecBuffer.buffer);
                    buf.close().get();
                }else{
                    continue;
                }

            }catch(std::exception &e){
                LOG(ERROR) << "There was an error while reading downloaded chunk: " << e.what();
                LOG(DEBUG) << "Chunk-range" << start << " - " << end;
                if(file) {
                    LOG(DEBUG) << "File ID " << file->getId();
                    LOG(DEBUG) << "File Size " << file->getFileSize();
                }
                LOG(DEBUG) << "Url " << builder.to_string();
                LOG(INFO) << "Retrying";
                continue;
            }
            std::atomic_thread_fence(std::memory_order_release);
            cache->event.signal();

            //write buffer to disk
            boost::asio::post(WritePool,
                               [cacheName, id=file->getId()]() -> void {
                                   DownloadItem strong_cache = DownloadCache.get(cacheName);
                                   if (strong_cache && strong_cache->buffer != nullptr) {
                                       fs::path path = downloadPath;
                                       path /= cacheName;
                                       FILE *fp = fopen(path.string().c_str(), "wb");
                                       if (fp != nullptr) {
                                           fwrite(strong_cache->buffer->data(), sizeof(unsigned char), strong_cache->buffer->size(), fp);
                                           fclose(fp);
                                           fs::permissions(path, fs::owner_all);
                                           insertFileToCacheDatabase(path, strong_cache->buffer->size());
                                       }
                                       strong_cache->event.signal();
                                       strong_cache.reset();
                                   }
                               });

    //        fs::path path = downloadPath;
    //        path /= cacheName;
    //        FILE *fp = fopen(path.string().c_str(), "wb");
    //        if (fp != nullptr){
    //            auto const * buf = cache->buffer;
    //            fwrite(buf->data(), sizeof(unsigned char), buf->size(), fp);
    //            fclose(fp);
    //            fs::permissions(path, fs::owner_all);
    //            insertFileToCacheDatabase(path, buf->size());
    //        }
    //        fs::permissions(path, fs::owner_all);


            return true;
        }

        fuse_reply_err(fuseReq, EIO);
        cache->buffer = new std::vector<uint8_t>(0);
        cache->event.signal();
        return false;
    }

    void FileIO::create_write_buffer(){
        if(write_buffer == nullptr){
            write_buffer = new std::vector<unsigned char>((int)(write_buffer_size), 0);
        }
    }
    void FileIO::create_write_buffer2(){
        create_write_buffer();
        if(write_buffer2 == nullptr){
            write_buffer2 = new std::vector<unsigned char>((int)(write_buffer_size), 0);
        }
    }

    /*
     * \param spillover:bool is it from the spillover or the original
     */
    DownloadItem FileIO::getFromCache(std::string const & chunkName, off_t off){

        fs::path path = downloadPath;
        path /= chunkName;

        if( ! fs::is_regular_file(path)) {
            return nullptr;
        }

        auto filesize = fs::file_size(path);
        if(!bufferMatchesExpectedBufferSize(filesize, off)){
            return nullptr;
        }
        auto item = std::make_shared<__no_collision_download__>();
        auto item2 = FileManager::DownloadCache.insert(chunkName, item);
        if( item != item2){
            item2->wait();
            return item2;
        }
        item->buffer = new std::vector<uint8_t>(filesize,0);
        FILE *fp = fopen(path.string().c_str(), "rb");
        if (fp == nullptr) {
            return nullptr;
        }
        auto read_size = fread( reinterpret_cast<char*>(item->buffer->data()), sizeof(unsigned char), filesize, fp);
        fclose(fp);

        if(!bufferMatchesExpectedBufferSize(read_size))
            return nullptr;

        item->event.signal();
        item->event.signal();
        return item;

    }

    void FileIO::getFromCloud(fuse_req_t req, const size_t &_size, const off_t &off){

        size_t filesize = m_file->attribute.st_size;
        if( off > filesize || _size  > filesize ){
            fuse_reply_err(req, EIO);
            return;
        }

        const uint64_t chunkNumber = getChunkNumber(off, block_download_size);
        const uint64_t chunkStart = getChunkStart(off, block_download_size);
        const auto fileSize = m_file->getFileSize();
        size_t size = _size + off > fileSize ? fileSize-off-1: _size;
        const size_t size2 = (off + size) % block_download_size;
        const bool spillOver = chunkNumber != getChunkNumber(off+size-1, block_download_size);
        if(spillOver){
            size -= size2;
        }
        std::string cacheName = m_file->getId() + "-" + std::to_string(chunkStart);
        std::vector<uint64_t> chunksToDownload;
        uint64_t spillOverPrecopy=0;

        DownloadItem item;
        std::string chunkId = m_file->getId();
        chunkId += "-";
        chunkId += std::to_string(chunkStart);

        bool repliedReq = false;

        // serialize the access to the cloud on a per file basis
        auto localFile = m_file; // keep a hard reference to m_file and use this localFile as "this" can be deleted due to the asychronisity of the code
        _Object *file = m_file.get();
        file->_mutex.lock();
        file->_mutex.unlock();


        std::vector<unsigned char> *buffer = [&spillOver, &_size]() -> std::vector<unsigned char>*{
            if(spillOver)
                return new std::vector<unsigned char>(_size);
            return nullptr;
        }();

        if ( ((item = DownloadCache.get(chunkId))  || (item = getFromCache(chunkId, chunkStart)) )
             && (item->buffer != nullptr || (item->wait() && item->buffer != nullptr) )
           ) {
            if(bufferMatchesExpectedBufferSize(item->buffer->size())){
                handleReplyData(req, item.get(), buffer, size, off % FileIO::block_download_size, spillOver, &spillOverPrecopy);
                repliedReq = !spillOver;
            }else{
                fuse_reply_err(req, EIO);
                if(spillOver){
                    delete buffer;
                }
                return;
            }

        }else{
           chunksToDownload.push_back(chunkNumber);
        }


        if(spillOver) {
            const uint64_t chunkNumber2 = chunkNumber + 1;
            const uint64_t chunkStart2 = chunkStart + block_download_size;
            if( buffer != nullptr) {

                chunkId = file->getId();
                chunkId += "-";
                chunkId += std::to_string(chunkStart2);


                cacheName = chunkId;
                DownloadItem item;

//                if ((item = DownloadCache.get(chunkId)) || (item = getFromCache(chunkId)) ){
                if ( ((item = DownloadCache.get(chunkId))  || (item = getFromCache(chunkId, chunkStart2)) )
                     && (item->buffer != nullptr || (item->wait() && item->buffer != nullptr) )
                   )
                {
                    if(bufferMatchesExpectedBufferSize(item->buffer->size())){                        
                        if(chunksToDownload.empty()){
                            // if the chunks to download is not emptty, this means that haven;t finished downloading the first part and we should wair rather send incomplete data
                            assert((spillOverPrecopy + size2) <= buffer->size());
                            handleReplyData(req, item.get(), buffer, size2, 0, spillOver, &spillOverPrecopy);
                            buffer = nullptr;
                            repliedReq = true;
                        }
                    }else{
                        fuse_reply_err(req, EIO);
                        delete buffer;
                        return;
                    }
                }
                else
                {
                    chunksToDownload.push_back(chunkNumber2);
                }

                item.reset();
            }else{
                chunksToDownload.push_back(chunkNumber2);
            }

        }


        int off2 = off % block_download_size;
        if( off2 >= FileIO::block_read_ahead_start && off2 <= FileIO::block_read_ahead_end){
            uint64_t start = chunkStart;
            start += spillOver ? 2*block_download_size : block_download_size;
            uint64_t temp = 0;
            for(uint64_t i = 0; i < FileIO::number_of_blocks_to_read_ahead; i++){
                temp = start + i*block_download_size;
                if (temp >= fileSize){
                    break;
                }
                auto path_to_buffer2 = FileIO::downloadPath;
                path_to_buffer2 /= file->getId() + "-" + std::to_string(start);
                if(!fs::exists(path_to_buffer2)) {
                    chunksToDownload.push_back(temp / block_download_size);
                }
            }

        }else if( off == 0 ){
            auto sz = file->getFileSize();
            if (sz > 0) {
                auto lastChunk = getChunkNumber(sz-1, block_download_size);
                chunksToDownload.push_back(lastChunk);
    //                if(lastChunk > 1){
    //                    chunksToDownload.push_back(1);
    //                }
            }

        }

        if(!chunksToDownload.empty()){
            file->_mutex.lock();
            
//            file->create_heap_handles(block_download_size);
            for (auto _chunkNumber: chunksToDownload) {
                auto start = _chunkNumber*block_download_size;
                std::string cacheName2 = file->getId() + "-" + std::to_string(start);
                item = DownloadCache.get(cacheName2);
                if ( (!item) ){
                    DownloadItem cache = std::make_shared<__no_collision_download__>();

                    auto chunkSize = (_chunkNumber +1)*block_download_size >= file->getFileSize() ?
                                     file->getFileSize() - _chunkNumber*block_download_size : block_download_size;
                    std::atomic_thread_fence(std::memory_order_release);
                    DownloadItem cache2 = DownloadCache.insert(cacheName2, cache);
                    if(cache.get() != cache2.get()){
                        continue;
                    }
//                    void FileIO::download(DownloadItem cache, std::string cacheName2, uint64_t start, uint64_t end,  uint_fast8_t backoff) {

                    fs::path path(downloadPath);
                    path /= cacheName2;

                    if( (fs::exists(path) && (fs::file_size(path) == block_download_size|| _chunkNumber == getChunkNumber(file->getFileSize(), block_download_size)))  ) {
                        boost::asio::post(*ReadPool,
                                           [io = this, start, chunkSize, path, weak_obj = std::weak_ptr<__no_collision_download__>(cache)]() -> void {
                                               auto strong_obj = weak_obj.lock();

                                               if (strong_obj) {

                                                   FILE *fp = fopen(path.string().c_str(), "rb");
                                                   if (fp != nullptr) {
                                                       auto fsize = fs::file_size(path);
                                                       auto buf = new std::vector<unsigned char>(fsize);
                                                       fread(buf->data(), sizeof(unsigned char), buf->size(), fp);
                                                       fclose(fp);
                                                       strong_obj->buffer = buf;
                                                   }else{
                                                       fs::permissions(path, fs::owner_all);
                                                        if(errno == EPERM){
                                                           LOG(ERROR) << strerror(errno);
                                                        }else{
                                                            if(errno == EMFILE){
                                                                LOG(ERROR) << "There was an error opening file " << path.string();
                                                            }else{
                                                                LOG(ERROR) << "There was an error opening file " << path.string() <<"\n" << strerror(errno) ;
                                                            }
                                                        }
                                                   }
                                                   std::atomic_thread_fence(std::memory_order_acquire);
                                                   strong_obj->event.signal();
                                                   strong_obj.reset();
                                               }
                                           });
                    } else {
                        bool isRequired = start >= off && start < off + _size;
                        boost::asio::post(*DownloadPool,
                           [req = isRequired ? req : nullptr, cacheName2, start, chunkSize, path, weak_file = std::weak_ptr<_Object>(localFile), weak_obj = std::weak_ptr<__no_collision_download__>(cache)]() -> void {
                               auto strong_obj = weak_obj.lock();
                               auto strong_file = weak_file.lock();

                               if (strong_obj && strong_file) {
                                   FileIO::download(req, strong_file.get(), strong_obj.get(), cacheName2, start, start + chunkSize - 1);
                               }
                               strong_file.reset();
                               strong_obj.reset();

                           });
                        }
                }
            }
            item.reset();
            std::atomic_thread_fence(std::memory_order_release);
            file->_mutex.unlock();

//        mtxDownloadInsert.unlock();
        }

        if(!repliedReq){
            if( buffer != nullptr) delete buffer;
            if(!chunksToDownload.empty()){
                getFromCloud(req, _size, off);
            }else{
                LOG(FATAL) << "this shouldn't happen";
            }
        }

    }

    void FileIO::setFileName(){
        fs::path file_location(FileIO::uploadPath);
        file_location /= m_file->getId();
        f_name = file_location.string();
    }

    void FileIO::open(){

        m_event.wait();
        if(isOpen()){
            fclose(m_fp);
            VLOG(9) << "Closing file " << f_name<< ".released. fd is " << m_fd<< ". This is " << (uintptr_t ) this;
//            LOG(TRACE) << "Closing file " << f_name<< ".released. fd is " << m_fd<< ". This is " << (uintptr_t ) this;
            m_fp = nullptr;
            m_fd = -1;
        }

        if(m_writeable){
            clearFileFromCache();
            b_is_cached = true;
            m_file->attribute.st_size = 0;
            m_fp = fopen(f_name.c_str(), "w+b");
            m_fd = fileno(m_fp);
            VLOG(9) << "Opened file for writing: " << f_name << ". fd is " << m_fd << ". This is " << (uintptr_t ) this;
//            LOG(TRACE) << "Opened file for writing: " << f_name << ". fd is " << m_fd << ". This is " << (uintptr_t ) this;
        }
        else if(m_readable) {
            if (m_file->getIsUploaded()) {
                //file is not cached on the hdd
                b_is_cached = false;
                m_event.signal();
                return;
            }

            fs::path path = f_name.data();
            path += ".released";
            if(fs::exists(path)) {
                m_fp = fopen(path.c_str(), "rb");
                if(m_fp != nullptr) {
                    m_fd = fileno(m_fp);
                    VLOG(9) << "Opened file for reading: " << path.string() << ". fd is " << m_fd << ". This is " << (uintptr_t ) this;
//                    LOG(TRACE) << "Opened file for reading: " << path.string() << ". fd is " << m_fd << ". This is " << (uintptr_t ) this;
                }else{
                    LOG(ERROR) << "Error while opening cached file\n" << strerror(errno);
                }
            }
        }

        m_event.signal();
        return;

    }

    void FileIO::clearFileFromCache(){
#warning todo
//        auto buffers = m_file->m_buffers;
//        if(buffers != nullptr) {
//            for (auto weak: *buffers) {
//                weak.reset();
//            }
//        }
    }

    void FileIO::upload(bool runAsynchronously){
        if(runAsynchronously) {
            boost::asio::post(
                    *UploadPool,
                   [io = this]() -> void {

                       if (io->checkFileExists()) {
                           sleep(3);
                           // file can have no parents happen when launching DriveFS
                           // so wait until it is filled. if it's deleted
                           auto file = io->m_file;
#warning bsoncxx
//                           while(io->m_file->parents.empty() && !io->m_file->getIsTrashed() && !io->m_file->getIsUploaded()){
//                               if(file->getIsTrashed()){
//                                   LOG(INFO) << "fiile with id " << file->getId() << "is trashed";
//                                   return;
//                               }
//                               LOG(INFO) << "while uploading: waiting for " << file->getId() << "to have parents";
//                               sleep(1);
//                           }


                           if(file->getIsTrashed()){
                               io->checkFileExists();
                           }else if(!file->getIsUploaded()) {
                               io->_upload();
                               io->m_file->setIsUploaded(true);
                           }else{
                               io->move_files_to_download_after_finish_uploading();
                               io->m_file->setIsUploaded(true);
                           }
                       }


                       io->deleteObject();
                   });
        }else{
            _upload();
        }

    }

    bool FileIO::checkFileExists(){
        // make sure that the file is still valid and not deleted before uploading
        if(m_file->getIsTrashed()){
            return false;
        }
        if(FileManager::fromInode(m_file->getInode())){
            return true;
        };

//        const bool temp =
//
//        LOG_IF(temp, DEBUG) << "Cached file no longer exists with name " << m_file->getName() << " and id " << m_file->getId();

//        return !temp;
//        assert(false);
        return false;

    }
    void FileIO::_upload(){
        std::string uploadUrl = m_account->getUploadUrlForFile(m_file);
        auto uploadFileName = f_name + ".released";
        if(uploadUrl.empty() && m_file->getIsTrashed()){
            fs::remove(uploadFileName);
            return;

        }

        // make sure that the file is no longer setting attribute
        m_file->_mutex.lock();
        m_file->_mutex.unlock();
        LOG(INFO) << "About to upload file \""<< m_file->getName() << "\"";

        bool status = m_account->upload(uploadUrl, uploadFileName, m_file->getFileSize());
        if(status){
            move_files_to_download_after_finish_uploading();
            LOG(INFO) << "Successfully uploaded file \""<< m_file->getName() << "\"";
            if( fs::remove(uploadFileName) ){
                LOG(TRACE) << "Removed cache file \""<< m_file->getName() << "\"";
            }else{
                LOG(ERROR) << "There was an error removing file \""<< m_file->getName() << "\" from cache";
            }
        }else{
            while(!resumeFileUploadFromUrl(uploadUrl)){
                LOG(INFO) << "Retrying to upload file \"" << m_file->getName() << "\"";
            };
        }
    }

    void FileIO::move_files_to_download_after_finish_uploading() {
        this->m_file->setIsUploaded(true);
        if(move_files_to_download_on_finish_upload && !m_file->getIsTrashed()){
            auto uploadFileName = f_name + ".released";
            if(fs::exists(uploadFileName)) {
                LOG(DEBUG) << "Moving " << m_file->getName() << " from upload cache to download cache";
                try {
                    auto sz = m_file->getFileSize();
                    auto sz_actual = fs::file_size(uploadFileName);
                    std::vector<fs::path> paths;
                    std::vector<size_t> sizes;
                    if(sz != sz_actual){
                        LOG(ERROR) << "file size in database is not the same as the actual fileSize";
                        LOG(TRACE) << "id: " << m_file->getId();
                        sz = sz_actual;
                    }
                    const int nChunks = getChunkNumber(sz, block_download_size);
                    paths.reserve(nChunks);
                    sizes.reserve(nChunks);
                    auto start = 0;
                    int in_fd= ::open(uploadFileName.c_str(), O_RDONLY);
            VLOG(9) << "Opening file " << f_name<< ".released. fd is " << m_fd << ". This is " << (uintptr_t ) this;
//                    LOG(TRACE) << "Opening file " << f_name<< ".released. fd is " << m_fd << ". This is " << (uintptr_t ) this;
                    if(in_fd < 0) {
                        LOG(ERROR) << "Failed to open input file " << uploadFileName << "\nReasonm: "
                                   << strerror(errno);
                        return;
                    }

                    auto read_size = block_download_size > sz ? sz : block_download_size;
                    while ((start + read_size) <= sz) {

                        fs::path out_path = cachePath;
                            out_path /= "download";
                        out_path /= m_file->getId() + "-" + std::to_string(start);
                        int out_fd = ::open(out_path.c_str(), O_WRONLY | O_CREAT, S_IRWXU);


                        if (out_fd < 0) {
                            LOG(ERROR) << "Unable to open file " << out_path.string() << ": "
                                       << strerror(errno);
                        } else {
                            VLOG(9) << "Writing chunk after upload " << out_path.string();
                            size_t totalCopied = 0;
                            off_t offset = start + totalCopied;
                            while(totalCopied < read_size){
                                auto copied = sendfile(out_fd, in_fd, &offset, read_size-totalCopied);
                                if(copied < 0){
                                    if (errno == EINTR || errno == EAGAIN) {
                                        // Interrupted system call/try again
                                        // Just skip to the top of the loop and try again
                                        continue;
                                    }
                                    LOG(ERROR) << "There was an error with copying file chunk: " << strerror(errno);
                                    break;
                                }

                                totalCopied += copied;
                            }

                            close(out_fd);
                            fs::permissions(out_path, fs::owner_all);

                            paths.push_back(out_path);
                            sizes.push_back(read_size);
                        }


                        start += read_size;
                        if ((start + read_size) > sz) {
                            read_size = sz - start;
                        }

                        if (read_size <= 0 || read_size > block_download_size) {
                            break;
                        }

                    }
            VLOG(9) << "Closing file " << f_name<< ".released. fd is " << m_fd << ". This is " << (uintptr_t ) this;
//                    LOG(TRACE) << "Closing file " << f_name<< ".released. fd is " << m_fd << ". This is " << (uintptr_t ) this;
                    close(in_fd);
                    insertFilesToCacheDatabase(paths,sizes);
                } catch (std::exception &e) {
                    LOG(ERROR) << "There was an error when trying to move the file from upload to download: "
                               << e.what();
                }
            }

        }

    }
    void FileIO::release(){

        m_event.wait();
        if(isOpen()){
            if(last_write_to_buffer >0) {
                fwrite((char *) write_buffer->data(), sizeof(char), last_write_to_buffer, m_fp);
                last_write_to_buffer = 0;
            }
            fclose(m_fp);
            VLOG(9) << "Closing file " << f_name<< ".released. fd is " << m_fd << ". This is " << (uintptr_t ) this;
//            LOG(TRACE) << "Closing file " << f_name<< ".released. fd is " << m_fd << ". This is " << (uintptr_t ) this;

            m_fp = nullptr;
            m_fd = -1;
        }


        if(b_needs_uploading){
            m_account->upsertFileToDatabase(m_file, {});

            if(fs::exists(f_name)){
                fs::path released;
                if(f_name.empty()) {
                    released = uploadPath;
                    released /= m_file->getId();
                }else{
                    try{
                        released = fs::path(f_name);
                        released += ".released";
                        fs::rename(f_name, released);
                    }catch(std::exception &e){
                        LOG(ERROR) << "There was an error when trying to rename a file";
                        LOG(ERROR) << e.what();
                    }
                }

            }else{
                b_needs_uploading = false;
            }

        }

        m_event.signal();


    }

    bool FileIO::validateCachedFileForUpload(bool deleteCachedFile){
        auto path = fs::path(f_name + ".released");
        if(fs::exists(path)){
            if( fs::file_size(path) == m_file->getFileSize()) {
                return true;
            }
        }
        path = fs::path(f_name);
        if(deleteCachedFile){
            if(fs::exists(path)){
                fs::remove(path);
            }
        }
        return false;
    }
    void FileIO::resumeFileUploadFromUrl(std::string url, bool runAsynchronously){
        if(runAsynchronously){
            boost::asio::post(*UploadPool,
            [io = this, url]()->void{

                while(!io->resumeFileUploadFromUrl(url));
                io->deleteObject();
            });
        }else{
            while(!resumeFileUploadFromUrl(url));
        }
    }

    bool FileIO::resumeFileUploadFromUrl(std::string url){
        auto start = m_account->getResumableUploadPoint(url, m_file->getFileSize());
        LOG(INFO) << "Resuming upload of: " << m_file->getName();
        if(start && start.value() >= 0){
            LOG(INFO) << "Starting from: " << start.value() / 1024 / 1024  << "MB / " << m_file->getFileSize()/1024/1024 << "MB  " << (int) ((double) start.value() / (double) m_file->getFileSize() * 100.0) << "%";
            bool status = m_account->upload(url, f_name + ".released", m_file->getFileSize(), start.value());
            if(status){
                move_files_to_download_after_finish_uploading();
                LOG(INFO) << "Successfully uploaded file \""<< m_file->getName() << "\"";
                if( fs::remove(f_name + ".released") ){
                    LOG(TRACE) << "Removed cache file \""<< m_file->getName() << "\"";
                }else{
                    LOG(ERROR) << "There was an error removing file \""<< m_file->getName() << "\" from cache";
                }

            }

            return status;
        }else{
            if(m_file->getIsUploaded()) {
                fs::path path = f_name;
                path += ".released";
                fs::remove(path);
                return true;
            }

            if(start && start.value()){
                auto value = start.value();
                if(value < 0){
                    value = -value;

//                    if(value == 400){
//                        m_file->setNewId(m_account->getNextId());
//                    }

                }
            }

            LOG(INFO) << "Starting from: 0 / " << m_file->getFileSize() / 1024/1024 << " MB" ;
            upload(false);
            return true;
        }
    }

    void FileIO::checkCacheSize() {

        db_handle_t db;
        auto w = db.getWork();

        // reset the database to mark all unvisited files
        std::string sql = "UPDATE " DBCACHENAME " SET exists=false";
        w->exec(sql);

        size_t increment_size=0;
        time_t mtime;
        struct stat st;
        memset(&st, 0, sizeof(struct stat));

        bool needsUpdating = false;
        uint_fast8_t count = 0;
        sql.reserve(4200);
        std::string sql_insert;
        sql_insert.reserve(1024*1024*2);
        sql_insert += "INSERT INTO " DBCACHENAME "(path,size,mtime,exists) VALUES ";
        for(fs::directory_entry& entry : boost::make_iterator_range(fs::directory_iterator(downloadPath), {})) {
            if(fs::exists(entry)) {
                fs::permissions(entry, fs::owner_all);
                size_t size = fs::file_size(entry);;
                increment_size += size;
                auto path = entry.path();
                stat(path.string().c_str(), &st);

                snprintf(sql.data(), 4200, "SELECT mtime, size FROM " DBCACHENAME " WHERE path='%s'", path.string().c_str());
                auto results = w->exec(sql);
                if(results.size() > 0){
                    auto doc = results[0];
                    if(doc[0].as<int64_t>() == st.st_mtim.tv_sec && doc[1].as<int64_t>() == size ){
                        continue;
                    }else{
                        if(needsUpdating)
                            sql_insert += ",";
                        else
                            needsUpdating = true;

                        sql_insert += "('";
                        sql_insert += path.string();
                        sql_insert += "',";
                        sql_insert += std::to_string(size);
                        sql_insert += ",";
                        sql_insert += std::to_string(st.st_mtim.tv_sec);
                        sql_insert += ", true)";
                    }
                }else{
                    if(needsUpdating)
                        sql_insert += ",";
                    else
                        needsUpdating = true;

                    sql_insert += "('";
                    sql_insert += path.string();
                    sql_insert += "',";
                    sql_insert += std::to_string(size);
                    sql_insert += ",";
                    sql_insert += std::to_string(st.st_mtim.tv_sec);
                    sql_insert += ", true) ";

                }
                count++;
                if(count % 200 == 0){
                    count = 0;
                    needsUpdating = false;
                    sql_insert += "ON CONFLICT (path) DO UPDATE SET "
                           "size=EXCLUDED.size,"
                           "exists=true,"
                           "mtime=EXCLUDED.mtime";
                    try {
                        w->exec(sql_insert);
                    } catch (std::exception &e) {
                        LOG(INFO) << sql_insert;
                        LOG(FATAL) << e.what();
                    }
                    sql_insert.clear();
                    sql_insert += "INSERT INTO " DBCACHENAME "(path,size,mtime,exists) VALUES ";

                }

            }
        }
        if(needsUpdating){
            sql_insert += "ON CONFLICT (path) DO UPDATE SET "
                   "size=EXCLUDED.size,"
                   "exists=true,"
                   "mtime=EXCLUDED.mtime";
            try {
                w->exec(sql_insert);
            } catch (std::exception &e) {
                LOG(INFO) << sql_insert;
                LOG(FATAL) << e.what();
            }
        }
        sql = "DELETE FROM " DBCACHENAME " WHERE exists=false";
        w->exec(sql);
        w->commit();
        FileIO::finishedInitialCheck = true;
        incrementCacheSize(increment_size);

    }

    void setMaxConcurrentDownload(int n){
        if(n > 0) {
            DownloadPool = new boost::asio::thread_pool(n);
            ReadPool = new boost::asio::thread_pool(n);
        }else{
            DownloadPool = new boost::asio::thread_pool(std::thread::hardware_concurrency());
            ReadPool  = new boost::asio::thread_pool(std::thread::hardware_concurrency());
        }
    }
    void setMaxConcurrentUpload(int n){
        if(n > 0) {
            UploadPool = new boost::asio::thread_pool(n);
        }else{
            UploadPool = new boost::asio::thread_pool(std::thread::hardware_concurrency());
        }
    }

    void FileIO::deleteFilesFromCacheOnDisk(){
        if(!FileIO::finishedInitialCheck){
            return;
        }
        int64_t oldSize = FileIO::cacheSize.load(std::memory_order_relaxed);
        int64_t workingSize = oldSize,
        // delete at most 50gb below the maximum
        targetSize = std::max<int64_t>(FileIO::maxCacheOnDisk * 0.9, FileIO::maxCacheOnDisk - 50LL*1024LL*1024LL*1024LL);

        std::string sql_select = "SELECT path,size,mtime FROM " DBCACHENAME " WHERE exists=true ORDER BY mtime ASC LIMIT 1000";

        while(oldSize > targetSize){
            std::string sql_delete = "INSERT INTO " DBCACHENAME " (path,size,mtime, exists) VALUES ";
            sql_delete.reserve(1024*1024*4);

            LOG(INFO)  << "Deleting files until target size is reached";
            LOG(DEBUG) << "Target Size: " << targetSize;
            LOG(DEBUG) << "Current Size: "  << oldSize;

            db_handle_t db;
            auto w = db.getWork();
            auto results = w->exec(sql_select);
            std::string now = std::to_string(time(nullptr));
            bool needsUpdating = false;
            if(results.size() == 0){
                break;
            }
            for( auto row: results){
                std::string filename = row[0].as<std::string>();
                if( fs::exists(fs::path(filename))) {
                    if(unlink(filename.c_str()) == 0 ){
                        int64_t size = row[1].as<int64_t>();
                        workingSize -= size;
                    }
                }
                if(needsUpdating){
                    sql_delete += ",";
                }else{
                    needsUpdating=true;
                }
                sql_delete += "('";
                sql_delete += filename;
                sql_delete += "',0,";
                sql_delete += now;
                sql_delete += ", false) ";


                if(workingSize < targetSize){
                    break;
                }
            }

            if(needsUpdating){
                sql_delete +=  "ON CONFLICT (path) DO UPDATE SET "
                               "exists=false";

                try{
                    w->exec(sql_delete);
                    w->commit();
                }catch(std::exception &e){
                    LOG(ERROR) << e.what();
                }
            }


            int64_t delta = workingSize-oldSize;
            oldSize = incrementCacheSize(delta);
            workingSize = workingSize;
        }


    }

    int64_t FileIO::incrementCacheSize(int64_t size){
        int64_t oldSize = FileIO::cacheSize.load(std::memory_order_relaxed);
        for (;;)    // Increment the cacheSize atomically via CAS loop.
        {
            int64_t newSize =  oldSize + size ;
            if(newSize < 0){
                newSize = 0;
            };
            if (FileIO::cacheSize.compare_exchange_weak(oldSize, newSize, std::memory_order_release, std::memory_order_relaxed)){

                if(newSize > maxCacheOnDisk) {
                    if (deleteCacheMutex.try_lock()) {

                        try {
                            deleteFilesFromCacheOnDisk();
                            deleteCacheMutex.unlock();
                        }catch(std::exception &e){
                            deleteCacheMutex.unlock();
                            LOG(ERROR) << "There was an error when deleting files from disk: " << e.what();
                        }

                    }
                }



                return newSize;
            }
            // The compare-exchange failed, likely because another thread changed size.
            // oldStatus has been updated. Retry the CAS loop.
        }

    }

    void FileIO::deleteFileFromUploadCache(const std::string &id) {
        fs::path path = uploadPath;
        path /= id;
        if( fs::exists(path)){
            LOG(INFO) << "Removing cached file";
            fs::remove(path);
        }

        path += ".released";
        if(fs::exists(path)){
            fs::remove(path);
        }
    }

    void FileIO::insertFileToCacheDatabase(fs::path path, size_t size){
        db_handle_t db;
        auto w = db.getWork();

        try {
            const char * fmt = "INSERT INTO " DBCACHENAME "(path,size,mtime,exists) VALUES ('%s',%lu,%lu,true) "
                    "ON CONFLICT (path) DO UPDATE SET "
                    "size=EXCLUDED.size,"
                    "exists=true,"
                    "mtime=EXCLUDED.mtime";
            std::string const sPath = path.string();
            uint64_t now = time(nullptr);
            int sz = std::snprintf(nullptr, 0, fmt, sPath.c_str(), size, now);
            std::string sql;
            sql.reserve(sz+1);
            snprintf(sql.data(), sz+1, fmt, sPath.c_str(), size, now);
            w->exec(sql);
            w->commit();

        }catch(std::exception &e) {
            LOG(ERROR) << e.what();
        }

        incrementCacheSize(size);
    }
    void FileIO::insertFilesToCacheDatabase(const std::vector<fs::path> &paths, const std::vector<size_t> &sizes){
        db_handle_t db;
        auto w = db.getWork();
        std::string sql = "INSERT INTO " DBCACHENAME "(path,size,mtime,exists) VALUES ";
        sql.reserve(512000);

        size_t totalSize = 0;
        std::string  now = std::to_string(time(nullptr));
        bool wasFirst = true;
        for(int i = 0; i < paths.size(); i++) {
            auto const & path = paths[i];
            if(wasFirst){
                wasFirst = false;
            }else{
                sql += ",";
            }
            sql += "('";
            sql += path.string();
            sql += "',";
            sql += std::to_string(sizes[i]);
            sql += ",";
            sql += now;
            sql += ", true)";

            totalSize += sizes[i];
        }

        sql += "ON CONFLICT (path) DO UPDATE SET "
               "size=EXCLUDED.size,"
               "exists=true,"
               "mtime=EXCLUDED.mtime";


        w->exec(sql);
        w->commit();

        incrementCacheSize(totalSize);


        LOG(INFO) << "Current size of cache is "
                  << ((double) DriveFS::FileIO::getDiskCacheSize()) / 1024.0 / 1024.0 / 1024.0 << " GB";

    }

}
