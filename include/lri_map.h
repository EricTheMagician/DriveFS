#ifndef LRI_MAP_H
#define LRI_MAP_H

#include <cstdint>
#include <unordered_map>
#include <optional>
#include "autoresetevent.h"
#include "lockable.h"
#include <boost/circular_buffer.hpp>

template <typename keyType, typename valueType>
class LRIMap: Lockable
{
    /*
     * class for storing key/value data in an unordered map with maximum size.
     * It probably isn't copy safe, since the return value can be dropped from the database
    */
public:
    LRIMap(size_t nElements): insertionList(nElements){}

    valueType* get(keyType const & key) noexcept
    {
        auto lock = this->getScopeLock();
        auto iter = data.find(key);
        if(iter == data.end())
            return nullptr;
        return &iter->second;

    }
    void set(keyType const &key, valueType const & value){
        auto lock = this->getScopeLock();
        if( insertionList.full()){
            const keyType& key = insertionList.front();
            data.erase(data.find(key));
            insertionList.pop_front();
        }
        insertionList.push_back(key);
        data.emplace(key, value);
    }
    
    void removeKey( keyType const & key){
        auto lock = this->getScopeLock();
        data.erase( data->find(key));
    }

private:
    LRIMap() = default;
    std::unordered_map<keyType, valueType> data;
    boost::circular_buffer<keyType> insertionList;
};

#endif // LRI_MAP_H
