//
// Created by eric on 27/01/18.
//

#pragma once
#include "gdrive/Account.h"
#include "gdrive/File.h"
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


namespace DriveFS {
    class Account: public BaseAccount {
    public:
        Account(): BaseAccount(
                "https://www.googleapis.com/drive/v3/",
                "126857315828-tj5cie9scsk0b5edmakl266p7pis80ts.apps.googleusercontent.com",
                "wxvtZ_SZpmEKXSB0kITXYx6C",
                "https://accounts.google.com/o/oauth2/v2/auth",
                "https://www.googleapis.com/oauth2/v4/token",
                "http://localhost:7878",
                "https://www.googleapis.com/auth/drive"){
            m_http_config.set_oauth2(m_oauth2_config) ;
        }

        Account(std::string access_tokoen, std::string refresh_token);

        static Account getAccount();
    protected:
        void run_internal() override;
        void loadFilesAndFolders() override;
        void getFilesAndFolders();
    private:
//        oauth2_config m_oauth2_config;
//        http_client_config m_http_config;
//        http_client m_api = http_client("https://gooleapis.com/drive/v3", m_http_config);

        std::string m_last_page_token="";
        std::atomic<ino_t> inode_count = 1;
        std::map<ino_t, std::shared_ptr<DriveFile>> inodeToFile;
        std::map<std::string, std::shared_ptr<DriveFile>> idToFile;

    };
};


#define DATABASEDATA "GDriveData"
#define DATABASESETTINGS "settings"
#define DATABASENAME "DriveFS"
#define GDRIVETOKENNAME "gdrive_tokens"
#define GDRIVELASTCHANGETOKEN "gdrive last change token"