//
// Created by eric on 29/01/18.
//

#include "gdrive/FileIO.h"
#include "gdrive/Filesystem.h"
#include "gdrive/Account.h"
#include "gdrive/File.h"
#include "FolderIO.h"
#include <easylogging++.h>
#include <algorithm>
#include <linux/fs.h>
#include "gdrive/FileManager.h"


namespace DriveFS{

    inline Account* getAccount(const fuse_req_t &req){
        return static_cast<Account *>(fuse_req_userdata(req));
    }

    void lookup(fuse_req_t req, fuse_ino_t parent_ino, const char *name){
        GDriveObject parent = FileManager::fromInode(parent_ino);
        if(!parent){
            int reply_err = fuse_reply_err(req, ENOENT);
            while(reply_err != 0){
                reply_err = fuse_reply_err(req, ENOENT);
            }
            return;
        }
        GDriveObject child =FileManager::fromParentIdAndName(parent->getId(), name);
        if(child){
            struct fuse_entry_param e;
            memset(&e, 0, sizeof(e));
            e.attr = child->attribute;
            e.ino = e.attr.st_ino;
            e.attr_timeout = 3600.0;
            e.entry_timeout = 3600.0;
            e.generation = 1;
            int reply_err = fuse_reply_entry(req, &e);
            while(reply_err != 0){
                reply_err = fuse_reply_entry(req, &e);
            }
            return;
        }
        int reply_err = fuse_reply_err(req, ENOENT);
        while(reply_err != 0){
            reply_err = fuse_reply_err(req, ENOENT);
        }
    }

    void forget(fuse_req_t req, fuse_ino_t /*ino*/, uint64_t /*nlookup*/){
//        auto object = FileManager::fromInode(ino);
//        if (object) {
//            object->forget(nlookup);
//        }

        fuse_reply_none(req);
    }

