//
// Created by Andy on 2020/5/12.
//

#include "XPacketQueue.h"

int gFlag = 0;
XPacketQueue::XPacketQueue(int capacity)
: mSize(0), mCapacity(capacity) {
    mMutex = PTHREAD_MUTEX_INITIALIZER;
    mCond = PTHREAD_COND_INITIALIZER;

    pthread_mutex_init(&mMutex, nullptr);
    pthread_cond_init(&mCond, nullptr);
}

XPacketQueue::~XPacketQueue() {
    pthread_mutex_lock(&mMutex);
    std::queue<std::shared_ptr<Packet>>().swap(mPacketQueue);
    pthread_mutex_unlock(&mMutex);

    pthread_mutex_destroy(&mMutex);
    pthread_cond_destroy(&mCond);
}

int XPacketQueue::put(const std::shared_ptr<Packet> packet) {

    pthread_mutex_lock(&mMutex);
    if (mCapacity != -1 && mPacketQueue.size() >= mCapacity) {
        pthread_cond_wait(&mCond, &mMutex);
    }

    std::shared_ptr<Packet> pkt;
    if (packet->avpkt->data && packet->avpkt->size) {
        pkt = std::make_shared<Packet>();
        if (av_packet_ref(pkt->avpkt, packet->avpkt) < 0) {
            pthread_mutex_unlock(&mMutex);
            return -1;
        }
    } else {
        /* AVPacket{data: null, size: 0} 在执行 av_packet_ref 之后会变成 AVPacket{data: "", size: 0}
           这会造成 avcodec_send_packet 的时候出现 Invaild Argument 错误 */
        pkt = packet;
    }

    pkt->flag = ++gFlag;
    mPacketQueue.emplace(pkt);
    mSize += pkt->avpkt->size;
    pthread_cond_signal(&mCond);
    pthread_mutex_unlock(&mMutex);
    return 0;
}

int XPacketQueue::putNullPacket(int streamIndex) {
    std::shared_ptr<Packet> pkt = std::make_shared<Packet>();
    pkt->avpkt->stream_index = streamIndex;
    return put(pkt);
}

std::shared_ptr<Packet> XPacketQueue::get() {
    pthread_mutex_lock(&mMutex);
    if (mPacketQueue.empty()) {
        pthread_cond_wait(&mCond, &mMutex);
    }

    auto pkt = std::move(mPacketQueue.front());
    mPacketQueue.pop();
    pthread_cond_signal(&mCond);
    pthread_mutex_unlock(&mMutex);
    return pkt;
}

int XPacketQueue::getAvailableCount() const {
    return static_cast<int>(mPacketQueue.size());
}

void XPacketQueue::flush() {
    pthread_mutex_lock(&mMutex);
    std::queue<std::shared_ptr<Packet>>().swap(mPacketQueue);
    mSize = 0;
    pthread_cond_signal(&mCond);
    pthread_mutex_unlock(&mMutex);
}
