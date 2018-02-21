//
// Created by eric on 27/01/18.
//

#include "gdrive/Account.h"
#include <easylogging++.h>
#include <pplx/pplxtasks.h>
//using namespace utility;
//using namespace web;
using namespace web::http;
using namespace web::http::client;
using namespace bsoncxx::builder;

static mongocxx::options::update upsert;
static mongocxx::options::find_one_and_update find_and_upsert;

namespace DriveFS {

    Account::Account() : BaseAccount(
        "https://www.googleapis.com/drive/v3/",
        "126857315828-tj5cie9scsk0b5edmakl266p7pis80ts.apps.googleusercontent.com",
        "wxvtZ_SZpmEKXSB0kITXYx6C",
        "https://accounts.google.com/o/oauth2/v2/auth",
        "https://www.googleapis.com/oauth2/v4/token",
        "http://localhost:7878",
        GDRIVE_OAUTH_SCOPE),
         m_id_buffer(10),
                         filesApi(m_apiClient)
    {
        upsert.upsert(true);
        find_and_upsert.upsert(true);

        mongocxx::pool::entry conn = pool.acquire();
        mongocxx::database client = conn->database(DATABASENAME);
        mongocxx::collection db = client[DATABASESETTINGS];

        auto maybeResult = db.find_one(
            document{} << "name" << GDRIVELASTCHANGETOKEN << finalize
        );

        if (maybeResult) {
            auto res = maybeResult->view()["value"];
            if (res) {
                LOG(INFO) << "Previous change tokens founds";
                m_newStartPageToken = res.get_utf8().value.to_string();
            }

        }
    }

    Account::Account(const std::string &at, const std::string &rt) : Account() {
        auto token = m_oauth2_config.token();
        m_oauth2_config.set_bearer_auth(true);
        token.set_access_token(at);
        token.set_refresh_token(rt);
        token.set_expires_in(0);
        token.set_token_type("Bearer");
        token.set_scope(GDRIVE_OAUTH_SCOPE);
        m_oauth2_config.set_token(token);
        m_oauth2_config.token_from_refresh().get();
        m_http_config.set_oauth2(m_oauth2_config);
        m_apiConfig->setHttpConfig(m_http_config);
        m_needToInitialize = false;
        getFilesAndFolders();

    }

