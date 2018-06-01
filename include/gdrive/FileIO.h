//
// Created by eric on 04/02/18.
//

#ifndef DRIVEFS_FILEIO_H
#define DRIVEFS_FILEIO_H

//
// Created by Eric Yen on 2016-10-06.
//


#define CACHEPATH "/home/cache2"


#include "gdrive/Account.h"
#include <boost/filesystem.hpp>
#include "DownloadBuffer.h"
#include <iostream>
#include <fstream>
#include <atomic>
#include <boost/heap/priority_queue.hpp>
#include <future>
#include <string_view>
#include <atomic>
#include <autoresetevent.h>
#include <boost/filesystem.hpp>


namespace fs = boost::filesystem;

namespace DriveFS {


//    class __no_collision_download__;

//    typedef std::shared_ptr<__no_collision_download__> DownloadItem;

    void setMaxConcurrentDownload(int n);
    void setMaxConcurrentUpload(int n);

    class FileIO {

    public:
        static uint_fast32_t write_buffer_size, // the size of the buffer before writing to disk
                block_download_size, // size of the download chunk
                block_read_ahead_start, block_read_ahead_end; // boundaries to download extra blocks ahead of time
        static uint_fast8_t number_of_blocks_to_read_ahead; // number of blocks to download ahead of time
        static bool download_last_chunk_at_the_beginning; // if true, download the last chunk when downloading the first chunk. useful for media players.
        static bool move_files_to_download_on_finish_upload;
        static int64_t maxCacheOnDisk;
        FileIO(GDriveObject object, int flag);

        ~FileIO();

        std::vector<unsigned char> * read(const size_t &size, const off_t &off);

        void open();

        void release();


        /*
         * \brief A cache is valid if the file has been released.
         * \param delete: bool delete temp file if the cache is invalid
         */
        bool validateCachedFileForUpload(bool deleteCachedFile = false);

        /*
         * \brief Resume file upload from this file;
         */
        void resumeFileUploadFromUrl(std::string url, bool runAsynchronously);

        void create_write_buffer();
        void create_write_buffer2();

//        static inline size_t get_buffer_size() {return write_buffer_size;};

        bool getIsReadable() const {
            return m_readable;
        }

        bool bufferMatchesExpectedBufferSize(const size_t &bufferSize);

        static void checkCacheSize();
        static int64_t incrementCacheSize(int64_t size);
        static void deleteFilesFromCacheOnDisk();
        static void setCachePath(const fs::path &path){
            FileIO::cachePath = path;
            FileIO::downloadPath = path / "download";
            FileIO::uploadPath = path / "upload";

            fs::create_directories(path);
            fs::create_directories(downloadPath);
            fs::create_directories(uploadPath);

        }
    public:

        std::vector<unsigned char> * getFromCloud(const size_t &size, const off_t &off);
        DownloadItem getFromCache(fs::path path, uint64_t chunkStart);
        void upload(bool runAsynchronously);

        std::string f_name; //f_name for the upload, d_name is the base download name
        GDriveObject m_file;
        bool b_is_uploading;
        bool b_is_cached;
        bool b_needs_uploading;

        bool m_readable, m_writeable;
//        int m_flags;
        std::vector<unsigned char> *write_buffer, *write_buffer2;
        off_t last_write_to_buffer, first_write_to_buffer;
        AutoResetEvent m_event;
        int m_fd=-1;
        FILE* m_fp;

        static bool renameOldCacheFile(const char* oldName, const char* newName);

    private:
        static Account* m_account;

        void _upload();
        bool checkFileExists();
        void download(DownloadItem cache, std::string cacheName, uint64_t start, uint64_t end, uint_fast8_t backoff=0);
        bool resumeFileUploadFromUrl(std::string url);
        void move_files_to_download_after_finish_uploading();
        void clearFileFromCache();
        void setFileName();
        inline bool isOpen(){
            return m_fp != nullptr;
        }

        static fs::path cachePath;
        static fs::path downloadPath;
        static fs::path  uploadPath;
        static std::atomic_int64_t cacheSize;
    public:
        static void setAccount(Account* account){
            m_account = account;
        }
        inline static int64_t getDiskCacheSize(){
            return cacheSize.load(std::memory_order_relaxed);
        }


    };
}
#endif //DRIVEFS_FILEIO_H
