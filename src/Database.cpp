#include "Database.h"
#include <unistd.h>
db_handle_t::DatabasePool db_handle_t::pool{};
db_handle_t::db_handle_t(): c(pool.getConnection()), w(new pqxx::work(*c)){
};

//db_handle_t::db_handle_t(db_handle_t const  & that ){
//
//}

db_handle_t::db_handle_t(db_handle_t &&that):
    c(that.c), w(that.w)
{
    that.c = nullptr;
    that.w = nullptr;
}

db_handle_t::~db_handle_t(){
    pool.releaseConnection(c);
    delete w;
}



void db_handle_t::DatabasePool::setDatabase(const std::string &uri, uint32_t max) {
    m_freeCons.reserve(max);
    for(int i = 0; i < max; i++){
        m_freeCons.push( new pqxx::connection(uri));
    }
}

pqxx::connection * db_handle_t::DatabasePool::getConnection() {
    pqxx::connection *c;
    while(!m_freeCons.pop(c)){
        usleep(50000); //50 ms == 50000 microseconds
    }
    return c;
}

void db_handle_t::DatabasePool::releaseConnection(pqxx::connection *c) {
    m_freeCons.push(c);
}