    void Account::run_internal() {

        mongocxx::pool::entry conn = pool.acquire();
        mongocxx::database client = conn->database(DATABASENAME);
        mongocxx::collection db = client[DATABASESETTINGS];
        auto token = m_oauth2_config.token();
        mongocxx::options::update options;
        options.upsert(true);
        db.update_one(document{} << "name" << GDRIVETOKENNAME << finalize,
                      document{} << "$set" << open_document << "name" << GDRIVETOKENNAME << "access_token"
                                 << token.access_token() << "refresh_token" << token.refresh_token() << close_document
                                 << finalize,
                      options
        );


        getFilesAndFolders();
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
            LOG(INFO) << "Access tokens founds";
            auto res = maybeResult->view();
            std::string at(res["access_token"].get_utf8().value.to_string()),
                rt(res["refresh_token"].get_utf8().value.to_string());
            return Account(at, rt);

        }
        return Account();
    }

    void Account::linkParentsAndChildren() {
        LOG(TRACE) << "Linking parent and children";
        mongocxx::pool::entry conn = pool.acquire();
        mongocxx::database client = conn->database(DATABASENAME);
        mongocxx::collection db = client[DATABASEDATA];
        mongocxx::options::find options;
        options.projection(document{} << "parents" << 1 << "id" << 1 << finalize);
        auto cursor = db.find(
            document{} << "parents" << open_document << "$exists" << 1 << close_document << finalize,
            options);

        for (auto doc: cursor) {
            std::string child = doc["id"].get_utf8().value.to_string();
            auto found = DriveFS::_Object::idToObject.find(child);
            printf("child %s\n", child.c_str());
            if (found != DriveFS::_Object::idToObject.end()) {
                printf("first %2d\t%s\n", (int) found->second->attribute.st_ino, found->second->getName().c_str());
                bsoncxx::array::view parents = doc["parents"].get_array();
                for (auto parentId : parents) {
                    auto found2 = DriveFS::_Object::idToObject.find(parentId.get_utf8().value.to_string());
                    printf("parent %s\n", parentId.get_utf8().value.to_string().c_str());
                    if (found2 != DriveFS::_Object::idToObject.end()) {
                        printf("second %2d\t%s\n", (int) found2->second->attribute.st_ino, found2->second->getName().c_str());
                        found2->second->addChild(found->second);
                        found->second->addParent(found2->second);
                    }

                }
            }
        }
    }

    void Account::loadFilesAndFolders() {
        LOG(TRACE) << "Filling GDrive Cache";
        bool needs_updating = false;
        mongocxx::pool::entry conn = pool.acquire();
        mongocxx::database client = conn->database(DATABASENAME);
        mongocxx::collection db = client[DATABASEDATA];

        //find root

        auto maybeRoot = db.find_one(document{} << "name" << "My Drive"
                                                << "parents"
                                                << open_document << "$exists" << 0
                                                << close_document << finalize);
        GDriveObject root;
        if (maybeRoot) {
            root = DriveFS::_Object::buildRoot(*maybeRoot);
        }

        auto cursor = db.find(document{} << "parents" << open_document << "$exists" << 1 << close_document << finalize);
        mongocxx::bulk_write documents;
        for (auto doc: cursor) {
//                    DriveFS::_Object object;

            bsoncxx::document::value value{doc};
            ino_t inode;

            //get the inode of the object
            inode = inode_count.fetch_add(1, std::memory_order_acquire) + 1;


            auto object = std::make_shared<DriveFS::_Object>(inode, doc);


            std::string id = object->getId();
            DriveFS::_Object::idToObject[id] = object;
            DriveFS::_Object::inodeToObject[inode] = object;

//                if(object->isUploaded()) {
//                        idToObject[id] = object;
//                        inodeToObject[inode] = object;
//                        this->doesFileExistOnAmazon(object);
//
//                }else{
//                        namespace fs = boost::filesystem;
//                        fs::path base{CACHEPATH};
//                        base /= object->m_id ;
//                        fs::path cache{base};
//                        cache += ".released";
//
//                        if( !fs::exists(cache) ) {
//
//                                needs_updating = true;
//                                mongocxx::model::delete_one delete_op(
//                                    document{} << "id" << object->m_id << finalize
//                                );
//                                documents.append(delete_op);
//                                try {
//                                        fs::remove(base);
//                                }catch(std::exception &e){
//
//                                }
//                                LOG(TRACE) << "Cached item not found -- " << object->m_id;
//
//                        }else{
//                                LOG(TRACE) << "Found cached item -- " << object->m_id;
//                                SFAsync([object,base,cache,this] {
//
//                                    FileIO io(object, 0, this->api);
//                                    io.b_is_uploading = true;
//                                    io.f_name = base.string();
//                                    io.upload(this);
//                                    fs::remove(cache);
//                                });
//                                idToObject[id] = object;
//                                inodeToObject[inode] = object;
//
//                        }
//                }

        }
//            if(needs_updating) {
//                db.bulk_write(documents);
//            }

        cursor = client[DATABASEDATA].find( document{} << "kind" << "drive#teamDrive" << finalize);
        if(cursor.begin() != cursor.end()){
            auto inode = inode_count.fetch_add(1, std::memory_order_acquire) + 1;
            auto team_drive = _Object::buildTeamDriveHolder(inode, root);
            for(auto doc: cursor){
                inode = inode_count.fetch_add(1, std::memory_order_acquire) + 1;
                _Object::buildTeamDrive(inode, doc, team_drive);
            }
        }

        LOG(TRACE) << "idToObject has " << DriveFS::_Object::idToObject.size() << " items.";
        LOG(TRACE) << "inodeToObject has " << DriveFS::_Object::inodeToObject.size() << " items.";
        linkParentsAndChildren();
    }

    void Account::getFilesAndFolders(std::string nextPageToken, int backoff) {

        LOG(INFO) << "Getting updated list of files and folders";

        http_client client(m_apiEndpoint, m_http_config);
        uri_builder uriBuilder("changes");
        if (nextPageToken.length() > 0) {
            uriBuilder.append_query("pageToken", nextPageToken);
        } else if (m_newStartPageToken.length() > 0) {
            uriBuilder.append_query("pageToken", m_newStartPageToken);
        } else {
            uriBuilder.append_query("pageToken", "1");
            // get root metadata
            LOG(TRACE) << "Getting root folder";
            auto response = client.request(methods::GET, "files/root").get().extract_utf8string();
            bsoncxx::document::value doc = bsoncxx::from_json(response.get());
            bsoncxx::document::view value = doc.view();
            mongocxx::pool::entry conn = pool.acquire();
            mongocxx::database client = conn->database(DATABASENAME);
            mongocxx::collection data = client[DATABASEDATA];

            auto rootValue = value["id"];
            if (rootValue) {
                auto rootId = rootValue.get_utf8().value.to_string();
                data.update_one(
                    document{} << "id" << rootId << finalize,
                    document{} << "$set" << open_document
                               << concatenate(value)
                               << close_document << finalize,
                    upsert
                );


            }
        }

        uriBuilder.append_query("pageSize", 1000);
        uriBuilder.append_query("includeTeamDriveItems", "true");
        uriBuilder.append_query("supportsTeamDrives", "true");
        uriBuilder.append_query("spaces", "drive");
        uriBuilder.append_query("fields", "changes,nextPageToken,newStartPageToken");

        auto response = client.request(methods::GET, uriBuilder.to_string()).get();
        if (response.status_code() != 200) {
            LOG(ERROR) << "Failed to get changes: " << response.reason_phrase();
            LOG(ERROR) << response.extract_json(true).get();
            unsigned int sleep_time = std::pow(2, backoff);
            LOG(INFO) << "Sleeping for " << sleep_time << " seconds before retrying";
            sleep(sleep_time);
            if (backoff <= 5) {
                getFilesAndFolders(nextPageToken, backoff + 1);
            }
            return;
        }
        mongocxx::bulk_write documents;
        bsoncxx::document::value doc = bsoncxx::from_json(response.extract_utf8string().get());
        bsoncxx::document::view value = doc.view();

        bool needs_updating = false;

        //get next page token
        bsoncxx::document::element nextPageTokenField = value["nextPageToken"];

        //get new start page token
        bsoncxx::document::element newStartPageToken = value["newStartPageToken"];
        if (newStartPageToken) {
            m_newStartPageToken = newStartPageToken.get_utf8().value.to_string();
        }


        auto eleChanges = value["changes"];
        auto toDelete = bsoncxx::builder::basic::array{};
        bool hasItemsToDelete = false;
        if (eleChanges) {
            bsoncxx::array::view changes = eleChanges.get_array().value;
            needs_updating = !changes.empty();
            int count = 0;
            for (const auto &change : changes) {
                auto doc = change.get_document();
                auto view = doc.view();
                auto file = view["file"];
                if (!file) {
                    auto deleted = view["removed"];
                    if (deleted) {
                        if (deleted.get_bool()) {
                            toDelete.append(view["fileId"].get_utf8());
                            hasItemsToDelete = true;
                        }
                    }
                    continue;
                }
                auto fileDoc = file.get_document();
                auto id = view["fileId"].get_utf8().value.to_string();

                mongocxx::model::update_one upsert_op(
                    document{} << "id" << id << finalize,
                    document{} << "$set" << fileDoc << finalize
                );

                upsert_op.upsert(true);
                documents.append(upsert_op);

//                auto found = idToFile.find(id);
//
//                if(  found != idToFile.cend() ){
//                    GDriveObject file = found->second;
//                    m_account->updateFileAfterUploading(object, bsoncxx::document::value(view));
//                }

            }
        }


        if (needs_updating) {
            mongocxx::pool::entry conn = pool.acquire();
            mongocxx::database client = conn->database(DATABASENAME);
            mongocxx::collection data = client[DATABASEDATA];
            mongocxx::collection settings = client[DATABASESETTINGS];

            data.bulk_write(documents);
            if (hasItemsToDelete) {
                data.delete_many(
                    document{} << "fileId" << open_document << "$in" << toDelete << close_document << finalize
                );
            }


            settings.find_one_and_update(document{} << "name" << GDRIVELASTCHANGETOKEN << finalize,
                                         document{} << "$set" << open_document << "value" << m_newStartPageToken
                                                    << close_document
                                                    << finalize,
                                         find_and_upsert

            );
//            if (updateCache)
//                m_account->updateCache();
        }

        // parse files

        if (nextPageTokenField) {
            getFilesAndFolders(nextPageTokenField.get_utf8().value.to_string());
        } else {
            getTeamDrives();
            loadFilesAndFolders();
        }


    }

    void Account::getTeamDrives(int backoff) {
        // load team drive folders
        http_client td_client(m_apiEndpoint, m_http_config);

        uri_builder uriBuilder("teamdrives");
        uriBuilder.append_query("fields", "teamDrives");
        http_response resp = td_client.request(methods::GET, uriBuilder.to_string()).get();

        if (resp.status_code() != 200) {
            LOG(ERROR) << "Failed to get team drives: " << resp.reason_phrase();
            LOG(ERROR) << resp.extract_json(true).get();
            unsigned int sleep_time = std::pow(2, backoff);
            LOG(INFO) << "Sleeping for " << sleep_time << " seconds before retrying";
            sleep(sleep_time);
            if (backoff <= 5) {
                getTeamDrives(backoff + 1);
            }
            return;

        }

        mongocxx::bulk_write documents;
        bsoncxx::document::value doc = bsoncxx::from_json(resp.extract_utf8string().get());
        bsoncxx::document::view value = doc.view();

        bool needs_updating = false;

        //get next page token
        bsoncxx::document::element nextPageTokenField = value["nextPageToken"];

        //get new start page token
        bsoncxx::document::element newStartPageToken = value["newStartPageToken"];
        if (newStartPageToken) {
            m_newStartPageToken = newStartPageToken.get_utf8().value.to_string();
        }


        auto eleChanges = value["teamDrives"];
        auto toDelete = bsoncxx::builder::basic::array{};

        if (eleChanges) {
            bsoncxx::array::view drives = eleChanges.get_array().value;
            needs_updating = !drives.empty();
            int count = 0;
            for (const auto &drive : drives) {
                auto doc = drive.get_document();
                auto view = doc.view();
                auto id = view["id"].get_utf8().value.to_string();

                mongocxx::model::update_one upsert_op(
                    document{} << "id" << id << finalize,
                    document{} << "$set" << doc << finalize
                );

                upsert_op.upsert(true);
                documents.append(upsert_op);
            }
        }

        if (needs_updating) {
            mongocxx::pool::entry conn = pool.acquire();
            mongocxx::database client = conn->database(DATABASENAME);
            mongocxx::collection data = client[DATABASEDATA];
            mongocxx::collection settings = client[DATABASESETTINGS];

            data.bulk_write(documents);

        }

    }
    std::string Account::getNextId(){
        m_event.wait();
        if(m_id_buffer.empty()){
            generateIds();
        }
        const std::string id(m_id_buffer.front());
        m_id_buffer.pop_front();
        m_event.signal();
        return id;
    }
    void Account::generateIds(int_fast8_t backoff){
        http_client client(m_apiEndpoint, m_http_config);
        uri_builder builder("files/generateIds");
        builder.append_query("coun=10");
        builder.append_query("space=drive");
        builder.append_query("fields=ids");

        http_response resp = client.request(methods::GET, builder.to_string()).get();
        if(resp.status_code() != 200){
            LOG(ERROR) << "Failed to generate Ids: " << resp.reason_phrase();
            LOG(ERROR) << resp.extract_json(true).get();
            unsigned int sleep_time = std::pow(2, backoff);
            LOG(INFO) << "Sleeping for " << sleep_time << " seconds before retrying";
            sleep(sleep_time);
            if (backoff <= 5) {
                generateIds(backoff + 1);
            }
            return;

        }

        bsoncxx::document::value doc = bsoncxx::from_json(resp.extract_utf8string().get());
        auto view = doc.view();
        for(auto id: view["ids"].get_array().value){
            m_id_buffer.push_front(id.get_utf8().value.to_string());
        }

    }

    bool Account::createFolderOnGDrive(std::string json, int backoff){
        http_client client("https://www.googleapis.com/upload/drive/v3/", m_http_config);
        uri_builder builder("files");
        builder.append_query("supportsTeamDrives", "true");
        builder.append_query("uploadType", "resumable");

        http_response resp = client.request(methods::POST, builder.to_string(), json).get();
        if(resp.status_code() != 200){
            LOG(ERROR) << "Failed to create folders: " << resp.reason_phrase();
            LOG(ERROR) << json;
            LOG(ERROR) << resp.extract_json(true).get();
            unsigned int sleep_time = std::pow(2, backoff);
            LOG(INFO) << "Sleeping for " << sleep_time << " seconds before retrying";
            sleep(sleep_time);
            if (backoff <= 5) {
                return createFolderOnGDrive(json, backoff + 1);
            }
            return false;

        }
        int code = resp.status_code();
        auto s = resp.extract_utf8string(true).get();
        return true;

    }

    bool Account::createFolderOnGDrive(json::value json, int backoff){
        http_client client("https://www.googleapis.com/upload/drive/v3/", m_http_config);
        uri_builder builder("files");
        builder.append_query("supportsTeamDrives", "true");
        builder.append_query("uploadType", "resumable");

        http_response resp = client.request(methods::POST, builder.to_string(), json).get();
        if(resp.status_code() != 200){
            LOG(ERROR) << "Failed to create folders: " << resp.reason_phrase();
            LOG(ERROR) << json;
            LOG(ERROR) << resp.extract_json(true).get();
            unsigned int sleep_time = std::pow(2, backoff);
            LOG(INFO) << "Sleeping for " << sleep_time << " seconds before retrying";
            sleep(sleep_time);
            if (backoff <= 5) {
                return createFolderOnGDrive(json, backoff + 1);
            }
            return false;

        }

        int code = resp.status_code();
        auto s = resp.extract_utf8string(true).get();



        return true;

    }

    bool Account::createFolderOnGDrive(std::shared_ptr<io::swagger::client::model::File> body, int backoff) {

//        m_apiConfig->setBaseUrl("https://www.googleapis.com/upload/drive/v3/");
//        m_apiConfig->setBaseUrl("https://www.googleapis.com/drive/v3");

        auto file = filesApi.create(/*alt*/"" , /*fields*/"",
                /* apiKey */ "", /* acces token */ /*m_http_config.oauth2()->access_token_key()*/ "",
                /* pretty print */ false,
                /* quotaUser*/ "", /* userIp */ "",
                /* std::shared_ptr<File>  */ body,
                /* ignoreDefaultVisibility */ false,  /* keepRevisionForever*/ false,
                /* ocrLanguage*/ "", /* supportsTeamDrives */ true,
                /* useContentAsIndexableText*/ false).get();

        return true;
    }

    GDriveObject Account::createNewChild(GDriveObject parent, const char *name, int mode, bool isFile){

        auto obj = _Object(inode_count.fetch_add(1, std::memory_order_acquire)+1, std::string(getNextId()), name, mode, isFile);

        if(!isFile){

            std::shared_ptr<io::swagger::client::model::File> body = std::make_shared<io::swagger::client::model::File>();
            body->setMimeType("application/vnd.google-apps.folder");
            body->setId(obj.getId());
            body->setName(name);
            std::vector<utility::string_t> parents(1);
            parents[0] = parent->getId();
            body->setParents(parents);
            bool status = createFolderOnGDrive(body);
            if(status){
                mongocxx::pool::entry conn = pool.acquire();
                mongocxx::database client = conn->database(DATABASENAME);
                mongocxx::collection db = client[DATABASESETTINGS];
                bsoncxx::document::value doc = obj.to_bson();
                db.insert_one(std::move(doc));
            }

        }
        //TODO: Add a regular file to dastabase

        auto o = std::make_shared<_Object>(obj);
        parent->addChild(o);
        o->addParent(parent);
        return o;
    }

    void Account::removeChildFromParent(GDriveObject child, GDriveObject parent){
        http_client client(m_apiEndpoint, m_http_config);
        std::stringstream ss;
        ss << "files/" << child->getId();
        uri_builder builder(ss.str());
        builder.append_query("removeParents", parent->getId());
    }

}