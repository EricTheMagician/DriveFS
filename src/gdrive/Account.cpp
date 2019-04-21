//
// Created by eric on 27/01/18.
//

#include "gdrive/Account.h"
#include "gdrive/Filesystem.h"
#include "BaseFileSystem.h"
#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>
#include <cpprest/filestream.h>
#include <easylogging++.h>
#include <gdrive/File.h>
#include <gdrive/FileIO.h>
#include <pplx/pplxtasks.h>
#include <regex>
#include <pqxx/pqxx>
#include <string_view>
#include <Database.h>
#include "gdrive/FileManager.h"
#include <algorithm>

using namespace web::http;
using namespace web::http::client;

static boost::asio::thread_pool *parseFilesAndFolderThreadPool;
namespace DriveFS {

    namespace detail{
        void appendSqlRepresentationFromJson(std::string *_sql, json::value file, ino_t nextInode, pqxx::work *w, bool *wasFirst, std::string parentId= ""){
            std::string &sql = *_sql;
            try {
                bool trashed = file.has_field("trashed") ? file["trashed"].as_bool(): false;
//                if(trashed){
//                    return;
//                }
                if(*wasFirst)
                    *wasFirst=false;
                else
                    sql+= ", ";
                auto capabilities = file["capabilities"];
                std::string mimeType = file.has_string_field("mimeType")? file["mimeType"].as_string() : "";

                bool isTrashable = false;
                bool canRename = false;
                bool canDownload = false;
                if(!capabilities.is_null()){
                    if(capabilities.has_field("canTrash") )
                        isTrashable =capabilities["canTrash"].as_bool();
                    if(capabilities.has_field("canRename") )
                        canRename = capabilities["canRename"].as_bool();
                    if(capabilities.has_field("canDownload") )
                        canDownload = capabilities["canDownload"].as_bool();
                }
                std::string size = [&file](){
                    if(file.has_string_field("size"))
                        return file["size"].as_string();
                    if(file.has_string_field("quotaBytesUsed"))
                        return file["quotaBytesUsed"].as_string();
                    return std::string("0");
                }();
                int uid = -1, gid = -1, mode=-1;
                if(file.has_field("appProperties")){
                    auto appProperties = file["appProperties"];
                    if(!appProperties.is_null()) {
                        if (appProperties.has_field(APP_UID)) {
                            uid = atoi(appProperties[APP_UID].as_string().c_str());
                        }
                        if (appProperties.has_field(APP_GID)) {
                            gid = atoi(appProperties[APP_GID].as_string().c_str());
                        }
                        if (appProperties.has_field(APP_MODE)) {
                            mode = atoi(appProperties[APP_MODE].as_string().c_str());
                        }
                    }
                }
                std::string parents;
                parents.reserve(1000);
                bool wasFirstParent = true;
                parents += "{";
                if(parentId.empty()) {
                    for (auto &parent: file["parents"].as_array()) {
                        if (wasFirstParent) {
                            wasFirstParent = false;
                        } else {
                            parents += ",";
                        }
                        parents += "\"" + parent.as_string() + "\"";
                    }
                }else{
                    parents += "'";
                    parents += parentId;
                    parents += "'";
                }
                parents += "}";

                sql += "('" + w->esc(file["id"].as_string());
                sql += "','" + w->esc(file["name"].as_string());
                sql += "','" + parents;
                sql += "','" + w->esc(mimeType);
                sql += "','" + (file.has_string_field("md5Checksum") ? w->esc(file["md5Checksum"].as_string()) : "");
                sql += "',";
                sql += size;
                sql += ",";
                sql +=  (isTrashable ? "true" : "false");
                sql += ",";
                sql += ( canRename ? "true" : "false");
                sql += ",";
                sql += ( canDownload ? "true" : "false");
                sql += ",";
                sql += ( trashed ? "true" : "false");
                sql += ",'"  + w->esc(file["modifiedTime"].as_string());
                sql += "','" + w->esc(file["createdTime"].as_string());
                sql += "'," + std::to_string(std::strtoll(file["version"].as_string().c_str(), nullptr, 10));
                sql += ", " + std::to_string(nextInode);
                sql += "," + std::to_string(uid);
                sql += "," + std::to_string(gid);
                sql += "," + std::to_string(mode);

                sql += ") ";
            }catch(std::exception &e){
                LOG(ERROR) << e.what();
                LOG(DEBUG) << file.serialize();
                LOG(DEBUG) << sql;
                assert(false);
                std::exit(0);
            }

        }
        void appendSqlRepresentationFromObject(std::string *_sql, _Object *file, pqxx::work *w, bool *wasFirst, std::vector<std::string> parentIds, bool updateParents=true){
            std::string &sql = *_sql;
            try {
                bool trashed = file->getIsTrashed();
                if(*wasFirst)
                    *wasFirst=false;
                else
                    sql+= ", ";

                std::string mimeType = file->getIsFolder() ? GOOGLE_FOLDER : GOOGLE_FILE;

                bool isTrashable = file->getIsTrashable();
                bool canRename = file->getCanRename();
                bool canDownload = !file->getIsFolder();
                std::string size = std::to_string(file->attribute.st_size);
                uint uid = file->attribute.st_uid, gid = file->attribute.st_gid, mode=file->attribute.st_mode;
                std::string parents;
                parents.reserve(1000);
                bool wasFirstParent = true;
                parents += "{";
                for (auto &parent: parentIds) {
                    if (wasFirstParent) {
                        wasFirstParent = false;
                    } else {
                        parents += ",";
                    }
                    parents += "\"" + parent + "\"";
                }
                parents += "}";

                sql += "('" + w->esc(file->getId());
                sql += "','" + w->esc(file->getName());
                if(updateParents){
                    sql += "','" + parents;
                }
                sql += "','" + w->esc(mimeType);
                sql += "','" + file->md5();
                sql += "',";
                sql += size;
                sql += ",";
                sql +=  (isTrashable ? "true" : "false");
                sql += ",";
                sql += ( canRename ? "true" : "false");
                sql += ",";
                sql += ( canDownload ? "true" : "false");
                sql += ",";
                sql += ( trashed ? "true" : "false");
                sql += ",'"  + w->esc(file->getModifiedTimeAsString());
                sql += "','" + w->esc(file->getCreatedTimeAsString());
                sql += "'," + std::to_string(file->getVersion());
                sql += ", " + std::to_string(file->attribute.st_ino);
                sql += "," + std::to_string(uid);
                sql += "," + std::to_string(gid);
                sql += "," + std::to_string(mode);

                sql += ") ";
            }catch(std::exception &e){
                LOG(ERROR) << e.what();
                LOG(DEBUG) << file->getId();
                LOG(DEBUG) << sql;
                assert(false);
                std::exit(0);
            }
        }

    }
    Account::Account(std::string dbUri)
            : BaseAccount(dbUri, "https://www.googleapis.com/drive/v3/",
                          "126857315828-tj5cie9scsk0b5edmakl266p7pis80ts.apps."
                          "googleusercontent.com",
                          "wxvtZ_SZpmEKXSB0kITXYx6C",
                          "https://accounts.google.com/o/oauth2/v2/auth",
                          "https://www.googleapis.com/oauth2/v4/token",
                          "http://localhost:7179", GDRIVE_OAUTH_SCOPE),
              m_id_buffer(10) {
        FileIO::setAccount(this);
        pqxx::connection c{m_dbUri + DATABASENAME};
        pqxx::nontransaction w(c);
        std::stringstream sql;
        sql << "SELECT value, id FROM " DATABASESETTINGS " WHERE name=\'" << GDRIVELASTCHANGETOKEN << "\'";

        pqxx::result result{w.exec(sql)};

        for (auto changeTokens : result) {
            std::string res = changeTokens[0].as<std::string>();
            std::string driveId = changeTokens[1].as<std::string>();
            LOG(INFO) << "Previous change tokens founds";
            auto s_driveId = driveId;
            if (s_driveId == "root") {
                m_newStartPageToken[std::string("")] = res;
            } else {
                m_newStartPageToken[std::string(s_driveId)] = res;
            }
        }
    }


