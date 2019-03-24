#include "gdrive/FileManager.h"
#include "Database.h"
#include "gdrive/Account.h"
#include <pqxx/pqxx>
#include <cpprest/json.h>



namespace DriveFS::FileManager{
    boost::compute::detail::lru_cache<ino_t, GDriveObject> inodeToObject(1024);
    boost::compute::detail::lru_cache<std::string, GDriveObject> idToObject(1024);

    PriorityCache<GDriveObject> DownloadCache(1,1);
    namespace detail {
        ::AutoResetEvent insertEvent(1);
        static std::vector<std::string> getParentsFromDB(std::string const &id){
            db_handle_t db;
            auto w = db.getWork();
            std::string sql;
            sql.reserve(512);
            sql += "SELECT "
                   "parents "
                   " FROM " + DATABASEDATA +
                   " WHERE trashed=false AND id='" +id+ "'";
            pqxx::result sql_results = w->exec(sql);

            std::vector<std::string> results;
            if(sql_results.size() > 0) {
                pqxx::array_parser array = sql_results[0][0].as_array();
                std::pair<pqxx::array_parser::juncture, std::string> pair = array.get_next();
                while (pair.first != pqxx::array_parser::row_end) {
                    pair = array.get_next();
                    results.push_back(pair.second);
                }
            }

            return results;

        }
    };


        std::vector<GDriveObject> getChildren(std::string const &id){
        db_handle_t db;
        auto w = db.getWork();
        std::string sql;
        sql.reserve(512);
        sql += "SELECT "
               "inode "
               " FROM " + DATABASEDATA +
               " WHERE trashed=false AND '"+id+ "'=ALL(parents) AND name not like '%/%'";
        pqxx::result result = w->exec(sql);
        std::vector<GDriveObject> results;
        results.reserve(result.size());
        for(const pqxx::row &row: result){
            results.push_back(fromInode(row[0].as<ino_t>()));

        }
        return results;
    }

    std::vector<GDriveObject> getParents(std::string const &id){

        auto parentIds = detail::getParentsFromDB(id);
        std::vector<GDriveObject> results;
        for(auto const & parentId: parentIds)
            results.push_back(fromId(parentId));


        return results;
    }

    std::vector<std::string> getParentIds(std::string const &id){
        return detail::getParentsFromDB(id);
    }

    GDriveObject fromParentIdAndName(const std::string &id, char const* name){
        db_handle_t db;
        auto w = db.getWork();
        std::string sql;
        sql.reserve(512);

        sql += "SELECT inode FROM " + DATABASEDATA + " WHERE name=\'" + w->esc(name) + "\'";
        sql += " and trashed=false and \'" + id + "\'=all(parents) LIMIT 1";

        try {
            pqxx::row result = w->exec1(sql);
            ino_t inode = result[0].as<ino_t>();
            return fromInode(inode);
        }catch(pqxx::sql_error &e){
            LOG(ERROR) << "unable to find inode for parent_id " << id
                       << " and child name " << name;
            return nullptr;
        }catch(pqxx::unexpected_rows &e){
            LOG(ERROR) << "found to many possibilities for id " << id
                       << " and child name " << name;
            LOG(ERROR) << sql;
            return nullptr;
        }


    }
    bool hasId(std::string const & id, bool removeFromCache){
        auto optional_object = idToObject.get(id);
        if(optional_object && *optional_object){
            if(removeFromCache){
                idToObject.insert(id, nullptr);
                inodeToObject.insert((*optional_object)->getInode(), nullptr);
            }
            return true;
        }

        db_handle_t db;
        auto w = db.getWork();

        std::string sql;
        sql.reserve(512);
        sql += "SELECT 1 FROM ";
        sql += DATABASEDATA;
        sql += " WHERE id='";
        sql += w->esc(id);
        sql += "'";

        pqxx::result result = w->exec(sql);
        return result.size() > 0;

    }


