//
// Created by eric on 29/01/18.
//

#pragma once
#include "BaseFileSystem.h"
#include "Account.h"

namespace DriveFS {
    struct _lockObject{
        _Object* _obj;
        _lockObject(_Object * obj): _obj(obj){
//            LOG(INFO) << "locking " << obj->getId();
            _obj->_mutex.lock();
        }
        ~_lockObject(){
//            LOG(INFO) << "unlocking " << _obj->getId();
            _obj->_mutex.unlock();
        }
    };

    struct fuse_lowlevel_ops getOps();

}