    void getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi){
       GDriveObject object(FileManager::fromInode(ino));
        if (object) {
            int reply_err = fuse_reply_attr(req, &(object->attribute), 30.0);
            while(reply_err != 0){
                reply_err = fuse_reply_attr(req, &(object->attribute), 30.0);
            }
            return;
        }
        int reply_err = fuse_reply_err(req, ENOENT);
        while(reply_err != 0){
            reply_err = fuse_reply_err(req, ENOENT);
        }
    }

    void setattr(fuse_req_t req, fuse_ino_t ino, struct stat *attr, int to_set, struct fuse_file_info *fi){
        auto file = FileManager::fromInode(ino);
        if(!file) {
            int reply_err = fuse_reply_err(req, ENOENT);
            while (reply_err != 0) {
                reply_err = fuse_reply_err(req, ENOENT);
            }
            return;
        }

        if(to_set & FUSE_SET_ATTR_SIZE){
            LOG(ERROR) << "Attempted to set size to file with name " << file->getName() << " and id " << file->getId();
            LOG(ERROR) << "Settingg the file size is currently not supported";
            int reply_err = fuse_reply_err(req, EIO);
            while(reply_err != 0){
                reply_err = fuse_reply_err(req, EIO);
            }

            return;
        }
        {
            auto _lock{file->getScopeLock()};
            if(to_set & FUSE_SET_ATTR_MODE){
                file->attribute.st_mode = attr->st_mode;
            }

            if(to_set & FUSE_SET_ATTR_ATIME){
                file->attribute.st_atim = attr->st_atim;
            }

            if(to_set & FUSE_SET_ATTR_MTIME){
                file->attribute.st_mtim = attr->st_mtim;
            }
            #if FUSE_VERSION >= 30
            if(to_set & FUSE_SET_ATTR_CTIME){
                file->attribute.st_ctim = attr->st_ctim;
            }
            #endif
            if(to_set & FUSE_SET_ATTR_MTIME_NOW){
                file->attribute.st_mtim = {time(nullptr),0};
            }

            if(to_set & FUSE_SET_ATTR_ATIME_NOW){
                file->attribute.st_atim = {time(nullptr),0};
            }
        }

        if(file->getIsUploaded()){
            auto account = getAccount(req);
            const bool status = account->updateObjectProperties(file->getId(), FileManager::asJSONForRename(file));
            if(!status){
                int reply_err = fuse_reply_err(req, EIO);
                while(reply_err != 0){
                    reply_err = fuse_reply_err(req, EIO);
                }
                return;
            }
        }


        int reply_err = fuse_reply_attr(req, &(file->attribute), 18000.0);
        while(reply_err != 0){
            reply_err = fuse_reply_attr(req, &(file->attribute), 18000.0);
        }

    }

    void readlink(fuse_req_t req, fuse_ino_t ino);

    void mknod(fuse_req_t req, fuse_ino_t parent, const char *name, mode_t mode, dev_t rdev);

    void mkdir(fuse_req_t req, fuse_ino_t parent_ino, const char *name, mode_t mode){
        auto parent = FileManager::fromInode(parent_ino);
        ::ScopeLock lock {parent->getScopeLock()};
        GDriveObject child = FileManager::fromParentIdAndName(parent->getId(), name, false);
        if (child) {
            LOG(INFO) << "Mkdir with name " << name << " already existed";
            int reply_err = fuse_reply_err(req, EEXIST);
            while(reply_err != 0){
                reply_err = fuse_reply_err(req, EEXIST);
            }
            return;
        }

        auto account = getAccount(req);
        auto folder = account->createNewChild(parent, name, mode, false);
        if(!folder){
            fuse_reply_err(req, EIO);
            return;
        }
        struct fuse_entry_param e;
        memset(&e, 0, sizeof(e));
        e.attr = folder->attribute;
        e.ino = e.attr.st_ino;
        e.attr_timeout = 3600.0;
        e.entry_timeout = 3600.0;
        folder->lookupCount.fetch_add(1, std::memory_order_relaxed);
        LOG(INFO) << "Mkdir with name " << name;
        int reply_err = fuse_reply_entry(req, &e);
        while(reply_err != 0){
            reply_err = fuse_reply_entry(req, &e);
        }
    }

    void unlink(fuse_req_t req, fuse_ino_t parent_ino, const char *name) {
        auto *account = getAccount(req);
        GDriveObject parent(FileManager::fromInode(parent_ino));
        parent->lock();
        bool signaled = false;
        GDriveObject child = FileManager::fromParentIdAndName(parent->getId(), name);

        if(child){
            parent->unlock();
            signaled = true;

            LOG(TRACE) << "Deleting file/folder with name " << name << " and parentId: "  << parent->getId();

            if (child->getIsUploaded()) {
                bool status =account->removeChildFromParent(child, parent);
                if(!status){
                    fuse_reply_err(req, EIO);
                    return;
                }
            }else{
                auto parents = FileManager::getParentIds(child->getId());
                if(parents.empty() || parents.size() == 1) {
                    FileManager::removeFileWithIDFromDB(child->getId());
                }else{
                    parents.erase( std::remove_if(parents.begin(), parents.end(), [pid = parent->getId()](std::string const &in){ return in == pid;}),
                                parents.end()
                               );
                    account->upsertFileToDatabase(child, parents);
                }
            }
            child->trash();

//#if FUSE_USE_VERSION >= 30
//                fuse_lowlevel_notify_inval_inode(account->fuse_session, parent_ino, 0, 0);
//#else
//                fuse_lowlevel_notify_inval_inode(account->fuse_channel, parent_ino, 0, 0);
//#endif


        }

        if (!signaled) {
            int reply_err = fuse_reply_err(req, ENOENT);
            while(reply_err != 0){
                reply_err = fuse_reply_err(req, ENOENT);
            }
        } else {
            int reply_err = fuse_reply_err(req, 0);
            while(reply_err != 0){
                reply_err = fuse_reply_err(req, 0);
            }
        }

    }

    void rmdir(fuse_req_t req, fuse_ino_t parent_ino, const char *name){
        auto parent = FileManager::fromInode(parent_ino);
        if(!parent->getIsFolder()){
            fuse_reply_err(req, ENOENT);
            return;
        }
        GDriveObject child = FileManager::fromParentIdAndName(parent->getId(), name);
        if (child) {
            auto lock { child->getScopeLock() };
            if (!(FileManager::getChildren(child->getId()).empty())) {
                int reply_err = fuse_reply_err(req, ENOTEMPTY);
                while(reply_err != 0){
                    reply_err = fuse_reply_err(req, ENOTEMPTY);
                }
                return;
            }

            auto account = getAccount(req);
            if (account->removeChildFromParent(child, parent)) {
                int reply_err = fuse_reply_err(req, 0);
                while(reply_err != 0){
                    reply_err = fuse_reply_err(req, 0);
                }
            } else {
                int reply_err = fuse_reply_err(req, EIO);
                while(reply_err != 0){
                    reply_err = fuse_reply_err(req, EIO);
                }
            }



        } else {
            int reply_err = fuse_reply_err(req, ENOENT);
            while(reply_err != 0){
                reply_err = fuse_reply_err(req, ENOENT);
            }
        }
    }

    void symlink(fuse_req_t req, const char *link, fuse_ino_t parent, const char *name);

    void rename(fuse_req_t req, fuse_ino_t parent_ino, const char *name, fuse_ino_t newparent_ino, const char *newname
#ifdef USE_FUSE3
            , unsigned int flags
#endif
    ){
        auto parent = FileManager::fromInode(parent_ino);
        auto child = FileManager::fromParentIdAndName(parent->getId(), name);

        bool newParents = parent_ino != newparent_ino;
        GDriveObject newParent;
#if defined(USE_FUSE3) && defined(RENAME_EXCHANGE)
        if(flags & RENAME_EXCHANGE){
            LOG(ERROR) << "Renaming: cannot atomically rename files";
            int reply_err = fuse_reply_err(req, EINVAL);
            while(reply_err != 0){
                reply_err = fuse_reply_err(req, EINVAL);
            }
            return;
        }
#endif
        if(newParents){
            newParent = FileManager::fromInode(newparent_ino);
            auto oldChild = FileManager::fromParentIdAndName(newParent->getId(), name);
            FileManager::resetChildrenBuffer(parent->getId());
            FileManager::resetChildrenBuffer(newParent->getId());
            if(oldChild){
#if defined(USE_FUSE3) && defined(RENAME_NOREPLACE)
                if(RENAME_NOREPLACE & flags){
                    int reply_err = fuse_reply_err(req, EEXIST);
                    while(reply_err != 0){
                        reply_err = fuse_reply_err(req, EEXIST);
                    }
                    return;
                }else{
                    LOG(INFO) << "Renaming: removing preexisting file ("<< name<<") from parent " << parent->getName();
                    oldChild->trash();
                }
#else
                oldChild->trash();

#endif
            }
            LOG(INFO) << "Renaming: Mving file ("<< name<<") from " << parent->getName() << " to " << newParent->getName();
        }

        if(child->getName() != newname) {
            FileManager::resetChildrenBuffer(parent->getId());
            LOG(INFO) << "Renaming: file from ("<< name<<") to " << newname;
            child->setName(newname);
        }
        Account *account = getAccount(req);

        if(child->getIsUploaded()){

            bool status = false;
            if(newParents) {
                status = account->updateObjectProperties(child->getId(),
                                                         FileManager::asJSONForRename(child),
                                                         newParent->getId(), parent->getId());
            } else {
                status = account->updateObjectProperties(
                        child->getId(),
                        FileManager::asJSONForRename(child)
                        );
            }
            if(!status){
                int reply_err = fuse_reply_err(req, EIO);
                while(reply_err != 0){
                    reply_err = fuse_reply_err(req, EIO);
                }
                return;
            }
            account->upsertFileToDatabase(child, {FileManager::fromInode(newparent_ino)->getId()});
        }else{
//            FileIO
            account->upsertFileToDatabase(child, {FileManager::fromInode(newparent_ino)->getId()});
        }

//#if FUSE_USE_VERSION >= 30
//        fuse_lowlevel_notify_inval_inode(account->fuse_session, parent_ino, 0, 0);
//        fuse_lowlevel_notify_inval_inode(account->fuse_session, child->attribute.st_ino, 0, 0);
//        if(newParents)
//            fuse_lowlevel_notify_inval_inode(account->fuse_session, newparent_ino, 0, 0);
//
//#else
//        fuse_lowlevel_notify_inval_inode(account->fuse_channel, parent_ino, 0, 0);
//        fuse_lowlevel_notify_inval_inode(account->fuse_channel, child->attribute.st_ino, 0, 0);
//        if(newParents)
//            fuse_lowlevel_notify_inval_inode(account->fuse_channel, newparent_ino, 0, 0);
//
//#endif

        int reply_err = fuse_reply_err(req, 0);
        while(reply_err != 0){
            reply_err = fuse_reply_err(req, 0);
        }

    }

    void link(fuse_req_t req, fuse_ino_t ino, fuse_ino_t newparent, const char *newname);

    void open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi){
        try{
            GDriveObject object = FileManager::fromInode(ino);
            if(!object || object->getIsTrashed()){
                int reply_err = fuse_reply_err(req, ENOENT);
                while(reply_err != 0){
                    reply_err = fuse_reply_err(req, ENOENT);
                }
                return;
            }
            if (object->getIsFolder()) {
                int reply_err = fuse_reply_err(req, EISDIR);
                while(reply_err != 0){
                    reply_err = fuse_reply_err(req, EISDIR);
                }
                return;
            }

            FileIO::shared_ptr io = new FileIO(object, fi->flags);
            fi->fh = (uintptr_t) io.get();

            int reply_err = fuse_reply_open(req, fi);
            while(reply_err != 0){
                reply_err = fuse_reply_open(req, fi);
            }
            return;
        }catch(std::exception &e){
            LOG(ERROR) << "Opening file" << e.what();
            int reply_err = fuse_reply_err(req, errno);
            while(reply_err != 0){
                reply_err = fuse_reply_err(req, errno);
            }
        }
    }

    void read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi){
        FileIO::shared_ptr io = (FileIO *) fi->fh;
//        if (io == nullptr) {
//            LOG(ERROR) << "io was null when reading file";
//            int reply_err = fuse_reply_err(req, EIO);
//            while(reply_err != 0){
//                reply_err = fuse_reply_err(req, EIO);
//            }
//            return;
//        }

        VLOG(10) << "Reading size " << size << " with off " << off << " and "
                 << ((size + off <= io->m_file->getFileSize()) ? "<=" : ">");
        if ((size + off) > io->m_file->getFileSize()) {
            size = io->m_file->getFileSize() - off;
            VLOG(10) << "adjusting size to " << size << " and file size " << io->m_file->getFileSize();
        }

        io->read(req, size, off);
//        fuse_reply_buf(req, (const char *) buf->data(), outsize);
    }

    void write(fuse_req_t req, fuse_ino_t ino, const char *buf, size_t size, off_t off, struct fuse_file_info *fi){
        GDriveObject object = FileManager::fromInode(ino);
        if(!object){
            int reply_err = fuse_reply_err(req, ENOENT);
            while(reply_err != 0){
                reply_err = fuse_reply_err(req, ENOENT);
            }
            return;
        }

//    SFAsync([req, off,buf,size,fi] {
        FileIO::shared_ptr io = (FileIO *) fi->fh;

       if(!(io->m_file)){
            int reply_err = fuse_reply_err(req, EAGAIN);
            while(reply_err != 0){
                reply_err = fuse_reply_err(req, EAGAIN);
            }
            return;
        }

        if(io->write_buffer == nullptr){
            io->m_event.wait();
            io->create_write_buffer();
            io->create_write_buffer2();
            io->m_event.signal();
        }
        io->b_needs_uploading = true;
        auto *fsize = &(io->m_file->attribute.st_size);
        *fsize = *fsize> off + size? *fsize: off+size;
        bool done = false;
//        io->stream.seekp(off);
//        io->stream.write(buf, size);
//        fuse_reply_write(req,size);
//        return;

//        auto *fsize = &(file->m_stat.st_size);

        off_t end = io->first_write_to_buffer + io->write_buffer_size,
        start = io-> first_write_to_buffer,
        current = io->first_write_to_buffer + io->last_write_to_buffer;

        if(current != off) {
            // not current
            io->m_file->lock();
            io->m_event.wait();
//            memcpy(io->write_buffer2, io->write_buffer, io->last_write_to_buffer);
            std::swap(io->write_buffer, io->write_buffer2);
            io->m_file->unlock();
            off_t off2 =  io->first_write_to_buffer,
                    end2 = io->last_write_to_buffer;
            SFAsync([io, off2,end2] {
                fseek(io->m_fp, off2, SEEK_SET);
                fwrite((char *)io->write_buffer2->data(), sizeof(char), end2, io->m_fp);
                io->m_file->unlock();
                io->m_event.signal();

            });
            io->last_write_to_buffer = 0;
            io->first_write_to_buffer = off;
            start = off; end = start + io->write_buffer_size; current = off;
        }

        if(!(off >= start && off+size < end)) {
//        }else {
            io->m_file->lock();
            io->m_event.wait();

//            memcpy(io->write_buffer2, io->write_buffer, io->last_write_to_buffer);
            std::swap(io->write_buffer2, io->write_buffer);
            io->m_file->unlock();
            off_t off2 =  io->first_write_to_buffer,
                    end2 = io->last_write_to_buffer;
            SFAsync([io, off2,end2] {
                fseek(io->m_fp, off2, SEEK_SET);
                fwrite((char *)io->write_buffer2->data(), sizeof(char), end2, io->m_fp);
                io->m_event.signal();
            });

            io->last_write_to_buffer = 0;
            io->first_write_to_buffer = off;

        }
        {
            auto lock{ io->m_file->getScopeLock()};
            memcpy(io->write_buffer->data() + off - io->first_write_to_buffer, buf, size);
        }

        io->last_write_to_buffer += size;

//            io->stream.seekp(io->first_write_to_buffer);
//            io->stream.write(io->write_buffer, io->last_write_to_buffer);
//        }
//        } else {
//            io->b_needs_uploading = true;
//            if( (io->last_write_to_buffer + size) >= io->get_buffer_size()){
//                io->stream.seekp(io->first_write_to_buffer);
//                io->stream.write(io->write_buffer, io->last_write_to_buffer);
//                io->last_write_to_buffer = 0;
//                io->first_write_to_buffer = off;
//            }
//            memcpy(io->write_buffer+io->last_write_to_buffer, buf, size);
//            io->last_write_to_buffer += size;
//            *fsize = io->last_write_to_buffer;
//        }
        int reply_err = fuse_reply_write(req, size);
        while(reply_err != 0){
            reply_err = fuse_reply_write(req, size);
        }

//    });


    }

    void flush(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi){
        //TODO: implement flush on io
        int reply_err = fuse_reply_err(req, 0);
        while(reply_err != 0){
            reply_err = fuse_reply_err(req, 0);
        }
    }

    void release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi){
            FileIO* io =  (FileIO *) fi->fh;

            if(io == nullptr){
                int reply_err = fuse_reply_err(req, EIO);
                while(reply_err != 0){
                    LOG(ERROR) << "Threre was an error replying";
                    reply_err = fuse_reply_err(req, EIO);
                }
                return;
            }

            do{
                usleep(15000);
            }while(io->getReferenceCount() > 1);


            fi->fh = 0;
            Account *account = getAccount(req);

            int reply_err = fuse_reply_err(req, 0);
            while(reply_err != 0){
                LOG(ERROR) << "Threre was an error replying";
                reply_err = fuse_reply_err(req, 0);
            }
            io->release();

            if(io->b_needs_uploading){
                std::vector<std::string> pids{};
                auto file = FileManager::fromInode(ino);
                account->upsertFileToDatabase(file, pids);
                LOG(INFO) << "Releasing file that needs upload: " << file->getName();
                fi->fh = 0;
                if(file) {
                    io->upload(true);
                }
            }else{
                io->deleteObject();
                fi->fh = 0;
            }

    }

    void fsync(fuse_req_t req, fuse_ino_t ino, int datasync, struct fuse_file_info *fi);

    void opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi){
        GDriveObject object = FileManager::fromInode(ino);
        if(! object->getIsFolder()){
            int reply_err = fuse_reply_err(req, ENOTDIR);
            while(reply_err != 0){
                reply_err = fuse_reply_err(req, ENOTDIR);
            }
            return;
        }
        auto lock { object->getScopeLock() };
        std::vector<GDriveObject> children = FileManager::getChildren(object->getId());

        FolderIO::shared_ptr io = new FolderIO(req, children.size());
        for (auto const &child: children) {
            if(child->getIsTrashed())
                continue;
            io->addDirEntry(child->getName().c_str(), child->attribute);
        }
        io->done();
        fi->fh = (uintptr_t) io.get();


        int reply_err = fuse_reply_open(req, fi);
        while(reply_err != 0){
            reply_err = fuse_reply_open(req, fi);
        }
    }

    void readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi){
        FolderIO::shared_ptr io = (FolderIO*)(fi->fh);
        reply_buf_limited(req, io->buffer->data(), io->accumulated_size,off,size);
        return;
    }

    void releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi){
        FolderIO::shared_ptr io {(FolderIO *) fi->fh};
        int reply_err = fuse_reply_err(req, 0);
        while(reply_err != 0){
            reply_err = fuse_reply_err(req, 0);
        }
        io->deleteObject();
    }

    void fsyncdir(fuse_req_t req, fuse_ino_t ino, int datasync, struct fuse_file_info *fi);

    void statfs(fuse_req_t req, fuse_ino_t ino){
        struct statvfs stat{
            .f_bsize = 1,
            .f_frsize=  65536*4,
            .f_blocks=  1000000000000,
            .f_bfree=  1000000000000,
            .f_bavail=  1000000000000,
            .f_files=  1000000,
            .f_ffree=  1000000000000,
            .f_favail=  1000000000000,
            .f_fsid=  1000000,
            .f_flag=  0,
        };
        int reply_err = fuse_reply_statfs(req, &stat);
        while(reply_err != 0){
            reply_err = fuse_reply_statfs(req, &stat);
        }
    }

    void setxattr(fuse_req_t req, fuse_ino_t ino, const char *name, const char *value, size_t size, int flags){
        int reply_err = fuse_reply_err(req, ENOENT);
        while(reply_err != 0){
            reply_err = fuse_reply_err(req, ENOENT);
        }
    }

    void getxattr(fuse_req_t req, fuse_ino_t ino, const char *name, size_t size){
        auto object = FileManager::fromInode(ino);
        if(object){
            std::string value;
            if(strcmp(name, "id") == 0){
                value = object->getId();
            }

            else if(!object->getIsFolder()) {
                if (strcmp(name, "md5Checksum") == 0) {
                    value = object->md5();
                    if(value.empty()){
                        value = "not available";
                    }
                }
            }

            if(value.empty()){
                if(size == 0) {
                    int reply_err = fuse_reply_xattr(req, 0);
                    while(reply_err != 0){
                        reply_err = fuse_reply_xattr(req, 0);
                    }
                }else{
                    int reply_err = fuse_reply_err(req, 0);
                    while(reply_err != 0){
                        reply_err = fuse_reply_err(req, 0);
                    }
                }
            }else if(size >= value.size()+1){
                int reply_err = fuse_reply_buf(req, value.c_str(), value.size());
                while(reply_err != 0){
                    reply_err = fuse_reply_buf(req, value.c_str(), value.size());
                }
            }else{
                int reply_err = fuse_reply_xattr(req, value.size()+1);
                while(reply_err != 0){
                    reply_err = fuse_reply_xattr(req, value.size()+1);
                }
            }
            return;

        }
        int reply_err = fuse_reply_err(req, ENOENT);
        while(reply_err != 0){
            reply_err = fuse_reply_err(req, ENOENT);
        }
    }

    void listxattr(fuse_req_t req, fuse_ino_t ino, size_t size){
        auto object = FileManager::fromInode(ino);
        if(object){

            if(object->getIsFolder()){
                constexpr int needednumberofBytes = 3;
                if(size >= needednumberofBytes) {
                    char buf[needednumberofBytes] = "id";
                    int reply_err = fuse_reply_buf(req, buf, needednumberofBytes);
                    while(reply_err != 0){
                        reply_err = fuse_reply_buf(req, buf, needednumberofBytes);
                    }
                }else if(size == 0){
                    int reply_err = fuse_reply_xattr(req, needednumberofBytes);
                    while(reply_err != 0){
                        reply_err = fuse_reply_xattr(req, needednumberofBytes);
                    }
                }else {
                    int reply_err = fuse_reply_err(req, ERANGE);
                    while(reply_err != 0){
                        reply_err = fuse_reply_err(req, ERANGE);
                    }
                }
                return;
                int reply_err = fuse_reply_err(req, 0);
                while(reply_err != 0){
                    reply_err = fuse_reply_err(req, 0);
                }
            }else{
                //md5Checksum
                constexpr int needednumberofBytes = 15;
                if(size >= needednumberofBytes){
                    char buf[needednumberofBytes];
                    memset(buf, 0, needednumberofBytes);
                    strcpy(buf, "md5Checksum"); //0-10 cha
                    strcpy(&buf[12], "id"); //12,13 char
                    int reply_err = fuse_reply_buf(req, buf, needednumberofBytes);
                    while(reply_err != 0){
                        reply_err = fuse_reply_buf(req, buf, needednumberofBytes);
                    }
                }else if(size == 0){
                    int reply_err = fuse_reply_xattr(req, needednumberofBytes);
                    while(reply_err != 0){
                        reply_err = fuse_reply_xattr(req, needednumberofBytes);
                    }
                }else{
                    int reply_err = fuse_reply_err(req, ERANGE);
                    while(reply_err != 0){
                        reply_err = fuse_reply_err(req, ERANGE);
                    }
                }
                return;
            }

        }

        int reply_err = fuse_reply_err(req, ENOENT);
        while(reply_err != 0){
            reply_err = fuse_reply_err(req, ENOENT);
        }
    }

    void removexattr(fuse_req_t req, fuse_ino_t ino, const char *name){
        int reply_err = fuse_reply_err(req, ENOSYS);
        while(reply_err != 0){
            reply_err = fuse_reply_err(req, ENOSYS);
        }
    }

    void access(fuse_req_t req, fuse_ino_t ino, int mask){
        int reply_err = fuse_reply_err(req, 0);
        while(reply_err != 0){
            reply_err = fuse_reply_err(req, 0);
        }
    }

    void create(fuse_req_t req, fuse_ino_t parent_ino, const char *name, mode_t mode, struct fuse_file_info *fi){
        Account *account = getAccount(req);
        GDriveObject parent = FileManager::fromInode(parent_ino);
        if (parent) {
            if (!parent->getIsFolder()) {
                int reply_err = fuse_reply_err(req, ENOTDIR);
                while(reply_err != 0){
                    reply_err = fuse_reply_err(req, ENOTDIR);
                }
                return;
            }
        } else {
            int reply_err = fuse_reply_err(req, ENOENT);
            while(reply_err != 0){
                reply_err = fuse_reply_err(req, ENOENT);
            }
            return;
        }

        if( strchr(name, '/') != nullptr){
            LOG(ERROR) << "File with a forward slash is illegal: " << name;
            int reply_err = fuse_reply_err(req, EINVAL);
            while(reply_err != 0){
                reply_err = fuse_reply_err(req, EINVAL);
            }
            return;
        }
        auto lock{ parent->getScopeLock() };
        GDriveObject child = FileManager::fromParentIdAndName(parent->getId(), name, false);
        if (child) {
            LOG(INFO) << "When creating file with name " << name << " parentId " << parent->getId() << " already existed";
            int reply_err = fuse_reply_err(req, EEXIST);
            while(reply_err != 0){
                reply_err = fuse_reply_err(req, EEXIST);
            }
            return;
        }


        LOG(INFO) << "Creating file with name " << name << " and parent Id " << parent->getId();

        child = account->createNewChild(parent, name, mode, true);
        FileIO::shared_ptr io {new FileIO(child, fi->flags)};
        fi->fh = (uintptr_t) io.get();
        struct fuse_entry_param e;

        memset(&e, 0, sizeof(struct fuse_entry_param));

        e.ino = child->attribute.st_ino;
        e.generation = 0; //child->attribute.;
        e.attr = child->attribute;
        e.attr_timeout = 3600;
        e.entry_timeout = 3600;

        account->insertFileToDatabase(child, parent->getId());
        FileManager::insertObjectToMemoryMap(child);

        io->open();
        int reply_err = fuse_reply_create(req, &e, fi);
        while(reply_err != 0){
            reply_err = fuse_reply_create(req, &e, fi);
        }

    }

    void getlk(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi, struct flock *lock);

    void setlk(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi, struct flock *lock, int sleep);

    void bmap(fuse_req_t req, fuse_ino_t ino, size_t blocksize, uint64_t idx);

    void ioctl(fuse_req_t req, fuse_ino_t ino, int cmd, void *arg, struct fuse_file_info *fi, unsigned flags, const void *in_buf, size_t in_bufsz, size_t out_bufsz);

    void poll(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi, struct fuse_pollhandle *ph){
        int reply_err = fuse_reply_err(req, ENOSYS);
        while(reply_err != 0){
            reply_err = fuse_reply_err(req, ENOSYS);
        }
    }

    void write_buf(fuse_req_t req, fuse_ino_t ino, struct fuse_bufvec *bufv, off_t off, struct fuse_file_info *fi) {
        struct fuse_bufvec dst = FUSE_BUFVEC_INIT(fuse_buf_size(bufv));
        FileIO::shared_ptr io{ (FileIO *) fi->fh};
        GDriveObject child = FileManager::fromInode(ino);
        child->attribute.st_size = std::max<size_t>(child->attribute.st_size, off+bufv->buf->size);

        dst.buf[0].flags = (fuse_buf_flags) (FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK);
        io->b_needs_uploading = true;
        io->b_needs_uploading = true;
        dst.buf[0].fd = io->m_fd;
        dst.buf[0].pos = off;


//        fuse_buf_copy(&dst, bufv, FUSE_BUF_SPLICE_NONBLOCK);
        ssize_t res = fuse_buf_copy(&dst, bufv, (fuse_buf_copy_flags) (FUSE_BUF_SPLICE_NONBLOCK | FUSE_BUF_SPLICE_MOVE));
        if (res < 0) {
            int reply_err = fuse_reply_err(req, -res);
            while(reply_err != 0){
                reply_err = fuse_reply_err(req, -res);
            }
        } else {
            auto temp = off + res;
            io->m_file->attribute.st_size = temp > io->m_file->attribute.st_size ? temp : io->m_file->attribute.st_size;
            int reply_err = fuse_reply_write(req, (size_t) res);
            while(reply_err != 0){
                reply_err = fuse_reply_write(req, (size_t) res);
            }

        }

    }
    void retrieve_reply(fuse_req_t req, void *cookie, fuse_ino_t ino, off_t offset, struct fuse_bufvec *bufv);

    void forget_multi(fuse_req_t req, size_t count, struct fuse_forget_data *forgets){

        for(int i = 0; i < count; i++){
            uint64_t ino = forgets[i].ino;
            auto object = FileManager::fromInode(ino);
//            if(object) {
//
//                object->forget(forgets[i].nlookup);
//            }
        }

        fuse_reply_none(req);

    }

    void flock(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi, int op);

    void fallocate(fuse_req_t req, fuse_ino_t ino, int mode, off_t offset, off_t length, struct fuse_file_info *fi);

    void readdirplus(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi){

    }
    void init(void *userdata, struct fuse_conn_info *conn){
        LOG(TRACE) << "Initializing fuse filesystem";
//        conn->max_read = FileIO::block_download_size*2-1;
//        conn->max_write = 0;

        if(std::thread::hardware_concurrency() >= 4) {
            // 4 was a somewhat arbitrary number, but it seemed like libfuse would create and destroy 4 threads very quickly.
            // this is terrible for systems with a smaller of cores such as VPS
//            conn->want = FUSE_CAP_SPLICE_WRITE | FUSE_CAP_SPLICE_MOVE | FUSE_CAP_WRITEBACK_CACHE | FUSE_CAP_ASYNC_DIO |
//                         FUSE_CAP_ASYNC_READ | FUSE_CAP_PARALLEL_DIROPS;
        }
    }


    fuse_lowlevel_ops getOps(){
        fuse_lowlevel_ops ops;
        memset(&ops, 0,sizeof(ops));
        ops.lookup = lookup;
        ops.getattr = getattr;
        ops.forget = forget;
        ops.forget_multi = forget_multi;
        ops.opendir = opendir;
//        ops.readdirplus = readdirplus;
        ops.readdir = readdir;
        ops.releasedir = releasedir;
        ops.access = access;
        ops.statfs = statfs;
        ops.create = create;
        ops.open = open;
        ops.read = read;
        ops.write = write;
        ops.release = release;
        ops.unlink = unlink;
        ops.flush = flush;
        ops.mkdir = mkdir;
        ops.rmdir = rmdir;
        ops.listxattr = listxattr;
        ops.getxattr = getxattr;
        ops.setattr = setattr;
        ops.rename = rename;
        ops.init = init;
        ops.poll = poll;
        ops.write_buf = write_buf;
        ops.setxattr = setxattr;
        ops.removexattr = removexattr;
        return ops;
    }

}