    Account::Account(Account &&account):BaseAccount(account.m_dbUri, "https://www.googleapis.com/drive/v3/",
                                                    "126857315828-tj5cie9scsk0b5edmakl266p7pis80ts.apps."
                                                    "googleusercontent.com",
                                                    "wxvtZ_SZpmEKXSB0kITXYx6C",
                                                    "https://accounts.google.com/o/oauth2/v2/auth",
                                                    "https://www.googleapis.com/oauth2/v4/token",
                                                    "http://localhost:7878", GDRIVE_OAUTH_SCOPE){
        FileIO::setAccount(this);


        m_newStartPageToken = std::move(account.m_newStartPageToken);

    }

    Account::Account(std::string dbUri, const std::string &at,
                     const std::string &rt)
            : Account(dbUri) {
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
        parseFilesAndFolderThreadPool = new boost::asio::thread_pool(16);
        getRootFolderId();
        setMaximumInodeFromDatabase();
        if (m_newStartPageToken.size() == 0) {
            std::string nextPageToken = "";
            do {
                nextPageToken = getFilesAndFolders(nextPageToken);
            } while (!nextPageToken.empty());
        }
        getTeamDrives();
        parseFilesAndFolderThreadPool->join();
        delete parseFilesAndFolderThreadPool;
        parseFilesAndFolderThreadPool = nullptr;

        loadFilesAndFolders();

        SFAsync(&Account::background_update, this, std::string(""));
        resumeUploadsOnStartup();
    }

    void Account::resumeUploadsOnStartup(){
        db_handle_t db;
        auto w = db.getWork();

        std::string sql = "SELECT inode, uploadUrl FROM " DATABASEDATA " WHERE "
                "mimeType NOT LIKE 'application/vnd.google-apps.%' AND "
                "(md5Checksum is NULL OR LENGTH(md5Checksum)=0) AND "
                "trashed=false AND array_length(parents,1) > 0";
        pqxx::result results;
        try{
            results = w->exec(sql);
            w->commit();
        }catch(std::exception &e){
            LOG(ERROR) << e.what();
            LOG(ERROR) << sql;
            w->commit();
            return;
        }

        for(auto const &result: results){
            GDriveObject file = FileManager::fromInode(result[0].as<ino_t>());
            std::string uploadUrl = result[1].is_null() ? "" : result[1].as<std::string>();

            FileIO::shared_ptr io {new FileIO(file, 0)};
            if (io->validateCachedFileForUpload(true)) {
                auto parents = FileManager::getParentIds(file);
                if (parents.empty()) {
                    io->deleteObject();
//                    toDelete << object->getId();
//                    hasItemsToDelete = true;
                    continue;
                }

                if (!uploadUrl.empty()) {
                    LOG(INFO) << "Adding to queue upload of file with name "
                              << file->getName() << " and id " << file->getId();
                    io->resumeFileUploadFromUrl(uploadUrl, true);
                } else {
                    LOG(INFO) << "Adding to queue upload of file with name "
                              << file->getName() << " and id " << file->getId();
                    io->upload(true);
                }
            } else {
                io->deleteObject();
//                toDelete << object->getId();
//                hasItemsToDelete = true;
                continue;
            }

        }

    }

    void Account::run_internal() {

        pqxx::connection c{m_dbUri + DATABASENAME};
        pqxx::nontransaction w(c);
        std::stringstream sql;
        auto &token = m_oauth2_config.token();

        m_refresh_token = token.refresh_token();

        sql <<  "INSERT INTO " DATABASESETTINGS
                "(name, value) VALUES ('"
                GDRIVEACCESSTOKENNAME "','"
            << w.esc(token.access_token()) << "'), " ;

        sql << "('" GDRIVEREFRESHTOKENNAME "', '"
            << w.esc(m_refresh_token) << "');";
        LOG(INFO) << sql.str();
        w.exec(sql);
        w.commit();

//        SFAsync(&Account::background_update, this, std::string(""));
//        background_update("");
    }

    Account Account::getAccount(const std::string &suri) {
        LOG(TRACE) << "Getting Account";
        db_handle_t db;
        pqxx::work *w = db.getWork();
        std::string at, rt;
        try {
            std::string sql =
            "SELECT value FROM " DATABASESETTINGS " WHERE name=\'" GDRIVEACCESSTOKENNAME "\' and value is not null and length(value) > 1";
            LOG(INFO) << sql;
            pqxx::row at_ = w->exec1(sql);
            sql.clear();

            sql = "SELECT value FROM " DATABASESETTINGS " WHERE name=\'" GDRIVEREFRESHTOKENNAME "\' and value is not null and length(value) > 1";
            pqxx::row rt_ = w->exec1(sql);
            at = at_[0].c_str();
            rt = rt_[0].c_str();
            w->commit();
        }catch(std::exception &e){
            LOG(ERROR) << e.what();
        }
        if(!at.empty() && !rt.empty())
            return Account(suri, at, rt);

        return Account(suri);
    }

