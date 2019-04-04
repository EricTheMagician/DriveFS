//
// Created by eric on 3/17/19.
//

#pragma once
#include <string>
#include <pqxx/pqxx>
#include <pqxx/result>
#include <pqxx/connection.hxx>
#include <autoresetevent.h>
#include <thread>
#include <vector>
#include <boost/lockfree/queue.hpp>
#include <boost/lockfree/stack.hpp>
#include <sstream>

class db_handle_t {
public:
    db_handle_t();
    db_handle_t(db_handle_t const  & that ) = delete;
    db_handle_t(db_handle_t &&that);
    ~db_handle_t();

    inline pqxx::work* getWork(){return w;}
    static void setDatabase(const std::string &uri, uint32_t max=std::thread::hardware_concurrency()){
        pool.setDatabase(uri, max);
    };
private:
    inline pqxx::connection* getConnection(){return c;};
    pqxx::connection *c;
    pqxx::work *w;
private:
    class DatabasePool {
    public:
        DatabasePool():m_freeCons(std::thread::hardware_concurrency()){};

        void setDatabase(const std::string &uri, uint32_t max);


        pqxx::connection* getConnection();
        void releaseConnection(pqxx::connection *c);
    private:


        static std::vector<db_handle_t> pool_;  // Keep a static vector of pools,

        pqxx::connection *createCon();
        void              releaseCon(pqxx::connection *c);
        uint32_t          initializeCons();
        pqxx::connection *getCon();

        boost::lockfree::stack<pqxx::connection*> m_freeCons;

    };
    static DatabasePool pool;
};

