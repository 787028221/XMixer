//
// Created by Andy on 2020/5/12.
//
#ifndef XPacketQueue_hpp
#define XPacketQueue_hpp

#include <queue>
#include <pthread.h>
#include "XFFHeader.h"

class XPacketQueue {
public:
    explicit XPacketQueue(int capacity = PQ_DEFAULT_CAPACITY);

    ~XPacketQueue();

    int put(std::shared_ptr<Packet> pkt);

    int putNullPacket(int streamIndex);

    std::shared_ptr<Packet> get();
    
    int getAvailableCount() const;

    void flush();
    
private:
    static const size_t PQ_DEFAULT_CAPACITY = 10;
    
private:
    std::queue<std::shared_ptr<Packet>> mPacketQueue;
    
    pthread_mutex_t mMutex;

    pthread_cond_t mCond;
    
    int mSize;
    
    int mCapacity;
};

#endif /* XPacketQueue_hpp */