    void Account::background_update(std::string teamDriveId) {
        while (true) {
            try {
                refresh_token();

                http_client client(this->m_apiEndpoint, this->m_http_config);
                uri_builder uriBuilder("changes");
                std::string pageToken = this->m_newStartPageToken[teamDriveId];
                if (pageToken.empty()) {
                    LOG(ERROR) << "pageToken is empty";
                }
                if (teamDriveId.empty()) {
                    LOG(INFO) << "Getting updated list of files and folders for root "
                                 "folder with token "
                              << pageToken;
                    uriBuilder.append_query("includeTeamDriveItems", "false");
                } else {
                    LOG(INFO) << "Getting updated list of files and folders for folder "
                              << teamDriveId << " and token " << pageToken;
                    uriBuilder.append_query("teamDriveId", teamDriveId);
                    uriBuilder.append_query("includeTeamDriveItems", "true");
                }
                uriBuilder.append_query("restrictToMyDrive", "true");
                uriBuilder.append_query("pageToken", pageToken);
                uriBuilder.append_query("pageSize", 1000);
                uriBuilder.append_query("supportsTeamDrives", "true");
                uriBuilder.append_query("spaces", "drive");
                uriBuilder.append_query("fields",
                                        "changes,nextPageToken,newStartPageToken");

                auto response =
                        client.request(methods::GET, uriBuilder.to_string()).get();
                if (response.status_code() != 200) {
                    LOG(ERROR) << "Failed to get changes: " << response.reason_phrase();
                    LOG(ERROR) << response.extract_json(true).get();
                    background_update(teamDriveId);
                    return;
                }
                auto jsonResponse = response.extract_json().get();

                // get next page token
                bool hasNextPageTokenField = jsonResponse.has_string_field("nextPageToken");
                if (hasNextPageTokenField) {
                    std::string sv = jsonResponse["nextPageToken"].as_string();
                    if (!sv.empty()) {
                        m_newStartPageToken[teamDriveId] = std::move(sv);
                    }
                }

                // get new start page token

                if (jsonResponse.has_string_field("newStartPageToken")) {
                    std::string temp = jsonResponse["newStartPageToken"].as_string();
                    if (!temp.empty() && temp.compare(pageToken) != 0) {
                        m_newStartPageToken[teamDriveId] = std::move(temp);
                    }
                }


                db_handle_t db;
                pqxx::work *w = db.getWork();
                std::string sql_insert, sql_delete;
                sql_insert.reserve(10000);
                sql_delete.reserve(10000);

                /*
update test as t set
    column_a = c.column_a
from (values
    ('123', 1),
    ('345', 2)
) as c(column_b, column_a)
where c.column_b = t.column_b;
                 https://stackoverflow.com/questions/18797608/update-multiple-rows-in-same-query-using-postgresql
                 */
                sql_insert += "INSERT INTO " DATABASEDATA "(id,name,parents,mimeType,md5Checksum,"
                              "size,"
                              "isTrashable,canRename,canDownload,"
                              "trashed,modifiedTime,createdTime, version, inode,uid,gid,mode)"
                              " VALUES ";


                sql_delete += "UPDATE ";
                sql_delete += DATABASEDATA;
                sql_delete += " SET trashed=true WHERE id IN (";


                bool not_needs_updating = true;
                bool not_hasItemsToDelete = true;

                std::vector<std::string> childToInvalidate;
                std::vector<std::string> objectToInvalidate;
                childToInvalidate.reserve(200);
                objectToInvalidate.reserve(300);

                // parse files
                if (jsonResponse.has_array_field("changes")) {
                    auto changes = jsonResponse["changes"].as_array();
                    for (auto &change : changes) {

//                        LOG(INFO) << change.serialize();

                        if (!change.has_object_field("file")) {
                            // this section is for a file being deleted

                            if (change.has_boolean_field("removed")) {
                                auto deleted = change["removed"].as_bool();
                                if (deleted) {
                                    // LOG(DEBUG) << bsoncxx::to_json(doc);
                                    if (change.has_string_field("fileId")) {
                                        std::string fileId = change["fileId"].as_string();
                                        if(!FileManager::hasId(fileId, true))
                                            continue;
                                        if (not_hasItemsToDelete) {
                                            not_hasItemsToDelete = false;
                                        } else {
                                            sql_delete += ",";
                                        }
                                        sql_delete += "'";
                                        sql_delete += fileId;
                                        sql_delete += "'";
                                        childToInvalidate.push_back(fileId);
                                    }
                                }
                            }

                            continue;
                        }

                        // this section is for a file being updated

                        auto file = change["file"];


//                        LOG(INFO) << file.serialize();
                        std::string id = file["id"].as_string();

                        detail::appendSqlRepresentationFromJson(&sql_insert, file, getNextInode(), w, &not_needs_updating);

                        bool hasChild = FileManager::hasId(id, true);
                        if (hasChild) {
                            childToInvalidate.push_back(id);

                        }


                        if (!file.has_array_field("parents")) {
                            LOG(DEBUG) << file.serialize();
                            continue;
                        }

                        std::vector<std::string> parentIds = FileManager::getParentIds(id);

                        std::vector<std::string> newParentIds,  differences;
                        json::array _newParentIds = file["parents"].as_array();
                        newParentIds.reserve(_newParentIds.size());
                        for (auto const &v: _newParentIds) {
                            newParentIds.push_back(v.as_string());
                        }

                        std::sort(parentIds.begin(), parentIds.end());
                        std::sort(newParentIds.begin(), newParentIds.end());

                        std::set_difference(
                                newParentIds.begin(), newParentIds.end(),
                                parentIds.begin(), parentIds.end(),
                                std::inserter(differences, differences.begin()));

                        // if there's no difference between the old parents and new parts, just invalidate
                        // the parent entries
                        // otherwise, invalidate the new parents and the old parents
                        for (auto const &newParent : newParentIds) {
                            objectToInvalidate.push_back(newParent);
                        }
                        for (auto const &oldParent: differences) {
                            objectToInvalidate.push_back(oldParent);
                        }



                        // notify the kernel that the inode is invalid.

                        // new child

//                        auto inode = getNextInode();
//                        auto file = std::make_shared<DriveFS::_Object>(inode, fileDoc);
//
//                        if (file->getName().find('/') != std::string::npos) {
//                            LOG(WARNING) << "Found a file with a slash in it's name. It is "
//                                            "not a valid linux name. Not inserting \'"
//                                         << file->getName() << "\' to the filesystem";
//                            continue;
//                        }
//
//                        _Object::insertObjectToMemoryMap(file);
//                        bsoncxx::array::view parents =
//                                fileDoc.view()["parents"].get_array();
//                        for (auto parentId : parents) {
//                            auto it = DriveFS::_Object::idToObject.find(
//                                    std::string(parentId));
//                            if (it != DriveFS::_Object::idToObject.end()) {
//                                it->second->addChild(file);
//                                file->addParent(it->second);
//                                this->invalidateInode(it->second->attribute.st_ino);
//                            }
//                        }
//                    }

                    }
                }

                if (!not_needs_updating or !not_hasItemsToDelete) {
                    if (!not_needs_updating) {
                        sql_insert += " ON CONFLICT (id) DO UPDATE SET "
                                      "parents=EXCLUDED.parents,"
                                      "name=EXCLUDED.name, md5Checksum=EXCLUDED.md5Checksum,"
                                      "trashed=EXCLUDED.trashed,modifiedTime=EXCLUDED.modifiedTime,createdTime=EXCLUDED.createdTime";

                        try {
                            w->exec(sql_insert);
                        }catch(pqxx::sql_error &e){
                            LOG(INFO) << e.what();
                            LOG(DEBUG) << sql_insert;
                        }

                    }
                    if (!not_hasItemsToDelete) {
                        sql_delete += ")";
//                                      " ON CONFLICT (id) DO UPDATE SET "
//                                      "trashed=EXCLUDED.trashed";

                        try {
                            w->exec(sql_delete);
                        }catch(pqxx::sql_error &e){
                            LOG(INFO) << e.what();
                            LOG(DEBUG) << sql_delete;
                        }
                    }


                    sql_insert.clear();
                    sql_insert += "UPDATE ";
                    sql_insert += DATABASESETTINGS;
                    sql_insert += " SET value='";
                    sql_insert += m_newStartPageToken[teamDriveId];
                    sql_insert += "' WHERE name='";
                    sql_insert += GDRIVELASTCHANGETOKEN;
                    sql_insert += "' AND id='";
                    sql_insert += teamDriveId.empty() ? "root" : teamDriveId;
                    sql_insert += "'";

                    try {
                        w->exec(sql_insert);
                    }catch(pqxx::sql_error &e){
                        LOG(INFO) << e.what();
                        LOG(DEBUG) << sql_insert;
                    }

                    for(auto const &objectId: objectToInvalidate){
                        invalidateId(objectId);
                    }
                    for(auto const &objectId: childToInvalidate){
                        invalidateParentEntries(objectId);
                    }

                }
                w->commit();
                if (hasNextPageTokenField) {
                    continue;
                }


            } catch( const web::http::  http_exception& e) {
                LOG(ERROR) << e.what();
                continue;
            }
            catch (pqxx::sql_error &e) {
                LOG(ERROR) << e.what();
                LOG(FATAL) << e.sqlstate();
                continue;
            }catch (std::exception &e) {
                LOG(FATAL) << e.what();
                continue;
            }

            if (teamDriveId.empty()) {

                for (auto &item : this->m_newStartPageToken) {
                    if (!item.first.empty()) {
                        background_update(item.first);
                    }
                }

                LOG(INFO) << "Finished checking for changes.Sleeping for " << this->refresh_interval << " seconds.";
                sleep(this->refresh_interval);

                continue;
            } else {
                // return only if the teamdrive id is not root. ie only the root folder
                // should be in the loop.
                return;
            }
        }
    }

