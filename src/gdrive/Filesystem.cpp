//
// Created by eric on 29/01/18.
//

#include <gdrive/FileIO.h>
#include "gdrive/Filesystem.h"
#include "gdrive/Account.h"
#include "gdrive/File.h"
#include "FolderIO.h"
#include <easylogging++.h>
#include <algorithm>

namespace DriveFS{


    inline Account* getAccount(const fuse_req_t &req){
        return static_cast<Account *>(fuse_req_userdata(req));
    }

    GDriveObject getObjectFromInodeAndReq(fuse_req_t req, ino_t inode){

        auto inodeToObject = &DriveFS::_Object::inodeToObject;
        const auto cursor = inodeToObject->find(inode);

        if(cursor == inodeToObject->cend()){
            //object not found
            return nullptr;
        }

        return cursor->second;

    }

    void lookup(fuse_req_t req, fuse_ino_t parent_ino, const char *name){
        SFAsync([=] {
            GDriveObject parent(getObjectFromInodeAndReq(req, parent_ino));
            if(!parent){
                fuse_reply_err(req, ENOENT);
                return;
            }
            for (auto child: parent->children) {
                if (child->getName().compare(name) == 0) {
                    struct fuse_entry_param e;
                    memset(&e, 0, sizeof(e));
                    e.attr = child->attribute;
                    e.ino = e.attr.st_ino;
                    e.attr_timeout = 18000.0;
                    e.entry_timeout = 18000.0;
                    child->lookupCount.fetch_add(1, std::memory_order_relaxed);
                    fuse_reply_entry(req, &e);
                    return;
                }
            }
            fuse_reply_err(req, ENOENT);
        });
    }

    void forget(fuse_req_t req, fuse_ino_t ino, uint64_t nlookup){
        SFAsync([=] {
            auto object = getObjectFromInodeAndReq(req, ino);
            if (object) {
                uint64_t current = object->lookupCount.fetch_sub(nlookup, std::memory_order_acquire) - nlookup;
                if (current == 0) {
                    ino_t self = object->attribute.st_ino;
                    for (const GDriveObject &parent: object->parents) {
                        auto children = &(parent->children);
                        auto it = std::find(children->begin(), children->end(), object);
                        if (it != children->end()) {
                            parent->children.erase(it);
                        }
                    }
                    _Object::inodeToObject.erase(self);
                    _Object::idToObject.erase(object->getId());
                }

            }

            fuse_reply_none(req);
        });
    }

