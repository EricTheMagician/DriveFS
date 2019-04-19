#include "Database.h"
#include <unistd.h>
db_handle_t::DatabasePool db_handle_t::pool{};
std::string db_handle_t::DatabasePool::s_uri;
db_handle_t::db_handle_t(): c(pool.getConnection()), w(new pqxx::work(*c)){
};

//db_handle_t::db_handle_t(db_handle_t const  & that ){
//
//}
pqxx::work* db_handle_t::getWork(){
//    if(w != nullptr){
//        return dynamic_cast<pqxx::work*>(w);
//    }

//    w = new pqxx::work(*c);

//    return dynamic_cast<pqxx::work*>(w);
    return w;
}


/*
pqxx::nontransaction* db_handle_t::getTransaction(){
    if(w != nullptr){
        return dynamic_cast<pqxx::nontransaction*>(w);
    }

    w = new pqxx::nontransaction(*c);

    return dynamic_cast<pqxx::nontransaction*>(w);

}
*/

db_handle_t::db_handle_t(db_handle_t &&that):
    c(that.c), w(that.w)
{
    that.c = nullptr;
    that.w = nullptr;
}

db_handle_t::~db_handle_t(){
    pool.releaseConnection(c);
    if(w!= nullptr){
        delete w;
    }
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