    void Account::invalidateId(std::string const &id){
        if(FileManager::hasId(id, true))
            invalidateInode(FileManager::fromId(id)->getInode());
    }

    void Account::linkParentsAndChildren() {
        /*
        LOG(TRACE) << "Linking parent and children";
        mongocxx::pool::entry conn = pool.acquire();
        mongocxx::database client = conn->database(std::string(DATABASENAME));
        mongocxx::collection db = client[std::string(DATABASEDATA)];
        mongocxx::options::find options;
        options.projection(document{} << "parents" << 1 << "id" << 1 << finalize);
        auto cursor = db.find(document{} << "parents" << open_document << "$exists"
                                         << 1 << close_document << finalize,
                              options);

        for (auto doc : cursor) {
            std::string child(doc["id"]);

            auto found = DriveFS::_Object::idToObject.find(child);
            if (found != DriveFS::_Object::idToObject.end()) {
                bsoncxx::array::view parents = doc["parents"].get_array();
                for (auto parentId : parents) {
                    auto found2 = DriveFS::_Object::idToObject.find(
                            std::string(parentId));
                    if (found2 != DriveFS::_Object::idToObject.end()) {
                        found2->second->addChild(found->second);
                        found->second->addParent(found2->second);
                    }
                }
            }
        }
         */

        LOG(TRACE) << "Finished linking parent and children";
    }

    void Account::loadFilesAndFolders() {
#warning loadFilesAndFolders
        /*
        LOG(TRACE) << "Filling GDrive file information from cache";
        bool needs_updating = false;
        mongocxx::pool::entry conn = pool.acquire();
        mongocxx::database client = conn->database(std::string(DATABASENAME));
        mongocxx::collection db = client[std::string(DATABASEDATA)];
        // find root

        auto maybeRoot =
                db.find_one(document{} << "isRoot" << 1 << "parents" << open_document
                                       << "$exists" << 0 << close_document << finalize);
        GDriveObject root;
        if (maybeRoot) {
            root = DriveFS::_Object::buildRoot(*maybeRoot);
        } else {
            LOG(WARNING) << "Root folder not found. Attemping to fix. "
                         << "You should not see this message more than once.";

            root = DriveFS::_Object::buildRoot(getRootFolder());
        }

        // select files with at least one parent.
        auto cursor =
                db.find(document{} << "$nor" << open_array << open_document << "parents"
                                   << open_document << "$exists" << 0 << close_document
                                   << close_document << open_document << "parents"
                                   << open_document << "$size" << 0 << close_document
                                   << close_document << close_array << finalize);
        mongocxx::bulk_write documents;
        bsoncxx::builder::stream::array toDelete;
        bool hasItemsToDelete = false;
        for (auto doc : cursor) {
            //                    DriveFS::_Object object;

            bsoncxx::document::value value{doc};
            ino_t inode;
            if (doc["parents"].get_array().value.empty()) {
                continue;
            }

            // get the inode of the object
            inode = inode_count.fetch_add(1, std::memory_order_acquire) + 1;

            auto object = std::make_shared<DriveFS::_Object>(inode, doc);

            if (!(object->getIsFolder() || object->getIsUploaded() ||
                  object->getIsTrashed())) {

                FileIO *io = new FileIO(object, 0);
                if (io->validateCachedFileForUpload(true)) {
                    auto p = doc["parents"].get_array().value;
                    if (p.empty()) {
                        delete io;
                        toDelete << object->getId();
                        hasItemsToDelete = true;
                        continue;
                    }

                    auto const gid = object->getId();
                    auto found = _Object::idToObject.find(gid);
                    if (found != _Object::idToObject.end()) {
                        delete io;
                        LOG(ERROR) << "Found an un-uploaded object that appeared more than "
                                      "once in the DB.";
                        if (found->second->getIsUploaded()) {
                            LOG(TRACE) << "Deleting it from the DB since the other one is "
                                          "already uploaded";
                            db.delete_one(doc);
                            continue;
                        }
                        LOG(ERROR) << bsoncxx::to_json(doc);
                        continue;
                    }

                    auto ele = doc["uploadUrl"];
                    if (ele) {
                        std::string url(ele);
                        LOG(INFO) << "Adding to queue upload of file with name "
                                  << object->getName() << " and id " << object->getId();
                        io->resumeFileUploadFromUrl(url, true);
                    } else {
                        LOG(INFO) << "Adding to queue upload of file with name "
                                  << object->getName() << " and id " << object->getId();
                        io->upload(true);
                    }
                } else {
                    delete io;
                    toDelete << object->getId();
                    hasItemsToDelete = true;
                    continue;
                }
            }

            if (!object->getIsTrashed()) {

                if (object->getName().find('/') != std::string::npos) {
                    LOG(ERROR)
                            << "The file name contained a slash, which is illegal on unix";
                    LOG(ERROR) << "File name is: " << object->getName();
                    continue;
                }
                std::string id = object->getId();
                DriveFS::_Object::idToObject[id] = object;
                DriveFS::_Object::inodeToObject[inode] = object;
            }

            //            if(!object->getIsUploaded()) {
            //                namespace fs = boost::filesystem;
            //                fs::path base{FileIO::cachePath};
            //                base /= "upload";
            //                base /= object->getId() ;
            //                fs::path cache{base};
            //                cache += ".released";
            //
            //                if( !fs::exists(cache) ) {
            //
            //                    needs_updating = true;
            //                    mongocxx::model::delete_one delete_op(
            //                        document{} << "id" << object->getId() << finalize
            //                    );
            //                    documents.append(delete_op);
            //                    try {
            //                            fs::remove(base);
            //                    }catch(std::exception &e){
            //
            //                    }
            //                    LOG(TRACE) << "Cached item not found -- " <<
            //                    object->getId();
            //
            //                }else{
            //                    LOG(TRACE) << "Found cached item -- " <<
            //                    object->getId(); SFAsync([object,base,cache,this] {
            //
            //                        FileIO io(object, 0, this);
            //                        io.b_is_uploading = true;
            //                        io.f_name = base.string();
            //                        io.upload();
            //                        fs::remove(cache);
            //                    });
            //
            //                }
            //            }
        }
        //            if(needs_updating) {
        //                db.bulk_write(documents);
        //            }

        cursor = client[std::string(DATABASEDATA)].find(
                document{} << "kind"
                           << "drive#teamDrive" << finalize);
        if (cursor.begin() != cursor.end()) {
            auto inode = inode_count.fetch_add(1, std::memory_order_acquire) + 1;
            auto team_drive = _Object::buildTeamDriveHolder(inode, root);
            for (auto doc : cursor) {
                inode = inode_count.fetch_add(1, std::memory_order_acquire) + 1;
                _Object::buildTeamDrive(inode, doc, team_drive);
            }
        }

        LOG(TRACE) << "idToObject has " << DriveFS::_Object::idToObject.size()
                   << " items.";
        LOG(TRACE) << "inodeToObject has " << DriveFS::_Object::inodeToObject.size()
                   << " items.";
        linkParentsAndChildren();
         */
    }

