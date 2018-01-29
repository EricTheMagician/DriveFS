//
// Created by eric on 27/01/18.
//

#include "gdrive/Account.h"
#include <easylogging++.h>
//using namespace utility;
//using namespace web;
using namespace web::http;
using namespace web::http::client;

namespace DriveFS{

    Account::Account(std::string at, std::string rt):Account(){
        auto token = m_oauth2_config.token();
        m_oauth2_config.set_bearer_auth(true);
        token.set_access_token(std::move(at));
        token.set_refresh_token(std::move(rt));
        token.set_expires_in(-1);
        token.set_token_type("Bearer");
        m_needToInitialize = false;

    }

    void Account::run_internal(){

            mongocxx::pool::entry conn = pool.acquire();
            mongocxx::database client = conn->database(DATABASENAME);
            mongocxx::collection db = client[DATABASESETTINGS];
            auto token = m_oauth2_config.token();
            mongocxx::options::update options;
            options.upsert(true);
            db.update_one( document{} << "name" << GDRIVETOKENNAME << finalize,
                document{} << "$set" << open_document << "name" << GDRIVETOKENNAME << "access_token" << token.access_token() << "refresh_token" << token.refresh_token() << close_document <<  finalize,
                           options
            );


    }

    Account Account::getAccount() {
            LOG(TRACE) << "Getting Account";
            mongocxx::pool::entry conn = pool.acquire();
            mongocxx::database client = conn->database(DATABASENAME);
            mongocxx::collection db = client[DATABASESETTINGS];

            auto maybeResult = db.find_one(
                document{} << "name" << GDRIVETOKENNAME << finalize
            );

            if (maybeResult) {
                    auto res = maybeResult->view();
                    return Account(res["access_token"].get_utf8().value.to_string(),
                                   res["refresh_token"].get_utf8().value.to_string());
            }
            return Account();
    }

    void Account::loadFilesAndFolders() {

    }


}