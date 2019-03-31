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
#include <cpprest/http_client.h>
#include <string_view>
#include <optional>
#include <cpprest/json.h>
#include <pqxx/result>

using namespace web::http::client; // HTTP client features
using namespace web::http::oauth2::experimental;
#define GDRIVE_OAUTH_SCOPE "https://www.googleapis.com/auth/drive"

namespace DriveFS
{
class Account : public BaseAccount
{
public:
  Account(std::string dbUri);

  Account(std::string dbUri, const std::string &access_tokoen,
          const std::string &refresh_token);

  Account(Account &&acount);
  static Account getAccount(const std::string & uri);

  inline void refresh_token(int backoff = 0)
  {
    //            m_id_buffer.clear();
    BaseAccount::refresh_token(backoff);
  };

  virtual GDriveObject createNewChild(GDriveObject const &parent, const char *name,
                                      int mode, bool isFile);
  bool removeChildFromParent(GDriveObject const &child, GDriveObject const &parent);
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
  inline const std::string & getDBUri() const {return  m_dbUri;}
  void removeFileWithIDFromDB(std::string id);


protected:
  void run_internal() override;
  virtual void loadFilesAndFolders() override;
  [[nodiscard]] virtual std::string getFilesAndFolders(std::string nextPageToken = "",
                                  int backoff = 0,
                                  std::string teamDriveId="");

private:
  void parseFilesAndFolders(web::json::value value);
  void getTeamDrives(int backoff = 0);
  void linkParentsAndChildren();
  std::string getUploadUrlForFile(http_request, int backoff = 1);
  void generateIds(int_fast8_t backoff = 0);
  web::json::value createFolderOnGDrive(std::string const &json, int backoff = 0);
  bool trash(GDriveObject const &file, int backoff = 0);
  virtual void background_update(std::string teamDriveId);
  std::string getRootFolderId();
  void setMaximumInodeFromDatabase();
  std::string m_rootFolderId;
  void invalidateParentEntries(const GDriveObject &obj);
  void invalidateParentEntries(std::string const &id);
  void invalidateParentEntries(const pqxx::result &result);
  void invalidateId(std::string const &id);
  boost::circular_buffer<std::string> m_id_buffer;

  std::map<std::string, std::string>
      m_newStartPageToken; // map teamDriveId to newStartPageToken
};

}; // namespace DriveFS

#define DATABASEDATA "gdrivedata"
#define DATABASESETTINGS "drive_settings"
#define DATABASENAME "DriveFS"
#define GDRIVEACCESSTOKENNAME "access token"
#define GDRIVEREFRESHTOKENNAME "refresh_tokens"
#define GDRIVELASTCHANGETOKEN "last change token"
