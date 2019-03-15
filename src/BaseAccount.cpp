//
// Created by eric on 28/01/18.
//

#include "BaseAccount.h"
#include <boost/exception/all.hpp>
#include <easylogging++.h>

using namespace utility;
using namespace web;
using namespace web::http;
using namespace web::http::client;
using namespace web::http::oauth2::experimental;
using namespace web::http::experimental::listener;

// Some of the code is copied from microsoft's cpprest sdk which is licensed
// under MIT

//
// Utility method to open browser on Windows, OS X and Linux systems.
//
static void open_browser(utility::string_t auth_uri) {
#if defined(_WIN32) && !defined(__cplusplus_winrt)
  // NOTE: Windows desktop only.
  auto r =
      ShellExecuteA(NULL, "open", conversions::utf16_to_utf8(auth_uri).c_str(),
                    NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
  // NOTE: OS X only.
  string_t browser_cmd(U("open \"") + auth_uri + U("\""));
  (void)system(browser_cmd.c_str());
#else
  // NOTE: Linux/X11 only.
  std::string browser_cmd(U("xdg-open \"") + auth_uri + U("\""));
  (void)system(browser_cmd.c_str());
#endif
}

oauth2_code_listener::oauth2_code_listener(uri listen_uri,
                                           oauth2_config &config)
    : m_listener(new http_listener(listen_uri)), m_config(config) {
  m_listener->support([this](http::http_request request) -> void {
    if (request.request_uri().path() == U("/") &&
        request.request_uri().query() != U("")) {
      m_resplock.lock();
      try {
        m_config.token_from_redirected_uri(request.request_uri())
            .then([this, request](pplx::task<void> token_task) -> void {
              try {
                token_task.wait();
                m_tce.set(true);
              } catch (const oauth2_exception &e) {
                ucout << "Error: " << e.what() << std::endl;
                m_tce.set(false);
              } catch (const std::exception &e) {
                ucout << "Error: " << e.what() << std::endl;
                m_tce.set(false);
              }
            });
        request.reply(status_codes::OK, U("Ok."));

      } catch (boost::system::system_error &e) {
        std::cout << boost::diagnostic_information(e) << std::endl
                  << e.code() << std::endl;
        std::cout << ERR_error_string(e.code().value(), nullptr) << std::endl;

        request.reply(status_codes::InternalError,
                      boost::diagnostic_information(e));
        m_resplock.unlock();
        return;
      } catch (std::exception &e) {
        std::cout << e.what();
        m_resplock.unlock();
        return;
      };

      m_resplock.unlock();
    } else {
      std::cout << request.request_uri().path() << "\n";
      std::cout << request.request_uri().query() << "\n";
      request.reply(status_codes::NotFound, U("Not found."));
    }
  });

  m_listener->open().wait();
}

oauth2_code_listener::~oauth2_code_listener() { m_listener->close().wait(); }

pplx::task<bool> oauth2_code_listener::listen_for_code() {
  return pplx::create_task(m_tce);
}

void BaseAccount::run() {
  ucout << "Running gdrive session..." << std::endl;

  if (!m_oauth2_config.token().is_valid_access_token()) {
    if (authorization_code_flow().get()) {
      m_http_config.set_oauth2(m_oauth2_config);
    } else {
      ucout << "Authorization failed for gdrive." << std::endl;
    }
  }

  run_internal();
}

pplx::task<bool> BaseAccount::authorization_code_flow() {
  open_browser_auth();
  if(!m_listener){
      m_listener = std::make_unique<oauth2_code_listener>(m_redirect, m_oauth2_config);
  }
  return m_listener->listen_for_code();
}

BaseAccount::BaseAccount(std::string dbUri, std::string api, std::string id,
                         std::string secret, std::string auth,
                         std::string token, std::string redirect,
                         std::string scope)
    : m_dbUri(dbUri), m_apiEndpoint(api),
      m_redirect(redirect),
      m_oauth2_config(id, secret, auth, token, redirect, scope, "test/0.0.1"),
      m_listener(nullptr),
      m_needToInitialize(true), m_event(1), m_key(id),
      m_token_expires_at(std::chrono::system_clock::now()),
      pool(mongocxx::uri(dbUri)), refresh_interval(300),
      inode_count(1){};
//BaseAccount(dbUri, "https://www.googleapis.com/drive/v3/",
//                  "126857315828-tj5cie9scsk0b5edmakl266p7pis80ts.apps."
//                  "googleusercontent.com",
//                  "wxvtZ_SZpmEKXSB0kITXYx6C",
//                  "https://accounts.google.com/o/oauth2/v2/auth",
//                  "https://www.googleapis.com/oauth2/v4/token",
//                  "http://localhost:7878", GDRIVE_OAUTH_SCOPE)
void BaseAccount::open_browser_auth() {
  auto auth_uri(m_oauth2_config.build_authorization_uri(true));
  ucout << "Open the browser and visit the following page:" << std::endl;
  ucout << auth_uri << std::endl;

}

BaseAccount::~BaseAccount() {}

void BaseAccount::refresh_token(int backoff) {
  m_event.wait();
  try {
    if (std::chrono::system_clock::now() >= m_token_expires_at) {
      LOG(INFO) << "Refreshing access tokens";
      m_oauth2_config.token_from_refresh().get();
      auto token = m_oauth2_config.token();
      token.set_refresh_token(m_refresh_token);
      m_oauth2_config.set_token(token);
      m_token_expires_at =
          std::chrono::system_clock::now() +
          std::chrono::seconds(m_oauth2_config.token().expires_in()) -
          std::chrono::minutes(2);
      m_http_config.set_oauth2(m_oauth2_config);
    }
  } catch (std::exception &e) {
    m_event.signal();
    LOG(ERROR) << "Failed to refresh token";
    LOG(ERROR) << e.what();
    unsigned int sleep_time = std::pow(2, backoff);
    LOG(INFO) << "Sleeping for " << sleep_time << " seconds before retrying";
    sleep(sleep_time);
    if (backoff <= 6) {
      refresh_token(backoff + 1);
    }
    return;
  }
  m_event.signal();
}