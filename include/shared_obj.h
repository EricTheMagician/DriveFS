#pragma once

#include <cstdint>
#include <atomic>
    #include <boost/intrusive_ptr.hpp>

class shared_obj{
public:
    shared_obj(): referenceCount_(1){
    }
    void deleteObject(){
        intrusive_ptr_release(this);
    }
private:
    std::atomic_uint_fast8_t referenceCount_;
    friend void intrusive_ptr_add_ref(shared_obj * obj){
      obj->referenceCount_.fetch_add(1, std::memory_order_relaxed);
    }
    friend void intrusive_ptr_release(shared_obj * obj)
    {
      if (obj->referenceCount_.fetch_sub(1, std::memory_order_release) == 1) {
        std::atomic_thread_fence(std::memory_order_acquire);
        delete obj;
      }
    }
};
