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
#include <experimental/filesystem>

#define MAX_CACHE_SIZE  (768*1024*1024) //(768*1024*1024) //2GB
#define BLOCK_DOWNLOAD_SIZE (1048576L *2L) //1MB
#define NUM_BLOCK_READ_AHEAD 8

//these two define the range over which we should start doing a readhead
#define BLOCKREADAHEADSTART 262144L
#define BLOCKREADAHEADFINISH 393216L

namespace fs = std::experimental::filesystem;

namespace DriveFS {


//    class __no_collision_download__;

//    typedef std::shared_ptr<__no_collision_download__> DownloadItem;

    class FileIO {

    public:
        static size_t write_buffer_size;
        static fs::path downloadPath;
        static bool download_last_chunk_at_the_beginning;
        FileIO(GDriveObject object, int flag, Account *account);

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
        bool resumeFileUploadFromUrl(std::string url);

        void create_write_buffer();
        void create_write_buffer2();

//        static inline size_t get_buffer_size() {return write_buffer_size;};

        bool getIsReadable() const {
            return m_readable;
        }

    public:

        std::vector<unsigned char> * getFromCache(const size_t &size, const off_t &off);
        void upload();

        std::string f_name; //f_name for the upload, d_name is the base download name
        GDriveObject m_file;
        bool b_is_uploading;
        bool b_is_cached;
        bool b_needs_uploading;

        bool m_readable, m_writeable;
//        int m_flags;
        std::vector<unsigned char> *write_buffer, *write_buffer2;
        off_t last_write_to_buffer, first_write_to_buffer;
        std::fstream stream;
        AutoResetEvent m_event;

    private:
        Account *m_account;

        void download(DownloadItem cache, std::string cacheName, uint64_t start, uint64_t end, uint_fast8_t backoff=0);

        void clearFileFromCache();
        void setFileName();


    };
}
#endif //DRIVEFS_FILEIO_H
