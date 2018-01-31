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
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/document/value.hpp>
#include <bsoncxx/document/view.hpp>
#include <bsoncxx/json.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/pool.hpp>
#include "easylogging++.h"

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
    protected:
        void run_internal() override;
        void loadFilesAndFolders() override;
        void getFilesAndFolders(std::string nextPageToken="", int backoff=0);
    private:
        void getTeamDrives(int backoff=0);
        void linkParentsAndChildren();
//        oauth2_config m_oauth2_config;
//        http_client_config m_http_config;
//        http_client m_api = http_client("https://gooleapis.com/drive/v3", m_http_config);

        std::string m_newStartPageToken="";
        std::atomic<ino_t> inode_count = 1;

    };
};


#define DATABASEDATA "GDriveData"
#define DATABASESETTINGS "settings"
#define DATABASENAME "DriveFS"
#define GDRIVETOKENNAME "gdrive_tokens"
#define GDRIVELASTCHANGETOKEN "gdrive last change token"
