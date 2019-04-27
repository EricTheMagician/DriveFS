#include "Database.h"
#include <unistd.h>
db_handle_t::DatabasePool db_handle_t::pool{};
std::string db_handle_t::DatabasePool::s_uri;
db_handle_t::db_handle_t(): c(pool.getConnection()), nt(nullptr), w(nullptr){
};

//db_handle_t::db_handle_t(db_handle_t const  & that ){
//
//}
pqxx::work* db_handle_t::getWork(){
    if(w != nullptr)
        return w;

    w = new pqxx::work(*c);
    return w;
}


pqxx::nontransaction* db_handle_t::getTransaction(){
    if(nt != nullptr){
        return nt;
    }

    nt = new pqxx::nontransaction(*c);

    return nt;

}

db_handle_t::db_handle_t(db_handle_t &&that):
    c(that.c), w(that.w), nt(that.nt)
{
    that.c = nullptr;
    that.w = nullptr;
    that.nt = nullptr;
}

db_handle_t::~db_handle_t(){
    if(w!= nullptr){
        delete w;
    }
    if(nt!= nullptr){
        delete nt;
    }
    pool.releaseConnection(c);
}



void db_handle_t::DatabasePool::setDatabase(const std::string &uri, uint32_t max) {
//    m_freeCons.reserve(max);
//    for(int i = 0; i < max; i++){
//        m_freeCons.push( new pqxx::connection(uri));
//    }
    db_handle_t::DatabasePool::s_uri = uri;
}

pqxx::connection * db_handle_t::DatabasePool::getConnection() {
//    pqxx::connection *c;
//    while(!m_freeCons.pop(c)){
//        usleep(50000); //50 ms == 50000 microseconds
//    }
    return  new pqxx::connection{s_uri};
}

void db_handle_t::DatabasePool::releaseConnection(pqxx::connection *c) {
    delete c;
//    m_freeCons.push(new pqxx::connection{s_uri});
}