    std::string Account::getFilesAndFolders(std::string nextPageToken, int backoff,
                                            std::string teamDriveId) {

        refresh_token();
        if (teamDriveId.empty()) {
            LOG(INFO) << "Getting current list of files and folders with token: "
                      << nextPageToken;
        } else {
            LOG(INFO) << "Getting team drive " << teamDriveId
                      << " list of files and folders with token: " << nextPageToken;
        }

        http_client aClient(m_apiEndpoint, m_http_config);
        uri_builder uriBuilder("files");

        if (nextPageToken.length() > 0) {
            uriBuilder.append_query("pageToken", nextPageToken);
        } else if (teamDriveId.empty()) {
            // get root metadata
            getRootFolderId();
        }

        if (teamDriveId.empty()) {
            uriBuilder.append_query("corpora", "user");
            uriBuilder.append_query("includeTeamDriveItems", "false");
            uriBuilder.append_query("q", "'me' in owners");
        } else {
            uriBuilder.append_query("corpora", "teamDrive");
            uriBuilder.append_query("teamDriveId", teamDriveId);
            uriBuilder.append_query("includeTeamDriveItems", "true");
        }
        uriBuilder.append_query("pageSize", 1000);
        uriBuilder.append_query("supportsTeamDrives", "true");
        //        uriBuilder.append_query("spaces", "drive");
        uriBuilder.append_query("fields", "files,nextPageToken");

        auto response = aClient.request(methods::GET, uriBuilder.to_string()).get();
        if (response.status_code() != 200) {
            LOG(ERROR) << "Failed to get changes: " << response.reason_phrase();
            LOG(ERROR) << response.extract_json(true).get();
            unsigned int sleep_time = std::pow(2, backoff);
            LOG(INFO) << "Sleeping for " << sleep_time << " seconds before retrying";
            sleep(sleep_time);
            return getFilesAndFolders(nextPageToken,
                                      backoff > 5 ? backoff : backoff + 1);
        }

        auto jsonResponse = response.extract_json().get();
        bool needs_updating = false;

        if(jsonResponse.has_string_field("nextPageToken")){
            m_newStartPageToken[std::string(teamDriveId)] = nextPageToken = jsonResponse["nextPageToken"].as_string();
        }else if(jsonResponse.has_string_field("newStartPageToken")) {
            m_newStartPageToken[std::string(teamDriveId)] = nextPageToken = jsonResponse["newStartPageToken"].as_string();
        }else{
            nextPageToken = "";
        }



        // // get new start page token
        // bsoncxx::document::element newStartPageToken = value["newStartPageToken"];
        // if (nextPageTokenField) {
        //   m_newStartPageToken[teamDriveId] =
        //       nextPageTokenField;
        // } else if (newStartPageToken) {
        //   m_newStartPageToken[teamDriveId] =
        //       newStartPageToken;
        // }
        boost::asio::post(*parseFilesAndFolderThreadPool,
                           [docToParse = std::move(jsonResponse), this]() -> void {
                               this->parseFilesAndFolders(std::move(docToParse));
                           });

        // parse files
        if (!nextPageToken.empty()) {
            return std::string(nextPageToken);
        } else {
            uri_builder uriBuilder_change_token("changes/startPageToken");
            if (!teamDriveId.empty()) {
                uriBuilder_change_token.append_query("supportsTeamDrives", "true");
                uriBuilder_change_token.append_query("teamDriveId", teamDriveId);
            }
            http_response resp =
                    aClient.request(methods::GET, uriBuilder_change_token.to_string())
                            .get();
            auto jsonChangeDoc = resp.extract_json().get();

            // get next page token
            if(jsonChangeDoc.has_string_field("startPageToken")) {
                std::string newStartPageToken = jsonChangeDoc["startPageToken"].as_string();
                db_handle_t db;
                pqxx::work *w = db.getWork();
                m_newStartPageToken[teamDriveId] = newStartPageToken;
                std::stringstream sql;
                sql << "INSERT INTO " << DATABASESETTINGS << "(name, id, value) VALUES "
                    << "('" << std::string(GDRIVELASTCHANGETOKEN)
                    << "', '" << (teamDriveId.empty() ? "root" : std::string(teamDriveId))
                    << "', '" << w->esc(newStartPageToken)
                    << "');";
                w->exec(sql);
                w->commit();
            }

            return "";
        }

    }

    void Account::parseFilesAndFolders(web::json::value value) {
        bool needs_updating = false;

        if(value.has_array_field("files")){
            auto files = value["files"].as_array();
            db_handle_t db;
            std::string sql;
            sql.reserve(256000);
            sql += "INSERT INTO ";
            sql += DATABASEDATA;
            sql +="(id,name,parents,mimeType,md5Checksum,"
                  "size,"
                  "isTrashable,canRename,canDownload,"
                  "trashed,modifiedTime,createdTime, version, inode,"
                  "uid,gid,mode)"
               " VALUES ";
            bool wasFirst=true;
            pqxx::work *w = db.getWork();

            for (auto &file: files) {
                detail::appendSqlRepresentationFromJson(&sql, file, getNextInode(), w, &wasFirst);

            }
            sql += " ON CONFLICT (id) DO UPDATE SET parents=EXCLUDED.parents, name=EXCLUDED.name, md5Checksum=EXCLUDED.md5Checksum,"
                   "trashed=EXCLUDED.trashed,modifiedTime=EXCLUDED.modifiedTime,createdTime=EXCLUDED.createdTime";
            w->exec(sql);
            w->commit();

        }
    }

    void Account::setMaximumInodeFromDatabase(){
        db_handle_t db;
        pqxx::work *w = db.getWork();

        pqxx::row result = w->exec1("select max(inode) from " DATABASEDATA);
        this->inode_count = result[0].as<ino_t>();

    }

