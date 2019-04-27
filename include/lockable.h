#ifndef LOCKABLE_H
#define LOCKABLE_H

#include <autoresetevent.h>

struct ScopeLock{
    AutoResetEvent *event;
    ScopeLock(AutoResetEvent *ev): event(ev){
        event->wait();
    }
    ~ScopeLock(){
//            LOG(INFO) << "unlocking " << _obj->getId();
        if(event != nullptr)
            event->signal();
    }

    ScopeLock(ScopeLock const &) = delete;
    ScopeLock(ScopeLock &&that){
        event = that.event;
        that.event = nullptr;
    }
};

class Lockable
{
public:
    Lockable(): m_event(1){}
    inline void lock(){
        m_event.wait();
    }

    inline void unlock(){
        m_event.signal();
    }

    inline ScopeLock getScopeLock(){
        return ScopeLock(&m_event);
    }

private:
    AutoResetEvent m_event;
};



#endif // LOCKABLE_H
