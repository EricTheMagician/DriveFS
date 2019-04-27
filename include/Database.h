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

    pqxx::work* getWork();
    pqxx::nontransaction* getTransaction();
    static void setDatabase(const std::string &uri, uint32_t max=std::thread::hardware_concurrency()){
        pool.setDatabase(uri, max);
    };
    static void cleanUpOnExit(){
        DatabasePool::cleanUpOnExit();
    }
private:
    inline pqxx::connection* getConnection(){return c;};
    pqxx::connection *c;
//    pqxx::transaction_base *w;
    pqxx::work *w;
    pqxx::nontransaction *nt;
private:
    class DatabasePool {
    public:
        DatabasePool():m_freeCons(std::thread::hardware_concurrency()){};

        void setDatabase(const std::string &uri, uint32_t max);


        pqxx::connection* getConnection();
        void releaseConnection(pqxx::connection *c);
        static void cleanUpOnExit(){
            pqxx::connection* c;
            while( pool.m_freeCons.pop(c) ){
                delete c;
            };
        }

    private:
        static std::string s_uri;
        pqxx::connection *createCon();
        void              releaseCon(pqxx::connection *c);
        uint32_t          initializeCons();
        pqxx::connection *getCon();

        boost::lockfree::stack<pqxx::connection*> m_freeCons;

    };
    static DatabasePool pool;
};