    std::string Account::getRootFolderId() {
        LOG(TRACE) << "Getting root folder";
        if(!m_rootFolderId.empty()){
            return m_rootFolderId;
        }

        db_handle_t db;
        pqxx::work *w = db.getWork();

        // check database first
        {
            std::string sql = "SELECT id FROM " DATABASEDATA " WHERE name='_gdrive_root_name_'";
            pqxx::result result = w->exec(sql);
            if (result.size() == 1) {
                m_rootFolderId = result[0][0].c_str();
                return m_rootFolderId;
            }
        }

        //get from google drive
        {
            http_client http_client(m_apiEndpoint, m_http_config);
            auto jsonRoot = http_client.request(methods::GET, "files/root")
                    .get().extract_json().get();

            m_rootFolderId = jsonRoot["id"].as_string();
            std::stringstream sql, sql2;

            struct timespec now {time(nullptr),0};
            std::string s_now = getRFC3339StringFromTime(now);
            std::string mimeType = jsonRoot["mimeType"].as_string();
            sql << "INSERT INTO " DATABASEDATA "(uid,gid,mode,id,inode,name,mimeType,trashed,isTrashable,canRename"
                                                     ",modifiedTime,createdTime)"
                << " VALUES (-1,-1,-1,'"
                << m_rootFolderId
                << "', 1, '_gdrive_root_name_', '"
                << mimeType
                << "',false,false,false,'"
                << s_now
                << "', '" << s_now
                << "') "

                ;
            sql2 << "INSERT INTO " DATABASEDATA "(uid,gid,mode, id,inode,name,mimeType,trashed,isTrashable,canRename"
                                                     ",modifiedTime,createdTime,parents) VALUES "

                // insert an empty team drive folder
                << " (-1,-1,-1,'teamdrive_folder', "
                << getNextInode()
                << ",'Team Drives', '" << mimeType <<"', false, false, false, '"
                << s_now << "', '" << s_now << "', '{" << m_rootFolderId << "}'); "
                    ;

            db_handle_t db;
            LOG(INFO) << sql.str();
            LOG(INFO) << sql2.str();
            pqxx::work *w = db.getWork();
            w->exec(sql);
            w->exec(sql2);
            w->commit();
            return m_rootFolderId;

        }



        return "";

    }

    void Account::getTeamDrives(int backoff) {
        refresh_token();

        // load team drive folders
        http_client td_client(m_apiEndpoint, m_http_config);

        uri_builder uriBuilder("teamdrives");
        uriBuilder.append_query("fields", "teamDrives");
        http_response resp =
                td_client.request(methods::GET, uriBuilder.to_string()).get();

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

        auto jsonDoc = resp.extract_json().get();

        bool needs_updating = false;

        // update the database with teamdrives
        if (jsonDoc.has_array_field("teamDrives")) {
            db_handle_t db;
            pqxx::work *w = db.getWork();
            std::stringstream sql;
            sql << "INSERT INTO " << DATABASEDATA << "(uid,gid,mode,id,inode,name,mimeType,trashed,isTrashable,canRename"
                                                     ",modifiedTime,createdTime,parents)"
                << " VALUES ";
            bool wasFirst=true;

            // get the team drives
            struct timespec now {time(nullptr),0};
            std::string s_now = getRFC3339StringFromTime(now);
            for (auto &drive : jsonDoc["teamDrives"].as_array()) {
                VLOG(3) << drive.serialize();

                if(drive["id"].as_string() == m_rootFolderId) continue;

                if(wasFirst){
                    wasFirst = false;
                }else{
                    sql << ", ";
                }
                sql << "(-1,-1,-1,'"
                    << drive["id"].as_string()
                    << "', " << getNextInode() << ", '" << drive["name"].as_string() << "', '"
                    << "application/vnd.google-apps.folder"
                    << "',false,false,false,'"
                    << s_now
                    << "', '" << s_now
                    << "', '{\"teamdrive_folder\"}') "
                        ;

            }

            sql << " ON CONFLICT (id) DO NOTHING";
            w->exec(sql);
            w->commit();

            std::string nextPageToken = "";
            // get the list of all files and folders in the team drives if it has not yet been fetched
            for (auto &drive : jsonDoc["teamDrives"].as_array()) {
                std::string id = drive["id"].as_string();
                if(id == m_rootFolderId) continue;
                if(m_newStartPageToken.find(id) != m_newStartPageToken.end()){
                    continue;
                }
                do {
                    nextPageToken = getFilesAndFolders(nextPageToken, 0, id);

                } while (!nextPageToken.empty());
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

        http_response resp;
        try {
            resp = client.request(methods::GET, builder.to_string()).get();
        } catch (std::exception &e) {
            LOG(ERROR) << "Failed to generate Ids: " << e.what();
            unsigned int sleep_time = std::pow(2, backoff);
            LOG(INFO) << "Sleeping for " << sleep_time << " seconds before retrying";
            sleep(sleep_time);
            generateIds(backoff + 1);
            return;
        }
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

        auto jsonResponse = resp.extract_json().get();
        for (auto id : jsonResponse["ids"].as_array()) {
            m_id_buffer.push_front(id.as_string());
        }
    }

    web::json::value Account::createFolderOnGDrive(std::string const &json, int backoff) {
        refresh_token();

        http_client client("https://www.googleapis.com/drive/v3/", m_http_config);
        uri_builder builder("files");
        builder.append_query("supportsTeamDrives", "true");
        //        builder.append_query("uploadType", "resumable");

        http_response resp;
        try {
            resp = client.request(methods::POST, builder.to_string(), json, "application/json").get();
        }catch(std::exception &e){
            LOG(ERROR) << "Failed to create folders: " << e.what();
            LOG(ERROR) << json;
            LOG(ERROR) << resp.extract_json(true).get();
            unsigned int sleep_time = std::pow(2, backoff);
            LOG(INFO) << "Sleeping for " << sleep_time << " seconds before retrying";
            sleep(sleep_time);

            return createFolderOnGDrive(json, backoff);
        }
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
            return web::json::value::null();
        }
//        int code = resp.status_code();
        return resp.extract_json(true).get();
    }

    GDriveObject Account::
    createNewChild(GDriveObject const &parent, const char *name,
                                         mode_t mode, bool isFile) {

        auto obj = _Object(inode_count.fetch_add(1, std::memory_order_acquire) + 1,
                           getNextId(), name, mode, isFile);

        if (!isFile) {
            web::json::value doc;
            doc["mimeType"] = web::json::value::string("application/vnd.google-apps.folder");
            doc["id"] = web::json::value::string(obj.getId());
            doc["name"] = web::json::value::string(name);
            doc["parents"][0] = web::json::value::string(parent->getId());

            LOG(INFO) << doc.serialize();

            web::json::value status = createFolderOnGDrive(doc.serialize());

            if (!status.is_null()) {
                db_handle_t db;
                auto w = db.getWork();
                std::string sql;
                sql.reserve(512);
                sql += "INSERT INTO ";
                sql += DATABASEDATA;
                sql += "(id,name,parents,mimeType,md5Checksum,"
                              "size,"
                              "isTrashable,canRename,canDownload,"
                              "trashed,modifiedTime,createdTime, version, inode,uid,gid,mode)"
                              " VALUES ('";
                sql += obj.getId();
                sql += "', '";
                sql += obj.getName();
                sql += "', '{\"";
                sql += parent->getId();
                sql += "\"}', 'application/vnd.google-apps.folder', '',0,"
                       "true,true,true,"
                       "false,'";
                sql += getRFC3339StringFromTime(obj.attribute.st_mtim);
                sql += "', '";
                sql += getRFC3339StringFromTime(obj.attribute.st_atim);
                sql += "',1, ";
                sql += std::to_string(obj.getInode());
                sql += ",-1,-1,-1)";
                LOG(INFO) << sql;
                w->exec(sql);
                w->commit();
            } else {
                return nullptr;
            }
        }

        auto o = std::make_shared<_Object>(std::move(obj));

        FileManager::insertObjectToMemoryMap(o);

//        parent->addChild(o);
//        o->addParent(parent);
//
//        assert(o->parents.size() == 1);
        return o;
    }

/*
 * trash the child object if it only has one parent left, otherwise, remove one
 * parent of the child
 */
    bool Account::removeChildFromParent(GDriveObject const &child, GDriveObject const &parent) {
        auto parentIds = FileManager::getParentIds(child->getId());
        bool status = false;
        db_handle_t db;
        auto w = db.getWork();
        std::string sql;
        sql.reserve(256);

        if(parentIds.empty()){
            return true;
        }else if (parentIds.size() == 1) {

            status = child->getIsUploaded() ? this->trash(child): true;
            if(!status)
                return false;
            snprintf( sql.data(), 256, "UPDATE " DATABASEDATA " SET parents='{}', trashed=true where inode=%lu", child->getInode());
        } else {
            status = child->getIsUploaded() ? true : updateObjectProperties(child->getId(), "{}", "", parent->getId());
            if (!status)
                return false;
            snprintf( sql.data(), 256, "UPDATE " DATABASEDATA " SET parents=array_remove(parents, '%s') where inode=%lu", parent->getId().c_str(), child->getInode());
        }
        try{
            w->exec(sql);
            w->commit();
            return true;
        }catch(std::exception &e){
            LOG(ERROR) << e.what();
            return false;
        }

    }

    bool Account::trash(GDriveObject const &file, int backoff) {
        refresh_token();

        http_client client(m_apiEndpoint, m_http_config);
        std::stringstream ss;
        ss << "files/";
        ss << file->getId();
        uri_builder builder(ss.str());
        builder.append_query("supportsTeamDrives", "true");
        http_response resp;
        try {
            resp = client.request(methods::DEL, builder.to_string()).get();
        } catch (std::exception &e) {
            LOG(ERROR) << "There was an error when trying to trash a file";
            LOG(ERROR) << e.what();
            unsigned int sleep_time = std::pow(2, backoff);
            LOG(INFO) << "Sleeping for " << sleep_time << " seconds before retrying";
            sleep(sleep_time);
            if (backoff <= 10) {
                return trash(file, backoff);
            }
            return false;
        }
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
            if (backoff <= 10) {
                return trash(file, backoff + 1);
            }
            return false;
        };
        return true;
    }

