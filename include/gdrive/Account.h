//
// Created by eric on 27/01/18.
//

#pragma once
#include "gdrive/Account.h"
#include "gdrive/File.h"
#include <string_view>
#include <cpprest/http_client.h>
#include "BaseAccount.h"
#include <boost/filesystem.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/builder/stream/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/document/value.hpp>
#include <bsoncxx/document/view.hpp>
#include <bsoncxx/json.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/pool.hpp>
#include "easylogging++.h"
#include <boost/circular_buffer.hpp>
#include <string_view>

using namespace web::http::client;          // HTTP client features
using namespace web::http::oauth2::experimental;
using bsoncxx::builder::concatenate;
using bsoncxx::builder::stream::close_array;
using bsoncxx::builder::stream::close_document;
using bsoncxx::builder::stream::document;
using bsoncxx::builder::stream::finalize;
using bsoncxx::builder::stream::open_array;
using bsoncxx::builder::stream::open_document;
using bsoncxx::builder::basic::kvp;
#define GDRIVE_OAUTH_SCOPE "https://www.googleapis.com/auth/drive"

namespace DriveFS {
    class Account: public BaseAccount {
    public:
        Account();

        Account(const std::string &access_tokoen, const std::string &refresh_token);

        static Account getAccount();
        inline ino_t getNextInode(){
            return inode_count.fetch_add(1, std::memory_order_acquire)+1;
        }

        GDriveObject createNewChild(GDriveObject parent, const char *name, int mode, bool isFile);
        bool removeChildFromParent(GDriveObject child, GDriveObject parent);
        void upsertFileToDatabase(GDriveObject file);
        std::string getUploadUrlForFile(GDriveObject file, std::string mimeType = "application/octet-stream");
        bool upload(std::string uploadUrl, std::string filePath, size_t fileSize, int64_t  start=0, std::string mimeType = "application/octet-stream");
        std::optional<int64_t> getResumableUploadPoint(std::string url, size_t fileSize, int backoff=0);
    protected:
        void run_internal() override;
        void loadFilesAndFolders() override;
        void getFilesAndFolders(std::string nextPageToken="", int backoff=0);
    private:
        void getTeamDrives(int backoff=0);
        void linkParentsAndChildren();
        std::string getNextId();
        std::string getUploadUrlForFile(http_request, int backoff=1);
        void generateIds(int_fast8_t backoff=0);
        std::string createFolderOnGDrive(const std::string json, int backoff=0);
        bool trash(GDriveObject file, int backoff=0);
        void refresh_token(int backoff=0);

        boost::circular_buffer<std::string> m_id_buffer;

        std::string m_newStartPageToken="";
        std::atomic<ino_t> inode_count = 1;
    };
};


#define DATABASEDATA "GDriveData"
#define DATABASESETTINGS "settings"
#define DATABASENAME "DriveFS"
#define GDRIVETOKENNAME "gdrive_tokens"
#define GDRIVELASTCHANGETOKEN "gdrive last change token"
