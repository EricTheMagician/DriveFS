//
// Created by eric on 27/01/18.
//

#include "gdrive/Account.h"
#include "BaseFileSystem.h"
#include <cpprest/filestream.h>
#include <easylogging++.h>
#include <pplx/pplxtasks.h>
#include <gdrive/FileIO.h>
#include <regex>

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
                         m_id_buffer(10) {

        upsert.upsert(true);
        find_and_upsert.upsert(true);

        mongocxx::pool::entry conn = pool.acquire();
        mongocxx::database client = conn->database(std::string(DATABASENAME));
        mongocxx::collection db = client[std::string(DATABASESETTINGS)];

        auto maybeResult = db.find(
                document{} << "name" << std::string(GDRIVELASTCHANGETOKEN) << finalize
        );

        for (auto changeTokens: maybeResult) {
            auto res = changeTokens["value"];
            auto driveId = changeTokens["id"];
            if (res && driveId) {
                LOG(INFO) << "Previous change tokens founds";
                auto s_driveId = driveId.get_utf8().value;
                if( s_driveId == "root") {
                    m_newStartPageToken[std::string("")] = res.get_utf8().value.to_string();
                }else{
                    m_newStartPageToken[s_driveId.to_string()] = res.get_utf8().value.to_string();
                }
            }

        }
    }

    Account::Account(const std::string &at, const std::string &rt) : Account() {
        m_refresh_token = rt;
        auto token = m_oauth2_config.token();
        m_oauth2_config.set_bearer_auth(true);
        token.set_access_token(at);
        token.set_refresh_token(rt);
        token.set_expires_in(0);
        token.set_token_type("Bearer");
        token.set_scope(GDRIVE_OAUTH_SCOPE);
        m_oauth2_config.set_token(token);
        refresh_token();
        m_http_config.set_oauth2(m_oauth2_config);
        m_needToInitialize = false;
        getFilesAndFolders();
        loadFilesAndFolders();
        SFAsync(&Account::background_update, this, std::string(""), false);
    }


    void Account::run_internal() {

        mongocxx::pool::entry conn = pool.acquire();
        mongocxx::database client = conn->database(std::string(DATABASENAME));
        mongocxx::collection db = client[std::string(DATABASESETTINGS)];
        auto &token = m_oauth2_config.token();
        mongocxx::options::update options;
        options.upsert(true);
        db.update_one(document{} << "name" << std::string(GDRIVETOKENNAME) << finalize,
                      document{} << "$set" << open_document << "name" << std::string(GDRIVETOKENNAME) << "access_token"
                                 << token.access_token() << "refresh_token" << token.refresh_token() << close_document
                                 << finalize,
                      options
        );
        m_refresh_token = token.refresh_token();


        getFilesAndFolders();
        SFAsync(&Account::background_update, this, std::string(""), false);

    }

    Account Account::getAccount() {
        LOG(TRACE) << "Getting Account";
        mongocxx::pool::entry conn = pool.acquire();
        mongocxx::database client = conn->database(std::string(DATABASENAME));
        mongocxx::collection db = client[std::string(DATABASESETTINGS)];

        auto maybeResult = db.find_one(
                document{} << "name" << std::string(GDRIVETOKENNAME) << finalize
        );

        if (maybeResult) {
            LOG(INFO) << "Access tokens founds";
            auto res = maybeResult->view();
            std::string at(res["access_token"].get_utf8().value.to_string()),
                    rt(res["refresh_token"].get_utf8().value.to_string());

            if (!at.empty() && !rt.empty())
                return Account(at, rt);

        }
        return Account();
    }

    void Account::background_update(std::string teamDriveId, bool skip_sleep) {
        try {
            if ( !skip_sleep || teamDriveId.empty()) {
                sleep(this->refresh_interval);
            }
            refresh_token();

            http_client client(this->m_apiEndpoint, this->m_http_config);
            uri_builder uriBuilder("changes");
            std::string pageToken = this->m_newStartPageToken[teamDriveId];
            if (pageToken.empty()) {
                LOG(ERROR) << "pageToken is empty";
            }
            if (teamDriveId.empty()) {
                LOG(INFO) << "Getting updated list of files and folders for root folder with token " << pageToken;
            } else {
                LOG(INFO) << "Getting updated list of files and folders for folder " << teamDriveId << " and token "
                          << pageToken;
                uriBuilder.append_query("teamDriveId", teamDriveId);
            }
            uriBuilder.append_query("pageToken", pageToken);
            uriBuilder.append_query("pageSize", 1000);
            uriBuilder.append_query("includeTeamDriveItems", "true");
            uriBuilder.append_query("supportsTeamDrives", "true");
            uriBuilder.append_query("spaces", "drive");
            uriBuilder.append_query("fields", "changes,nextPageToken,newStartPageToken");

            auto response = client.request(methods::GET, uriBuilder.to_string()).get();
            if (response.status_code() != 200) {
                LOG(ERROR) << "Failed to get changes: " << response.reason_phrase();
                LOG(ERROR) << response.extract_json(true).get();
                background_update(teamDriveId, false);
                return;
            }
            mongocxx::bulk_write documents;
            bsoncxx::document::value doc = bsoncxx::from_json(response.extract_utf8string().get());
            bsoncxx::document::view value = doc.view();

            bool needs_updating = false;

            auto eleChanges = value["changes"];
            auto toDelete = bsoncxx::builder::basic::array{};
            bool hasItemsToDelete = false;
            if (eleChanges) {
                bsoncxx::array::view changes = eleChanges.get_array().value;
                int count = 0;
                for (const auto &change : changes) {
                    auto doc = change.get_document();
                    auto view = doc.view();
                    auto file = view["file"];
                    if (!file) {
                        auto deleted = view["removed"];
                        if (deleted) {
                            if (deleted.get_bool().value) {
                                LOG(DEBUG) << bsoncxx::to_json(doc);
                                if (view["fileId"]) {
                                    toDelete.append(view["fileId"].get_utf8());
                                    hasItemsToDelete = true;
                                }
                            }
                        }
                        continue;
                    }

                    needs_updating = true;
                    auto fileDoc = file.get_document();
                    auto id = view["fileId"].get_utf8().value.to_string();

                    mongocxx::model::update_one upsert_op(
                            document{} << "id" << id << finalize,
                            document{} << "$set" << fileDoc << finalize
                    );

                    upsert_op.upsert(true);
                    documents.append(upsert_op);

                    auto found = _Object::idToObject.find(id);

                    if (found != _Object::idToObject.cend()) {
                        GDriveObject file = found->second;
                        std::vector<GDriveObject> oldParents = file->parents;
                        auto fileView = fileDoc.value;
                        file->updateInode(fileDoc);

                        auto newParentIds = fileView["parents"];
                        if (!newParentIds) {
                            LOG(DEBUG) << bsoncxx::to_json(view);
                            continue;
                        }

                        //remove old parents
                        for (auto parentId: newParentIds.get_array().value) {
                            const std::string s_parentId = parentId.get_utf8().value.to_string();
                            bool need_to_remove = true;
                            for (auto &parent: oldParents) {
                                if (parent->getId() == s_parentId) {
                                    need_to_remove = false;
                                    break;
                                }
                            }

                            if (need_to_remove) {
                                auto cursor = DriveFS::_Object::idToObject.find(s_parentId);
                                if (cursor != DriveFS::_Object::idToObject.cend()) {
                                    GDriveObject parent;
                                    parent = cursor->second;
                                    parent->removeChild(file);
                                    file->removeParent(parent);
                                }//if it't not already present, it probably means we already removed it locally and the background update is now syncing.
                            }


                        }

                        VLOG(3) << bsoncxx::to_json(fileDoc);

                        // add new parents
                        oldParents = file->parents;
                        for (auto parentId: newParentIds.get_array().value) {
                            bool need_to_set = true;
                            const std::string s_parentId = parentId.get_utf8().value.to_string();
                            for (auto parent: oldParents) {
                                if (parent->getId() != s_parentId) {
                                    continue;
                                } else {
                                    need_to_set = false;
                                    break;
                                }
                            }
                            if (need_to_set) {
                                auto cursor = DriveFS::_Object::idToObject.find(s_parentId);
                                if (cursor != DriveFS::_Object::idToObject.cend()) {
                                    GDriveObject parent = cursor->second;
                                    parent->addChild(file);
                                    file->addParent(parent);
                                    fuse_lowlevel_notify_inval_inode(this->fuse_session, parent->attribute.st_ino, 0,
                                                                     0);
                                }
                            }
                        }

                        //invalidate old parents
                        for (const auto &parent: oldParents) {
                            fuse_lowlevel_notify_inval_inode(this->fuse_session, parent->attribute.st_ino, 0, 0);
                        }

                        // notify the kernel that the inode is invalid.
                        fuse_lowlevel_notify_inval_inode(this->fuse_session, file->attribute.st_ino, -1, 0);

                    } else {
                        auto inode = inode_count.fetch_add(1, std::memory_order_acquire) + 1;
                        auto file = std::make_shared<DriveFS::_Object>(inode, fileDoc);

                        _Object::idToObject[file->getId()] = file;
                        _Object::inodeToObject[inode] = file;

                        for (auto &p : file->parents) {
                            p->addChild(file);
                            file->addParent(p);
                        }
                    }

                }
            }


            if (needs_updating or hasItemsToDelete) {
                mongocxx::pool::entry conn = pool.acquire();
                mongocxx::database client = conn->database(std::string(DATABASENAME));
                mongocxx::collection data = client[std::string(DATABASEDATA)];
                mongocxx::collection settings = client[std::string(DATABASESETTINGS)];

                if (needs_updating) {
                    data.bulk_write(documents);
                }
                if (hasItemsToDelete) {
                    data.delete_many(
                            document{} << "id" << open_document << "$in" << toDelete << close_document << finalize
                    );

                }


                settings.find_one_and_update(document{} << "name" << std::string(GDRIVELASTCHANGETOKEN) << finalize,
                                             document{} << "$set" << open_document
                                                        << "value" << m_newStartPageToken[teamDriveId]
                                                        << "id" << (teamDriveId.empty() ? "root" : teamDriveId)
                                                        << close_document
                                                        << finalize,
                                             find_and_upsert

                );
//            if (updateCache)
//                m_account->updateCache();
            }

            // parse files
            //get next page token
            bsoncxx::document::element nextPageTokenField = value["nextPageToken"];
            if (nextPageTokenField) {
                auto temp = nextPageTokenField.get_utf8().value.to_string();
                if (!temp.empty()) {
                    m_newStartPageToken[teamDriveId] = std::move(temp);
                }
            }

            //get new start page token
            bsoncxx::document::element newStartPageToken = value["newStartPageToken"];
            if (newStartPageToken) {
                auto temp = newStartPageToken.get_utf8().value.to_string();
                if (!temp.empty() && temp != pageToken) {
                    m_newStartPageToken[teamDriveId] = std::move(temp);
                }
            }

            if (nextPageTokenField) {
                background_update(teamDriveId, true);
            }
        } catch (std::exception &e) {
            LOG(ERROR) << e.what();
            background_update(teamDriveId, true);
        }

        if (teamDriveId.empty()) {

            for (auto item : this->m_newStartPageToken) {
                if (!item.first.empty())
                    background_update(item.first, true);
            }
            background_update();
        }
    }


    void Account::linkParentsAndChildren() {
        LOG(TRACE) << "Linking parent and children";
        mongocxx::pool::entry conn = pool.acquire();
        mongocxx::database client = conn->database(std::string(DATABASENAME));
        mongocxx::collection db = client[std::string(DATABASEDATA)];
        mongocxx::options::find options;
        options.projection(document{} << "parents" << 1 << "id" << 1 << finalize);
        auto cursor = db.find(
                document{} << "parents" << open_document << "$exists" << 1 << close_document << finalize,
                options);

        for (auto doc: cursor) {
            std::string child = doc["id"].get_utf8().value.to_string();
            auto found = DriveFS::_Object::idToObject.find(child);
            if (found != DriveFS::_Object::idToObject.end()) {
                bsoncxx::array::view parents = doc["parents"].get_array();
                for (auto parentId : parents) {
                    auto found2 = DriveFS::_Object::idToObject.find(parentId.get_utf8().value.to_string());
                    if (found2 != DriveFS::_Object::idToObject.end()) {
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
        mongocxx::database client = conn->database(std::string(DATABASENAME));
        mongocxx::collection db = client[std::string(DATABASEDATA)];

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
        bsoncxx::builder::stream::array toDelete;
        bool hasItemsToDelete = false;
        for (auto doc: cursor) {
//                    DriveFS::_Object object;

            bsoncxx::document::value value{doc};
            ino_t inode;

            //get the inode of the object
            inode = inode_count.fetch_add(1, std::memory_order_acquire) + 1;

            auto object = std::make_shared<DriveFS::_Object>(inode, doc);

            if (!object->getIsFolder() && !object->getIsUploaded()) {

                FileIO *io = new FileIO(object, 0, this);
                if (io->validateCachedFileForUpload(true)) {
                    auto ele = doc["uploadUrl"];
//                    LOG(INFO) << "Resuming upload of: " << object->getName();
                    if (ele) {
                        std::string url = ele.get_utf8().value.to_string();
                        SFAsync([io, url] {
                            io->resumeFileUploadFromUrl(url);
                            delete io;
                        });
                    } else {
                        SFAsync([io] {
                            io->upload();
                            delete io;
                        });
                    }
                } else {
                    delete io;
                    toDelete << object->getId();
                    hasItemsToDelete = true;
                    continue;
                }

            }

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

        cursor = client[std::string(DATABASEDATA)].find(document{} << "kind" << "drive#teamDrive" << finalize);
        if (cursor.begin() != cursor.end()) {
            auto inode = inode_count.fetch_add(1, std::memory_order_acquire) + 1;
            auto team_drive = _Object::buildTeamDriveHolder(inode, root);
            for (auto doc: cursor) {
                inode = inode_count.fetch_add(1, std::memory_order_acquire) + 1;
                _Object::buildTeamDrive(inode, doc, team_drive);
            }
        }

        LOG(TRACE) << "idToObject has " << DriveFS::_Object::idToObject.size() << " items.";
        LOG(TRACE) << "inodeToObject has " << DriveFS::_Object::inodeToObject.size() << " items.";
        linkParentsAndChildren();
    }

    void Account::getFilesAndFolders(std::string nextPageToken, std::string teamDriveId, int backoff) {

        refresh_token();
        LOG(INFO) << "Getting updated list of files and folders";

        http_client client(m_apiEndpoint, m_http_config);
        uri_builder uriBuilder("changes");
        if (nextPageToken.length() > 0) {
            uriBuilder.append_query("pageToken", nextPageToken);
        } else if (m_newStartPageToken.size() > 0) {
            auto found = m_newStartPageToken.find(teamDriveId);
            if (found != m_newStartPageToken.cend()) {
                uriBuilder.append_query("pageToken", found->second);
            } else {
                uriBuilder.append_query("pageToken", "1");
            }
        } else {
            uriBuilder.append_query("pageToken", "1");
            // get root metadata
            LOG(TRACE) << "Getting root folder";
            auto response = client.request(methods::GET, "files/root").get().extract_utf8string();
            bsoncxx::document::value doc = bsoncxx::from_json(response.get());
            bsoncxx::document::view value = doc.view();
            mongocxx::pool::entry conn = pool.acquire();
            mongocxx::database client = conn->database(std::string(DATABASENAME));
            mongocxx::collection data = client[std::string(DATABASEDATA)];

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

        uriBuilder.append_query("pageSize", "500");
        uriBuilder.append_query("includeTeamDriveItems", "true");
        uriBuilder.append_query("includeCorpusRemovals", "true");
        uriBuilder.append_query("supportsTeamDrives", "true");
//        uriBuilder.append_query("spaces", "drive");
        uriBuilder.append_query("fields", "changes,nextPageToken,newStartPageToken");
        if (!teamDriveId.empty()) {
            uriBuilder.append_query("teamDriveId", teamDriveId);
        }

        auto response = client.request(methods::GET, uriBuilder.to_string()).get();
        if (response.status_code() != 200) {
            LOG(ERROR) << "Failed to get changes: " << response.reason_phrase();
            LOG(ERROR) << response.extract_json(true).get();
            unsigned int sleep_time = std::pow(2, backoff);
            LOG(INFO) << "Sleeping for " << sleep_time << " seconds before retrying";
            sleep(sleep_time);
            if (backoff <= 5) {
                getFilesAndFolders(nextPageToken, teamDriveId, backoff + 1);
            }
            return;
        }
        bsoncxx::document::value doc = bsoncxx::from_json(response.extract_utf8string().get());
        bsoncxx::document::view value = doc.view();

        bool needs_updating = false;

        //get next page token
        bsoncxx::document::element nextPageTokenField = value["nextPageToken"];

        //get new start page token
        bsoncxx::document::element newStartPageToken = value["newStartPageToken"];
        if (nextPageTokenField) {
            m_newStartPageToken[teamDriveId] = nextPageTokenField.get_utf8().value.to_string();
        } else if (newStartPageToken) {
            m_newStartPageToken[teamDriveId] = newStartPageToken.get_utf8().value.to_string();
        }

        parseFilesAndFolders(value, teamDriveId, false);

        // parse files
        if (nextPageTokenField) {
            getFilesAndFolders(nextPageTokenField.get_utf8().value.to_string(), teamDriveId);
        } else {
            if (teamDriveId.empty()) {
                getTeamDrives();
            };
        }


    }

    void Account::parseFilesAndFolders(bsoncxx::document::view value, std::string teamDriveId, bool notify_filesystem) {
        bool needs_updating = false;
        mongocxx::bulk_write documents;

        auto eleChanges = value["changes"];
        auto toDelete = bsoncxx::builder::basic::array{};
        bool hasItemsToDelete = false;
        if (eleChanges) {
            bsoncxx::array::view changes = eleChanges.get_array().value;
            int count = 0;
            for (const auto &change : changes) {
                auto doc = change.get_document();
                auto view = doc.view();
                auto file = view["file"];
                if (!file) {
                    auto deleted = view["removed"];
                    if (deleted) {
                        if (deleted.get_bool().value) {
                            LOG(DEBUG) << bsoncxx::to_json(doc);
                            if (view["fileId"]) {
                                toDelete.append(view["fileId"].get_utf8());
                                hasItemsToDelete = true;
                            }
                        }
                    }
                    continue;
                }

                needs_updating = true;
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


        if (needs_updating or hasItemsToDelete) {
            mongocxx::pool::entry conn = pool.acquire();
            mongocxx::database client = conn->database(std::string(DATABASENAME));
            mongocxx::collection data = client[std::string(DATABASEDATA)];
            mongocxx::collection settings = client[std::string(DATABASESETTINGS)];

            if (needs_updating) {
                data.bulk_write(documents);
            }
            if (hasItemsToDelete) {
                data.delete_many(
                        document{} << "id" << open_document << "$in" << toDelete << close_document << finalize
                );

            }


            settings.find_one_and_update(document{} << "name" << std::string(GDRIVELASTCHANGETOKEN)
                                                    << "id" << (teamDriveId.empty() ? "root" : teamDriveId)
                                                    << finalize,
                                         document{} << "$set" << open_document
                                                    << "value" << m_newStartPageToken[teamDriveId]
                                                    << close_document
                                                    << finalize,
                                         find_and_upsert

            );
//            if (updateCache)
//                m_account->updateCache();
        }

    }

    void Account::getTeamDrives(int backoff) {
        refresh_token();

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
        auto eleTeamDrives = value["teamDrives"];
        auto toDelete = bsoncxx::builder::basic::array{};


        // update the database with teamdrives
        if (eleTeamDrives) {
            bsoncxx::array::view drives = eleTeamDrives.get_array().value;
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
            mongocxx::database client = conn->database(std::string(DATABASENAME));
            mongocxx::collection data = client[std::string(DATABASEDATA)];

            data.bulk_write(documents);

        }

        // get the changes  for the team drives
        if (eleTeamDrives) {
            bsoncxx::array::view drives = eleTeamDrives.get_array().value;
            needs_updating = !drives.empty();
            int count = 0;
            for (const auto &drive : drives) {
                auto doc = drive.get_document();
                auto view = doc.view();
                auto id = view["id"].get_utf8().value.to_string();
                getFilesAndFolders("", id);
            }
        }

    }

    std::string Account::getNextId() {
        m_event.wait();
        if (m_id_buffer.empty()) {
            m_event.signal();
            generateIds();
            m_event.wait();
        }
        const std::string id(m_id_buffer.front());
        m_id_buffer.pop_front();
        m_event.signal();
        return id;
    }

    void Account::generateIds(int_fast8_t backoff) {
        refresh_token();

        http_client client(m_apiEndpoint, m_http_config);
        uri_builder builder("files/generateIds");
        builder.append_query("coun=10");
        builder.append_query("space=drive");
        builder.append_query("fields=ids");

        http_response resp = client.request(methods::GET, builder.to_string()).get();
        if (resp.status_code() != 200) {
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
        for (auto id: view["ids"].get_array().value) {
            m_id_buffer.push_front(id.get_utf8().value.to_string());
        }

    }

    std::string Account::createFolderOnGDrive(std::string json, int backoff) {
        refresh_token();


        http_client client("https://www.googleapis.com/drive/v3/", m_http_config);
        uri_builder builder("files");
        builder.append_query("supportsTeamDrives", "true");
//        builder.append_query("uploadType", "resumable");

        http_response resp = client.request(methods::POST, builder.to_string(), json, "application/json").get();
        if (resp.status_code() != 200) {
            LOG(ERROR) << "Failed to create folders: " << resp.reason_phrase();
            LOG(ERROR) << json;
            LOG(ERROR) << resp.extract_json(true).get();
            unsigned int sleep_time = std::pow(2, backoff);
            LOG(INFO) << "Sleeping for " << sleep_time << " seconds before retrying";
            sleep(sleep_time);
            if (backoff <= 5) {
                return createFolderOnGDrive(json, backoff + 1);
            }
            return "";

        }
        int code = resp.status_code();
        return resp.extract_utf8string(true).get();

    }

    GDriveObject Account::createNewChild(GDriveObject parent, const char *name, int mode, bool isFile) {

        auto obj = _Object(inode_count.fetch_add(1, std::memory_order_acquire) + 1, getNextId(), name, mode, isFile);

        if (!isFile) {

            bsoncxx::builder::stream::document doc;
            doc << "mimeType" << "application/vnd.google-apps.folder"
                << "id" << getNextId()
                << "name" << name
                << "parents" << open_array << parent->getId() << close_array;

            std::string status = createFolderOnGDrive(bsoncxx::to_json(doc));


            if (!status.empty()) {
                mongocxx::pool::entry conn = pool.acquire();
                mongocxx::database client = conn->database(std::string(DATABASENAME));
                mongocxx::collection db = client[std::string(DATABASEDATA)];
                db.insert_one(obj.to_bson());
            } else {
                return nullptr;
            }

        }

        auto o = std::make_shared<_Object>(std::move(obj));
        _Object::idToObject[o->getId()] = o;
        _Object::inodeToObject[o->attribute.st_ino] = o;
        parent->addChild(o);
        o->addParent(parent);

        return o;
    }

    /*
     * trash the child object if it only has one parent left, otherwise, remove one parent of the child
     */
    bool Account::removeChildFromParent(GDriveObject child, GDriveObject parent) {


        if (child->parents.size() == 1) {
            if (trash(child)) {
                parent->removeChild(child);
                return true;
            }
        } else {
            child->removeParent(parent);
        }
        return false;
    }

    bool Account::trash(GDriveObject file, int backoff) {
        refresh_token();

        http_client client(m_apiEndpoint, m_http_config);
        std::stringstream ss;
        ss << "files/";
        ss << file->getId();
        uri_builder builder(ss.str());
        builder.append_query("supportsTeamDrives", "true");
        http_response resp = client.request(methods::DEL, builder.to_string()).get();
        if (resp.status_code() != 204) {
            if (resp.status_code() == 404) {
                return true;
            }
            LOG(ERROR) << "Failed to delete file or folder: " << resp.reason_phrase();
            LOG(ERROR) << "http status code\t" << resp.status_code();
            LOG(ERROR) << resp.extract_utf8string(true).get();
            unsigned int sleep_time = std::pow(2, backoff);
            LOG(INFO) << "Sleeping for " << sleep_time << " seconds before retrying";
            sleep(sleep_time);
            if (backoff <= 5) {
                return trash(file, backoff + 1);
            }
            return false;
        };
        return true;
    }

    void Account::upsertFileToDatabase(GDriveObject file) {

        if (file) {
            try {
                mongocxx::pool::entry conn = pool.acquire();
                mongocxx::database client = conn->database(std::string(DATABASENAME));
                mongocxx::collection data = client[std::string(DATABASEDATA)];
                auto status = data.update_one(
                        document{} << "id" << file->getId() << finalize,
                        document{} << "$set" << open_document << concatenate(file->to_bson()) << close_document
                                   << finalize,
                        upsert
                );
                status;
            } catch (std::exception &e) {
                LOG(ERROR) << e.what();
            }
        }
    }

    std::string Account::getUploadUrlForFile(GDriveObject file, std::string mimeType) {
        refresh_token();

        http_client client("https://www.googleapis.com/upload/drive/v3", m_http_config);
        uri_builder builder("files");
        builder.append_query("supportsTeamDrives", "true");
        builder.append_query("uploadType", "resumable");

//        http_response resp = client.request(methods::POST, builder.to_string(), json, "application/json").get();

        http_request req;
        auto &headers = req.headers();
        headers.add("X-Upload-Content-Length", std::to_string(file->getFileSize()));
        headers.add("X-Upload-Content-Type", mimeType);

        bsoncxx::builder::stream::array parents;
        for (auto parent: file->parents) {
            parents << parent->getId();
        }
        bsoncxx::builder::stream::document doc;

        doc << "id" << file->getId()
            << "createdTime" << file->getCreatedTimeAsString()
            << "name" << file->getName()
            << "parents" << parents;


        auto body = bsoncxx::to_json(doc.extract());
        req.set_body(body, "application/json");
//        json::value body;
//        body["id"] = file->getId();
//        body["createdTime"] = file->getCreatedTimeAsString();
//        body["name"]
//        req.set_body(body);
        req.set_request_uri(builder.to_uri());
        req.set_method(methods::POST);

        auto resp = client.request(req).get();
        std::string location;
        if (resp.status_code() != 200) {
            LOG(ERROR) << "Failed to get uploadUrl: " << resp.reason_phrase();
            LOG(ERROR) << resp.extract_utf8string(true).get();
            LOG(INFO) << "Sleeping for 1 second before retrying";
            sleep(1);
            location = getUploadUrlForFile(req);
        } else {
            location = resp.headers()["Location"];
        }

        SFAsync([location, file] {

            mongocxx::pool::entry conn = pool.acquire();
            mongocxx::database dbclient = conn->database(std::string(DATABASENAME));
            mongocxx::collection db = dbclient[std::string(DATABASEDATA)];
            file->m_event.wait();
            db.update_one(document{} << "id" << file->getId() << finalize,
                          document{} << "$set" << open_document <<
                                     "uploadUrl" << location << close_document << finalize
            );
            file->m_event.signal();

        });

        return location;
    }

    std::string Account::getUploadUrlForFile(http_request req, int backoff) {
        refresh_token();

        http_client client("https://www.googleapis.com/upload/drive/v3/files", m_http_config);
        http_response resp = client.request(req).get();
        if (resp.status_code() != 200) {
            LOG(ERROR) << "Failed to get uploadUrl: " << resp.reason_phrase();
            LOG(ERROR) << resp.extract_utf8string(true).get();
            unsigned int sleep_time = std::pow(2, backoff);
            LOG(INFO) << "Sleeping for " << sleep_time << " second before retrying";
            sleep(sleep_time);
            return getUploadUrlForFile(req, backoff + 1);
        }
        return resp.headers()["Location"];
//        return location;

    }

    bool
    Account::upload(std::string uploadUrl, std::string filePath, size_t fileSize, int64_t start, std::string mimeType) {
        try {
            concurrency::streams::istream stream = concurrency::streams::file_stream<unsigned char>::open_istream(
                    filePath).get();
            http_client client(uploadUrl, m_http_config);

            http_request req;
            if (start == 0) {
                req.set_body(stream, fileSize, mimeType);
            } else {
                stream.seek(start);
                req.set_body(stream, fileSize - start, mimeType);
                std::stringstream ss;
                ss << "bytes " << start << "-" << fileSize - 1 << "/" << fileSize;
                req.headers()["Content-Range"] = ss.str();
            }

            req.set_method(methods::PUT);

            auto resp = client.request(req).get();
            auto status_code = resp.status_code();
            stream.close().wait();
            if (status_code == 200 || status_code == 201) {
                return true;
            } else if (status_code == 401) {
                refresh_token();
            } else {
                LOG(ERROR) << "Failed to get uploadUrl: " << resp.reason_phrase();
                LOG(ERROR) << resp.extract_utf8string(true).get();
                return false;
            }
        } catch (std::exception &e) {
            LOG(ERROR) << e.what();
            return false;
        }

    }

    std::optional<int64_t> Account::getResumableUploadPoint(std::string url, size_t fileSize, int backoff) {
        refresh_token();

        http_client client(url, m_http_config);
        http_request request;
        request.set_method(methods::PUT);
        http_headers &headers = request.headers();
        std::stringstream ss;
        ss << "bytes */" << fileSize;
        headers.add("Content-Range", ss.str());

        http_response response = client.request(request).get();
        int status_code = response.status_code();
        if (status_code == 404) {
            return std::nullopt;
        }
        if (status_code == 308) {
            headers = response.headers();
            const std::string range = headers["Range"];
            const std::regex numbers("-(\\d+)", std::regex_constants::icase);
            std::smatch match;
            if (std::regex_search(range, match, numbers) && match.size() > 1) {
                return std::optional<int64_t>((int64_t) std::strtoll(match.str(1).c_str(), nullptr, 10) + 1);
            } else {
                return std::optional<int64_t>(0);
            }
        }
        if (status_code >= 400) {
            LOG(ERROR) << "Failed to get start point: " << response.reason_phrase();
            LOG(ERROR) << response.extract_utf8string(true).get();
//            LOG(ERROR) << response.headers()[""]
            unsigned int sleep_time = std::pow(2, backoff);
            LOG(INFO) << "Sleeping for " << sleep_time << " seconds before retrying";
            sleep(sleep_time);
            if (backoff <= 5) {
                return getResumableUploadPoint(url, fileSize, backoff + 1);
            }

        }

        return std::nullopt;

    }

    bool Account::updateObjectProperties(std::string id, std::string json, int backoff) {
        http_client client(m_apiEndpoint, m_http_config);
        std::stringstream uri;
        uri << "file/" << id;
        uri_builder builder(uri.str());

        http_response response = client.request(methods::PATCH, builder.to_string(), json, "application/json").get();
        if (response.status_code() != 200) {
            LOG(ERROR) << "Failed to update object properties: " << response.reason_phrase();
            LOG(ERROR) << response.extract_utf8string(true).get();
//            LOG(ERROR) << response.headers()[""]
            unsigned int sleep_time = std::pow(2, backoff);
            LOG(INFO) << "Sleeping for " << sleep_time << " seconds before retrying";
            sleep(sleep_time);
            if (backoff <= 5) {
                return updateObjectProperties(id, json, backoff + 1);
            }

            return false;

        }

        return true;

    }

}
