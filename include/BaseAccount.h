//
// Created by eric on 28/01/18.
//

#pragma once

#include <autoresetevent.h>
#include <cpprest/http_client.h>
#include <cpprest/http_listener.h>
#include <mongocxx/client.hpp>
#include <mongocxx/pool.hpp>

using namespace utility;
using namespace web;
using namespace web::http;
using namespace web::http::client;
using namespace web::http::oauth2::experimental;
using namespace web::http::experimental::listener;

class oauth2_code_listener {
public:
  oauth2_code_listener(uri listen_uri, oauth2_config &config);
  ~oauth2_code_listener();
  pplx::task<bool> listen_for_code();

private:
  std::unique_ptr<http_listener> m_listener;
  pplx::task_completion_event<bool> m_tce;
  oauth2_config &m_config;
  std::mutex m_resplock;
};

class BaseAccount {

public:
  BaseAccount(std::string dbUri, std::string api, std::string id,
              std::string secret, std::string auth, std::string token,
              std::string redirect, std::string scope);
  virtual ~BaseAccount();
  void run();
  inline bool needToInitialize() const { return m_needToInitialize; }
  inline http_client getClient() {
    refresh_token();
    return http_client(m_apiEndpoint, m_http_config);
  }

  inline struct fuse_session *getFuseSession() const { return m_fuse_session; }
  inline void setFuseSession(struct fuse_session *session) {
    m_fuse_session = session;
  }

  struct fuse_session *fuse_session;
  inline ino_t getNextInode() {
    return inode_count.fetch_add(1, std::memory_order_acquire) + 1;
  }

  inline void setRefreshInterval(int interval_in_seconds) {
    this->refresh_interval = interval_in_seconds;
  }

#if FUSE_USE_VERSION < 30
  struct fuse_chan *fuse_channel;
#endif

private:
  void open_browser_auth();

protected:
  void refresh_token(int backoff = 0);
  virtual void run_internal() = 0;
  virtual void loadFilesAndFolders() = 0;
  pplx::task<bool> authorization_code_flow();

  oauth2_config m_oauth2_config;
  http_client_config m_http_config;
  std::unique_ptr<oauth2_code_listener> m_listener;
  std::string m_apiEndpoint;
  AutoResetEvent m_event;
  std::string m_key;
  std::string m_refresh_token;
  std::chrono::system_clock::time_point m_token_expires_at;
  std::atomic<ino_t> inode_count = 1;
  int refresh_interval = 300;
  struct fuse_session *m_fuse_session;
  bool m_needToInitialize;

public:
  mongocxx::pool pool;
};