    void Account::insertFileToDatabase(GDriveObject file, const std::string &parentId) {
        file->m_event.wait();
        db_handle_t db;
        auto w = db.getWork();

        std::string sql;
        sql.reserve(512);
        sql += "INSERT INTO " DATABASEDATA "(id,name,parents,mimeType,md5Checksum,"
                      "size,"
                      "isTrashable,canRename,canDownload,"
                      "trashed,modifiedTime,createdTime, version, inode,uid,gid,mode)"
                      " VALUES ";
        bool wasFirst = true;
        detail::appendSqlRepresentationFromObject(&sql, file.get(), w, &wasFirst, {parentId});
        sql += " ON CONFLICT (id) DO UPDATE SET "
                      "name=EXCLUDED.name, md5Checksum=EXCLUDED.md5Checksum,"
                      "parents=EXCLUDED.parents,size=EXCLUDED.size,"
                      "trashed=EXCLUDED.trashed,modifiedTime=EXCLUDED.modifiedTime,createdTime=EXCLUDED.createdTime";

        try{
            w->exec(sql);
        }catch(std::exception &e){
            LOG(ERROR) << sql;
            LOG(FATAL) << "There was an error with inserting file in to the database: " << e.what();
        }
        w->commit();

        file->m_event.signal();
    }

    void Account::upsertFileToDatabase(GDriveObject file, const std::vector<std::string> &parentIds) {
        if (file) {
            db_handle_t db;
            auto w = db.getWork();
//            auto count = data.count(document{} << "id" << file->getId() << finalize);
            bool updateParents = !parentIds.empty();
            file->m_event.wait();

            std::string sql;
            sql.reserve(512);
            sql += "INSERT INTO " DATABASEDATA "(id,name,";
            if(updateParents){
                sql += "parents,";
            }
            sql +=    "mimeType,md5Checksum,"
                      "size,"
                      "isTrashable,canRename,canDownload,"
                      "trashed,modifiedTime,createdTime, version, inode,uid,gid,mode)"
                      " VALUES ";
            bool wasFirst = true;
            detail::appendSqlRepresentationFromObject(&sql, file.get(), w, &wasFirst, parentIds, updateParents);
            sql += " ON CONFLICT (id) DO UPDATE SET "
                   "name=EXCLUDED.name, md5Checksum=EXCLUDED.md5Checksum,"
                   "trashed=EXCLUDED.trashed,modifiedTime=EXCLUDED.modifiedTime,";
            if(updateParents){
                sql += "parents=EXCLUDED=parents,";
            }

            sql += "createdTime=EXCLUDED.createdTime"
                    ",size=EXCLUDED.size";

            w->exec(sql);
            w->commit();

            file->m_event.signal();
        }

    }

    std::string Account::getUploadUrlForFile(GDriveObject file,
                                             std::string mimeType, int backoff) {
        if (file->getIsTrashed()) {
            return "";
        }
        refresh_token();

        http_client client("https://www.googleapis.com/upload/drive/v3",
                           m_http_config);
        uri_builder builder("files");
        builder.append_query("supportsTeamDrives", "true");
        builder.append_query("uploadType", "resumable");

        //        http_response resp = client.request(methods::POST,
        //        builder.to_string(), json, "application/json").get();

        http_request req;
        auto &headers = req.headers();
        headers.add("X-Upload-Content-Length", std::to_string(file->getFileSize()));
        headers.add("X-Upload-Content-Type", mimeType);

        web::json::value jsonValue;
        const std::vector<std::string> parentIds = FileManager::getParentIds(file->getId());
        assert( !parentIds.empty() );
        for (int i =0; i < parentIds.size(); i++) {
            jsonValue["parents"][i] = web::json::value::string(parentIds[0]);
        }
        jsonValue["id"] = web::json::value::string(file->getId());
        jsonValue["createdTime"] = web::json::value::string(file->getCreatedTimeAsString());
        jsonValue["name"] = web::json::value::string(file->getName());

        VLOG(9) << "Body for getting upload url";
        VLOG(9) << jsonValue.serialize();
        req.set_body(jsonValue.serialize(), "application/json");

        req.set_request_uri(builder.to_uri());
        req.set_method(methods::POST);

        http_response resp;

        std::string location;
        try {
            resp = client.request(req).get();
        } catch (std::exception) {
            LOG(ERROR) << "There was an error with getting the upload url";
            unsigned int sleep_time = std::pow(2, backoff);
            LOG(INFO) << "Sleeping for " << sleep_time << " second before retrying";
            sleep(sleep_time);

            location = getUploadUrlForFile(file, mimeType, backoff + 1);
        }

        if (resp.status_code() == 404) {
            LOG(ERROR) << "Failed to get uploadUrl: " << resp.reason_phrase() << "\n\t"
                       << "for file with id " << file->getId();
            unsigned int sleep_time = std::pow(2, backoff);
            LOG(INFO) << "Sleeping for " << sleep_time << " second before retrying";
            sleep(sleep_time);
            location = getUploadUrlForFile(file, mimeType, backoff + 1);
        } else if (resp.status_code() != 200) {
            unsigned int sleep_time = std::pow(2, backoff);
            if (backoff > 5) {
                LOG(ERROR) << "Failed to get uploadUrl: " << resp.reason_phrase()
                           << "\n\t" << resp.extract_utf8string(true).get();
                LOG(INFO) << "Sleeping for " << sleep_time << " second before retrying";
            }
            sleep(sleep_time);
            if (backoff >= 15) {
                backoff = 14;
            }
            location = getUploadUrlForFile(file, mimeType, backoff + 1);
        } else {
            location = resp.headers()["Location"];
        }

        if (backoff == 0) {

            db_handle_t db;
            auto w = db.getWork();

            _lockObject lock(file.get());
            std::string sql;
            sql.reserve(256);
            sql += "UPDATE " DATABASEDATA " SET uploadURL='";
            sql += w->esc(location);
            sql += "'";
            try {
                w->exec(sql);
            }catch(std::exception &e){
                LOG(ERROR) << e.what();
                LOG(DEBUG) << sql;
            }
            w->commit();
        }

        return location;

    }