    GDriveObject fromId(std::string const &id) {
        auto optional_object = idToObject.get(id);
        if(optional_object && *optional_object){
            return *optional_object;
        }
        db_handle_t db;
        auto w = db.getWork();
        std::string sql;
        sql.reserve(512);
        sql += "SELECT "
               "name,"            // 0
               "size,"             // 1
               "mimeType,"         // 2
               "canDownload,"      // 3
               "modifiedTime,"     // 4
               "createdTime,"      // 5
               "inode,"               // 6
               "md5Checksum,"      // 7
               "uid,"      // 8
               "gid,"      // 9
               "mode,"      //10
               "trashed,"   //11
               " FROM " + DATABASEDATA +
               " WHERE trashed=false AND id='";
        sql += id + "'";
        pqxx::row result {};

        try{
            result = w->exec1(sql);
        }catch(pqxx::sql_error &e){
            LOG(ERROR) << e.what();
            LOG(ERROR) << "unable to get object for id " << id;
            LOG(ERROR) << sql;
            return nullptr;
        }

/*
 *         _Object(ino_t &&inode, std::string &&id, std::string &&name,
                std::string &&modifiedDate, std::string &&createdDate,
                std::string &&mimeType, size_t &&size, std::string &&md5);

 */
        GDriveObject so = std::make_shared<_Object>(result[6].as<ino_t>(),                                  // inode
                                                    id,                                          // id
                                                    result[0].as<std::string>(),                            // name
                                                    result[4].as<std::string>(),                            // modifiedDate
                                                    result[5].as<std::string>(),                            // createdDate
                                                    result[2].as<std::string>(),                            // mimeType
                                                    result[1].is_null() ? 0  : result[1].as<size_t>(),      // size
                                                    result[7].is_null() ? "" : result[7].as<std::string>(), // md5
                                                    result[8].as<int>(),   // uid
                                                    result[9].as<int>(),   // gid
                                                    result[10].as<int>(),   // mode
                                                    ( result[11].is_null() ? false: result[11].as<bool>()) // trashed

        );

        inodeToObject.insert(so->getInode(), so);
        idToObject.insert(so->getId(), so);

        return so;

    }
    GDriveObject fromInode(ino_t inode){
        auto optional_object = inodeToObject.get(inode);
        if(optional_object && *optional_object){
            return *optional_object;
        }

        db_handle_t db;
        auto w = db.getWork();
        std::string sql;
        sql.reserve(512);
        sql += "SELECT "
               "name,"            // 0
               "size,"             // 1
               "mimeType,"         // 2
               "canDownload,"      // 3
               "modifiedTime,"     // 4
               "createdTime,"      // 5
               "id,"               // 6
               "md5Checksum,"      // 7
               "uid,"      // 8
               "gid,"      // 9
               "mode,"      // 10
               "trashed"  // 11
               " FROM " + DATABASEDATA +
               " WHERE trashed=false AND inode=";
        sql += std::to_string(inode);
        pqxx::row result {};
        try{
            result = w->exec1(sql);
        }catch(pqxx::sql_error &e){
            LOG(ERROR) << e.what();
            LOG(ERROR) << "unable to get object for inode " << inode;
            LOG(ERROR) << sql;
            return nullptr;
        }

/*
 *         _Object(ino_t &&inode, std::string &&id, std::string &&name,
                std::string &&modifiedDate, std::string &&createdDate,
                std::string &&mimeType, size_t &&size, std::string &&md5);

 */
        GDriveObject so = std::make_shared<_Object>(inode,                                       // inode
                                                    result[6].as<std::string>(),                            // id
                                                    result[0].as<std::string>(),                            // name
                                                    result[4].as<std::string>(),                            // modifiedDate
                                                    result[5].as<std::string>(),                            // createdDate
                                                    result[2].as<std::string>(),                            // mimeType
                                                    result[1].is_null() ? 0  : result[1].as<size_t>(),      // size
                                                    result[7].is_null() ? "" : result[7].as<std::string>(), // md5
                                                    result[8].as<int>(),   // uid
                                                    result[9].as<int>(),   // gid
                                                    result[10].as<int>(),   // mode
                                                    ( result[11].is_null() ? false: result[11].as<bool>()) // trahsed
        );

        inodeToObject.insert(inode, so);
        idToObject.insert(so->getId(), so);

        return so;
    }

    void insertObjectToMemoryMap(const GDriveObject &object){
        detail::insertEvent.wait();
        auto file = object.get();
        try{
            inodeToObject.insert(file->attribute.st_ino, object);
            idToObject.insert(file->getId(), object);
        }catch(std::exception &e){
            LOG(ERROR) << "There was an error with inserting an object to the memory database: " << e.what()
                       << "\nExiting because it is probably in a broken state if we were to continue";
        }
        detail::insertEvent.signal();

    }

    std::string asJSONForRename(std::string const &id){
        return asJSONForRename(fromId(id));
    }

    std::string asJSONForRename(GDriveObject const &obj){
        using namespace web;
        json::value doc;
        doc["name"] = json::value::string(obj->getName()) ;
        struct stat &attribute = obj->attribute;
        doc["modifiedTime" ] = json::value::string(getRFC3339StringFromTime(attribute.st_mtim));
        doc["appProperties"][APP_MODE] = attribute.st_mode;
        doc["appProperties"][APP_UID ] = attribute.st_uid;
        doc["appProperties"][APP_GID ] = attribute.st_gid;
        return doc.serialize();

    }


}