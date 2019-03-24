//
// Created by eric on 27/01/18.
//

#include "BaseFile.h"
uid_t File::executing_uid;
gid_t File::executing_gid;
/*
void File::create_heap_handles(size_t write_buffer_size){
    auto count = getFileSize() / write_buffer_size + 1;
    m_handle_creation_event.wait();
    if(this->m_buffers == nullptr){
        this->m_buffers = new std::vector<WeakBuffer>(count);
    }else {
        if(count > this->m_buffers->size()){
            this->m_buffers->resize(count);
        }
    }

    if(this->heap_handles == nullptr){
        this->heap_handles = new std::vector<heap_handle>(count);
    }else {
        if(count > this->heap_handles->size()){
            this->heap_handles->resize(count);
        }
    }
    m_handle_creation_event.signal();
}
*/