    std::string Account::getUploadUrlForFile(http_request req, int backoff) {
        refresh_token();

        http_client client("https://www.googleapis.com/upload/drive/v3/files",
                           m_http_config);
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

    bool Account::upload(std::string uploadUrl, std::string filePath,
                         size_t fileSize, int64_t start, std::string mimeType) {
        refresh_token();
        boost::system::error_code ec;
        if (!fs::exists(filePath, ec) || ec) {
            // the file no longer exists, stop trying to upload
            LOG(ERROR) << filePath << " no longer exists and will stop trying to upload it.";
            return true;
        }

        try {
            concurrency::streams::basic_istream<unsigned char> stream =
                    concurrency::streams::file_stream<unsigned char>::open_istream(
                            //            concurrency::streams::istream stream =
                            //            concurrency::streams::file_stream<unsigned
                            //            char>::open_istream(
                            filePath)
                            .get();

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
            stream.close();
            if (status_code == 200 || status_code == 201) {

//                bsoncxx::document::value doc =
//                        bsoncxx::from_json(resp.extract_utf8string().get());
//                bsoncxx::document::view value = doc.view();
                auto value = resp.extract_json().get();
                try {

                    if (value.has_string_field("id")) {
                        auto const  id = value["id"].as_string();
                        auto const obj = FileManager::fromId(id);
                        if (obj) {
                            obj->setIsUploaded(true);
                        } else {
                            // if we are here, it's probably because the file has been deleted.
                            LOG(FATAL) << "We should not reach here: " << id;
                        }
                    }
                } catch (std::exception &e) {
                    LOG(ERROR) << e.what();
                }

                return true;
            } else if (status_code == 401) {
                refresh_token();
                return false;
            } else if (status_code == 40900) {
                //                LOG(ERROR)
                return false;
            } else {
                LOG(ERROR) << "Failed to upload, a file already exists: "
                           << resp.reason_phrase();
                LOG(ERROR) << resp.extract_utf8string(true).get();
                return false;
            }
        } catch (std::exception &e) {
            LOG(ERROR) << e.what();
            return false;
        }

    }

    std::optional<int64_t> Account::getResumableUploadPoint(std::string url,
                                                            size_t fileSize,
                                                            int backoff) {
        refresh_token();

        http_client client(url, m_http_config);
        http_request request;
        request.set_method(methods::PUT);
        http_headers &headers = request.headers();
        std::stringstream ss;
        ss << "bytes */" << fileSize;
        headers.add("Content-Range", ss.str());
        http_response response;
        try {
            response = client.request(request).get();
        } catch (std::exception &e) {
            LOG(ERROR) << "There was an error resumeable upload link." << e.what();
            int time = pow(2, backoff);
            LOG(ERROR) << "Sleeping for " << time << " seconds.";
            sleep(time);
            return getResumableUploadPoint(url, fileSize, backoff + 1);
        }
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
                return std::optional<int64_t>(
                        (int64_t)std::strtoll(match.str(1).c_str(), nullptr, 10) + 1);
            } else {
                return std::optional<int64_t>(0);
            }
        }
        if (status_code == 400) {
            LOG(DEBUG) << url;
            return std::optional<int64_t>(-400);
        }

        if (status_code >= 400) {
            LOG(ERROR) << "Failed to get start point: " << response.reason_phrase()
                       << " for url " << url;
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

    bool Account::updateObjectProperties(std::string id, std::string json,
                                         std::string addParents,
                                         std::string removeParents, int backoff) {
        http_client client(m_apiEndpoint, m_http_config);
        std::stringstream uri;
        uri << "files/" << id;
        uri_builder builder(uri.str());
        builder.append_query("supportsTeamDrives", "true");
        if (!addParents.empty())
            builder.append_query("addParents", addParents);

        if (!removeParents.empty())
            builder.append_query("removeParents", removeParents);

        http_response response = client
                .request(methods::PATCH, builder.to_string(),
                         json, "application/json")
                .get();
        if (response.status_code() != 200) {
            LOG(ERROR) << "Failed to update object properties: "
                       << response.reason_phrase();
            LOG(ERROR) << response.extract_utf8string(true).get();
            //            LOG(ERROR) << response.headers()[""]
            unsigned int sleep_time = std::pow(2, backoff);
            LOG(INFO) << "Sleeping for " << sleep_time << " seconds before retrying";
            sleep(sleep_time);
            if (backoff <= 5) {
                return updateObjectProperties(id, json, addParents, removeParents,
                                              backoff + 1);
            }

            return false;
        }

        return true;
    }

    void Account::invalidateParentEntries(const DriveFS::GDriveObject &obj) {
        _Object::trash(obj);
        db_handle_t db;
        pqxx::work *work = db.getWork();
        obj->m_event.wait();

        std::string sql;
        sql.reserve(256);
        sql += "SELECT parents from ";
        sql += DATABASEDATA;
        sql += " WHERE inode=";
        sql += obj->getInode();



        invalidateParentEntries(work->exec(sql));

    }

    void Account::invalidateParentEntries(std::string const &id) {
        std::vector<std::string> parents = FileManager::getParentIds(id);
        if(parents.empty()){
            return;
        }
        db_handle_t db;
        pqxx::work *work = db.getWork();
//        std::string sql;
//        sql.reserve(256);
//        sql += "SELECT array_to_string(parents, ',', '') from ";
//        sql += DATABASEDATA;
//        sql += " WHERE id='";
//        sql += id;
//        sql += "'";
//        pqxx::result parents;
//        try {
//            parents = work->exec(sql);
//            if(parents.size() == 0){
//                return;
//            }
//        }catch(std::exception &e){
//            LOG(ERROR) << e.what();
//            LOG(DEBUG) << sql;
//            return;
//        }
//
//        sql.clear();

        std::string sql;
        sql += "SELECT (inode, name) FROM "  DATABASEDATA " WHERE id='{";
        bool wasFirst = true;
        for(auto const & parent: parents){
            if(wasFirst)
                wasFirst = false;
            else
                sql += ",";

            sql+=parent;
        }
        sql += "}'";

        try {
            invalidateParentEntries(work->exec(sql));
            work->commit();
        }catch(pqxx::sql_error &e){
            LOG(ERROR) << e.what();
            LOG(DEBUG) << sql;
            work->commit();
        }
    }

    void Account::invalidateParentEntries(const pqxx::result &result){
        for( auto const & row: result) {
            this->invalidateEntry(row[0].as<ino_t>(), row[1].as<std::string>());
        }

    }

} // namespace DriveFS
