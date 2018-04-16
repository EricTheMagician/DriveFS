//
// Created by eric on 27/01/18.
//

#include "../include/BaseFile.h"
uid_t File::executing_uid;
gid_t File::executing_gid;

void File::create_heap_handles(size_t write_buffer_size){
    auto count = getFileSize() / write_buffer_size + 1;
    if(this->m_buffers == nullptr){
        this->m_buffers = new std::vector<WeakBuffer>(count);

    }else {
        if(count * write_buffer_size < getFileSize()){
            this->m_buffers->resize(count);
        }
    }

    if(this->heap_handles == nullptr){
        this->heap_handles = new std::vector<heap_handle>(count);
    }else {
        if(count * write_buffer_size < getFileSize()){
            this->heap_handles->resize(count);
        }
    }
}