    void getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi){
        SFAsync([=] {
            GDriveObject object(getObjectFromInodeAndReq(req, ino));
            if (object) {
                fuse_reply_attr(req, &(object->attribute), 180.0);
                return;
            }
            fuse_reply_err(req, ENOENT);
        });
    }

    void setattr(fuse_req_t req, fuse_ino_t ino, struct stat *attr, int to_set, struct fuse_file_info *fi);

    void readlink(fuse_req_t req, fuse_ino_t ino);

    void mknod(fuse_req_t req, fuse_ino_t parent, const char *name, mode_t mode, dev_t rdev);

    void mkdir(fuse_req_t req, fuse_ino_t parent_ino, const char *name, mode_t mode){
        SFAsync([=] {
            auto parent = getObjectFromInodeAndReq(req, parent_ino);
            GDriveObject child = parent->findChildByName(name);
            if (child) {
                fuse_reply_err(req, EEXIST);
                return;
            }

            auto account = getAccount(req);
            auto folder = account->createNewChild(parent, name, mode, false);
            struct fuse_entry_param e;
            memset(&e, 0, sizeof(e));
            e.attr = folder->attribute;
            e.ino = e.attr.st_ino;
            e.attr_timeout = 18000.0;
            e.entry_timeout = 18000.0;
            folder->lookupCount.fetch_add(1, std::memory_order_relaxed);
            fuse_reply_entry(req, &e);
        });
    }

    void unlink(fuse_req_t req, fuse_ino_t parent_ino, const char *name) {
        SFAsync([=] {
            auto account = getAccount(req);
            GDriveObject parent(getObjectFromInodeAndReq(req, parent_ino));
            parent->m_event.wait();
            bool signaled = false;
            auto children = &(parent->children);
            for (uint_fast32_t i = 0; i < children->size(); i++) {
                auto child = (*children)[i];
                if (child->getName().compare(name) == 0) {
                    children->erase(children->begin() + i);
                    parent->m_event.signal();
                    signaled = true;
                    child->m_event.wait();

                    if (child->getIsUploaded()) {
                        account->removeChildFromParent(child, parent);
                    }

                    child->trash();
                    child->m_event.signal();
                    break;
                }
            }

            if (!signaled) {
                parent->m_event.signal();
                fuse_reply_err(req, ENOENT);
            } else {
                fuse_reply_err(req, 0);
            }
        });

    }

    void rmdir(fuse_req_t req, fuse_ino_t parent_ino, const char *name){
        SFAsync([=] {
            auto parent = getObjectFromInodeAndReq(req, parent_ino);
            GDriveObject child = parent->findChildByName(name);
            if (child) {
                child->m_event.wait();

                if (!child->children.empty()) {
                    child->m_event.signal();
                    fuse_reply_err(req, ENOTEMPTY);
                } else {
                    auto account = getAccount(req);
                    if (account->removeChildFromParent(child, parent)) {
                        fuse_reply_err(req, 0);
                    } else {
                        fuse_reply_err(req, EIO);
                    }
                    child->m_event.signal();
                }


            } else {
                fuse_reply_err(req, ENOENT);
            }
        });
    }

    void symlink(fuse_req_t req, const char *link, fuse_ino_t parent, const char *name);

    void rename(fuse_req_t req, fuse_ino_t parent, const char *name, fuse_ino_t newparent, const char *newname, unsigned int flags);

    void link(fuse_req_t req, fuse_ino_t ino, fuse_ino_t newparent, const char *newname);

    void open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi){
        SFAsync([=] {
            GDriveObject object = getObjectFromInodeAndReq(req, ino);
            if (object->getIsFolder()) {
                fuse_reply_err(req, EISDIR);
                return;
            }

//            if (fi->flags & O_WRONLY || fi->flags & O_RDWR) {
//                fuse_reply_err(req, ENOSYS);
//                return;
//            }
            FileIO *io = new FileIO(object, fi->flags, getAccount(req));
            fi->fh = (uintptr_t) io;

            fuse_reply_open(req, fi);
            assert(io->getIsReadable());
            return;
        });
    }

    void read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi){
        SFAsync([=]{
            FileIO * io = (FileIO *) fi->fh;
            auto buf = io->read(size, off);

            fuse_reply_buf(req, (const char *) buf->data(), buf->size());
            delete buf;
        });
    }

    void write(fuse_req_t req, fuse_ino_t ino, const char *buf, size_t size, off_t off, struct fuse_file_info *fi){
        GDriveObject object = getObjectFromInodeAndReq(req, ino);
        if(object == nullptr){
            fuse_reply_err(req, ENOENT);
            return;
        }

//    SFAsync([req, off,buf,size,fi] {
        FileIO *io = (FileIO *) fi->fh;

       if(!(io->m_file)){
            fuse_reply_err(req, EAGAIN);
            return;
        }

        if(io->write_buffer == nullptr){
            io->create_write_buffer();
            io->create_write_buffer2();
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
            io->m_file->m_event.wait();
            io->m_event.wait();
//            memcpy(io->write_buffer2, io->write_buffer, io->last_write_to_buffer);
            std::swap(io->write_buffer, io->write_buffer2);
            io->m_file->m_event.signal();
            off_t off2 =  io->first_write_to_buffer,
                    end2 = io->last_write_to_buffer;
            SFAsync([io, off2,end2] {

                io->stream.seekp(off2);
                io->stream.write((char *)io->write_buffer2->data(), end2);
                io->m_file->m_event.signal();
                io->m_event.signal();

            });
            io->last_write_to_buffer = 0;
            io->first_write_to_buffer = off;
            start = off; end = start + io->write_buffer_size; current = off;
        }

        if(!(off >= start && off+size < end)) {
//        }else {
            io->m_file->m_event.wait();
            io->m_event.wait();

//            memcpy(io->write_buffer2, io->write_buffer, io->last_write_to_buffer);
            std::swap(io->write_buffer2, io->write_buffer);
            io->m_file->m_event.signal();
            off_t off2 =  io->first_write_to_buffer,
                    end2 = io->last_write_to_buffer;
            SFAsync([io, off2,end2] {

                io->stream.seekp(off2);
                io->stream.write((char *)io->write_buffer2->data(), end2);
                io->m_event.signal();
            });

            io->last_write_to_buffer = 0;
            io->first_write_to_buffer = off;

        }
        io->m_file->m_event.wait();
        memcpy(io->write_buffer->data() + off - io->first_write_to_buffer, buf, size);
        io->m_file->m_event.signal();

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
        fuse_reply_write(req, size);

//    });


    }

    void flush(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi){
        //TODO: implement flush on io
        fuse_reply_err(req, 0);
    }

    void release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi){
        SFAsync([=]{
            FileIO *io = (FileIO *) fi->fh;
            io->release();

            if(io->b_needs_uploading){
                //sleep for 3 seconds to make sure that the filesystem has not decided to delete the file.
                sleep(3);
                auto file = getObjectFromInodeAndReq(req, ino);
                if(file) {
                    io->upload();
                }
            }

            delete io;
        });
        fuse_reply_err(req, 0);
    }

    void fsync(fuse_req_t req, fuse_ino_t ino, int datasync, struct fuse_file_info *fi);

    void opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi){
        SFAsync([=] {
            GDriveObject object = getObjectFromInodeAndReq(req, ino);
            if(! object->getIsFolder()){
                fuse_reply_err(req, ENOTDIR);
                return;
            }
            FolderIO *io = new FolderIO(req, object->children.size());
            for (auto child: object->children) {
                io->addDirEntry(child->getName().c_str(), child->attribute);
            }
            io->done();
            fi->fh = (uintptr_t) io;


            fuse_reply_open(req, fi);
        });
    }

    void readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi){
        FolderIO *io = (FolderIO*)(fi->fh);
        if(io!= nullptr) {
            reply_buf_limited(req, io->buffer->data(), io->accumulated_size,off,size);
        }else{
            fuse_reply_err(req, EIO);
        }
    }

    void releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi){
        FolderIO *io = (FolderIO *) fi->fh;
        delete io;
        fuse_reply_err(req, 0);
    }

    void fsyncdir(fuse_req_t req, fuse_ino_t ino, int datasync, struct fuse_file_info *fi);

    void statfs(fuse_req_t req, fuse_ino_t ino){
        struct statvfs stat{
            .f_bsize = 1,
            .f_frsize=  65536*4,
            .f_blocks=  1000000,
            .f_bfree=  1000000,
            .f_bavail=  1000000,
            .f_files=  1000000,
            .f_ffree=  1000000,
            .f_favail=  1000000,
            .f_fsid=  1000000,
            .f_flag=  0,
        };
        fuse_reply_statfs(req, &stat);
    }

    void setxattr(fuse_req_t req, fuse_ino_t ino, const char *name, const char *value, size_t size, int flags);

    void getxattr(fuse_req_t req, fuse_ino_t ino, const char *name, size_t size){
        auto object = getObjectFromInodeAndReq(req, ino);
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

            if(!value.empty()){
                if(size >= value.size()+1){
                    fuse_reply_buf(req, value.c_str(), value.size());
                }else{
                    fuse_reply_xattr(req, value.size()+1);
                }
                return;

            }


        }
        fuse_reply_err(req, ENOENT);
    }

    void listxattr(fuse_req_t req, fuse_ino_t ino, size_t size){
        auto object = getObjectFromInodeAndReq(req, ino);
        if(object){

            if(object->getIsFolder()){
                constexpr int needednumberofBytes = 3;
                if(size >= needednumberofBytes) {
                    char buf[needednumberofBytes] = "id";
                    fuse_reply_buf(req, buf, needednumberofBytes);
                }else if(size == 0){
                    fuse_reply_xattr(req, needednumberofBytes);
                }else {
                    fuse_reply_err(req, ERANGE);
                }
                return;
                fuse_reply_err(req, 0);
            }else{
                //md5Checksum
                constexpr int needednumberofBytes = 15;
                if(size >= needednumberofBytes){
                    char buf[needednumberofBytes];
                    memset(buf, 0, needednumberofBytes);
                    strcpy(buf, "md5Checksum"); //0-10 cha
                    strcpy(&buf[12], "id"); //12,13 char
                    fuse_reply_buf(req, buf, needednumberofBytes);
                }else if(size == 0){
                    fuse_reply_xattr(req, needednumberofBytes);
                }else{
                    fuse_reply_err(req, ERANGE);
                }
                return;
            }

        }

        fuse_reply_err(req, ENOENT);
    }

    void removexattr(fuse_req_t req, fuse_ino_t ino, const char *name);

    void access(fuse_req_t req, fuse_ino_t ino, int mask){
        fuse_reply_err(req, 0);
    }

    void create(fuse_req_t req, fuse_ino_t parent_ino, const char *name, mode_t mode, struct fuse_file_info *fi){
        Account *account = getAccount(req);
        GDriveObject parent = getObjectFromInodeAndReq(req, parent_ino);
        if (parent) {
            if (!parent->getIsFolder()) {
                fuse_reply_err(req, ENOTDIR);
                return;
            }
        } else {
            fuse_reply_err(req, ENOENT);
            return;
        }

        for (auto child: parent->children) {
            if (child->getName().compare(name) == 0) {
                LOG(INFO) << "When creating file with name " << name << " parentId " << parent->getId() << " already existed";
                fuse_reply_err(req, EEXIST);
                return;
            }
        }


        LOG(INFO) << "Creating file with name " << name << " and parent Id " << parent->getId();

        GDriveObject child = account->createNewChild(parent, name, mode, true);
        FileIO *io = new FileIO(child, fi->flags, account);
        fi->fh = (uintptr_t) io;
        struct fuse_entry_param e;

        memset(&e, 0, sizeof(struct fuse_entry_param));

        e.ino = child->attribute.st_ino;
        e.generation = 0; //child->attribute.;
        e.attr = child->attribute;
        e.attr_timeout = 300;
        e.entry_timeout = 300;
        io->open();
        fuse_reply_create(req, &e, fi);

    }

    void getlk(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi, struct flock *lock);

    void setlk(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi, struct flock *lock, int sleep);

    void bmap(fuse_req_t req, fuse_ino_t ino, size_t blocksize, uint64_t idx);

    void ioctl(fuse_req_t req, fuse_ino_t ino, int cmd, void *arg, struct fuse_file_info *fi, unsigned flags, const void *in_buf, size_t in_bufsz, size_t out_bufsz);

    void poll(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi, struct fuse_pollhandle *ph);

    void write_buf(fuse_req_t req, fuse_ino_t ino, struct fuse_bufvec *bufv, off_t off, struct fuse_file_info *fi){



    }

    void retrieve_reply(fuse_req_t req, void *cookie, fuse_ino_t ino, off_t offset, struct fuse_bufvec *bufv);

    void forget_multi(fuse_req_t req, size_t count, struct fuse_forget_data *forgets);

    void flock(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi, int op);

    void fallocate(fuse_req_t req, fuse_ino_t ino, int mode, off_t offset, off_t length, struct fuse_file_info *fi);

    void readdirplus(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi){

    }

    fuse_lowlevel_ops getOps(){
        fuse_lowlevel_ops ops;
        ops.lookup = lookup;
        ops.getattr = getattr;
        ops.forget = forget;
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

        return ops;
    }

}