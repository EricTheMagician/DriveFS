//
// Created by eric on 04/02/18.
//

#include <gdrive/FileIO.h>
#include <thread>
#include <cpprest/http_client.h>
#include <easylogging++.h>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio.hpp>
#include <boost/range/iterator_range.hpp>
#include <gdrive/Account.h>
#include <mutex>
#include <sys/sendfile.h>

static std::mutex deleteCacheMutex;
#define DBCACHENAME "FileCacheDB"
using namespace web::http::client;          // HTTP client features
using Object = DriveFS::_Object;

inline uint64_t getChunkStart(uint64_t start, uint64_t buffer_size){
    uint64_t chunkNumber = start / buffer_size;
    return chunkNumber * buffer_size;
}

inline uint64_t getChunkNumber(uint64_t start, uint64_t buffer_size){
    return  start / buffer_size;

}

static boost::asio::thread_pool *DownloadPool;
static boost::asio::thread_pool *UploadPool;

namespace DriveFS{

    uint_fast32_t FileIO::write_buffer_size = 64*1024*1024, //64mb
            FileIO::block_read_ahead_start = UINT32_MAX,
            FileIO::block_read_ahead_end = UINT32_MAX,
            FileIO::block_download_size=1024*1024*2;
    uint_fast8_t FileIO::number_of_blocks_to_read_ahead = 0;
    bool FileIO::download_last_chunk_at_the_beginning = false;
    bool FileIO::move_files_to_download_on_finish_upload = true;
    std::atomic_int64_t FileIO::cacheSize = 0;
    Account* FileIO::m_account = nullptr;
    int64_t FileIO::maxCacheOnDisk = 0;


    fs::path FileIO::cachePath = "/tmp/DriveFS";
    fs::path FileIO::downloadPath = "/tmp/DriveFS/download";
    fs::path FileIO::uploadPath = "/tmp/DriveFS/upload";

    FileIO::FileIO(GDriveObject object, int flag):
            m_file(std::move(object)),
            b_is_uploading(false),
            b_is_cached(false),
            b_needs_uploading(false),
            m_readable( (flag & 0x8000) || (flag & O_RDONLY) || (flag & O_RDWR)),
            m_writeable((flag & O_WRONLY) || (flag & O_RDWR)),
            write_buffer(nullptr),
            write_buffer2(nullptr),
            first_write_to_buffer(0),
            last_write_to_buffer(0),
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

