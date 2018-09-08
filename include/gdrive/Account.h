//
// Created by eric on 27/01/18.
//

#pragma once
#include "BaseAccount.h"
#include "easylogging++.h"
#include "gdrive/Account.h"
#include "gdrive/File.h"
#include <boost/circular_buffer.hpp>
#include <boost/filesystem.hpp>
#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>
#include <bsoncxx/builder/stream/array.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/document/value.hpp>
#include <bsoncxx/document/view.hpp>
#include <bsoncxx/json.hpp>
#include <cpprest/http_client.h>
#include <string_view>

using namespace web::http::client; // HTTP client features
using namespace web::http::oauth2::experimental;
using bsoncxx::builder::concatenate;
using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::stream::close_array;
using bsoncxx::builder::stream::close_document;
using bsoncxx::builder::stream::document;
using bsoncxx::builder::stream::finalize;
using bsoncxx::builder::stream::open_array;
using bsoncxx::builder::stream::open_document;
#define GDRIVE_OAUTH_SCOPE "https://www.googleapis.com/auth/drive"

namespace DriveFS {
class Account : public BaseAccount {
public:
  Account(std::string dbUri);

  Account(std::string dbUri, const std::string &access_tokoen,
          const std::string &refresh_token);

  static Account getAccount(std::string uri);

  inline void refresh_token(int backoff = 0) {
    //            m_id_buffer.clear();
    BaseAccount::refresh_token(backoff);
  };

  virtual GDriveObject createNewChild(GDriveObject parent, const char *name,
                                      int mode, bool isFile);
  bool removeChildFromParent(GDriveObject child, GDriveObject parent);
  virtual void upsertFileToDatabase(GDriveObject file);
  virtual std::string
  getUploadUrlForFile(GDriveObject file,
                      std::string mimeType = "application/octet-stream",
                      int backoff = 0);
  virtual bool upload(std::string uploadUrl, std::string filePath,
                      size_t fileSize, int64_t start = 0,
                      std::string mimeType = "application/octet-stream");
  std::optional<int64_t>
  getResumableUploadPoint(std::string url, size_t fileSize, int backoff = 0);
  virtual bool updateObjectProperties(std::string id, std::string json,
                                      std::string addParents = "",
                                      std::string removeParents = "",
                                      int backoff = 0);
  std::string getNextId();
  void removeFileWithIDFromDB(std::string id);

protected:
  void run_internal() override;
  virtual void loadFilesAndFolders() override;
  virtual void getFilesAndFolders(std::string nextPageToken = "",
                                  std::string teamDriveID = "",
                                  int backoff = 0);

private:
  void parseFilesAndFolders(bsoncxx::document::view value,
                            std::string teamDriveId, bool notify_fs = true);
  void getTeamDrives(int backoff = 0);
  void linkParentsAndChildren();
  std::string getUploadUrlForFile(http_request, int backoff = 1);
  void generateIds(int_fast8_t backoff = 0);
  std::string createFolderOnGDrive(const std::string json, int backoff = 0);
  bool trash(GDriveObject file, int backoff = 0);
  virtual void background_update(std::string teamDriveId = "",
                                 bool skip_sleep = false);
  bsoncxx::document::value getRootFolder();

  boost::circular_buffer<std::string> m_id_buffer;

  std::map<std::string, std::string>
      m_newStartPageToken; // map teamDriveId to newStartPageToken
};
}; // namespace DriveFS

constexpr std::string_view DATABASEDATA = "GDriveData";
constexpr std::string_view DATABASESETTINGS = "settings";
constexpr std::string_view DATABASENAME = "DriveFS";
constexpr std::string_view GDRIVETOKENNAME = "gdrive_tokens";
constexpr std::string_view GDRIVELASTCHANGETOKEN = "gdrive last change token";
