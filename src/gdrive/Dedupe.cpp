//
// Created by eric on 3/26/19.
//

#include "Dedupe.h"
#include <gdrive/FileManager.h>
#include "Database.h"
#include "gdrive/Account.h"
#include <map>
#include <vector>
#include <iostream>

namespace DriveFS{
    namespace detail{
        struct file{
            std::string id, name, createdTime, modifiedTime;
            size_t size;
            bool isFolder;
        };
        std::ostream & operator << (std::ostream &out, const file &f)
        {
            out << f.id
                << "," << f.name
                << "," << f.createdTime
                << "," << f.modifiedTime
                << "," << f.size
                << "," << std::boolalpha << f.isFolder
                << "\n";
            return out;
        }

    }
    void getParents() {

        db_handle_t db;
        auto w = db.getWork();

        // get folders
        std::string sql;
        sql.reserve(512);
        sql = "SELECT id, name, array_to_string(parents, ',') as parents from " DATABASEDATA " WHERE mimeType=\'" GOOGLE_FOLDER "\'";
        pqxx::result parents = w->exec(sql);
        pqxx::result duplicates = w->exec(sql);
        for(auto const &row: parents) {
            std::cout << row[0].as<std::string>()
                    << "," << row[1].as<std::string>()
                    <<"," << (row[2].is_null() ? "root" : row[2].as<std::string>()) << "\n";
        }
    }
    void getDupicates(){

        db_handle_t db;
        auto w = db.getWork();

        // get folders
        std::string sql;
        sql.reserve(512);
        sql = "SELECT id, name from " DATABASEDATA " WHERE mimeType=\'" GOOGLE_FOLDER "\'";
        {
            pqxx::result parents = w->exec(sql);
            std::cout << "folderId,fileId,createdTime,modifiedTime,size,md5,folderName,fileName,delete(1) or keep(0),isFolder,moveContentsTo\n";
            for(auto const &row: parents){
                sql.clear();
                std::string const parentId = row[0].as<std::string>();
                std::string const parentName = row[1].as<std::string>();

                sql +=   "with duplicates as (select name, count(name)  from " DATABASEDATA " where trashed=false AND'";
                sql +=   parentId;
                sql +=  "'=ALL(parents) "
                        "group by name having count(name) > 1) "
                        "SELECT name,id,createdTime,modifiedTime,size, mimeType='" GOOGLE_FOLDER
                        "' as isFolder,md5Checksum from " DATABASEDATA " where trashed=false AND    '";
                sql += parentId;
                sql += "'=all(parents) and name in (select name from duplicates) order by name,modifiedTime";
                pqxx::result duplicates = w->exec(sql);

                uint8_t toDelete = 0;
                std::string previousName("");
                std::string previousFolder("");

                std::string moveContentsTo("");
                for(auto const &child: duplicates){
                    std::string childName = child[0].as<std::string>();
                    std::string childId = child[1].as<std::string>();
                    bool isFolder = child[5].as<bool>();
                    if(childName==previousName){
                        toDelete = 1;
                    }else{
                        previousName = childName;
                        toDelete=0;
                        moveContentsTo=childId;
                    }
                    std::cout
                        << parentId
                        << "," << childId
                        << "," << child[2].as<std::string>()
                        << "," << child[3].as<std::string>()
                        << "," << child[4].as<size_t>()
                        << "," << child[6].as<std::string>()
                        << "," << parentName
                        << "," << childName

                        << "," << std::to_string(toDelete)
                        << "," << std::boolalpha << isFolder
                        << ",";
                    if(toDelete && isFolder){
                        std::cout << moveContentsTo;
                    }
                    std::cout << "\n";

                }
//                if(!duplicates.empty()){
//                    std::cout << ",,,,,,,,,\n";
//                }

            }
        }

    }
}