        if(m_fp != nullptr){
            fclose(m_fp);
        }
    }

    std::vector<unsigned char>* FileIO::read(const size_t &size, const off_t &off) {
        if( (! m_file->getIsUploaded()) || (b_is_cached && isOpen()) ){
            if(!isOpen()){
                open();
            }
            if(isOpen()) {
                auto buf = new std::vector<unsigned char>(size);
                m_event.wait();
                fseek(m_fp, off, SEEK_SET);
                fread((char *) buf->data(), sizeof(char), size, m_fp);
                m_event.signal();
                return buf;
            }
        }
        try {
            return getFromCloud(size, off);
        }catch(std::exception &e){
            LOG(ERROR) << e.what();
            m_file->m_event.signal();
            return getFromCloud(size, off);
        }

    }

    void FileIO::download(DownloadItem cache, std::string cacheName, uint64_t start, uint64_t end,  uint_fast8_t backoff) {
        if(!m_file){
            LOG(ERROR) << "While downloading a file, this->m_file was null for " << cacheName << " ("<<start <<", " << end << ")";
            if(cache) {
                cache->isInvalid = true;
                cache->event.signal();
            }
            return;
        }
        VLOG(10) << "Downloading " << m_file->getName() <<"\t" << start;
        http_client client = m_account->getClient();
        uri_builder builder( std::string("files/") +  m_file->getId());
        builder.append_query("alt", "media");
//        builder.append_query("acknowledgeAbuse", "true");
        builder.append_query("supportsTeamDrives", "true");

        http_request req;
        auto &headers = req.headers();
        std::stringstream range;
        range << "bytes="
              << std::to_string(start)
              << "-"
              << std::to_string(end);
        headers.add("Range", range.str());

        req.set_method(methods::GET);
        req.set_request_uri(builder.to_uri());

        VLOG(10) << req.headers()["Range"];

        http_response resp;
        try {
            resp = client.request(req).get();
        }catch(std::exception &e){
            LOG(ERROR) << "There was an error while trying to download chunk";
            LOG(DEBUG) << "Chunk-range" << start << " - " << end;
            if(m_file) {
                LOG(DEBUG) << "File ID " << m_file->getId();
                LOG(DEBUG) << "File Size " << m_file->getFileSize();
            }
            LOG(DEBUG) << "Url " << builder.to_string();
            const unsigned int sleep_time = std::pow(2, backoff);
            LOG(INFO) << "Sleeping for " << sleep_time << " seconds before retrying";
            sleep(sleep_time);
            if (backoff <= 10) {
                download(cache, std::move(cacheName), start, end, backoff);
            }
            return;

        };
        if(resp.status_code() != 206 && resp.status_code() != 200){
            LOG(ERROR) << "Failed to get file fragment : " << resp.reason_phrase();
            LOG(ERROR) << resp.extract_json(true).get();
            const unsigned int sleep_time = std::pow(2, backoff);
            LOG(INFO) << "Sleeping for " << sleep_time << " seconds before retrying";
            sleep(sleep_time);
            if (backoff <= 10) {
                download(cache, std::move(cacheName), start, end, backoff + 1);
            }
            return;
        }

        cache->buffer = new std::vector<unsigned char>(resp.extract_vector().get());
        std::atomic_thread_fence(std::memory_order_release);
        cache->event.signal();

        //write buffer to disk
        fs::path path = cachePath / "download" / cacheName;
        FILE *fp = fopen(path.string().c_str(), "wb");
        if(fp != nullptr){
            fwrite(cache->buffer->data(), sizeof(unsigned char), cache->buffer->size(), fp);
            fclose(fp);
        }

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

    bool FileIO::bufferMatchesExpectedBufferSize(const size_t &bufferSize){
        return (bufferSize == block_download_size) || (bufferSize == m_file->getFileSize() % block_download_size);
    }

    /*
     * \param spillover:bool is it from the spillover or the original
     */
    DownloadItem FileIO::getFromCache(fs::path path, uint64_t chunkStart){

        if( ! fs::is_regular_file(path)) {
            return nullptr;
        }

        auto filesize = fs::file_size(path);
//        const uint64_t size2 = spillOver ? block_download_size - start /* how much it's been already read */: read_size;
        __no_collision_download__ __item__;
        __item__.name = m_file->getId();
        __item__.name += "-";
        __item__.name += chunkStart;
        __item__.isInvalid = false;
        __item__.size = filesize;


//        assert((start + size2) <= filesize);
        // read data from disk into memory
        FILE *fp = fopen(path.string().c_str(), "rb");
        if (fp == nullptr) {
            //failed opening file
            return nullptr;
        }
        __item__.buffer = new std::vector<unsigned char>(filesize, 0);
        __item__.last_access = time(nullptr);
        auto read_size = fread((char *)( __item__.buffer->data()), sizeof(unsigned char), filesize, fp);
        fclose(fp);

        if(!bufferMatchesExpectedBufferSize(read_size))
            return nullptr;

        // copy data

        DownloadItem shared = std::make_shared<__no_collision_download__>(std::move(__item__));
        uint64_t chunkNumber = getChunkNumber(chunkStart, block_download_size);
        (*m_file->m_buffers)[chunkNumber] = shared;
        Object::cache.insert(m_file, chunkNumber, filesize, shared);
        return shared;


    }

    std::vector<unsigned char> * FileIO::getFromCloud(const size_t &_size, const off_t &off){

        const uint64_t chunkNumber = getChunkNumber(off, block_download_size);
        const uint64_t chunkStart = getChunkStart(off, block_download_size);
        const auto fileSize = m_file->getFileSize();
        const size_t size = _size + off > fileSize ? fileSize-off-1: _size;
        const bool spillOver = chunkStart != getChunkStart(off+size, block_download_size);
        std::string cacheName = m_file->getId() + "\t" + std::to_string(chunkStart);
        std::vector<uint64_t> chunksToDownload;
        uint64_t spillOverPrecopy=0;

        DownloadItem item;
//        LOG(INFO) << "Calling wait " << std::to_string( (uintptr_t ) &this->m_file->m_event);
        m_file->m_event.wait();
        m_file->create_heap_handles(block_download_size);
//        LOG(INFO) << "Calling signal " << std::to_string( (uintptr_t ) &this->m_file->m_event);
        m_file->m_event.signal();
        auto buffer = new std::vector<unsigned char>(size, 0);
        auto path_to_buffer = FileIO::cachePath;
        path_to_buffer /= "download";
        path_to_buffer /= m_file->getId();
        path_to_buffer += "-";
        path_to_buffer += std::to_string(chunkStart);

        if ( (item = m_file->m_buffers->at(chunkNumber).lock()) ) {

            if(item->buffer==nullptr || item->buffer->empty()){
                item->event.wait();
                item->event.signal();
            }

            if(item->isInvalid || (!bufferMatchesExpectedBufferSize(item->buffer->size())) ){
                item->isInvalid = true;
                LOG(TRACE) << "cache was invalid for " << m_file->getName();
                chunksToDownload.push_back(chunkNumber);
            } else {
                const uint64_t start = off % block_download_size;
                const uint64_t size2 = spillOver ? item->buffer->size() - start: size;
                spillOverPrecopy = size2;
                LOG_IF((start+size2) > item->buffer->size(),
                        ERROR)  << "About to fail assertion\n"
                                << "ID: " << m_file->getId()
                                << "\nfileSize: " << m_file->getFileSize()
                                << "\nbufferSize: " << item->buffer->size()
                                << "\n(off,size)  (" << off << ", " << size  << ")";
                assert((start+size2) <= item->buffer->size());
                memcpy(buffer->data(), item->buffer->data()+start, size2);
//                m_file->cache.updateAccessTime(m_file, chunkNumber, item);
            }


        }else{
            item = getFromCache(path_to_buffer, chunkStart);
            if( item) {

                const uint64_t start = off % block_download_size;
                const uint64_t size2 = spillOver ? item->buffer->size() - start: size;
                spillOverPrecopy = size2;
                LOG_IF((start+size2) > item->buffer->size(),
                       ERROR)  << "About to fail assertion\n"
                               << "ID: " << m_file->getId()
                               << "\nfileSize: " << m_file->getFileSize()
                               << "\nbufferSize: " << item->buffer->size()
                               << "\n(off,size)  (" << off << ", " << size  << ")";

                assert((start+size2) <= item->buffer->size());
                memcpy(buffer->data(), item->buffer->data()+start, size2);


            }else{
                chunksToDownload.push_back(chunkNumber);
            }
        }

        if(spillOver) {

            const uint64_t chunkStart2 = chunkStart + block_download_size;
            const uint64_t chunkNumber2 = chunkNumber+1;

            path_to_buffer = FileIO::cachePath;
            path_to_buffer /= "download";
            path_to_buffer /= m_file->getId() + "-" + std::to_string(chunkStart2);

            cacheName = m_file->getId() + "\t" + std::to_string(chunkStart2);
            DownloadItem item;

            if ( (item=m_file->m_buffers->at(chunkNumber2).lock()) ) {
                if(item) {
                    if( item->buffer == nullptr || item->buffer->empty() ){
                        item->event.wait();
                        item->event.signal();
                    }

                    if(item->isInvalid || (!bufferMatchesExpectedBufferSize(item->buffer->size())) ){
                        item->isInvalid = true;
                        LOG(TRACE) << "cache was invalid for " << m_file->getName();
                        chunksToDownload.push_back(chunkNumber2);
                    }else{
                        uint64_t size2 = (off + size) % block_download_size;
                        assert((spillOverPrecopy+size2) <= buffer->size());
                        memcpy( buffer->data() + spillOverPrecopy, item->buffer->data(), size2);
//                        m_file->cache.updateAccessTime(m_file, chunkNumber2, item);
                    }

                }else{
                    chunksToDownload.push_back(chunkNumber2);
                }

            } else {
                item = getFromCache(path_to_buffer, chunkStart2);
                if( item) {
                    uint64_t size2 = (off + size) % block_download_size;
                    assert((spillOverPrecopy + size2) <= buffer->size());
                    memcpy(buffer->data() + spillOverPrecopy, item->buffer->data(), size2);
                }else{
                    chunksToDownload.push_back(chunkNumber2);
                }
            }

        }


        bool done = chunksToDownload.empty();
        int off2 = off % block_download_size;
        if( off == 0 ){
            auto sz = m_file->getFileSize();
            if (sz > 0) {
                chunksToDownload.push_back(getChunkNumber(sz-1, block_download_size));
            }
        }
        if( off2 >= FileIO::block_read_ahead_start && off2 <= FileIO::block_read_ahead_end){
            uint64_t start = chunkStart;
            start += spillOver ? 2*block_download_size : block_download_size;
            uint64_t temp = 0;
            for(uint64_t i = 0; i < FileIO::number_of_blocks_to_read_ahead; i++){
                temp = start + i*block_download_size;
                if (temp >= fileSize){
                    break;
                }
                auto path_to_buffer2 = FileIO::cachePath;
                path_to_buffer2 /= "download";
                path_to_buffer2 /= m_file->getId() + "-" + std::to_string(start);
                if(!fs::exists(path_to_buffer2)) {
                    chunksToDownload.push_back(temp / block_download_size);
                }
            }

        }

        if(!chunksToDownload.empty()){
            m_file->m_event.wait();
//            m_file->create_heap_handles(block_download_size);
            for (auto _chunkNumber: chunksToDownload) {
                auto start = _chunkNumber*block_download_size;
                std::string cacheName2 = m_file->getId() + "-" + std::to_string(start);
                DownloadItem item = m_file->m_buffers->at(_chunkNumber).lock();
                if ( (!item) || item->isInvalid){
                    DownloadItem cache = std::make_shared<__no_collision_download__>();
                    cache->last_access = time(NULL);
                    cache->name = cacheName2;

                    auto chunkSize = (_chunkNumber +1)*block_download_size >= m_file->getFileSize() ?
                                     m_file->getFileSize() - _chunkNumber*block_download_size : block_download_size;
                    cache->size = chunkSize;
                    Object::cache.insert(m_file, _chunkNumber, chunkSize, cache);
//                    void FileIO::download(DownloadItem cache, std::string cacheName2, uint64_t start, uint64_t end,  uint_fast8_t backoff) {
                    (*m_file->m_buffers)[_chunkNumber] = cache;

                    fs::path path(cachePath);
                    path /= "download";
                    path /= cacheName2;


                    boost::asio::defer(*DownloadPool,
                      [io=this, start, chunkSize, path, weak_obj = std::weak_ptr(cache)]()->void
                      {
                          auto strong_obj = weak_obj.lock();

                          if(strong_obj) {
                              if(fs::exists(path)){
                                  FILE *fp = fopen(path.string().c_str(), "rb");
                                  if(fp != nullptr){
                                      auto fsize = fs::file_size(path);
                                      auto buf = new std::vector<unsigned char>(fsize);
                                      fread(buf->data(), sizeof(unsigned char), buf->size(), fp);
                                      fclose(fp);
                                      strong_obj->buffer = buf;
                                      std::atomic_thread_fence(std::memory_order_acquire);
                                      strong_obj->event.signal();
                                      return;
                                  }
                              }else {
                                  io->download(strong_obj, strong_obj->name, start, start + chunkSize - 1, 0);
                              }
                          }
                      });
                }
            }
            m_file->m_event.signal();

//        mtxDownloadInsert.unlock();
        }

        if(done){
            return buffer;
        }else{
            delete buffer;
            return getFromCloud(size, off);
        }

    }

    void FileIO::setFileName(){
        fs::path file_location(FileIO::cachePath);
        file_location /= std::string("upload");
        file_location /= m_file->getId();
        f_name = file_location.string();
    }

    void FileIO::open(){

        if(m_writeable){
            clearFileFromCache();
            b_is_cached = true;
            m_file->attribute.st_size = 0;
            m_fp = fopen(f_name.c_str(), "w+b");
            m_fd = fileno(m_fp);
        }
        else if(m_readable) {
            if (m_file->getIsUploaded()) {
                //file is not cached on the hdd
                b_is_cached = false;
                return;
            }

            fs::path path = f_name.data();
            path += ".released";
            if(fs::exists(path)) {
                m_fp = fopen(path.c_str(), "rb");
                if(m_fp != nullptr) {
                    m_fd = fileno(m_fp);
                }else{
                    LOG(ERROR) << "Error while opening cached file\n" << strerror(errno);
                }
            }
        }

        return;

    }

    void FileIO::clearFileFromCache(){
        auto buffers = m_file->m_buffers;
        if(buffers != nullptr) {
            for (auto weak: *buffers) {
                weak.reset();
            }
        }
    }

    void FileIO::upload(bool runAsynchronously){
        if(runAsynchronously) {
            boost::asio::defer(*UploadPool,
                               [io = this]() -> void {

                                   if (io->checkFileExists()) {
                                       sleep(3);
                                       // file can have no parents happen when launching DriveFS
                                       // so wait until it is filled
                                       while(io->m_file->parents.empty());

                                       io->_upload();
                                   }


                                   delete io;
                               });
        }else{
            _upload();
        }

    }

    bool FileIO::checkFileExists(){
        // make sure that the file is still valid and not deleted before uploading
        const auto cursor = DriveFS::_Object::inodeToObject.find(m_file->getInode());

        const bool temp = cursor == DriveFS::_Object::inodeToObject.cend();


        LOG_IF(temp, DEBUG) << "Cached file no longer exists with name " << m_file->getName() << " and id " << m_file->getId();

        return !temp;

    }
    void FileIO::_upload(){
        std::string uploadUrl = m_account->getUploadUrlForFile(m_file);

        // make sure that the file is no longer setting attribute
        m_file->m_event.wait();
        m_file->m_event.signal();
        auto uploadFileName = f_name + ".released";
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
        if(move_files_to_download_on_finish_upload){
            auto uploadFileName = f_name + ".released";
            if(fs::exists(uploadFileName)) {
                LOG(DEBUG) << "Moving " << m_file->getName() << " from upload cache to download cache";
                try {
                    auto sz = m_file->getFileSize();
                    auto sz_actual = fs::file_size(uploadFileName);
                    if(sz != sz_actual){
                        LOG(ERROR) << "file size in database is not the same as the actual fileSize";
                        LOG(TRACE) << "id: " << m_file->getId();
                        sz = sz_actual;
                    }
                    auto start = 0;
                    int in_fd= ::open(uploadFileName.c_str(), O_RDONLY);

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
                        int out_fd = ::open(out_path.c_str(), O_WRONLY | O_CREAT);


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
                        }


                        start += read_size;
                        if ((start + read_size) > sz) {
                            read_size = sz - start;
                        }

                        if (read_size <= 0 || read_size > block_download_size) {
                            break;
                        }

                    }
                    close(in_fd);

                } catch (std::exception &e) {
                    LOG(ERROR) << "There was an error when trying to move the file from upload to download: "
                               << e.what();
                }
            }

        }

    }
    void FileIO::release(){

        if(last_write_to_buffer >0 && isOpen()){
            fwrite((char *) write_buffer->data(), sizeof(char), last_write_to_buffer, m_fp);
            last_write_to_buffer = 0;
            fclose(m_fp);
            m_fp = nullptr;
            m_fd = -1;
        }

        if(b_needs_uploading){
            m_account->upsertFileToDatabase(m_file);

            fs::path released;
            if(f_name.empty()) {
                released = uploadPath;
                released /= m_file->getId();
            }else{
                released = fs::path(f_name);
            }
            released += ".released";
            try {
                fs::rename(f_name, released);
            }catch(std::exception &e){
                LOG(ERROR) << e.what() << std::endl << m_file->getName() << "\t" << m_file->getId();
                b_needs_uploading = false;
            }

        }



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
            boost::asio::defer(*UploadPool,
            [io = this, url]()->void{

                // file can have no parents happen when launching DriveFS
                // so wait until it is filled
                while(io->m_file->parents.empty());

                while(!io->resumeFileUploadFromUrl(url));
                delete io;
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

        mongocxx::pool::entry conn = m_account->pool.acquire();
        mongocxx::database client = conn->database(std::string(DATABASENAME));
        mongocxx::collection db = client[std::string(DBCACHENAME)];

        // reset the database to mark all unvisited files
        db.update_many(
                document{} << finalize,
                document{} << "$set" << open_document << "exist" << false << close_document << finalize
        );

        mongocxx::bulk_write documents;
        size_t size;
        time_t mtime;
        struct stat st;
        memset(&st, 0, sizeof(struct stat));

        bool needsUpdating = false;

        for(fs::directory_entry& entry : boost::make_iterator_range(fs::directory_iterator(downloadPath), {})) {
//            std::cout << entry << "\n";
            if(fs::exists(entry)) {
                needsUpdating = true;
                size = fs::file_size(entry);
                incrementCacheSize(size);
                auto path = entry.path();
                stat(path.string().c_str(), &st);

                mongocxx::model::update_one upsert_op(
                        document{} << "filename" << path.string() << finalize,
                        document{} << "$set" << open_document
                                   << "filename" << path.string()
                                   << "size" << ((int64_t) size)
                                   << "mtime" << st.st_mtim.tv_sec
                                   << "exists" << true
                                   << close_document << finalize
                );

                upsert_op.upsert(true);
                documents.append(upsert_op);
            }


        }
        if(needsUpdating)
            db.bulk_write(documents);
    }

    void setMaxConcurrentDownload(int n){
        if(n > 0) {
            DownloadPool = new boost::asio::thread_pool(n);
        }else{
            DownloadPool = new boost::asio::thread_pool(std::thread::hardware_concurrency());
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

        int64_t oldSize = FileIO::cacheSize.load(std::memory_order_relaxed);
        int64_t workingSize = oldSize,
        targetSize = ((double)FileIO::maxCacheOnDisk) * 0.9;

        LOG(INFO) << "Deleting files until target size is reached";
        LOG(DEBUG) << "Target Size: " << targetSize;
        LOG(DEBUG) << "Current Size: "  << oldSize;

        mongocxx::pool::entry conn = m_account->pool.acquire();
        mongocxx::database client = conn->database(std::string(DATABASENAME));
        mongocxx::collection db = client[std::string(DBCACHENAME)];

        mongocxx::options::find options;
        options.sort( document{} << "mtime" << -1 << finalize);
        auto cursor = db.find(
                document{} << "exists" << true << finalize,
                options
        );
        auto toDelete = bsoncxx::builder::basic::array{};
        for( auto doc: cursor){
            std::string filename = doc["filename"].get_utf8().value.to_string();
            int64_t size = doc["size"].get_int64().value;
            workingSize -= size;
            unlink(filename.c_str());
            toDelete.append(filename);
            if(workingSize < targetSize){
                break;
            }
        }

        db.delete_many(
                document{} << "filename" << open_document << "$in" << toDelete << close_document << finalize
        );

        int64_t delta = workingSize-oldSize;
        incrementCacheSize(delta);



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

                        deleteFilesFromCacheOnDisk();

                        deleteCacheMutex.unlock();

                    }
                }



                return newSize;
            }
            // The compare-exchange failed, likely because another thread changed size.
            // oldStatus has been updated. Retry the CAS loop.
        }

    }
}